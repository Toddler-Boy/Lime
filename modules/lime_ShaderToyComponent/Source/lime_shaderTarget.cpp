#include "lime_shaderTarget.h"

namespace lime
{

//-----------------------------------------------------------------------------

shaderTarget::shaderTarget ( juce::OpenGLContext& oglContext, const juce::String& _name )
	: openGLContext ( oglContext )
	, name ( _name )
{
}
//-----------------------------------------------------------------------------

void shaderTarget::newContext ()
{
	shaderUpdated = true;

	glQuad.newContext ();
}
//-----------------------------------------------------------------------------

void shaderTarget::losingContext ()
{
	if ( targetBuffer )
		targetBuffer->glTexture.release ();

	glQuad.release ();

	shaderProgram.reset ();
}
//-----------------------------------------------------------------------------

void shaderTarget::render ( float viewportWidth, float viewportHeight, float scale )
{
	if ( ! enabled )
		return;

	auto	scaleW = scale;
	auto	scaleH = scale;

	// Are we rendering into a texture?
	if ( targetBuffer )
	{
		auto	targetWidth = pixWidth;
		auto	targetHeight = pixHeight;

		// Adjust texture-size as needed
		switch ( sizeMode )
		{
			case autoSizeModes::pixels:
				scaleW = 1.0;
				scaleH = 1.0;
				break;

			case autoSizeModes::pixelsScaled:
				break;

			case autoSizeModes::relative:
				targetWidth = int ( viewportWidth * scale * relWidth );
				targetHeight = int ( viewportHeight * scale * relHeight );

				scaleW *= relWidth;
				scaleH *= relHeight;
				break;

			case autoSizeModes::relativeSmaller:
				targetWidth = int ( std::min ( viewportWidth, viewportHeight ) * scale * relWidth );
				targetHeight = targetWidth;

				if ( viewportWidth > viewportHeight )
					scaleW /= viewportWidth / viewportHeight;

				scaleW *= relWidth;
				scaleH *= relWidth;
				break;
		}

		targetBuffer->glTexture.asTarget ( targetWidth, targetHeight );

		#if JUCE_WINDOWS
		{
			char	label[ 128 ] = { "target:" };
			std::strcat ( label, name.toRawUTF8 () );

			juce::gl::glObjectLabel ( juce::gl::GL_TEXTURE, targetBuffer->glTexture.getTextureID (), -1, label );
		}
		#endif

		juce::gl::glViewport ( 0, 0, targetWidth, targetHeight );

		setUniform_vec3 ( "iResolution", float ( targetWidth ), float ( targetHeight ), 1.0f );
		setUniform_f ( "iScale", scale );
	}
	else
	{
		juce::gl::glBindFramebuffer ( juce::gl::GL_FRAMEBUFFER, 0 );
		juce::gl::glViewport ( 0, 0, int ( viewportWidth * scale ), int ( viewportHeight * scale ) );

		setUniform_vec3 ( "iResolution", viewportWidth * scale, viewportHeight * scale, 1.0f );
		setUniform_f ( "iScale", scale );
	}

	//
	// Load textures
	//
	for ( auto i = 0; auto& tex : textures )
	{
		if ( auto texture = tex.tex )
		{
			if ( auto expected = true; texture->textureUpdated.compare_exchange_weak ( expected, false ) )
			{
				const auto	oldTextureID = texture->glTexture.getTextureID ();

				juce::gl::glActiveTexture ( juce::gl::GL_TEXTURE0 + i );
				juce::gl::glBindTexture ( juce::gl::GL_TEXTURE_2D, 0 );
				juce::gl::glBindTexture ( juce::gl::GL_TEXTURE_3D, 0 );

				if ( texture->isValid () )
				{
					if ( texture->lock.try_lock () )
					{
						std::visit ( [ texture ] ( auto&& arg )
						{
							using T = std::decay_t<decltype( arg )>;

							if constexpr ( std::is_same_v<T, openGL_Image> )
							{
								texture->glTexture.loadImage ( arg.getData (), arg.width, arg.height, arg.pixLen, arg.width * arg.pixLen, texture->yFlipped, texture->generateMipmaps, texture->isUint );
							}
							else if constexpr ( std::is_same_v<T, juce::Image> )
							{
								auto	bmp = juce::Image::BitmapData ( arg, juce::Image::BitmapData::readOnly );

								if ( texture->is3DLUT )
								{
									texture->glTexture.load3DLUT ( bmp.data, bmp.width, bmp.height, bmp.pixelStride );
								}
								else
								{
									auto	pixLen = texture->pixLen ? texture->pixLen : bmp.pixelStride;
									auto	stride = texture->pixLen ? ( texture->pixLen * bmp.width ) : bmp.lineStride;
									auto	width = texture->pixLen ? bmp.width / pixLen : bmp.width;
									auto	height = texture->pixLen ? bmp.height / pixLen : bmp.height;
									texture->glTexture.loadImage ( bmp.data, width, height, pixLen, stride, texture->yFlipped, texture->generateMipmaps, texture->isUint );
								}
							}
							else if constexpr ( std::is_same_v<T, shaderFloatTexture> )
							{
								texture->glTexture.loadFloatPalette ( arg.floatPalette.data (), arg.width, arg.height, 3, texture->yFlipped );
							}

						}, texture->source );

						texture->lock.unlock ();
					}
				}

				const auto	newTextureID = texture->glTexture.getTextureID ();
				if ( oldTextureID != newTextureID )
					shaderUpdated = true;

				#if JUCE_WINDOWS
					if ( newTextureID )
						juce::gl::glObjectLabel ( juce::gl::GL_TEXTURE, newTextureID, -1, texture->name.toRawUTF8 () );
				#endif
			}

			juce::gl::glActiveTexture ( juce::gl::GL_TEXTURE0 + i );

			const auto	texType = texture->glTexture.is3D () ? GLenum ( juce::gl::GL_TEXTURE_3D ) : GLenum ( juce::gl::GL_TEXTURE_2D );

			juce::gl::glBindTexture ( texType, texture->glTexture.getTextureID () );

			auto getFilterMode = [ &tex ] ( const bool mag )
			{
				if ( mag || ! tex.tex->generateMipmaps )
					return tex.linearFilter ? juce::gl::GL_LINEAR : juce::gl::GL_NEAREST;

				return tex.linearFilter ? juce::gl::GL_LINEAR_MIPMAP_LINEAR : juce::gl::GL_LINEAR_MIPMAP_NEAREST;
			};

			juce::gl::glTexParameteri ( texType, juce::gl::GL_TEXTURE_MAG_FILTER, getFilterMode ( true ) );
			juce::gl::glTexParameteri ( texType, juce::gl::GL_TEXTURE_MIN_FILTER, getFilterMode ( false ) );

			juce::gl::glTexParameteri ( texType, juce::gl::GL_TEXTURE_WRAP_S, tex.clampMode );
			juce::gl::glTexParameteri ( texType, juce::gl::GL_TEXTURE_WRAP_T, tex.clampMode );

			juce::gl::glTexParameterfv ( texType, juce::gl::GL_TEXTURE_BORDER_COLOR, tex.borderColor );
		}
		++i;
	}

	//
	// Select shader program
	//
	if ( shaderUpdated )
	{
		rmutex.lock ();
		compileOpenGLShaders ();
		rmutex.unlock ();

		resetUniforms ();
	}

	if ( shaderProgram )
		shaderProgram->use ();

	//
	// Create/update uniforms
	//
	updateUniforms ();

	// Send updated vertices
	if ( instanceData.empty () )
	{
		vertexBuffer = {
			vertices[ 2 ][ 0 ] * scaleW, vertices[ 2 ][ 1 ] * scaleH, vertices[ 2 ][ 2 ], vertices[ 2 ][ 3 ],	0.0f, 0.0f,
			vertices[ 3 ][ 0 ] * scaleW, vertices[ 3 ][ 1 ] * scaleH, vertices[ 3 ][ 2 ], vertices[ 3 ][ 3 ],	1.0f, 0.0f,
			vertices[ 1 ][ 0 ] * scaleW, vertices[ 1 ][ 1 ] * scaleH, vertices[ 1 ][ 2 ], vertices[ 1 ][ 3 ],	1.0f, 1.0f,
			vertices[ 0 ][ 0 ] * scaleW, vertices[ 0 ][ 1 ] * scaleH, vertices[ 0 ][ 2 ], vertices[ 0 ][ 3 ],	0.0f, 1.0f,
		};

		glQuad.setVertices ( vertexBuffer );
	}
	else
	{
		glQuad.setInstances ( instanceData, instanceStride );
	}

	//
	// Set blend mode
	//
	if ( blendEnabled )
	{
		juce::gl::glEnable ( juce::gl::GL_BLEND );
		if ( blendMode == BlendMode::add )
			juce::gl::glBlendFunc ( juce::gl::GL_ONE, juce::gl::GL_ONE );
		else
			juce::gl::glBlendFunc ( premultipliedAlpha ? juce::gl::GL_ONE : juce::gl::GL_SRC_ALPHA, juce::gl::GL_ONE_MINUS_SRC_ALPHA );
	}
	else
	{
		juce::gl::glDisable ( juce::gl::GL_BLEND );
	}

	//
	// Finally draw something
	//
	if ( measurePerformance )
		glQuad.beginMeasurement ();

	//
	// Clear background
	//
	if ( backgroundColor[ 3 ] > 0.0f )
	{
		juce::gl::glClearColor ( backgroundColor[ 0 ], backgroundColor[ 1 ], backgroundColor[ 2 ], backgroundColor[ 3 ] );
		juce::gl::glClear ( juce::gl::GL_COLOR_BUFFER_BIT );
	}

	glQuad.draw ();

	if ( targetBuffer && targetBuffer->generateMipmaps )
	{
		juce::gl::glBindTexture ( juce::gl::GL_TEXTURE_2D, targetBuffer->glTexture.getTextureID () );
		juce::gl::glGenerateMipmap ( juce::gl::GL_TEXTURE_2D );
	}

	if ( measurePerformance )
		glQuad.endMeasurement ();
}
//-----------------------------------------------------------------------------

void shaderTarget::setBounds ( const juce::Rectangle<float>& r )
{
	vertices[ 0 ][ 0 ] = r.getX ();
	vertices[ 0 ][ 1 ] = r.getY ();
	vertices[ 0 ][ 2 ] = 1.0f;
	vertices[ 0 ][ 3 ] = 1.0f;

	vertices[ 1 ][ 0 ] = r.getRight ();
	vertices[ 1 ][ 1 ] = r.getY ();
	vertices[ 1 ][ 2 ] = 1.0f;
	vertices[ 1 ][ 3 ] = 1.0f;

	vertices[ 2 ][ 0 ] = r.getX ();
	vertices[ 2 ][ 1 ] = r.getBottom ();
	vertices[ 2 ][ 2 ] = 1.0f;
	vertices[ 2 ][ 3 ] = 1.0f;

	vertices[ 3 ][ 0 ] = r.getRight ();
	vertices[ 3 ][ 1 ] = r.getBottom ();
	vertices[ 3 ][ 2 ] = 1.0f;
	vertices[ 3 ][ 3 ] = 1.0f;
}
//-----------------------------------------------------------------------------

juce::Rectangle<float> shaderTarget::getBounds () const
{
	return juce::Rectangle<float> ( vertices[ 0 ][ 0 ],
									vertices[ 0 ][ 1 ],
									vertices[ 1 ][ 0 ] - vertices[ 0 ][ 0 ],
									vertices[ 2 ][ 1 ] - vertices[ 0 ][ 1 ] );
}
//-----------------------------------------------------------------------------

void shaderTarget::setBounds ( const juce::Rectangle<int>& r )
{
	setBounds ( r.toFloat () );
}
//-----------------------------------------------------------------------------

void shaderTarget::setSize ( const int width, const int height )
{
	setBounds ( juce::Rectangle<float> ( float ( width ), float ( height ) ) );
}
//-----------------------------------------------------------------------------

void shaderTarget::setVertices ( const std::array<std::array<float, 4>, 4>& v )
{
	for ( auto i = 0; i < 4; ++i )
 	{
 		vertices[ i ][ 0 ] = v[ i ][ 0 ];
 		vertices[ i ][ 1 ] = v[ i ][ 1 ];
 		vertices[ i ][ 2 ] = v[ i ][ 2 ];
		vertices[ i ][ 3 ] = v[ i ][ 3 ];
	}
}
//-----------------------------------------------------------------------------

void shaderTarget::setBufferSizeRelative ( const float s )
{
	sizeMode = autoSizeModes::relative;

	relWidth = s;
	relHeight = s;
}
//-----------------------------------------------------------------------------

void shaderTarget::setBufferSizeRelative ( const float sw, const float sh )
{
	sizeMode = autoSizeModes::relative;

	relWidth = sw;
	relHeight = sh;
}
//-----------------------------------------------------------------------------

void shaderTarget::setBufferSizeSquareRelative ( const float s )
{
	sizeMode = autoSizeModes::relativeSmaller;

	relWidth = s;
	relHeight = s;
}
//-----------------------------------------------------------------------------

void shaderTarget::setBufferSizePixels ( const int w, const int h )
{
	sizeMode = autoSizeModes::pixels;

	pixWidth = w;
	pixHeight = h;
}
//-----------------------------------------------------------------------------

void shaderTarget::setBufferSizePixelsScaled ( const int w, const int h )
{
	sizeMode = autoSizeModes::pixelsScaled;

	pixWidth = w;
	pixHeight = h;
}
//-----------------------------------------------------------------------------

void shaderTarget::setName ( const juce::String& _name )
{
	name = _name;
}
//-----------------------------------------------------------------------------

void shaderTarget::setVertexShader ( const juce::String& shaderStr )
{
	setShader ( vertexShaderProgramStr, shaderStr );
}
//-----------------------------------------------------------------------------

void shaderTarget::setFragmentShader ( const juce::String& shaderStr )
{
	setShader ( fragmentShaderProgramStr, shaderStr );
}
//-----------------------------------------------------------------------------

void shaderTarget::setShader ( std::string& dst, const juce::String& shaderStr )
{
	const auto	newPrg = shaderStr.trim ().toStdString ();
	if ( newPrg == dst )
		return;

	rmutex.lock ();
	dst = std::move ( newPrg );
	shaderUpdated = true;
	rmutex.unlock ();
}
//-----------------------------------------------------------------------------

void shaderTarget::setTargetBuffer ( shaderTexture* txt )
{
	targetBuffer = txt;

	if ( txt )
		txt->textureUpdated = false;
}
//-----------------------------------------------------------------------------

void shaderTarget::setTargetBackgroundColor ( juce::Colour col )
{
	backgroundColor[ 0 ] = GLfloat ( col.getFloatRed () );
	backgroundColor[ 1 ] = GLfloat ( col.getFloatGreen () );
	backgroundColor[ 2 ] = GLfloat ( col.getFloatBlue () );
	backgroundColor[ 3 ] = GLfloat ( col.getFloatAlpha () );
}
//-----------------------------------------------------------------------------

void shaderTarget::setTexture ( const int index, shaderTexture* txt )
{
	textures[ index ].tex = txt;
}
//-----------------------------------------------------------------------------

void shaderTarget::setTextureBorderColor ( const int index, juce::Colour col )
{
	auto& bcol = textures[ index ].borderColor;

	bcol[ 0 ] = GLfloat ( col.getFloatRed () );
	bcol[ 1 ] = GLfloat ( col.getFloatGreen () );
	bcol[ 2 ] = GLfloat ( col.getFloatBlue () );
	bcol[ 3 ] = GLfloat ( col.getFloatAlpha () );
}
//-----------------------------------------------------------------------------

void shaderTarget::setTextureFilter ( const int index, const bool linear )
{
	textures[ index ].linearFilter = linear;
}
//-----------------------------------------------------------------------------

void shaderTarget::setTextureClampMode ( const int index, const int mode )
{
	textures[ index ].clampMode = mode;
}
//-----------------------------------------------------------------------------

namespace defaultShaderStrings
{

const static std::string	vertexShader = R"(
#version 410 core

uniform vec3		iResolution;				// viewport resolution (in pixels)

layout (location = 0) in vec4 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 fragCoord;

void main ()
{
	vec2	pos_RatioTo1 = aPos.xy / iResolution.xy;
	vec2	clip_space = pos_RatioTo1 * 2 - 1;

	gl_Position = vec4 ( clip_space * vec2 ( 1, -1 ), 1.0, aPos.w );
	fragCoord = aTexCoord;
}
)";

const static std::string	fragmentPrefix = R"(
#version 410 core

precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler3D;

in		vec2 fragCoord;							// already normalized ( 0.0 to 1.0 )
out		vec4 fragColor;

uniform vec3		iResolution;				// viewport resolution (in pixels)
uniform float		iScale;						// viewport scale
uniform float		iTime;						// shader playback time (in seconds)
uniform int			iFrame;						// shader playback frame
)";

}

