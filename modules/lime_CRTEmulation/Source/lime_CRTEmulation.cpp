#include "lime_CRTEmulation.h"

//-----------------------------------------------------------------------------

namespace lime
{

CRTEmulation::CRTEmulation ( const bool canHaveChildren, const int idleTimeout, const juce::File& _root, const resolutions& _res )
	: ShaderToyComponent ( canHaveChildren, idleTimeout )
	, juce::Thread ( "CRTEmulation webcam thread" )
	, res ( _res )
	, indexBuffer ( 1, 384, 272 )
{
	setName ( "lime::CRTEmulation" );
	setOpaque ( true );

	setRoot ( _root );

	ShaderToyComponent::setBackgroundColor ( juce::Colours::transparentBlack );

	//
	// All intermittent textures needed (index + palette -> YUV/YIQ -> RGB -> CRT emulation)
	//
	indexSourceTexture = addTexture ( "/C64 index buffer" );
	indexSourceTexture->fromImage ( indexBuffer, false, false, true );
	lumaChromaPalette = addTexture ( "/YUV/YIQ palette" );
	lumaChromaSourceTexture = addTexture ( "/YUV/YIQ image" );
	crtSourceTexture = addTexture ( "/RGB image" );
	crtSourceTexture->generateMipmaps = true;
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
	crtTargetProcessed->setTexture ( 1, glassTexture );
	crtTargetProcessed->setTexture ( 2, webcamTextureNV12_Y );
	crtTargetProcessed->setTexture ( 3, webcamTextureNV12_UV );
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

		// Find hole in overlay (where the screen is)
		const auto	pixelHole = getHoleBounds ( ovlImg );
		const auto	hole = expandHoleBounds ( pixelHole, res.scaledWidth * 0.937f / float ( res.scaledHeight ), 1.0f );

		ovlyCenter = hole.getCentre ();
		ovlyWidth = hole.getWidth ();
		ovlyHeight = hole.getHeight ();
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
	overlayTarget->setTexture ( 1, overlayLUT_dusk );
	overlayTarget->setTexture ( 2, overlayLUT_night );

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
				auto	bzl = std::get<juce::Image> ( dst->source );

				// Blend overlay texture into bezel-mask
				{
					auto	ovl = juce::Image::BitmapData ( std::get<juce::Image> ( overlayTexture->source ), juce::Image::BitmapData::readOnly );
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
		lightBounds = loadPartialTexture ( dst, root, 30 ).toFloat ();
		lightTarget->setEnabled ( overlayTexture->isValid () && dst->isValid () );
	} );

	//
	// Light shader
	//
	lightTarget = addTarget ( "lights.glsl" );
	lightTarget->setEnableBlend ( true, false, lime::shaderTarget::BlendMode::add );
	lightTarget->setTexture ( 0, lightTexture );

	//
	// Dust
	//
	{
		overlayDustTexture = addTexture ( "/overlay dust" );

		dustTarget = addTarget ( "overlay-dust-particle.glsl" );
		dustTarget->setEnableBlend ( true, false, shaderTarget::BlendMode::add );
		dustTarget->setTargetBuffer ( overlayDustTexture );
		dustTarget->setTargetBackgroundColor ( juce::Colours::black );

		// Initialize feedback buffer with random particle positions and zero velocity
		{
			struct DustParticle
			{
				float	px, py, pz;
				float	vx = 0.0f, vy = 0.0f, vz = 0.0f;
			};

			juce::Random				random;
			std::vector<DustParticle>	dustData ( 300 );

			for ( auto& p : dustData )
			{
				p.px = random.nextFloat () * 2.0f - 1.0f;
				p.py = random.nextFloat () * 2.0f - 1.0f;
				p.pz = random.nextFloat () * 2.0f - 1.0f;
			}

			std::vector<openGL_Quad::feedbackVarying> feedbackVars =
			{
				{ "outPos", 3 },
				{ "outVel", 3 },
			};
			dustTarget->initFeedbackBuffers ( std::span { dustData.data (), dustData.size () }, feedbackVars );
		}

		overlayDustTarget = addTarget ( "overlay-dust-layer.glsl" );
		overlayDustTarget->setEnableBlend ( true, false, lime::shaderTarget::BlendMode::add );
		overlayDustTarget->setTexture ( 0, overlayDustTexture );
		overlayDustTarget->setTexture ( 1, overlayTexture );
		overlayDustTarget->setTexture ( 2, overlayLUT_dusk );
		overlayDustTarget->setTexture ( 3, overlayLUT_night );
	}

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
		dustTarget->setBufferSizePixelsScaled ( rects[ 1 ].getWidth (), rects[ 1 ].getHeight () );
		overlayDustTarget->setBounds ( rects[ 1 ] );
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

void CRTEmulation::renderFrame ()
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

