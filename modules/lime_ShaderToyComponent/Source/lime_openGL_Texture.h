#pragma once

namespace lime
{
//-----------------------------------------------------------------------------

class openGL_Texture
{
public:
	openGL_Texture () = default;
	~openGL_Texture ();

	void clear ();
	void asTarget ( int width = 0, int height = 0, int intFormat = juce::gl::GL_RGBA );
	void loadImage ( const uint8_t* pixels, int width, int height, int pixLength, int stride = 0, bool yFlipped = true, bool generateMipmaps = false, bool isUint = false );
	void load3DLUT ( const uint8_t* pixels, int width, int height, int pixLength );
	void loadFloatPalette ( const float* pixels, int width, int height, int pixLength, bool yFlipped = true );

	[[ nodiscard ]] GLuint getTextureID () const noexcept { return textureID; }
	[[ nodiscard ]] GLuint getFrameBufferID () const noexcept { return frameBuffer; }

	[[ nodiscard ]] int getWidth () const noexcept { return width; }
	[[ nodiscard ]] int getHeight () const noexcept { return height; }
	[[ nodiscard ]] int getDepth () const noexcept { return depth; }
	[[ nodiscard ]] bool is3D () const noexcept { return depth; }
	[[ nodiscard ]] bool isTarget () const noexcept { return target; }

	void release ();

private:
	GLuint	textureID = 0;
	GLuint	frameBuffer = 0;
	int		width = 0;
	int		height = 0;
	int		depth = 0;
	bool	target = false;
	int		targetFormat = juce::gl::GL_RGBA;

	juce::OpenGLContext*	ownerContext = nullptr;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR ( openGL_Texture )
};
//-----------------------------------------------------------------------------
}