void shaderTarget::compileOpenGLShaders ()
{
	if ( fragmentShaderProgramStr.empty () )
	{
		shaderUpdated = false;
		return;
	}

	auto	fragmentPrefix = std::string ();
	if ( ! fragmentShaderProgramStr.starts_with ( '#' ) )
	{
		fragmentPrefix = defaultShaderStrings::fragmentPrefix;

		for ( auto index = 0; const auto& tex : textures )
		{
			const auto	channelName = std::string ( "iChannel" ) + std::to_string ( index );

			if ( auto texture = tex.tex )
			{
				fragmentPrefix += "uniform ";
				fragmentPrefix += std::visit ( [ & ] ( auto&& arg ) -> std::string
				{
					using T = std::decay_t<decltype( arg )>;

					if constexpr ( std::is_same_v<T, openGL_Image> )
					{
						if ( arg.isValid () && texture->isUint )
							return "usampler2D";
					}
					else if constexpr ( std::is_same_v<T, juce::Image> )
					{
						if ( arg.isValid () && arg.isSingleChannel () && texture->isUint )
							return "usampler2D";
					}

					if ( texture->is3DLUT )
						return "sampler3D";

					return "sampler2D";

				}, texture->source );

				fragmentPrefix += " " + channelName + ";\n";

				setUniform_i ( channelName, index );
			}
			else
			{
				removeUniform ( channelName );
			}

			++index;
		}
	}

	auto	shaderProgramAttempt = std::make_unique<juce::OpenGLShaderProgram> ( openGLContext );

	// Attempt to compile the program
	shaderProgramAttempt->addVertexShader ( vertexShaderProgramStr.empty () ? defaultShaderStrings::vertexShader : vertexShaderProgramStr );
	shaderProgramAttempt->addFragmentShader ( fragmentPrefix + fragmentShaderProgramStr );
	shaderProgramAttempt->link ();

	openGLStatus = shaderProgramAttempt->getLastError ();
	if ( openGLStatus.isNotEmpty () )
	{
		juce::Logger::writeToLog ( "[E]" + getName () );
		juce::Logger::writeToLog ( "[E]" + openGLStatus );

		// Find the line mention in the error message and print the shader code around it for easier debugging
		const auto	errorLine = openGLStatus.fromFirstOccurrenceOf ( "(", false, false ).upToFirstOccurrenceOf ( ")", false, false ).getIntValue () - 1;

		auto	shaderLines = juce::StringArray::fromLines ( fragmentPrefix + fragmentShaderProgramStr );

		// Print 5 lines before and after the error line
		for ( auto i = std::max ( 0, errorLine - 5 ); i < std::min ( shaderLines.size (), errorLine + 6 ); ++i )
		{
			const auto	lineNum = juce::String ( i + 1 ).paddedLeft ( ' ', 3 );
			const auto	linePrefix = ( i == errorLine ) ? ">>" : "  ";
			const auto	lineContent = shaderLines[ i ].replace ( "\t", "    " );

			juce::Logger::writeToLog ( juce::String ( i == errorLine ? "[E]" : "[L]" ) + linePrefix + lineNum + ": " + lineContent );
		}
	}

	shaderProgram = std::move ( shaderProgramAttempt );
	shaderUpdated = false;
}
//-----------------------------------------------------------------------------