		crtTarget->setUniform_f ( "u_decayFactor", { factorR, factorG, factorB } );

		// Calculate flicker visibility if decay is very high
		constexpr auto	flickerThreshold = 0.7f;
		constexpr auto	flickerMultiplier = 1.0f / ( 1.0f - flickerThreshold );

		const auto	flicker = std::max ( crtDecay - flickerThreshold, 0.0f ) * flickerMultiplier;
		crtTarget->setUniform_f ( "u_phosphorFlicker", std::pow ( flicker, 2.0f / 3.0f ) * 0.05f );
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

	// Update dust particles
	{
		const auto	dustVisible =		curSettings.overlay
									&&	overlayTexture->isValid ()
									&&	overlayDustTexture
									&&	curSettings.overlayDust;

		dustTarget->setEnabled ( dustVisible );
		dustTarget->setUniform_f ( "deltaTime", float ( deltaTime ) );
	}

	// Framerate-independent LERP
	auto fiLerp = [] ( const float source, const float target, const float speed, const float deltaTime )
	{
		if ( juce::approximatelyEqual ( source, target ) )
			return target;

		return target + ( source - target ) * std::pow ( 1.0f - speed, deltaTime );
	};

	// Update zoom
	{
		const auto	oldZoom = currentZoom;
		currentZoom = fiLerp ( currentZoom, curSettings.overlayZoom * 0.01f, 0.99999f, deltaTime );

		if ( ! juce::approximatelyEqual ( oldZoom, currentZoom ) )
			updateZoom ();
	}

	// Update overscan
	{
		const auto	over = std::lerp ( 1.0f, 0.86f, curSettings.overscan * 0.01f );
		currentOverscan = fiLerp ( currentOverscan, over, 0.99999f, deltaTime );

		const auto	yOver = currentOverscan * ( curSettings.isNTSC == false ? 1.0f : 0.8825f );

		crtTarget->setUniform_f ( "crtOverscan", { currentOverscan, yOver } );
	}
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

	auto lerpTransfrom = [] ( const juce::AffineTransform& ta, const juce::AffineTransform& tb, const float t )
	{
		return juce::AffineTransform {	std::lerp ( ta.mat00, tb.mat00, t ),
										std::lerp ( ta.mat01, tb.mat01, t ),
										std::lerp ( ta.mat02, tb.mat02, t ),
										std::lerp ( ta.mat10, tb.mat10, t ),
										std::lerp ( ta.mat11, tb.mat11, t ),
										std::lerp ( ta.mat12, tb.mat12, t ) };
	};

	const auto	finalTrans = lerpTransfrom ( zoTrans, ziTrans, currentZoom );

	return {
		tubeRect.transformedBy ( finalTrans ),
		overlayImgRect.transformedBy ( finalTrans ),
		bezelBounds.transformedBy ( finalTrans ),
		lightBounds.transformedBy ( finalTrans ),
	};
}
//-----------------------------------------------------------------------------

bool CRTEmulation::isOverlayEnabled () const
{
	return		curSettings.overlay
			&&	overlayTexture && overlayTexture->isValid ();
}
//-----------------------------------------------------------------------------

bool CRTEmulation::isBezelEnabled () const
{
	return		isOverlayEnabled ()
			&&	bezelTexture && bezelTexture->isValid ();
}
//-----------------------------------------------------------------------------

bool CRTEmulation::isDustOrBloomEnabled () const
{
	return		isOverlayEnabled ()
			&&	overlayDustTexture && ( curSettings.overlayDust || curSettings.overlayBloom );
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
	if ( curSettings.overlay && ! ovlyProfileName.equalsIgnoreCase ( curSettings.overlayProfile ) )
			loadOverlayProfile ( curSettings.overlayProfile );

	const auto	enabled = isOverlayEnabled ();

	overlayTarget->setEnabled ( enabled );
	bezelTarget->setEnabled ( isBezelEnabled () );
	lightTarget->setEnabled ( enabled && lightTexture->isValid () );
	overlayDustTarget->setEnabled ( isDustOrBloomEnabled () );

	// CRT Mask
	setTextureSource ( crtMaskTexture, "../CRT Masks/" + curSettings.crtMaskBitmap + ".png" );

	setSettings ( curSettings );
	updateZoom ();
}
//-----------------------------------------------------------------------------

