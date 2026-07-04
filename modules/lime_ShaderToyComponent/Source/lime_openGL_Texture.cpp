#include "lime_openGL_Texture.h"

namespace lime
{
//-----------------------------------------------------------------------------

openGL_Texture::~openGL_Texture ()
{
	release ();
}
//-----------------------------------------------------------------------------

constexpr std::pair<GLenum, GLenum> getTargetFormats ( GLint format )
{
	switch ( format )
	{
		case juce::gl::GL_R32F:
			return { juce::gl::GL_RED, juce::gl::GL_FLOAT };

		default:
			return { juce::gl::GL_RGBA, juce::gl::GL_UNSIGNED_BYTE };
	}
}
//-----------------------------------------------------------------------------

void openGL_Texture::asTarget ( int _width, int _height, int _intFormat )
{
	// Target without a size doesn't work
	jassert ( _width && _height );

	ownerContext = juce::OpenGLContext::getCurrentContext ();

	// Texture objects can only be created when the current thread has an active OpenGL
	// context. You'll need to create this object in one of the OpenGLContext's callbacks.
	jassert ( ownerContext );

	auto&	ogl = ownerContext->extensions;

	if ( ! textureID )
		juce::gl::glGenTextures ( 1, &textureID );

	if ( width != _width || height != _height || targetFormat != _intFormat )
	{
		width = _width;
		height = _height;
		depth = 0;
		target = true;
		targetFormat = _intFormat;

		const auto [ srcFormat, type ] = getTargetFormats ( targetFormat );

		juce::gl::glBindTexture ( juce::gl::GL_TEXTURE_2D, textureID );
		juce::gl::glPixelStorei ( juce::gl::GL_UNPACK_ALIGNMENT, 1 );
		juce::gl::glTexImage2D ( juce::gl::GL_TEXTURE_2D, 0, targetFormat, _width, _height, 0, srcFormat, type, nullptr );
		juce::gl::glBindTexture ( juce::gl::GL_TEXTURE_2D, 0 );
	}

	if ( ! frameBuffer )
		ogl.glGenFramebuffers ( 1, &frameBuffer );

	ogl.glBindFramebuffer ( juce::gl::GL_FRAMEBUFFER, frameBuffer );
	ogl.glFramebufferTexture2D ( juce::gl::GL_FRAMEBUFFER, juce::gl::GL_COLOR_ATTACHMENT0, juce::gl::GL_TEXTURE_2D, textureID, 0 );
}
//-----------------------------------------------------------------------------

void openGL_Texture::clear ()
{
	if ( ! frameBuffer )
		return;

	ownerContext = juce::OpenGLContext::getCurrentContext ();

	// Texture objects can only be created when the current thread has an active OpenGL
	// context. You'll need to create this object in one of the OpenGLContext's callbacks.
	jassert ( ownerContext );

	auto&	ogl = ownerContext->extensions;

	ogl.glBindFramebuffer ( juce::gl::GL_FRAMEBUFFER, frameBuffer );
	ogl.glFramebufferTexture2D ( juce::gl::GL_FRAMEBUFFER, juce::gl::GL_COLOR_ATTACHMENT0, juce::gl::GL_TEXTURE_2D, textureID, 0 );

	juce::gl::glClearColor ( 0.0f, 0.0f, 0.0f, 0.0f );
	juce::gl::glClear ( juce::gl::GL_COLOR_BUFFER_BIT );
}
//-----------------------------------------------------------------------------

constexpr std::tuple<GLint, GLenum, GLenum> getPixelFormats ( const int pixLen, bool isUint )
{
	switch ( pixLen )
	{
		case 1:
		{
			if ( isUint )
				return { juce::gl::GL_R8UI, juce::gl::GL_RED_INTEGER, juce::gl::GL_UNSIGNED_BYTE };

			return { juce::gl::GL_R8, juce::gl::GL_RED, juce::gl::GL_UNSIGNED_BYTE };
		}

		case 2:
			return { juce::gl::GL_RG, juce::gl::GL_RG, juce::gl::GL_UNSIGNED_BYTE };

		case 3:
			return { juce::gl::GL_RGB, juce::gl::GL_BGR, juce::gl::GL_UNSIGNED_BYTE };

		case 4:
			return { juce::gl::GL_RGBA, juce::gl::GL_BGRA, juce::gl::GL_UNSIGNED_BYTE };

		case 16:
			return { juce::gl::GL_RGBA16F, juce::gl::GL_RGBA, juce::gl::GL_FLOAT };

		default:
			// Unsupported pixel length
			jassertfalse;
			return { 0, 0, 0 };
	}
}
//-----------------------------------------------------------------------------

void openGL_Texture::loadImage ( const uint8_t* pixels, int _width, int _height, int _pixLen, int stride /*= 0 */, bool yFlipped /*= true */, bool generateMipmaps /*= false */, bool isUint /*=false */)
{
	jassert ( pixels );

	width = _width;
	height = _height;
	depth = 0;
	target = false;

	auto	[ glIntFmt, glSrcFmt, glType ] = getPixelFormats ( _pixLen, isUint );

	ownerContext = juce::OpenGLContext::getCurrentContext ();

	// Texture objects can only be created when the current thread has an active OpenGL
	// context. You'll need to create this object in one of the OpenGLContext's callbacks.
	jassert ( ownerContext != nullptr );

	if ( ! textureID )
		juce::gl::glGenTextures ( 1, &textureID );

	juce::gl::glBindTexture ( juce::gl::GL_TEXTURE_2D, textureID );
	juce::gl::glPixelStorei ( juce::gl::GL_UNPACK_ALIGNMENT, 1 );

	// No pixel data supplied, create empty texture
	if ( ! pixels )
	{
		juce::gl::glTexImage2D ( juce::gl::GL_TEXTURE_2D, 0, glIntFmt, _width, _height, 0, glSrcFmt, glType, nullptr );
		return;
	}

	// Y-flip
	if ( yFlipped )
	{
		std::vector<uint8_t>	dstVec ( width * height * _pixLen );
		if ( dstVec.size () )
		{
			for ( auto y = 0; y < height; ++y )
				std::copy_n ( pixels + ( ( height - 1 ) - y ) * stride, width * _pixLen, dstVec.data () + y * width * _pixLen );

			juce::gl::glTexImage2D ( juce::gl::GL_TEXTURE_2D, 0, glIntFmt, _width, _height, 0, glSrcFmt, glType, dstVec.data () );
		}
	}
	else
	{
		if ( const auto	skip = _width * _pixLen - stride; skip )
		{
			std::vector<uint8_t>	dstVec ( width * height * _pixLen );
			if ( dstVec.size () )
			{
				for ( auto y = 0; y < height; ++y )
					std::copy_n ( pixels + y * stride, width * _pixLen, dstVec.data () + y * width * _pixLen );

				juce::gl::glTexImage2D ( juce::gl::GL_TEXTURE_2D, 0, glIntFmt, _width, _height, 0, glSrcFmt, glType, dstVec.data () );
			}
		}
		else
		{
			juce::gl::glTexImage2D ( juce::gl::GL_TEXTURE_2D, 0, glIntFmt, _width, _height, 0, glSrcFmt, glType, pixels );
		}
	}

	if ( generateMipmaps )
		juce::gl::glGenerateMipmap ( juce::gl::GL_TEXTURE_2D );

	juce::gl::glBindTexture ( juce::gl::GL_TEXTURE_2D, 0 );
}
//-----------------------------------------------------------------------------

void openGL_Texture::load3DLUT ( const uint8_t* pixels, int _width, int _height, int _pixLen )
{
	width = _width / _height;
	height = _height;
	depth = _height;
	target = false;

	ownerContext = juce::OpenGLContext::getCurrentContext ();

	// Texture objects can only be created when the current thread has an active OpenGL
	// context. You'll need to create this object in one of the OpenGLContext's callbacks.
	jassert ( ownerContext != nullptr );

	if ( ! textureID )
		juce::gl::glGenTextures ( 1, &textureID );

	juce::gl::glBindTexture ( juce::gl::GL_TEXTURE_3D, textureID );
	juce::gl::glPixelStorei ( juce::gl::GL_UNPACK_ALIGNMENT, 1 );

	//
	// Ensure 3D textures use a single column of width * height, repeated 'depth' times
	//
	if ( auto pixelStrip = std::vector<float> ( _width * _height * 3 ); pixelStrip.size () )
	{
		auto	dst = pixelStrip.data ();
		for ( auto z = 0; z < depth; z++ )
		{
			for ( auto y = 0; y < height; y++ )
			{
				auto	src = pixels + ( y * _width * _pixLen ) + ( z * width * _pixLen );
				for ( auto x = 0; x < width; x++ )
				{
					*dst++ = src[ 0 ] * ( 1.0f / 255.0f );
					*dst++ = src[ 1 ] * ( 1.0f / 255.0f );
					*dst++ = src[ 2 ] * ( 1.0f / 255.0f );
					src += _pixLen;
				}
			}
		}

		juce::gl::glTexImage3D ( juce::gl::GL_TEXTURE_3D, 0, juce::gl::GL_RGB, width, height, depth, 0, juce::gl::GL_BGR, juce::gl::GL_FLOAT, pixelStrip.data () );
	}

	juce::gl::glBindTexture ( juce::gl::GL_TEXTURE_3D, 0 );
}
//-----------------------------------------------------------------------------

void openGL_Texture::loadFloatPalette ( const float* pixels, int _width, int _height, int _pixLen, bool yFlipped )
{
	width = _width;
	height = _height;
	depth = 0;
	target = false;

	jassert ( _pixLen == 3 );

	ownerContext = juce::OpenGLContext::getCurrentContext ();

	// Texture objects can only be created when the current thread has an active OpenGL
	// context. You'll need to create this object in one of the OpenGLContext's callbacks.
	jassert ( ownerContext != nullptr );

	if ( ! textureID )
		juce::gl::glGenTextures ( 1, &textureID );

	juce::gl::glBindTexture ( juce::gl::GL_TEXTURE_2D, textureID );
	juce::gl::glPixelStorei ( juce::gl::GL_UNPACK_ALIGNMENT, 1 );

	// Y-flip
	if ( yFlipped )
	{
		std::vector<float>	dstVec ( width * height * _pixLen );
		if ( dstVec.size () )
		{
			for ( auto y = 0; y < height; ++y )
				std::copy_n ( pixels + ( ( height - 1 ) - y ) * width * _pixLen, width * _pixLen, dstVec.data () + y * width * _pixLen );

			juce::gl::glTexImage2D ( juce::gl::GL_TEXTURE_2D, 0, juce::gl::GL_RGB32F, _width, _height, 0, juce::gl::GL_RGB, juce::gl::GL_FLOAT, dstVec.data () );
		}
	}
	else
	{
		juce::gl::glTexImage2D ( juce::gl::GL_TEXTURE_2D, 0, juce::gl::GL_RGB32F, _width, _height, 0, juce::gl::GL_RGB, juce::gl::GL_FLOAT, pixels );
	}

	juce::gl::glBindTexture ( juce::gl::GL_TEXTURE_2D, 0 );
}
//-----------------------------------------------------------------------------

void openGL_Texture::release ()
{
	// If the texture is deleted while the owner context is not active, it's
	// impossible to delete it, so this will be a leak until the context itself
	// is deleted.
	jassert ( ( ! textureID && ! frameBuffer ) || ownerContext == juce::OpenGLContext::getCurrentContext () );

	if ( ownerContext != juce::OpenGLContext::getCurrentContext () )
		return;

	if ( frameBuffer )
		juce::gl::glDeleteFramebuffers ( 1, &frameBuffer );

	if ( textureID )
		juce::gl::glDeleteTextures ( 1, &textureID );

	textureID = 0;
	frameBuffer = 0;
	width = 0;
	height = 0;
	depth = 0;
	target = false;
}
//-----------------------------------------------------------------------------
}