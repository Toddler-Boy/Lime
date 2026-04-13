#include "lime_CRTEmulation.h"

//-----------------------------------------------------------------------------

namespace lime
{

CRTEmulation::CRTEmulation ( const bool canHaveChildren, const juce::File& _root, const resolutions& _res )
	: ShaderToyComponent ( canHaveChildren )
	, juce::Thread ( "CRTEmulation webcam thread" )
	, res ( _res )
{
	setName ( "lime::CRTEmulation" );
	setOpaque ( true );

	setRoot ( _root );

	ShaderToyComponent::setBackgroundColor ( juce::Colours::transparentBlack );

	//
	// All intermittent textures needed (index + palette -> YUV/YIQ -> RGB -> CRT emulation)
	//
	indexSourceTexture = addTexture ( "/C64 index buffer" );
	indexSourceTexture->fromImage ( openGL_Image ( 1, 384, 272 ), false, false, true );
	lumaChromaPalette = addTexture ( "/YUV/YIQ palette" );
	lumaChromaSourceTexture = addTexture ( "/YUV/YIQ image" );
	crtSourceTexture = addTexture ( "/RGB image" );
	crtProcessedTexture[ 0 ] = addTexture ( "/CRT image1" );
	crtProcessedTexture[ 1 ] = addTexture ( "/CRT image2" );

	//
	// Index to Luma/Chroma shader (converts index of only sixteen colors to YUV/YIQ image)
	//
	indexTarget = addTarget ( "encoder-pal.glsl" );
	indexTarget->setEnableBlend ( false );

	indexTarget->setTexture ( 0, indexSourceTexture );
	indexTarget->setTexture ( 1, lumaChromaPalette );

	indexTarget->setTextureFilter ( 0, false );
	indexTarget->setTextureFilter ( 1, false );

	indexTarget->setTextureClampMode ( 0, juce::gl::GL_REPEAT );

	indexTarget->setSize ( res.nativeWidth, res.nativeHeight );
	indexTarget->setTargetBuffer ( lumaChromaSourceTexture );
	indexTarget->setBufferSizePixels ( res.nativeWidth, res.nativeHeight );

	//
	// Luma/Chroma shader (converts YUV/YIQ image into RGB with optional defects)
	//
	lumaChromaTarget = addTarget ( "decoder-pal.glsl" );
	lumaChromaTarget->setEnableBlend ( false );
	lumaChromaTarget->setTexture ( 0, lumaChromaSourceTexture );
	lumaChromaTarget->setSize ( res.nativeWidth, res.nativeHeight );
	lumaChromaTarget->setTargetBuffer ( crtSourceTexture );
	lumaChromaTarget->setBufferSizePixels ( res.nativeWidth, res.nativeHeight );

	//
	// CRT shader (renders CRT emulated screen into a texture)
	//
	crtMaskTexture = addTexture ( "CRT Masks/Slot Mask.png", [] ( lime::shaderTexture* dst, const juce::File& root )
	{
		if ( auto mask = juce::SoftwareImageType ().convert ( juce::ImageFileFormat::loadFrom ( root ) ); mask.isValid () )
			dst->fromImage ( mask );
	} );

	crtTarget = addTarget ( "crt-simulation.glsl" );
	crtTarget->setEnableBlend ( false );
	crtTarget->setTexture ( 0, crtSourceTexture );
	crtTarget->setTexture ( 1, crtMaskTexture );
	crtTarget->setTexture ( 2, crtProcessedTexture[ 1 ] );
	crtTarget->setTextureClampMode ( 1, juce::gl::GL_REPEAT );

	crtTarget->setSize ( res.scaledWidth, res.scaledHeight );
	crtTarget->setTargetBuffer ( crtProcessedTexture[ 0 ] );
	crtTarget->setBufferSizePixels ( res.scaledWidth, res.scaledHeight );
	crtProcessedTexture[ 0 ]->generateMipmaps = true;
	crtProcessedTexture[ 1 ]->generateMipmaps = true;

	//
	// Shadow texture (on top of CRT image, adds shadows and ambient occlusion)
	//
	shadowTexture = addTexture ( "/overlay shadows", [ this ] ( lime::shaderTexture* dst, const juce::File& root )
	{
		loadPartialTexture ( dst, root );
	} );

	//
	// Glass reflection texture
	//
	glassTexture = addTexture ( "../reflections.png", [] ( lime::shaderTexture* dst, const juce::File& root )
	{
		if ( auto rfl = juce::SoftwareImageType ().convert ( juce::ImageFileFormat::loadFrom ( root ) ); rfl.isValid () )
			dst->fromImage ( rfl );
	} );

	//
	// Webcam textures
	//
	{
		// NV12
		camImageNV12_Y.clear ();
		camImageNV12_UV.fill ( 0x80 );

		webcamTextureNV12_Y = addTexture ( "/webcamNV12_Y" );
		webcamTextureNV12_Y->fromImage ( camImageNV12_Y, false );

		webcamTextureNV12_UV = addTexture ( "/webcamNV12_UV" );
		webcamTextureNV12_UV->fromImage ( camImageNV12_UV, false );
	}

	//
	// Renders the CRT emulated texture to screen with optional curve (also adds shadows and reflections)
	//
	crtTargetProcessed = addTarget ( "crt-curved.glsl" );
	crtTargetProcessed->setEnableBlend ( false );
	crtTargetProcessed->setTexture ( 0, crtProcessedTexture[ 0 ] );
	crtTargetProcessed->setTexture ( 1, shadowTexture );
	crtTargetProcessed->setTexture ( 2, glassTexture );
	crtTargetProcessed->setTexture ( 3, webcamTextureNV12_Y );
	crtTargetProcessed->setTexture ( 4, webcamTextureNV12_UV );
	crtTargetProcessed->setTextureClampMode ( 0, juce::gl::GL_CLAMP_TO_BORDER );
	crtTargetProcessed->setTextureClampMode ( 1, juce::gl::GL_MIRRORED_REPEAT );

	//
	// Overlay textures
	//
	overlayTexture = addTexture ( "/overlay", [ this ] ( lime::shaderTexture* dst, const juce::File& root )
	{
		auto	ovlImg = juce::SoftwareImageType ().convert ( juce::ImageFileFormat::loadFrom ( root ) );

		dst->fromImage ( ovlImg );
		overlayImgRect = ovlImg.getBounds ().toFloat ();
	} );

	//
	// Overlay LUTs (day -> dusk -> night)
	//
	{
		auto loadOverlayLUT = [] ( lime::shaderTexture* dst, const juce::File& root )
		{
			dst->from3DLUT ( juce::SoftwareImageType ().convert ( juce::ImageFileFormat::loadFrom ( root ) ) );
		};
		overlayLUT_dusk = addTexture ( "../dusk.png", loadOverlayLUT );
		overlayLUT_night = addTexture ( "../night.png", loadOverlayLUT );
	}

	//
	// Overlay shader
	//
	overlayTarget = addTarget ( "overlay.glsl" );
	overlayTarget->setTexture ( 0, overlayTexture );
	overlayTarget->setTexture ( 2, overlayLUT_dusk );
	overlayTarget->setTexture ( 3, overlayLUT_night );

	//
	// Bezel texture
	//
	{
		auto loadBezel = [ this ] ( lime::shaderTexture* dst, const juce::File& root )
		{
			const auto	edgeRect = loadPartialTexture ( dst, root );

			bezelBounds = edgeRect.toFloat ();

			if ( dst->isValid () )
			{
				auto	bzl = dst->imgTexture;

				// Blend overlay texture into bezel-mask
				{
					auto	ovl = juce::Image::BitmapData ( overlayTexture->imgTexture, juce::Image::BitmapData::readOnly );
					auto	bzlDst = juce::Image::BitmapData ( bzl, juce::Image::BitmapData::readWrite );

					for ( auto bzlY = 0; bzlY < edgeRect.getHeight (); ++bzlY )
					{
						for ( auto bzlX = 0; bzlX < edgeRect.getWidth (); ++bzlX )
						{
							if ( auto pix = bzlDst.getPixelColour ( bzlX, bzlY ); pix.getAlpha () )
							{
								auto	ovlCol = ovl.getPixelColour ( edgeRect.getX () + bzlX, edgeRect.getY () + bzlY );
								bzlDst.setPixelColour ( bzlX, bzlY, ovlCol.withAlpha ( ovlCol.getFloatAlpha () * pix.getFloatAlpha () ) );
							}
						}
					}
				}

				dst->fromImage ( bzl );
			}

			bezelTarget->setEnabled ( isBezelEnabled () );
		};
		bezelTexture = addTexture ( "/overlay bezel reflections", loadBezel );
	}

	//
	// Bezel shader
	//
	bezelTarget = addTarget ( "crt-bezel-reflections.glsl" );
	bezelTarget->setTexture ( 0, crtProcessedTexture[ 0 ] );
	bezelTarget->setTexture ( 1, bezelTexture );
	bezelTarget->setTexture ( 2, overlayLUT_dusk );
	bezelTarget->setTexture ( 3, overlayLUT_night );
	bezelTarget->setTextureClampMode ( 0, juce::gl::GL_MIRRORED_REPEAT );

	//
	// Light texture (glowing things, that stay bright even when it turns to night)
	//
	lightTexture = addTexture ( "/overlay lights", [ this ] ( lime::shaderTexture* dst, const juce::File& root )
	{
		lightBounds = loadPartialTexture ( dst, root ).toFloat ();
		lightTarget->setEnabled ( overlayTexture->isValid () && dst->isValid () );
	} );

	//
	// Light shader
	//
	lightTarget = addTarget ( "2d-texture.glsl" );
	lightTarget->setEnableBlend ( true, true, lime::shaderTarget::BlendMode::add );
	lightTarget->setTexture ( 0, lightTexture );

	//
	// Setup camera for glass reflections
	//
	startThread ( juce::Thread::Priority::normal );
}
//-----------------------------------------------------------------------------

CRTEmulation::~CRTEmulation ()
{
	stopThread ( -1 );
}
//-----------------------------------------------------------------------------

void CRTEmulation::updateZoom ()
{
	const auto	rects = calcRects ();

	crtTargetProcessed->setBounds ( rects[ 0 ] );

	if ( rects.size () > 1 )
	{
		overlayTarget->setBounds ( rects[ 1 ] );
		bezelTarget->setBounds ( rects[ 2 ] );
		lightTarget->setBounds ( rects[ 3 ] );
	}
}
//-----------------------------------------------------------------------------

void CRTEmulation::resized ()
{
	ShaderToyComponent::resized ();

	updateZoom ();
}
//-----------------------------------------------------------------------------

void CRTEmulation::renderOpenGL ()
{
	// Get deltaTime
	const auto	deltaTime = getDeltaTime ();

	// Calculate decay-factors and flicker-strength for phosphor-decay emulation
	{
		const auto	crtDecay = curSettings.crtPhosphorDecay * 0.01f;
		const auto	decay = std::lerp ( 1.0f, 10.0f, crtDecay );

		constexpr float	decayFactors[ 3 ] = { 18.0f, 17.0f, 20.0f };

		const auto	factorR = float ( std::exp2 ( -( decayFactors[ 0 ] * decay ) * deltaTime ) );
		const auto	factorG = float ( std::exp2 ( -( decayFactors[ 1 ] * decay ) * deltaTime ) );
		const auto	factorB = float ( std::exp2 ( -( decayFactors[ 2 ] * decay ) * deltaTime ) );

		setGlobalUniform ( "u_decayFactor", { factorR, factorG, factorB } );

		// Calculate flicker visibility if decay is very high
		constexpr auto	flickerThreshold = 0.7f;
		constexpr auto	flickerMultiplier = 1.0f / ( 1.0f - flickerThreshold );

		const auto	flicker = std::max ( crtDecay - flickerThreshold, 0.0f ) * flickerMultiplier;
		setGlobalUniform ( "u_phosphorFlicker", std::pow ( flicker, 2.0f / 3.0f ) * 0.05f );
	}

	// Set-up a ping-pong mechanism for CRT processing
	{
		crtTarget->setTexture ( 2, crtProcessedTexture[ crtProcessedTextureIndex ] );

		crtProcessedTextureIndex ^= 1;

		const auto	tex = crtProcessedTexture[ crtProcessedTextureIndex ];

		crtTarget->setTargetBuffer ( tex );
		crtTargetProcessed->setTexture ( 0, tex );
		bezelTarget->setTexture ( 0, tex );
	}

	ShaderToyComponent::renderOpenGL ();
}
//-----------------------------------------------------------------------------

std::vector<juce::Rectangle<float>> CRTEmulation::calcRects ()
{
	const auto	b = getLocalBounds ().toFloat ();

	// No monitor overlay, so transform CRT image directly to component-bounds
	if ( ! overlayTexture->isValid () || ! curSettings.overlay )
		return { getTubeRect ( b ) };

	//
	// Calculate CRT image rectangles
	//
	const auto	crtRect = juce::Rectangle<float> {	ovlyCenter.getX () - ovlyWidth / 2.0f,
													ovlyCenter.getY () - ovlyHeight / 2.0f,
													ovlyWidth,
													ovlyHeight };

	// CRT bounds
	const auto	tubeRect = getTubeRect ( crtRect );

	// Lerp between zoomed-out and zoomed-in versions
	const juce::RectanglePlacement	rp;

	const auto	zoTrans = rp.getTransformToFit ( overlayImgRect, b );
	const auto	ziTrans = rp.getTransformToFit ( crtRect, b );

	const auto	zoom = curSettings.overlayZoom * 0.01f;

	auto lerpTransfrom = [] ( const juce::AffineTransform& ta, const juce::AffineTransform& tb, const float t )
	{
		return juce::AffineTransform {	std::lerp ( ta.mat00, tb.mat00, t ),
										std::lerp ( ta.mat01, tb.mat01, t ),
										std::lerp ( ta.mat02, tb.mat02, t ),
										std::lerp ( ta.mat10, tb.mat10, t ),
										std::lerp ( ta.mat11, tb.mat11, t ),
										std::lerp ( ta.mat12, tb.mat12, t ) };
	};

	const auto	finalTrans = lerpTransfrom ( zoTrans, ziTrans, zoom );

	return {
		tubeRect.transformedBy ( finalTrans ),
		overlayImgRect.transformedBy ( finalTrans ),
		bezelBounds.transformedBy ( finalTrans ),
		lightBounds.transformedBy ( finalTrans ),
	};
}
//-----------------------------------------------------------------------------

bool CRTEmulation::isBezelEnabled () const
{
	return		curSettings.overlay
			&&	overlayTexture && overlayTexture->isValid ()
			&&	bezelTexture && bezelTexture->isValid ();
}
//-----------------------------------------------------------------------------

bool CRTEmulation::isShadowEnabled () const
{
	return		curSettings.overlay
			&&	overlayTexture && overlayTexture->isValid ()
			&&	shadowTexture && shadowTexture->isValid ();
}
//-----------------------------------------------------------------------------

bool CRTEmulation::isGlassEnabled () const
{
	return glassTexture && glassTexture->isValid ();
}
//-----------------------------------------------------------------------------

bool CRTEmulation::isWebcamNeeded () const
{
	return		curSettings.webcam
			&&	curSettings.crtReflections
			&&	webcamTextureNV12_Y
			&&	webcamTextureNV12_UV;
}
//-----------------------------------------------------------------------------

juce::Rectangle<float> CRTEmulation::getTubeRect ( const juce::Rectangle<float>& target )
{
	const auto	outerRect = juce::Rectangle<float> { res.scaledWidth * 0.937f, float ( res.scaledHeight ) };
	const auto	zoTrans = juce::RectanglePlacement ().getTransformToFit ( outerRect, target );

	return outerRect.transformedBy ( zoTrans );
}
//-----------------------------------------------------------------------------

void CRTEmulation::setRoot ( const juce::File& _root )
{
	ShaderToyComponent::setRoot ( _root, {}, "Shaders", "Overlays" );
	root = _root;

	//
	// Load available overlay profiles
	//
	{
		overlayProfiles.clear ();

		rootOverlays = root.getChildFile ( "Overlays/" );

		const auto	files = rootOverlays.findChildFiles ( juce::File::TypesOfFileToFind::findDirectories, false, "*" );

		// Only add folders that contain a profile.ini file
		for ( const auto& f : files )
			if ( f.getChildFile ( "profile.yml" ).existsAsFile () )
				overlayProfiles.add ( f.getFileName () );

		overlayProfiles.sortNatural ();
	}

	//
	// Load available CRT masks
	//
	{
		crtMasks.clear ();

		rootCRTMasks = root.getChildFile ( "CRT Masks/" );

		const auto	files = rootCRTMasks.findChildFiles ( juce::File::TypesOfFileToFind::findFiles, false, "*.png" );

		for ( const auto& f : files )
			crtMasks.add ( f.getFileNameWithoutExtension () );

		crtMasks.sortNatural ();
	}
}
//-----------------------------------------------------------------------------

void CRTEmulation::loadOverlayProfile ( const juce::String& profileName )
{
	if ( ! parseOverlayProfile ( profileName ) )
		return;

	setTextureSource ( overlayTexture, profileName + "/overlay.png" );
	setTextureSource ( bezelTexture, profileName + "/bezel.png" );
	setTextureSource ( lightTexture, profileName + "/lights.png" );
	setTextureSource ( shadowTexture, profileName + "/shadows.png" );
}
//-----------------------------------------------------------------------------

void CRTEmulation::reloadOverlayProfile ()
{
	if ( parseOverlayProfile ( ovlyProfileName ) )
		updateZoom ();
}
//-----------------------------------------------------------------------------

void CRTEmulation::updateOverlay ()
{
	auto	enabled = curSettings.overlay;

	const auto	selProfile = curSettings.overlayProfile;
	if ( enabled && ! ovlyProfileName.equalsIgnoreCase ( selProfile ) )
		loadOverlayProfile ( selProfile );

	enabled = enabled && overlayTexture->isValid ();

	overlayTarget->setEnabled ( enabled );
	bezelTarget->setEnabled ( isBezelEnabled () );
	lightTarget->setEnabled ( enabled && lightTexture->isValid () );

	// CRT Mask
	{
		const auto	maskFile = curSettings.crtMaskBitmap;
		setTextureSource ( crtMaskTexture, "../CRT Masks/" + maskFile + ".png" );
	}

	setSettings ( curSettings );
	updateZoom ();
}
//-----------------------------------------------------------------------------

bool CRTEmulation::parseOverlayProfile ( const juce::String& profileName )
{
	ovlyProfileName = profileName;

	ovlyWidth = 0.0f;

	// Check if profile exists
	auto	file = rootOverlays.getChildFile ( profileName ).getChildFile ( "profile.yml" );
	if ( ! file.existsAsFile () )
		return false;

	static const	std::vector<std::pair<std::string, YamlConfig::ConfigValue>>	overlayDefaults
	{
		{ "multipliers/daytime",	1.0f },
		{ "multipliers/bezel",		1.0f },
		{ "multipliers/reflection",	1.0f },
		{ "multipliers/grain",		1.0f },

		{ "screen/center",		YamlConfig::vec2f { 0.0f, 0.0f } },
		{ "screen/size",		YamlConfig::vec2f { 0.0f, 0.0f } },

		{ "bezel/one/amount",	0.7f },
		{ "bezel/one/radius",	2 },
		{ "bezel/one/zoom",		YamlConfig::vec2f { 1.0f, 1.0f } },
		{ "bezel/one/shift",	YamlConfig::vec2f { 0.0f, 0.0f } },

		{ "bezel/two/amount",	0.5f },
		{ "bezel/two/radius",	20 },
		{ "bezel/two/zoom",		YamlConfig::vec2f { 1.0f, 1.0f } },
		{ "bezel/two/shift",	YamlConfig::vec2f { 0.0f, 0.0f } },

		{ "shadow/zoom",		YamlConfig::vec2f { 1.0f, 1.0f } },
		{ "shadow/shift",		YamlConfig::vec2f { 0.0f, 0.0f } },
	};

	auto	yml = YamlConfig ( overlayDefaults );
	yml.load ( file );

	mulDaytime = yml.get<float> ( "multipliers/daytime" );
	mulBezel = yml.get<float> ( "multipliers/bezel" );
	mulReflection = yml.get<float> ( "multipliers/reflection" );
	mulGrain = yml.get<float> ( "multipliers/grain" );

	//
	// Get screen properties
	//
	{
		std::tie ( ovlyCenter.x, ovlyCenter.y ) = yml.get<YamlConfig::vec2f> ( "screen/center" );
		std::tie ( ovlyWidth, ovlyHeight ) = yml.get<YamlConfig::vec2f> ( "screen/size" );
	}

	//
	// Bezel properties
	//
	{
		auto setBezelValues = [ this, &yml ] ( const std::string& sectionName, const std::string& appendix )
		{
			setGlobalUniform ( "rflAmount" + appendix, yml.get<float> ( sectionName + "amount" ) );

			const auto	radius = yml.get<int> ( sectionName + "radius" );
			setGlobalUniform ( "rflRadius" + appendix, std::clamp ( radius, 2, 50 ) );

			const auto [ zoomX, zoomY ] = yml.get<YamlConfig::vec2f> ( sectionName + "zoom" );
			setGlobalUniform ( "rflZoom" + appendix, { zoomX, zoomY } );

			const auto [shiftX, shiftY] = yml.get<YamlConfig::vec2f> ( sectionName + "shift" );
			setGlobalUniform ( "rflShift" + appendix, { shiftX * 0.1f, shiftY * 0.1f } );
		};

		setBezelValues ( "bezel/one/", "1" );
		setBezelValues ( "bezel/two/", "2" );
	}

	//
	// Shadows
	//
	{
		const auto [ zoomX, zoomY ] = yml.get<YamlConfig::vec2f> ( "shadow/zoom" );
		setGlobalUniform ( "crtShadowScale", { zoomX, zoomY } );

		const auto [ shiftX, shiftY ] = yml.get<YamlConfig::vec2f> ( "shadow/shift" );
		setGlobalUniform ( "crtShadowTranslate", { shiftX, shiftY } );
	}

	setSettings ( curSettings );

	return true;
}
//-----------------------------------------------------------------------------

void CRTEmulation::setSettings ( const settings& set )
{
	const auto	raw = set.crtEmulation ? 0 : 1;

	//
	// Set encoder/decoder shaders
	//
	if ( set.needsNewEncoders ( curSettings ) )
	{
		const auto	isNTSC = set.isNTSC ? 1 : 0;

		struct encDecShaders
		{
			const juce::String	encoder;
			const juce::String	decoder;
		};

		const static encDecShaders	shaders[ 2 ][ 2 ] =
		{
			{
				{ "encoder-pal.glsl",	"decoder-pal.glsl" },
				{ "encoder-ntsc.glsl",	"decoder-ntsc.glsl" }
			},

			{
				{ "encoder-yuv.glsl",	"decoder-yuv.glsl" },
				{ "encoder-yiq.glsl",	"decoder-yiq.glsl" }
			}
		};

		// Index -> YUV/YIQ
		setTargetShader ( indexTarget, shaders[ raw ][ isNTSC ].encoder );

		// YUV/YIQ -> RGB
		setTargetShader ( lumaChromaTarget, shaders[ raw ][ isNTSC ].decoder );

		lumaChromaTarget->setTextureFilter ( 0, set.crtEmulation );
	}

	//
	// Set CRT shaders
	//
	if ( set.needsNewSimulation ( curSettings ) )
	{
		crtTarget->setTextureFilter ( 0, set.crtEmulation );

		const static juce::String	rawCrtShader[ 2 ][ 2 ] =
		{
			{	"crt-simulation.glsl",	"crt-curved.glsl"	},
			{	"raw-overscan.glsl",	"2d-texture.glsl"	}
		};

		setTargetShader ( crtTarget, rawCrtShader[ raw ][ 0 ] );
		setTargetShader ( crtTargetProcessed, rawCrtShader[ raw ][ 1 ] );
	}

	curSettings = set;

	//
	// Monitor shadows (only visible when overlays are enabled)
	//
	setGlobalUniform ( "crtShadow", isShadowEnabled () * set.overlayShadow * 0.01f );

	// Overlay uniforms
	setGlobalUniform ( "ovlDust", set.overlayDust * 0.01f );
	setGlobalUniform ( "ovlChromaticAberration", set.overlayChromaticAberration * 0.01f );
	setGlobalUniform ( "ovlGrain", set.overlayGrain * 0.01f * mulGrain );

	// Daytime LUT blending (for day -> dusk -> night transition)
	setGlobalUniform ( "lutBlend", set.overlayDaytime * 0.01f * mulDaytime );

	// Index to YUV/YIQ encoder
	setGlobalUniform ( "decJailbars", set.encJailbars * 0.01f );

	// Signal decoder
	{
		// NTSC has a stronger brightness bias
		const auto	brightness = ( set.brightness - ( set.isNTSC ? 70.0f : 50.0f ) ) * ( 1.0f / 256.0f );
		const auto	contrast = std::lerp ( 0.4f, 1.2f, set.contrast * 0.01f );
		const auto	saturation = std::lerp ( 0.0f, 0.8f, ( set.saturation + ( set.isNTSC ? 0.0f : 5.0f ) ) * 0.01f );

		setGlobalUniform ( "encBrightnessContrastSaturation", { brightness, contrast, saturation } );
	}

	//
	// TV Overscan
	//
	{
		const auto	over = std::lerp ( 1.0f, 0.86f, set.overscan * 0.01f );
		const auto	yOver = over * ( set.isNTSC == false ? 1.0f : 0.8825f );

		setGlobalUniform ( "crtOverscan", { over, yOver } );
	}

	//
	// CRT emulation uniforms
	//
	{
		setGlobalUniform ( "decSharpening", set.decSharpening * 0.01f );
		setGlobalUniform ( "decLumablur", set.decLumaBlur * 0.01f );
		setGlobalUniform ( "decChromablur", set.decChromaBlur * 0.01f );

		setGlobalUniform ( "decInterference", set.decInterference * 0.01f );
		setGlobalUniform ( "decCrosstalk", set.decCrosstalk * 0.01f );
		setGlobalUniform ( "decSubcarrier", set.decSubcarrier * 0.01f );
		setGlobalUniform ( "decNoise", set.decNoise * 0.01f );

		// Curve
		setGlobalUniform ( "crtCurve", set.crtCurve * 0.01f );

		// Bleed
		{
			setGlobalUniform ( "crtBleed", set.crtBleed * 0.01f );
			auto setUni_vec2 = [ this ] ( const char* uniName, const std::pair<int8_t, int8_t>& value )
			{
				setGlobalUniform ( uniName, { value.first * 0.01f, value.second * 0.01f } );
			};

			setUni_vec2 ( "crtRedOffset", set.crtBleedRed );
			setUni_vec2 ( "crtGreenOffset", set.crtBleedGreen );
			setUni_vec2 ( "crtBlueOffset", set.crtBleedBlue );
		}

		// H-wave
		setGlobalUniform ( "crtHoffset", set.crtHwave * 0.01f );

		// Scanlines
		setGlobalUniform ( "crtScanlines", set.crtScanlines * 0.01f );

		// Shadowmask
		setGlobalUniform ( "crtMask", set.crtMask * 0.01f );

		// Glow
		setGlobalUniform ( "crtGlow", set.crtGlow * 0.01f );

		// Ambient
		setGlobalUniform ( "crtAmbient", set.crtAmbient * 0.01f );

		// Phosphor Decay
		setGlobalUniform ( "crtRefreshRate", set.isNTSC ? 59.826f : 50.125f );
		setGlobalUniform ( "crtPhosphorDecay", set.crtPhosphorDecay * 0.01f );
		// Vignette
		setGlobalUniform ( "crtVignette", set.crtVignette * 0.01f );

		// Webcam stuff
		{
			// Reflection
			crtTargetProcessed->setUniform_f ( "crtSource", set.webcam ? 1.0f : 0.0f );

			const auto	reflectionValue = set.crtReflections * 0.01f * mulReflection;
			crtTargetProcessed->setUniform_f ( "crtReflection", reflectionValue * isGlassEnabled () );

			// Aspect ratio correction for webcam image
			crtTargetProcessed->setUniform_f ( "crtRflCorrection", ( 4.0f / 3.0f ) / ( float ( camImageNV12_Y.width ) / float ( camImageNV12_Y.height ) ) );
			crtTargetProcessed->setUniform_i ( "crtWebcamFormat", 0 );

			// Webcam settings
			{
				const auto	brightness = std::lerp ( 0.4f, 1.2f, set.webcamBrightness * 0.01f );
				const auto	contrast = std::lerp ( 0.4f, 1.2f, set.webcamContrast * 0.01f );
				const auto	saturation = std::lerp ( 0.0f, 2.0f, set.webcamSaturation * 0.01f );

				crtTargetProcessed->setUniform_f ( "camBrightnessContrastSaturation", { brightness, contrast, saturation } );
			}
		}
	}

	//
	// Bezel uniforms
	//
	{
		const auto	value = set.overlayBezel * 0.01f * mulBezel;
		setGlobalUniform ( "rflLevel", value );
		bezelTarget->setEnabled ( isBezelEnabled () && value > 0.0f );
	}
}
//-----------------------------------------------------------------------------

void CRTEmulation::fileChanged ( const juce::File& file, gin::FileSystemWatcher::FileSystemEvent event )
{
	lime::ShaderToyComponent::fileChanged ( file, event );

	if ( file.isDirectory () )
		return;

	//
	// Change happened inside our data-folder
	//
	if ( file.isAChildOf ( root ) )
	{
		auto	parent = file.getRelativePathFrom ( root ).replaceCharacter ( '\\', '/' );

		// Overlays
		if ( parent.startsWithIgnoreCase ( "Overlays/" ) )
		{
			if ( event != gin::FileSystemWatcher::fileUpdated )
				return;

			// Reload monitor profile
			if ( parent.endsWithIgnoreCase ( ".yml" ) )
				reloadOverlayProfile ();

			return;
		}
	}
}
//-----------------------------------------------------------------------------

void CRTEmulation::setBackgroundColor ( const juce::Colour _bckCol )
{
	bckCol = _bckCol;

	crtTargetProcessed->setTextureBorderColor ( 0, bckCol );
	crtTargetProcessed->setTargetBackgroundColor ( bckCol );
	setGlobalUniform ( "backCol", { bckCol.getFloatRed (), bckCol.getFloatGreen (), bckCol.getFloatBlue () } );
}
//-----------------------------------------------------------------------------

void CRTEmulation::setIndexTextureSource ( const juce::Image& img )
{
	indexSourceTexture->fromImage ( img, false, false, true );
}
//-----------------------------------------------------------------------------

void CRTEmulation::setIndexTextureSource ( const openGL_Image& img )
{
	indexSourceTexture->fromImage ( img, false, false, true );
}
//-----------------------------------------------------------------------------

void CRTEmulation::setLumaChromaPalette ( const std::span<float>& palette )
{
	constexpr auto	height = 3;	// three palettes stacked vertically in one texture (2x YUV + 1x YIQ)
	const auto	width = int (  palette.size () / height );

	indexTarget->lock ();
	lumaChromaTarget->lock ();

	lumaChromaPalette->fromFloatVector ( { palette, width / 3, height }, false );

	lumaChromaTarget->unlock ();
	indexTarget->unlock ();
}
//-----------------------------------------------------------------------------

juce::Rectangle<int> CRTEmulation::loadPartialTexture ( lime::shaderTexture* dst, const juce::File& root )
{
	if ( ! overlayTexture->isValid () )
		return {};

	if ( auto rfl = juce::SoftwareImageType ().convert ( juce::ImageFileFormat::loadFrom ( root ) ); rfl.isARGB () )
	{
		auto	edgeRect = findImageRect ( rfl );

		dst->fromImage ( rfl.getClippedImage ( edgeRect ) );

		return edgeRect;
	}

	return {};
}
//-----------------------------------------------------------------------------

juce::Rectangle<int> CRTEmulation::findImageRect ( juce::Image& img )
{
	// Find smallest rectangle still containing all pixels from image
	auto	bmp = juce::Image::BitmapData ( img, juce::Image::BitmapData::readOnly );
	const auto	pixCnt = bmp.width * bmp.height;

	// Top
	auto	y = 0;
	for ( ; y < pixCnt && !bmp.data[ y * bmp.pixelStride + 3 ]; ++y );
	y /= bmp.width;

	// Bottom
	auto	h = pixCnt - 1;
	for ( ; h >= y && !bmp.data[ h * bmp.pixelStride + 3 ]; --h );
	h /= bmp.width;

	auto isColumnUsed = [ &bmp ] ( const int col ) -> bool
	{
		auto	data = bmp.data + col * bmp.pixelStride + 3;
		int		testY = 0;
		for ( ; testY < bmp.height && !data[ testY * bmp.lineStride ]; ++testY );
		return testY < bmp.height;
	};

	// Left
	auto	x = 0;
	for ( ; x < bmp.width && ! isColumnUsed ( x ); ++x );

	// Right
	auto	w = bmp.width - 1;
	for ( ; w >= x && ! isColumnUsed ( w ); --w );

	return { x, y, ( w + 1 ) - x, ( h + 1 ) - y };
}
//-----------------------------------------------------------------------------

void CRTEmulation::run ()
{
	do
	{
		if ( isShowing () && isWebcamNeeded () )
			addWebcamListener ();
		else
			removeWebcamListener ();

		wait ( 100.0 );

	} while ( ! threadShouldExit () );

	removeWebcamListener ();
}
//-----------------------------------------------------------------------------

void CRTEmulation::addWebcamListener ()
{
	if ( ! camera )
	{
		camera = std::make_unique<Webcam> ( 1920, 1080, 60 );
		if ( !camera->getError ().empty () )
		{
			Z_WARN ( "Webcam error: " << camera->getError () );
			camera.reset ();
			return;
		}

		camera->onDataReceived = [ this ] ( uint8_t* data, int width, int height, int strideY, int strideUV, pixFmt format )
		{
			if ( format == pixFmt::NV12 )
			{
				// Resize NV12 textures if needed
				if ( camImageNV12_Y.width != width || camImageNV12_Y.height != height )
				{
					camImageNV12_Y = lime::openGL_Image ( 1, width, height );
					camImageNV12_UV = lime::openGL_Image ( 2, width / 2, height / 2 );
				}

				// Upload Y as texture
				{
					if ( width == strideY )
					{
						std::copy_n ( data, width * height, camImageNV12_Y.data.data () );
					}
					else
					{
						// If stride is larger than width, we need to copy line by line
						for ( int y = 0; y < height; ++y )
							std::copy_n ( data + y * strideY, width, camImageNV12_Y.data.data () + y * width );
					}
					webcamTextureNV12_Y->fromImage ( camImageNV12_Y, false );
				}

				// Upload UV as texture
				{
					if ( width == strideUV )
					{
						std::copy_n ( (uint16_t*)( data + width * height ), ( width / 2 ) * ( height / 2 ), (uint16_t*)( camImageNV12_UV.data.data () ) );
					}
					else
					{
						// If stride is larger than width, we need to copy line by line
						for ( int y = 0; y < height / 2; ++y )
							std::copy_n ( (uint16_t*)( data + width * height + y * strideUV ), width / 2, (uint16_t*)( camImageNV12_UV.data.data () + y * width ) );
					}
					webcamTextureNV12_UV->fromImage ( camImageNV12_UV, false );
				}
			}
			else if ( format == pixFmt::YUY2 )
			{
			}
		};
	}

	if ( isCamInUse || !camera )
		return;

	isCamInUse = true;

	Z_DLOG ( "camera->start" );
	camera->start ();
}
//-----------------------------------------------------------------------------

void CRTEmulation::removeWebcamListener ()
{
	if ( ! isCamInUse || ! camera )
		return;

	isCamInUse = false;

	Z_DLOG ( "camera->stop" );
	camera->stop ();
}
//-----------------------------------------------------------------------------

}