void shaderTarget::setUniform_f ( const std::string& uniName, const float n1 )
{
	auto&	uni = uniforms[ uniName ];

	uni.isFloat = true;
	uni.count = 1;
	uni.fValues[ 0 ] = n1;
}
//-----------------------------------------------------------------------------

void shaderTarget::setUniform_f ( const std::string& uniName, const std::initializer_list<const float>& values )
{
	setUniform_f ( uniName, std::span<const float> ( values ) );
}
//-----------------------------------------------------------------------------

void shaderTarget::setUniform_f ( const std::string& uniName, const std::span<const float>& values )
{
	auto&	uni = uniforms[ uniName ];

	jassert ( values.size () <= std::size ( uni.fValues ) );

	uni.isFloat = true;
	uni.count = int ( values.size () );
	for ( auto i = 0; i < uni.count; ++i )
		uni.fValues[ i ] = values[ i ];
}
//-----------------------------------------------------------------------------

void shaderTarget::setUniform_i ( const std::string& uniName, const int n1 )
{
	auto&	uni = uniforms[ uniName ];

	uni.isFloat = false;
	uni.count = 1;
	uni.iValues[ 0 ] = n1;
}
//-----------------------------------------------------------------------------

void shaderTarget::setUniform_vec2 ( const std::string& uniName, const float n1, const float n2 )
{
	auto&	uni = uniforms[ uniName ];

	uni.isFloat = true;
	uni.count = 2;

	uni.fValues[ 0 ] = n1;
	uni.fValues[ 1 ] = n2;
}
//-----------------------------------------------------------------------------

