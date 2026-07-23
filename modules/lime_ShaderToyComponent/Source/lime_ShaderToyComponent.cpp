#include "lime_ShaderToyComponent.h"
#include <regex>

#include <chrono>

namespace lime
{
//-----------------------------------------------------------------------------

const static std::string	frameTimeShader = R"(
uniform float iDelta;
uniform float iGPUTime;
uniform float iPercent;

float digitBin ( const int x )
{
    const int digits[10] = int[](
        480599,
        139810,
        476951,
        476999,
        350020,
        464711,
        464727,
        476228,
        481111,
        481095
    );

    return float ( digits[ x ] );
}

float printValue ( vec2 vStringCoords, float fValue, float fMaxDigits, float fDecimalPlaces )
{
    bool	bNeg = ( fValue < 0.0 );
	fValue = abs ( fValue );

	float	fLog10Value = log2 ( abs ( fValue ) ) / log2 ( 10.0 );
	float	fBiggestIndex = max ( floor ( fLog10Value ), 0.0 );
	float	fDigitIndex = fMaxDigits - floor ( vStringCoords.x );
	float	fCharBin = 0.0;
	if ( fDigitIndex > ( -fDecimalPlaces - 1.01 ) )
	{
		if ( fDigitIndex > fBiggestIndex )
		{
			if ( ( bNeg ) && ( fDigitIndex < ( fBiggestIndex + 1.5 ) ) )
				fCharBin = 1792.0;
		}
		else
		{
			if ( fDigitIndex == -1.0 )
			{
				if ( fDecimalPlaces > 0.0 )
					fCharBin = 2.0;
			}
			else
			{
				float	fReducedRangeValue = fValue;
				if ( fDigitIndex < 0.0 )
				{
					fReducedRangeValue = fract( fValue );
					fDigitIndex += 1.0;
				}
				float	fDigitValue = ( abs ( fReducedRangeValue / ( pow ( 10.0, fDigitIndex ) ) ) );
				fCharBin = digitBin ( int ( floor ( mod ( fDigitValue, 10.0 ) ) ) );
			}
        }
	}
	return floor ( mod ( ( fCharBin / pow ( 2.0, floor ( fract ( vStringCoords.x ) * 4.0 ) + ( floor ( vStringCoords.y * 5.0 ) * 4.0 ) ) ), 2.0 ) );
}

void main ()
{
	vec2	uv = fragCoord * 10.0;
	float	text = 0.0;

	text += printValue ( uv - vec2 ( 0.5, 4.5 ), iDelta, 2.0, 2.0 );
	text += printValue ( uv - vec2 ( 0.5, 2.5 ), iGPUTime, 2.0, 2.0 );
	text += printValue ( uv - vec2 ( 0.5, 0.5 ), iPercent, 2.0, 2.0 );

	fragColor = vec4 ( vec3 ( text ), max ( 0.33, text ) );
}
)";
//-----------------------------------------------------------------------------

ShaderToyComponent::ShaderToyComponent ( const bool canHaveChildren, const int _idleTimeout )
	: idleTimeout ( _idleTimeout )
{
	setName ( "ShaderToyComponent" );
	setOpaque ( true );

	// Use a separate listener object to avoid inheritance ambiguity
	globalListener.onMouseMove = [ this ] ( const juce::MouseEvent& e ) { handleGlobalMouseMove ( e ); };
	globalListener.onMouseUp = [ this ] ( const juce::MouseEvent& e ) { handleGlobalMouseUp ( e ); };

	// Register this separate object globally
	if ( idleTimeout > 0 )
		juce::Desktop::getInstance ().addGlobalMouseListener ( &globalListener );

	// File system watcher to reload shaders and textures on change
	fsWatcher.addListener ( this );
	fsWatcher.coalesceEvents ( 50 );

	#if JUCE_MAC
		openGLContext.setOpenGLVersionRequired ( juce::OpenGLContext::OpenGLVersion::openGL4_1 );
	#else
		openGLContext.setOpenGLVersionRequired ( juce::OpenGLContext::OpenGLVersion::openGL4_3 );
	#endif

	// Attach the OpenGL context
	juce::OpenGLPixelFormat	pf;
	pf.depthBufferBits = 0;
	openGLContext.setPixelFormat ( pf );

	openGLContext.setComponentPaintingEnabled ( canHaveChildren );
	openGLContext.setRenderer ( this );
	openGLContext.attachTo ( *this );
	openGLContext.setSwapInterval ( 1 );

	// Add fragment shader to frameTimeTarget
	frameTimeTarget.setShaders ( frameTimeShader );
}
//-----------------------------------------------------------------------------

