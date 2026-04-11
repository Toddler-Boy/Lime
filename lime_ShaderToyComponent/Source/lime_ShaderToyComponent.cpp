#include "lime_ShaderToyComponent.h"
#include <regex>

namespace lime
{
//-----------------------------------------------------------------------------

ShaderToyComponent::ShaderToyComponent ( const bool canHaveChildren )
{
	fsWatcher.addListener ( this );

	setName ( "ShaderToyComponent" );
	setOpaque ( true );

	fsWatcher.coalesceEvents ( 50 );

	#if JUCE_MAC
		openGLContext.setOpenGLVersionRequired ( juce::OpenGLContext::OpenGLVersion::openGL4_1 );
	#else
		openGLContext.setOpenGLVersionRequired ( juce::OpenGLContext::OpenGLVersion::openGL4_3 );
	#endif

	// Attach the OpenGL context
	openGLContext.setComponentPaintingEnabled ( canHaveChildren );
	openGLContext.setRenderer ( this );
	openGLContext.attachTo ( *this );
	openGLContext.setSwapInterval ( 1 );
}
//-----------------------------------------------------------------------------

ShaderToyComponent::~ShaderToyComponent ()
{
	openGLContext.detach ();
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::newOpenGLContextCreated ()
{
	// You should at least have one quad on the screen
	jassert ( targets.size () != 0 );
	if ( targets.empty () )
		return;

	juce::gl::glDepthRange ( 0.0, 1.0 );
	juce::gl::glEnable ( juce::gl::GL_DEPTH_CLAMP );
	juce::gl::glDisable ( juce::gl::GL_DEPTH_TEST );
	juce::gl::glDisable ( juce::gl::GL_SCISSOR_TEST );
	juce::gl::glDisable ( juce::gl::GL_CULL_FACE );

	for ( auto& tex : textures )
		tex->textureUpdated = true;

	for ( auto& tgt : targets )
		tgt->newContext ();
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::openGLContextClosing ()
{
	// You should at least have one quad on the screen
	jassert ( targets.size () != 0 );
	if ( targets.empty () )
		return;

	for ( auto& tex : textures )
		tex->glTexture.release ();

	for ( auto& tgt : targets )
		tgt.get ()->losingContext ();
}
//-----------------------------------------------------------------------------

void ShaderToyComponent::renderOpenGL ()
{
	jassert ( juce::OpenGLHelpers::isContextActive () );

	// You should at least have one quad on the screen
	if ( targets.empty () )
		return;

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

	if ( captureAddress )
		juce::gl::glReadPixels ( 0, 0,
								 int ( viewportWidth * renderingScale ), int ( viewportHeight * renderingScale ),
								 juce::gl::GL_RGB, juce::gl::GL_UNSIGNED_BYTE, captureAddress );

	lastFrameRendered.store ( lastFrameRequested.load () );
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
		dst->setShaderProgram ( loadFragmentShader ( dst->getName () ) );

	// (Re)load all textures
	for ( auto& dst : textures )
		if ( ! dst->name.startsWithChar ( '/' ) && dst->load )
			dst->load ( dst.get (), root );
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
			const auto	tgtName = ( shader.isString () ? shader : shader[ "shader" ] ).toString ();

			auto	tgt = addTarget ( tgtName );
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
							const auto&	prop = *uni.getDynamicObject ()->getProperties ().begin ();

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
				const auto& prop = *uni.getDynamicObject ()->getProperties ().begin ();

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

shaderTarget* ShaderToyComponent::addTarget ( const juce::String& name )
{
	jassert ( name.isNotEmpty () );

	auto	tgt = std::make_unique<shaderTarget> ( openGLContext, name );

	setTargetShader ( tgt.get (), name );

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

void ShaderToyComponent::setTargetShader ( shaderTarget* dst, const juce::String& name )
{
	jassert ( dst );
	jassert ( name.isNotEmpty () );

	dst->setName ( name );
	dst->setShaderProgram ( loadFragmentShader ( name ) );
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

double ShaderToyComponent::getDeltaTime ()
{
	const auto	currentTime = std::chrono::steady_clock::now ();
	const std::chrono::duration<double>	_deltaTime = currentTime - lastTime;
	lastTime = currentTime;

	return std::min ( _deltaTime.count (), 0.1 );
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

	const auto	name = file.getFullPathName ().replaceCharacter ( '\\', '/' );

	// recompile ALL shaders, if their common-file has changed
	if ( name.endsWithIgnoreCase ( "/common.glsl" ) )
	{
		for ( auto& shader : targets )
			shader->setShaderProgram ( loadFragmentShader ( shader->getName () ) );

		return;
	}

	// Recompile shaders
	for ( auto& dst : targets )
		if ( name.endsWithIgnoreCase ( dst->getName () ) )
			dst->setShaderProgram ( loadFragmentShader ( file.getFileName () ) );

	// Reload textures
	for ( auto& txt : textures )
		if ( ! txt->glTexture.isTarget () && name.endsWithIgnoreCase ( txt->name ) )
			txt->load ( txt.get (), root );
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

juce::String ShaderToyComponent::loadFragmentShader ( juce::String name )
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

	// Check for common-file
	if ( auto commonStr = findFile ( "common.glsl" ).loadFileAsString (); commonStr.isNotEmpty () )
		return commonStr + "\n" + shaderStr;

	return shaderStr;
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
			#ifdef GL_STACK_OVERFLOW
		case juce::gl::GL_STACK_OVERFLOW:                 return "GL_STACK_OVERFLOW";
			#endif
			#ifdef GL_STACK_UNDERFLOW
		case juce::gl::GL_STACK_UNDERFLOW:                return "GL_STACK_UNDERFLOW";
			#endif
			#ifdef GL_INVALID_FRAMEBUFFER_OPERATION
		case juce::gl::GL_INVALID_FRAMEBUFFER_OPERATION:  return "GL_INVALID_FRAMEBUFFER_OPERATION";
			#endif
		default: break;
	}

	return "Unknown error";
}

[[ maybe_unused ]] static void checkGLError ()
{
	for ( ;; )
	{
		const GLenum e = juce::gl::glGetError ();

		if ( e == juce::gl::GL_NO_ERROR )
			break;

		juce::Logger::writeToLog ( "[E]" + juce::String ( getGLErrorMessage ( e ) ) );
		jassertfalse;
	}
}
#endif
//-----------------------------------------------------------------------------
}