void shaderTarget::setUniform_vec3 ( const std::string& uniName, const float n1, const float n2, const float n3 )
{
	auto&	uni = uniforms[ uniName ];

	uni.isFloat = true;
	uni.count = 3;

	uni.fValues[ 0 ] = n1;
	uni.fValues[ 1 ] = n2;
	uni.fValues[ 2 ] = n3;
}
//-----------------------------------------------------------------------------

void shaderTarget::setUniform_vec4 ( const std::string& uniName, const float n1, const float n2, const float n3, const float n4 )
{
	auto&	uni = uniforms[ uniName ];

	uni.isFloat = true;
	uni.count = 4;

	uni.fValues[ 0 ] = n1;
	uni.fValues[ 1 ] = n2;
	uni.fValues[ 2 ] = n3;
	uni.fValues[ 3 ] = n4;
}
//-----------------------------------------------------------------------------

void shaderTarget::setUniform_ivec4 ( const std::string& uniName, const int n1, const int n2, const int n3, const int n4 )
{
	auto&	uni = uniforms[ uniName ];

	uni.isFloat = false;
	uni.count = 4;

	uni.iValues[ 0 ] = n1;
	uni.iValues[ 1 ] = n2;
	uni.iValues[ 2 ] = n3;
	uni.iValues[ 3 ] = n4;
}
//-----------------------------------------------------------------------------