ShaderToyComponent::~ShaderToyComponent ()
{
	if ( idleTimeout > 0 )
		juce::Desktop::getInstance ().removeGlobalMouseListener ( &globalListener );

	openGLContext.detach ();
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::newOpenGLContextCreated ()
{
	// You should at least have one quad on the screen
	jassert ( targets.size () != 0 );
	if ( targets.empty () )
		return;

	#if JUCE_WINDOWS || JUCE_LINUX
		// Silence these chatty info messages that aren't useful for us
		if ( juce::gl::glDebugMessageControl )
			juce::gl::glDebugMessageControl ( juce::gl::GL_DONT_CARE, juce::gl::GL_DONT_CARE, juce::gl::GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, juce::gl::GL_FALSE );
	#endif

	juce::gl::glDepthRange ( 0.0, 1.0 );
	juce::gl::glEnable ( juce::gl::GL_DEPTH_CLAMP );
	juce::gl::glDisable ( juce::gl::GL_DEPTH_TEST );
	juce::gl::glDisable ( juce::gl::GL_SCISSOR_TEST );
	juce::gl::glDisable ( juce::gl::GL_CULL_FACE );

	for ( auto& tex : textures )
		tex->textureUpdated = true;

	for ( auto& tgt : targets )
		tgt->newContext ();

	frameTimeTarget.newContext ();

	juce::gl::glGenQueries ( 2, &renderTimeQuery[ 0 ] );

	for ( auto i = 0; i < std::ssize ( renderTimeQuery ); ++i )
	{
		juce::gl::glBeginQuery ( juce::gl::GL_TIME_ELAPSED, renderTimeQuery[ i ] );
		juce::gl::glEndQuery ( juce::gl::GL_TIME_ELAPSED );
	}
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::openGLContextClosing ()
{
	juce::gl::glDeleteQueries ( 2, &renderTimeQuery[ 0 ] );

	// You should at least have one quad on the screen
	jassert ( targets.size () != 0 );
	if ( targets.empty () )
		return;

	for ( auto& tex : textures )
		tex->glTexture.release ();

	for ( auto& tgt : targets )
		tgt.get ()->losingContext ();

	frameTimeTarget.losingContext ();
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::renderOpenGL ()
{
	calcDeltaTime ();
	renderFrame ();

	jassert ( juce::OpenGLHelpers::isContextActive () );

	// You should at least have one quad on the screen
	if ( targets.empty () )
		return;

	// Get GPU time from previous frame
	const auto	shouldMeassure = measureRenderTime.load ();
	if ( shouldMeassure )
	{
		queryIndex ^= 1;

		GLuint	queryReady = 0;
		juce::gl::glGetQueryObjectuiv ( renderTimeQuery[ queryIndex ^ 1 ], juce::gl::GL_QUERY_RESULT_AVAILABLE, &queryReady );

		if ( queryReady )
		{
			GLuint64	timeNS = 0;
			juce::gl::glGetQueryObjectui64v ( renderTimeQuery[ queryIndex ^ 1 ], juce::gl::GL_QUERY_RESULT, &timeNS );
			lastGpuTimeMS = timeNS / 1000000.0;
		}

		// Measure render time
		juce::gl::glBeginQuery ( juce::gl::GL_TIME_ELAPSED, renderTimeQuery[ queryIndex ] );
	}

	// Get viewport scale, width, and height
	auto	renderingScale = float ( openGLContext.getRenderingScale () );
	auto	viewportWidth = float ( getWidth () );
	auto	viewportHeight = float ( getHeight () );

	juce::gl::glBindFramebuffer ( juce::gl::GL_FRAMEBUFFER, 0 );

	//
	// Set glViewport
	//
	juce::gl::glViewport ( 0, 0, int ( viewportWidth * renderingScale ), int ( viewportHeight * renderingScale ) );

	//
	// Clear background
	//
	if ( backgroundColor[ 3 ] > 0.0f )
	{
		juce::gl::glClearColor ( backgroundColor[ 0 ], backgroundColor[ 1 ], backgroundColor[ 2 ], backgroundColor[ 3 ] );
		juce::gl::glClear ( juce::gl::GL_COLOR_BUFFER_BIT );
	}

	//
	// Render targets
	//
	for ( auto& tup : targets )
		tup.get ()->render ( viewportWidth, viewportHeight, renderingScale );

	if ( shouldMeassure )
	{
		juce::gl::glEndQuery ( juce::gl::GL_TIME_ELAPSED );

		if ( displayRenderTime.load () )
		{
			const auto	deltaTime = getDeltaTime ();

			deltaTime_cur = std::lerp ( deltaTime_cur, deltaTime, 0.1 );
			lastGpuTimeMS_cur = std::lerp ( lastGpuTimeMS_cur, lastGpuTimeMS, 0.1 );

			const auto	loadPercentage = lastGpuTimeMS_cur / ( deltaTime_cur * 10.0 );

			frameTimeTarget.setUniform_f ( "iDelta", float ( deltaTime_cur * 1000.0 ) );
			frameTimeTarget.setUniform_f ( "iGPUTime", float ( lastGpuTimeMS_cur ) );
			frameTimeTarget.setUniform_f ( "iPercent", float ( loadPercentage ) );

			frameTimeTarget.setSize ( 100, 100 );
			frameTimeTarget.render ( viewportWidth, viewportHeight, renderingScale );
		}
	}

	if ( captureAddress )
		juce::gl::glReadPixels ( 0, 0,
								 int ( viewportWidth * renderingScale ), int ( viewportHeight * renderingScale ),
								 juce::gl::GL_RGB, juce::gl::GL_UNSIGNED_BYTE, captureAddress );

	lastFrameRendered.store ( lastFrameRequested.load () );

	// Reset OpenGL state, so JUCE doesn't get confused
	juce::gl::glUseProgram ( 0 );
	juce::gl::glBindBuffer ( juce::gl::GL_ARRAY_BUFFER, 0 );
	juce::gl::glBindVertexArray ( 0 );
	juce::gl::glEnable ( juce::gl::GL_BLEND );
	juce::gl::glBlendFunc ( juce::gl::GL_ONE, juce::gl::GL_ONE_MINUS_SRC_ALPHA );
	juce::gl::glBindFramebuffer ( juce::gl::GL_FRAMEBUFFER, 0 );
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::resized ()
{
	const auto	b = getLocalBounds ().toFloat ();

	// Set new bounds for all targets
	for ( auto& tgt : targets )
		if ( tgt->wantsViewportSize () )
			tgt->setBounds ( b );
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::setRoot ( const juce::File& _root, const juce::File& _local, const juce::String& _shaderFolderName/* = "!Shaders"*/, const juce::String& _textureFolderName/* = "!Textures"*/ )
{
	shaderFolderName = _shaderFolderName;
	textureFolderName = _textureFolderName;

	fsWatcher.removeFolder ( root );
	fsWatcher.removeFolder ( localRoot );

	root = _root;
	localRoot = _local;

	if ( root == juce::File () && localRoot == juce::File () )
		return;

	fsWatcher.addFolder ( root );
	if ( localRoot != juce::File () && ! localRoot.isAChildOf ( root ) && root != localRoot )
		fsWatcher.addFolder ( localRoot );

	// (Re)load all shaders
	for ( auto& dst : targets )
		dst->setShaders ( loadShader ( dst->getName () ) );

	// (Re)load all textures
	for ( auto& dst : textures )
		if ( ! dst->name.startsWithChar ( '/' ) && dst->load )
			if ( auto file = findFile ( dst->name ); file.existsAsFile () )
				dst->load ( dst.get (), file);
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::setPipeline ( const juce::File& _file )
{
 	renderPipeline = _file;

 	if ( renderPipeline == juce::File () )
 		return;

	hardcodedTextureCount = textures.size ();
	hardcodedShaderCount = targets.size ();

	parsePipeline ();
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::parsePipeline ()
{
	resetPipeline ();

	auto	jsonStr = renderPipeline.loadFileAsString ().toStdString ();

	// Remove all C++ style comments from string
	jsonStr = std::regex_replace ( jsonStr, std::regex ( R"rx(\/\/.*|\/\*[\s\S]*?\*\/|("(\\.|[^"])*"))rx" ), "$1" );

	// Remove unecessary white-space from the start (easier to debug)
	jsonStr = std::regex_replace ( jsonStr, std::regex ( R"rx(^\s*$)rx" ), "" );

	auto	rootObj = juce::var ();
	if ( auto err = juce::JSON::parse ( jsonStr, rootObj ); ! err.wasOk () )
	{
		juce::Logger::writeToLog ( "[E]" + renderPipeline.getFullPathName () + " parse error" );
		juce::Logger::writeToLog ( "[E]" + err.getErrorMessage () );
		return;
	}

	if ( ! rootObj.isObject () )
	{
		juce::Logger::writeToLog ( "[E]" + renderPipeline.getFullPathName () + " has to be an object" );
		return;
	}

	// Parse array of strings/objects
	{
		if ( ! rootObj.hasProperty ( "layers" ) || ! rootObj[ "layers" ].isArray () )
		{
			juce::Logger::writeToLog ( "[E]" + renderPipeline.getFullPathName () + " needs a \"layers\" object that is an array" );
			return;
		}

		auto&	layersArray = *rootObj[ "layers" ].getArray ();

		for ( auto index = 0; auto& shader : layersArray )
		{
			const auto	isLastTarget = ++index == layersArray.size ();
			auto	tgtFile = ( shader.isString () ? shader : shader[ "shader" ] ).toString ();
			auto	tgtName = tgtFile;

			if ( shader.isObject () )
				tgtName = shader.getProperty ( "name", tgtFile );

			auto	tgt = addTarget ( tgtName, tgtFile );
			tgt->setEnableBlend ( false );

			// Defaults for shader
			auto	tgtAutoSize = true;
			auto	tgtGenerateMipmaps = false;
			auto	tgtSquareMode = false;
			auto	tgtScale = 1.0f;

			if ( shader.isObject () )
			{
				// Get properties for shader
				tgtAutoSize = shader.getProperty ( "autosize", tgtAutoSize );
				tgtGenerateMipmaps = shader.getProperty ( "mipmap", tgtGenerateMipmaps );
				tgtScale = shader.getProperty ( "rect", tgtScale );
				tgtScale = shader.getProperty ( "square", tgtScale );
				tgtSquareMode = shader.hasProperty ( "square" );

				// Get textures
				{
					auto&	tgtTextures = shader[ "textures" ];
					if ( tgtTextures.isArray () )
					{
						for ( auto txtIndex = 0; auto& tex : *tgtTextures.getArray () )
						{
							const auto	texName = ( tex.isString () ? tex : tex[ "name" ] ).toString ();

							auto	txt = getTexture ( texName );

							if ( ! txt && texName.startsWithChar ( '/' ) )
							{
								juce::Logger::writeToLog ( "[E]texture " + texName + " does not exist. Textures starting with a \"/\" are reserved for pre-allocated textures" );
							}
							else
							{
								if ( txt == nullptr )
								{
									txt = addTexture ( texName, [] ( shaderTexture* dst, const juce::File& f ) {
										if ( auto sit = juce::SoftwareImageType ().convert ( juce::ImageFileFormat::loadFrom ( f ) ); sit.isValid () )
											dst->fromImage ( sit );
									} );
								}

								// Defaults for texture
								{
									auto	texClamp = int ( juce::gl::GL_REPEAT );
									auto	texFilter = true;
									auto	texBorderColor = juce::Colour ( 0 );

									// Get properties for texture
									if ( tex.isObject () )
									{
										// Clamp mode
										{
											const auto	clampStr = tex.getProperty ( "wrap", "repeat" ).toString ();

											if ( clampStr == "repeat" )			texClamp = juce::gl::GL_REPEAT;
											else if ( clampStr == "edge" )		texClamp = juce::gl::GL_CLAMP_TO_EDGE;
											else if ( clampStr == "mirror" )	texClamp = juce::gl::GL_MIRRORED_REPEAT;
											else if ( clampStr == "border" )	texClamp = juce::gl::GL_CLAMP_TO_BORDER;
											else
											{
												juce::Logger::writeToLog ( "[E]unknown wrap mode: " + clampStr + " for texture " + tex.toString () + " (target " + tgtName + ")" );
											}
										}

										// Filter
										{
											const auto	filterStr = tex.getProperty ( "filter", "linear" ).toString ();

											if ( filterStr == "near" )			texFilter = false;
											else if ( filterStr == "linear" )	texFilter = true;
											else
											{
												juce::Logger::writeToLog ( "[E]unknown filter mode: " + filterStr + " for texture " + tex.toString () + " (target " + tgtName + ")" );
											}
										}

										// Border color
										{
											const auto	txtColStr = tex.getProperty ( "color", "0, 0, 0, 0" ).toString ().trim ();

											auto	colLines = juce::StringArray::fromTokens ( txtColStr, ",", "" );

											if ( colLines.size () == 3 )
												colLines.add ( "1.0" ); // Add alpha

											if ( colLines.size () == 4 )
												texBorderColor = juce::Colour::fromFloatRGBA ( colLines[ 0 ].getFloatValue (), colLines[ 1 ].getFloatValue (), colLines[ 2 ].getFloatValue (), colLines[ 3 ].getFloatValue () );
											else
											{
												juce::Logger::writeToLog ( "[E]unknown color: " + txtColStr + " for texture " + tex.toString () + " (target " + tgtName + ")" );
											}
										}
									}

									tgt->setTexture ( txtIndex, txt );
									tgt->setTextureClampMode ( txtIndex, texClamp );
									tgt->setTextureFilter ( txtIndex, texFilter );
									tgt->setTextureBorderColor ( txtIndex, texBorderColor );
								}
							}

							++txtIndex;
						}
					}
				}

				// Get uniforms
				{
					auto&	tgtUniforms = shader[ "uniforms" ];
					if ( tgtUniforms.isArray () )
					{
						for ( auto& uni : *tgtUniforms.getArray () )
						{
							const auto	uniObj = uni.getDynamicObject ();
							if ( ! uniObj || uniObj->getProperties ().isEmpty () )
								continue;

							const auto&	prop = *uniObj->getProperties ().begin ();

							const auto	uniName = prop.name.toString ().toStdString ();

							auto	value = prop.value;

							if ( value.isDouble () )
								tgt->setUniform_f ( uniName, float ( double ( value ) ) );
							else if ( value.isArray () )
							{
								std::vector<float>		vec;
								for ( auto& val : *value.getArray () )
									vec.emplace_back ( float ( double ( val ) ) );

								tgt->setUniform_f ( uniName, vec );
							}
						}
					}
				}
			}

			// Create texture for target
			if ( ! isLastTarget )
			{
				if ( tgtSquareMode )
					tgt->setBufferSizeSquareRelative ( tgtScale );
				else
					tgt->setBufferSizeRelative ( tgtScale );

				auto	txt = addTexture ( tgtName );
				txt->generateMipmaps = tgtGenerateMipmaps;

				tgt->setTargetBuffer ( txt );
			}

			tgt->setAutoSize ( tgtAutoSize );
		}
	}

	// Get global uniforms
	{
		auto&	glbUniforms = rootObj[ "uniforms" ];
		if ( glbUniforms.isArray () )
		{
			for ( auto& uni : *glbUniforms.getArray () )
			{
				const auto	uniObj = uni.getDynamicObject ();
				if ( ! uniObj || uniObj->getProperties ().isEmpty () )
					continue;

				const auto& prop = *uniObj->getProperties ().begin ();

				const auto	uniName = prop.name.toString ().toStdString ();

				auto	value = prop.value;

				if ( value.isDouble () )
				{
					for ( auto& tgt : targets )
						tgt->setUniform_f ( uniName, float ( double ( value ) ) );
				}
				else if ( value.isArray () )
				{
					std::vector<float>		vec;
					for ( auto& val : *value.getArray () )
						vec.emplace_back ( float ( double ( val ) ) );

					for ( auto& tgt : targets )
						tgt->setUniform_f ( uniName, vec );
				}
			}
		}
	}
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::resetPipeline ()
{
	// Remove all dynamic pipeline textures and targets
	textures.resize ( hardcodedTextureCount );
	targets.resize ( hardcodedShaderCount );
}
//-----------------------------------------------------------------------------

shaderTarget* ShaderToyComponent::addTarget ( const juce::String& name, juce::String file )
{
	jassert ( name.isNotEmpty () );

	auto	tgt = std::make_unique<shaderTarget> ( openGLContext, name );

	if ( file.isEmpty () )
		file = name;

	setTargetShader ( tgt.get (), name, file );

	return ( targets.emplace_back ( std::move ( tgt ) ) ).get ();
}
//-----------------------------------------------------------------------------

shaderTexture* ShaderToyComponent::addTexture ( const juce::String& name, std::function<void ( shaderTexture*, const juce::File& )> load )
{
	jassert ( name.isNotEmpty () );

	auto	txt = std::make_unique<shaderTexture> ();

	txt->load = std::move ( load );

	setTextureSource ( txt.get (), name );

	return ( textures.emplace_back ( std::move ( txt ) ) ).get ();
}
//-----------------------------------------------------------------------------

shaderTarget* ShaderToyComponent::getTarget ( const juce::String& name )
{
	jassert ( name.isNotEmpty () );

	// Using any map would be slower than looping through 5-10 targets
	for ( auto& tgt : targets )
		if ( tgt->getName () == name )
			return tgt.get ();

	return nullptr;
}
//-----------------------------------------------------------------------------

shaderTexture* ShaderToyComponent::getTexture ( const juce::String& name )
{
	jassert ( name.isNotEmpty () );

	// Using any map would be slower than looping through 5-10 textures
	for ( auto& txt : textures )
		if ( txt->name == name )
			return txt.get ();

	return nullptr;
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::setTargetShader ( shaderTarget* dst, const juce::String& name, juce::String file )
{
	jassert ( dst );
	jassert ( name.isNotEmpty () );

	dst->setName ( name );
	if ( file.isEmpty () )
		file = name;

	dst->setFile ( file );
	dst->setShaders ( loadShader ( file ) );
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::setTextureSource ( shaderTexture* dst, const juce::String& name )
{
	jassert ( dst );
	jassert ( name.isNotEmpty () );

	if ( dst->name == name )
		return;

	dst->unload ();
	dst->name = name;

	if ( ! name.startsWithChar ( '/' ) && dst->load )
		if ( auto file = findFile ( name ); file.existsAsFile () )
			dst->load ( dst, file );
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::setBackgroundColor ( juce::Colour col )
{
	backgroundColor[ 0 ] = GLfloat ( col.getFloatRed () );
	backgroundColor[ 1 ] = GLfloat ( col.getFloatGreen () );
	backgroundColor[ 2 ] = GLfloat ( col.getFloatBlue () );
	backgroundColor[ 3 ] = GLfloat ( col.getFloatAlpha () );
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::setFrameAndTime ( const int iFrame, const float iTime )
{
	lastFrameRequested = iFrame;

	for ( auto& tgt : targets )
	{
		tgt->setUniform_i ( "iFrame", iFrame );
		tgt->setUniform_f ( "iTime", iTime );
	}

	openGLContext.triggerRepaint ();
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::setGlobalUniform ( const std::string& name, const std::initializer_list<const float>& values )
{
	setGlobalUniform ( name, std::span<const float> ( values ) );
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::setGlobalUniform ( const std::string& name, const std::span<const float>& values )
{
	for ( auto& tgt : targets )
		tgt->setUniform_f ( name, values );
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::setGlobalUniform ( const std::string& name, const float value )
{
	for ( auto& tgt : targets )
		tgt->setUniform_f ( name, value );
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::setGlobalUniform ( const std::string& name, const int value )
{
	for ( auto& tgt : targets )
		tgt->setUniform_i ( name, value );
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::setCaptureAddress ( void* addr )
{
	captureAddress = addr;

	openGLContext.executeOnGLThread ( [ this ] ( juce::OpenGLContext& ctx )
	{
		ctx.setSwapInterval ( captureAddress ? 0 : 1 );
	}, true );
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::enableRenderTimeMeasurement ( const bool enable )
{
	measureRenderTime = enable;
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::enableRenderTimeDisplay ( const bool enable )
{
	displayRenderTime = enable;
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::calcDeltaTime ()
{
	const auto	currentTime = std::chrono::steady_clock::now ();
	const std::chrono::duration<double>	_deltaTime = currentTime - lastTime;
	lastTime = currentTime;

	lastDeltaTime.store ( std::min ( _deltaTime.count (), 0.1 ) );
}
//-----------------------------------------------------------------------------

juce::File ShaderToyComponent::findFile ( const juce::String& name )
{
	auto	file = juce::File ();

	if ( localRoot != juce::File () )
		if ( file = localRoot.getChildFile ( name ); file.existsAsFile () )
			return file;

	if ( root != juce::File () )
	{
		if ( name.endsWithIgnoreCase ( ".glsl" ) )
		{
			if ( file = root.getChildFile ( shaderFolderName + "/" + name ); file.existsAsFile () )
				return file;
		}
		else
		{
			if ( file = root.getChildFile ( textureFolderName + "/" + name ); file.existsAsFile () )
				return file;
		}
	}

	return {};
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::fileChanged ( const juce::File& file, gin::FileSystemWatcher::FileSystemEvent /*event*/ )
{
	if ( file == renderPipeline )
	{
		juce::MessageManager::callAsync ( [ this ] {

			openGLContext.detach ();

			parsePipeline ();

			openGLContext.attachTo ( *this );

			resized ();
		} );

		return;
	}

	// Build list of all shaders that need to be reloaded, based on dependencies
	{
		std::set<std::string>	shadersToReload;

		// First check dependencies
		for ( const auto& [ shader, dependencies ] : shaderDependencies )
			for ( const auto& dep : dependencies )
				if ( findFile ( dep ) == file )
					shadersToReload.insert ( shader );

		// Check file matches
		for ( const auto& dst : targets )
			if ( findFile ( dst->getFile () ) == file )
				shadersToReload.insert ( dst->getFile ().toStdString () );

		// Reload shaders
		for ( auto& shader : targets )
			if ( shadersToReload.contains ( shader->getFile ().toStdString () ) )
				shader->setShaders ( loadShader ( shader->getFile () ) );
	}

	// Reload textures
	for ( auto& txt : textures )
		if ( ! txt->glTexture.isTarget () && txt->load )
			if ( auto txtFile = findFile ( txt->name ); txtFile == file && txtFile.existsAsFile () )
				txt->load ( txt.get (), txtFile );
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::processRecursive ( std::string& source, std::set<std::string>& dependencies )
{
	// Matches #include "filename" or #include <filename>
	static const std::regex	includeRegex ( R"(^\s*#include\s+["<]([^">]+)[">])", std::regex_constants::multiline );

	std::smatch	match;

	while ( std::regex_search ( source, match, includeRegex ) )
	{
		auto	fileName = match[ 1 ].str ();

		// Add to our hot-reload tracking list
		dependencies.insert ( fileName );

		// Load content and recursively process it before injection
		auto	includeContent = findFile ( fileName ).loadFileAsString ().toStdString ();
		processRecursive ( includeContent, dependencies );

		// Replace the #include line with the processed content
		source.replace ( match.position (), match.length (), includeContent );
	}
}
//-----------------------------------------------------------------------------

juce::String ShaderToyComponent::loadShader ( juce::String name )
{
	if ( ! name.endsWithIgnoreCase ( ".glsl" ) )
		return {};

	if ( name.startsWithChar ( '/' ) )
		name = name.substring ( 1 );

	auto	shaderStr = findFile ( name ).loadFileAsString ();
	if ( shaderStr.isEmpty () )
	{
		juce::Logger::writeToLog ( "[E]Can't find shader named " + name.quoted () );
		return {};
	}

	//
	// Pre-process includes
	//
	std::string				shaderSource = shaderStr.toStdString ();
	std::set<std::string>	dependencies;
	processRecursive ( shaderSource, dependencies );
	shaderStr = shaderSource;

	shaderDependencies[ name.toStdString () ] = dependencies;

	// Check for common-file
	if ( ! shaderStr.containsIgnoreCase ( "#version" ) && dependencies.empty () )
	{
		if ( auto commonStr = findFile ( "common.glsl" ).loadFileAsString (); commonStr.isNotEmpty () )
		{
			shaderDependencies[ name.toStdString () ] = { "common.glsl" };
			return commonStr + "\n" + shaderStr;
		}
	}

	return shaderStr;
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::handleGlobalMouseMove ( const juce::MouseEvent& e )
{
	const auto	screenPos = e.getScreenPosition ();

	if ( screenPos == lastMouseScreenPos )
		return;

	if ( ! curCursorVisible )
		if ( screenPos.getDistanceFrom ( mouseHideScreenPos ) < 15 )
			return;

	lastMouseScreenPos = screenPos;

	if ( e.mods.isAnyMouseButtonDown () )
		return;

	processStateAt ( screenPos );
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::handleGlobalMouseUp ( const juce::MouseEvent& e )
{
	const auto [ isOverChild, isOverThis ] = getUIVisibility ( e.getScreenPosition () );

	if ( ! isOverChild && isOverThis && ! isTimerRunning () )
		startTimer ( idleTimeout );
}
//-----------------------------------------------------------------------------

std::pair<bool, bool> ShaderToyComponent::getUIVisibility ( const juce::Point<int> screenPos )
{
	auto	localPos = getLocalPoint ( nullptr, screenPos );

	const auto* hit = getComponentAt ( localPos );
	const auto	isOverChild = ( hit != nullptr && hit != this );
	const auto	isOverThis = getLocalBounds ().contains ( localPos );

	return { isOverChild, isOverThis };
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::processStateAt ( const juce::Point<int> screenPos )
{
	const auto [ isOverChild, isOverThis ] = getUIVisibility ( screenPos );

	if ( isOverChild )
	{
		stopTimer ();
		updateUI ( true, true );
	}
	else if ( isOverThis )
	{
		updateUI ( true, true );
		startTimer ( idleTimeout );
	}
	else
	{
		if ( ! isTimerRunning () )
			startTimer ( idleTimeout );
	}
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::timerCallback ()
{
	stopTimer ();

	auto	isCurrentlyOverUs = getScreenBounds ().contains ( juce::Desktop::getMousePosition () );

	updateUI ( false, ! isCurrentlyOverUs );
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::updateUI ( const bool childrenVisible, const bool cursorVisible )
{
	if ( childrenVisible == curChildrenVisible && cursorVisible == curCursorVisible )
		return;

	if ( curCursorVisible && ! cursorVisible )
		mouseHideScreenPos = juce::Desktop::getMousePosition ();

	curChildrenVisible = childrenVisible;
	curCursorVisible = cursorVisible;

	for ( auto* child : getChildren () )
		child->setVisible ( childrenVisible );

	setMouseCursor ( cursorVisible ? juce::MouseCursor::NormalCursor : juce::MouseCursor::NoCursor );
}
//-----------------------------------------------------------------------------

#if JUCE_DEBUG
static const char* getGLErrorMessage ( const GLenum e ) noexcept
{
	switch ( e )
	{
		case juce::gl::GL_INVALID_ENUM:                   return "GL_INVALID_ENUM";
		case juce::gl::GL_INVALID_VALUE:                  return "GL_INVALID_VALUE";
		case juce::gl::GL_INVALID_OPERATION:              return "GL_INVALID_OPERATION";
		case juce::gl::GL_OUT_OF_MEMORY:                  return "GL_OUT_OF_MEMORY";
		case juce::gl::GL_STACK_OVERFLOW:                 return "GL_STACK_OVERFLOW";
		case juce::gl::GL_STACK_UNDERFLOW:                return "GL_STACK_UNDERFLOW";
		case juce::gl::GL_INVALID_FRAMEBUFFER_OPERATION:  return "GL_INVALID_FRAMEBUFFER_OPERATION";
		default: break;
	}

	return "Unknown error";
}

[[ maybe_unused ]] static void checkGLError ()
{
	for ( ;; )
	{
		const auto	e = juce::gl::glGetError ();

		if ( e == juce::gl::GL_NO_ERROR )
			break;

		juce::Logger::writeToLog ( "[E]" + juce::String ( getGLErrorMessage ( e ) ) );
		jassertfalse;
	}
}
#endif
//-----------------------------------------------------------------------------
}