bool CRTEmulation::parseOverlayProfile ( const juce::String& profileName )
{
	ovlyProfileName = profileName;

	// Check if profile exists
	auto	file = rootOverlays.getChildFile ( profileName ).getChildFile ( "profile.yml" );
	if ( ! file.existsAsFile () )
		return false;

	static const	std::vector<std::pair<std::string, YamlConfig::ConfigValue>>	overlayDefaults
	{
		{ "multipliers/daytime",		1.0f },
		{ "multipliers/bezel",			1.0f },
		{ "multipliers/shadow",			1.0f },
		{ "multipliers/reflection",		1.0f },
		{ "multipliers/grain",			1.0f },
		{ "multipliers/bloom",			1.0f },
		{ "multipliers/light-bloom",	1.0f },
		{ "multipliers/dust",			1.0f },

		{ "screen/center",		YamlConfig::vec2f { 0.0f, 0.0f } },
		{ "screen/size",		YamlConfig::vec2f { 0.0f, 0.0f } },

		{ "bezel/radius",		30 },
		{ "bezel/zoom",			YamlConfig::vec2f { 1.0f, 1.0f } },
		{ "bezel/shift",		YamlConfig::vec2f { 0.0f, 0.0f } },

		{ "shadow/offset",		YamlConfig::vec2f { 0.2f, 0.3f } },
		{ "shadow/blur",		4.0f },
	};

	auto	yml = YamlConfig ( overlayDefaults );
	yml.load ( file );

	mulDaytime = yml.get<float> ( "multipliers/daytime" );
	mulBezel = yml.get<float> ( "multipliers/bezel" );
	mulShadow = yml.get<float> ( "multipliers/shadow" );
	mulReflection = yml.get<float> ( "multipliers/reflection" );
	mulGrain = yml.get<float> ( "multipliers/grain" );
	mulBloom = yml.get<float> ( "multipliers/bloom" );
	mulLightBloom = yml.get<float> ( "multipliers/light-bloom" );
	mulDust = yml.get<float> ( "multipliers/dust" );

	//
	// Bezel properties
	//
	{
		const auto	radius = yml.get<int> ( "bezel/radius" );
		bezelTarget->setUniform_i ( "rflRadius", std::clamp ( radius, 2, 50 ) );

		const auto [ zoomX, zoomY ] = yml.get<YamlConfig::vec2f> ( "bezel/zoom" );
		bezelTarget->setUniform_f ( "rflZoom", { zoomX, zoomY } );

		const auto [shiftX, shiftY] = yml.get<YamlConfig::vec2f> ( "bezel/shift" );
		bezelTarget->setUniform_f ( "rflShift", { shiftX * 0.1f, shiftY * 0.1f } );
	}

	//
	// Shadows
	//
	{
		const auto [ shiftX, shiftY ] = yml.get<YamlConfig::vec2f> ( "shadow/offset" );
		overlayTarget->setUniform_f ( "ovlShadowOffset", { shiftX * 0.1f, shiftY * 0.1f } );

		const auto	blur = yml.get<float> ( "shadow/blur" );
		overlayTarget->setUniform_f ( "ovlShadowBlur", blur );
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
	overlayTarget->setUniform_f ( "ovlShadow", isOverlayEnabled () * set.overlayShadow * 0.01f * mulShadow );
	overlayTarget->setUniform_f ( "ovlChromaticAberration", set.overlayChromaticAberration * 0.01f );
	overlayTarget->setUniform_f ( "ovlGrain", set.overlayGrain * 0.01f * mulGrain );

	// Overlay uniforms
	lightTarget->setUniform_f ( "ovlBloom", set.overlayBloom * 0.01f * mulLightBloom );

	overlayDustTarget->setEnabled ( isDustOrBloomEnabled () );
	overlayDustTarget->setUniform_f ( "ovlBloom", set.overlayBloom * 0.01f * mulBloom );
	overlayDustTarget->setUniform_f ( "ovlDust", set.overlayDust * 0.01f * mulDust );

	// Daytime LUT blending (for day -> dusk -> night transition)
	{
		const auto	lutBlend = set.overlayDaytime * 0.01f * mulDaytime;

		overlayTarget->setUniform_f ( "lutBlend", lutBlend );
		bezelTarget->setUniform_f ( "lutBlend", lutBlend );
		overlayDustTarget->setUniform_f ( "lutBlend", lutBlend );
	}

	// Index to YUV/YIQ encoder
	indexTarget->setUniform_f ( "decJailbars", set.encJailbars * 0.01f );

	// Signal decoder
	{
		// NTSC has a stronger brightness bias
		const auto	brightness = ( set.brightness - ( set.isNTSC ? 70.0f : 50.0f ) ) * ( 1.0f / 256.0f );
		const auto	contrast = std::lerp ( 0.4f, 1.2f, set.contrast * 0.01f );
		const auto	saturation = std::lerp ( 0.0f, 0.8f, ( set.saturation + ( set.isNTSC ? 0.0f : 5.0f ) ) * 0.01f );

		lumaChromaTarget->setUniform_f ( "encBrightnessContrastSaturation", { brightness, contrast, saturation } );
	}

	//
	// CRT emulation uniforms
	//
	{
		lumaChromaTarget->setUniform_f ( "decSharpening", set.decSharpening * 0.01f );
		lumaChromaTarget->setUniform_f ( "decLumablur", set.decLumaBlur * 0.01f );
		lumaChromaTarget->setUniform_f ( "decChromablur", set.decChromaBlur * 0.01f );

		lumaChromaTarget->setUniform_f ( "decInterference", set.decInterference * 0.01f );
		lumaChromaTarget->setUniform_f ( "decCrosstalk", set.decCrosstalk * 0.01f );
		lumaChromaTarget->setUniform_f ( "decSubcarrier", set.decSubcarrier * 0.01f );
		lumaChromaTarget->setUniform_f ( "decNoise", set.decNoise * 0.01f );

		// Bleed
		{
			crtTarget->setUniform_f ( "crtBleed", set.crtBleed * 0.01f );
			auto setUni_vec2 = [ this ] ( const char* uniName, const std::pair<int8_t, int8_t>& value )
			{
				crtTarget->setUniform_f ( uniName, { value.first * 0.01f, value.second * 0.01f } );
			};

			setUni_vec2 ( "crtRedOffset", set.crtBleedRed );
			setUni_vec2 ( "crtGreenOffset", set.crtBleedGreen );
			setUni_vec2 ( "crtBlueOffset", set.crtBleedBlue );
		}

		// H-wave
		crtTarget->setUniform_f ( "crtHoffset", set.crtHwave * 0.01f );

		// Scanlines
		crtTarget->setUniform_f ( "crtScanlines", set.crtScanlines * 0.01f );

		// Shadowmask
		crtTarget->setUniform_f ( "crtMask", set.crtMask * 0.01f );

		// Glow
		crtTarget->setUniform_f ( "crtGlow", set.crtGlow * 0.01f );

		// Ambient
		crtTarget->setUniform_f ( "crtAmbient", set.crtAmbient * 0.01f );

		// Phosphor Decay
		crtTarget->setUniform_f ( "crtRefreshRate", set.isNTSC ? 59.826f : 50.125f );

		// Curve
		crtTargetProcessed->setUniform_f ( "crtCurve", set.crtCurve * 0.01f );

		// Vignette
		crtTargetProcessed->setUniform_f ( "crtVignette", set.crtVignette * 0.01f );

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
		bezelTarget->setUniform_f ( "rflLevel", value );
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
	crtTargetProcessed->setUniform_f ( "backCol", { bckCol.getFloatRed (), bckCol.getFloatGreen (), bckCol.getFloatBlue () } );
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
	constexpr auto	channels = 3;	// YUV or YIQ
	constexpr auto	height = 3;		// three palettes stacked vertically in one texture (2x YUV + 1x YIQ)
	const auto	width = int ( palette.size () / height );

	indexTarget->lock ();
	lumaChromaTarget->lock ();

	lumaChromaPaletteSrc = { palette, width / channels, height };
	lumaChromaPalette->fromFloatVector ( lumaChromaPaletteSrc, false );

	lumaChromaTarget->unlock ();
	indexTarget->unlock ();
}
//-----------------------------------------------------------------------------

juce::Rectangle<int> CRTEmulation::loadPartialTexture ( lime::shaderTexture* dst, const juce::File& root, const int expansion )
{
	if ( ! overlayTexture->isValid () )
		return {};

	if ( auto rfl = juce::SoftwareImageType ().convert ( juce::ImageFileFormat::loadFrom ( root ) ); rfl.isARGB () )
	{
		auto	edgeRect = getCropBounds ( rfl ).expanded ( expansion, expansion );

		// Keep the edge rectangle within the bounds of the image
		rfl.getBounds ().intersectRectangle ( edgeRect );

		dst->fromImage ( rfl.getClippedImage ( edgeRect ) );

		return edgeRect;
	}

	return {};
}
//-----------------------------------------------------------------------------

juce::Rectangle<int> CRTEmulation::getCropBounds ( juce::Image& img )
{
	// Find smallest rectangle still containing all pixels from image
	const auto	bmp = juce::Image::BitmapData ( img, juce::Image::BitmapData::readOnly );
	const auto	pixCnt = bmp.width * bmp.height;

	// Top
	auto	y = 0;
	for ( ; y < pixCnt && ! bmp.data[ y * bmp.pixelStride + 3 ]; ++y );
	y /= bmp.width;

	// Bottom
	auto	h = pixCnt - 1;
	for ( ; h >= y && ! bmp.data[ h * bmp.pixelStride + 3 ]; --h );
	h /= bmp.width;

	auto isColumnUsed = [ &bmp ] ( const int col ) -> bool
	{
		auto	data = bmp.data + col * bmp.pixelStride + 3;
		int		testY = 0;
		for ( ; testY < bmp.height && ! data[ testY * bmp.lineStride ]; ++testY );
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

juce::Rectangle<int> CRTEmulation::getHoleBounds ( juce::Image& img )
{
	// Find smallest rectangle that covers all non-opaque pixels
	const auto	bmp = juce::Image::BitmapData ( img, juce::Image::BitmapData::readOnly );
	const auto	pixCnt = bmp.width * bmp.height;

	// Top
	auto	y = 0;
	for ( ; y < pixCnt && bmp.data[ y * bmp.pixelStride + 3 ] == 255; ++y );
	y /= bmp.width;

	// Bottom
	auto	h = pixCnt - 1;
	for ( ; h >= y && bmp.data[ h * bmp.pixelStride + 3 ] == 255; --h );
	h /= bmp.width;

	auto isColumnOpaque = [ &bmp ] ( const int col ) -> bool
	{
		auto	data = bmp.data + col * bmp.pixelStride + 3;
		auto	testY = 0;
		for ( ; testY < bmp.height && data[ testY * bmp.lineStride ] == 255; ++testY );
		return testY < bmp.height;
	};

	// Left
	auto	x = 0;
	for ( ; x < bmp.width && ! isColumnOpaque ( x ); ++x );

	// Right
	auto	w = bmp.width - 1;
	for ( ; w >= x && ! isColumnOpaque ( w ); --w );

	return { x, y, ( w + 1 ) - x, ( h + 1 ) - y };
}
//-----------------------------------------------------------------------------

juce::Rectangle<float> CRTEmulation::expandHoleBounds ( const juce::Rectangle<int>& hole, const float targetRatio, const float expansionPixels )
{
	auto	newRect = hole.toFloat ();
	auto	currentRatio = newRect.getWidth () / newRect.getHeight ();

	if ( currentRatio < targetRatio )
		newRect = newRect.withWidth ( newRect.getHeight () * targetRatio );
	else
		newRect = newRect.withHeight ( newRect.getWidth () / targetRatio );

	// Center it back onto the original hole
	newRect.setCentre ( hole.toFloat ().getCentre () );

	// Proportional expansion
	if ( targetRatio >= 1.0f )
		newRect.expand ( expansionPixels * targetRatio, expansionPixels );
	else
		newRect.expand ( expansionPixels, expansionPixels / targetRatio );

	return newRect;
}
//-----------------------------------------------------------------------------

void CRTEmulation::run ()
{
	do
	{
		if ( isShowing () && isWebcamNeeded () )
		{
			addWebcamListener ();
			if ( ! camera )
				return;
		}
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
		if ( ! camera->getError ().empty () )
		{
			Z_WARN ( "Webcam error: " << camera->getError () );
			camera.reset ();
			return;
		}

		camera->onDataReceived = [ this ] ( uint8_t* dataY, uint8_t* dataUV, int width, int height, int strideY, int strideUV, pixFmt format )
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
						std::memmove ( camImageNV12_Y.getData (), dataY, width * height );
					}
					else
					{
						// If stride is larger than width, we need to copy line by line
						for ( auto y = 0; y < height; ++y )
							std::memmove ( camImageNV12_Y.getLinePointer ( y ), dataY + y * strideY, width );
					}
					webcamTextureNV12_Y->fromImage ( camImageNV12_Y, false );
				}

				// Upload UV as texture
				{
					if ( width == strideUV )
					{
						std::memmove ( camImageNV12_UV.getData (), dataUV, ( width / 2 ) * ( height / 2 ) * 2 );
					}
					else
					{
						// If stride is larger than width, we need to copy line by line
						for ( auto y = 0; y < height / 2; ++y )
							std::memmove ( camImageNV12_UV.getLinePointer ( y ), dataUV + y * strideUV, width );
					}
					webcamTextureNV12_UV->fromImage ( camImageNV12_UV, false );
				}
			}
			else if ( format == pixFmt::YUY2 )
			{
			}
		};
	}

	if ( isCamInUse || ! camera )
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