void shaderTarget::uniform::update ()
{
	auto& uni = *uniformPtr;

	if ( isFloat )
	{
		switch ( count )
		{
			case 1:	uni.set ( fValues[ 0 ] );											return;
			case 2:	uni.set ( fValues[ 0 ], fValues[ 1 ] );								return;
			case 3:	uni.set ( fValues[ 0 ], fValues[ 1 ], fValues[ 2 ] );				return;
			case 4:	uni.set ( fValues[ 0 ], fValues[ 1 ], fValues[ 2 ], fValues[ 3 ] );	return;
		}
	}

	switch ( count )
	{
		case 1:	uni.set ( iValues[ 0 ] );												return;
		case 2:	uni.set ( iValues[ 0 ], iValues[ 1 ] );									return;
		case 3:	uni.set ( iValues[ 0 ], iValues[ 1 ], iValues[ 2 ] );					return;
		case 4:	uni.set ( iValues[ 0 ], iValues[ 1 ], iValues[ 2 ], iValues[ 3 ] );		return;
	}
}
//-----------------------------------------------------------------------------

void shaderTarget::removeUniform ( const std::string& uniName )
{
	uniforms.erase ( uniName );
}
//-----------------------------------------------------------------------------

void shaderTarget::resetUniforms ()
{
	for ( auto& [ uniName, uni ] : uniforms )
		uni.uniformPtr.reset ();
}
//-----------------------------------------------------------------------------

void shaderTarget::updateUniforms ()
{
	if ( ! shaderProgram )
		return;

	for ( auto& [ uniName, uni ] : uniforms )
	{
		if ( ! uni.uniformPtr )
			uni.uniformPtr = std::make_unique<juce::OpenGLShaderProgram::Uniform> ( *shaderProgram, uniName.c_str () );

		uni.update ();
	}
}
//-----------------------------------------------------------------------------
}