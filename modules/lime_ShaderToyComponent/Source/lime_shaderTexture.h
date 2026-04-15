#pragma once

#include <mutex>
#include <span>

#include "lime_openGL_Image.h"
#include "lime_openGL_Texture.h"

namespace lime
{
//-----------------------------------------------------------------------------

struct shaderFloatTexture
{
	std::span<float>	floatPalette;
	int					width = 0;
	int					height = 0;
};

struct shaderTexture
{
	std::mutex			lock;
	std::atomic<bool>	textureUpdated = false;
	juce::String		name;

	const openGL_Image*	newTexture;
	juce::Image			imgTexture;
	juce::Image			lut3D;
	shaderFloatTexture	floatTexture;
	openGL_Texture		glTexture;

	std::function<void ( shaderTexture* dst, const juce::File& root )>		load;
	bool				yFlipped = true;
	bool				generateMipmaps = false;
	bool				isUint = false;
	int					pixLen = 0;

	void unload ()
	{
		newTexture = nullptr;
		imgTexture = {};
		lut3D = {};
		floatTexture = {};
		textureUpdated = true;
	}

	void fromImage ( const openGL_Image& img, const bool _yFlipped = true, const bool _generateMipMaps = true, const bool _isUint = false )
	{
		if ( lock.try_lock () )
		{
			unload ();
			newTexture = &img;
			yFlipped = _yFlipped;
			generateMipmaps = _generateMipMaps;
			isUint = _isUint;
			pixLen = img.pixLen;

			lock.unlock ();
		}
	}

	void fromImage ( const juce::Image& img, const bool _yFlipped = true, const bool _generateMipMaps = true, const bool _isUint = false, const int _pixLen = 0 )
	{
		if ( lock.try_lock () )
		{
			unload ();
			imgTexture = img;
			yFlipped = _yFlipped;
			generateMipmaps = _generateMipMaps;
			isUint = _isUint;
			pixLen = _pixLen;

			lock.unlock ();
		}
	}

	void from3DLUT ( const juce::Image& lut )
	{
		if ( lock.try_lock () )
		{
			unload ();
			lut3D = lut;

			lock.unlock ();
		}
	}

	void fromFloatVector ( const shaderFloatTexture& txt, const bool _yFlipped = true )
	{
		if ( lock.try_lock () )
		{
			unload ();
			floatTexture = txt;
			yFlipped = _yFlipped;

			lock.unlock ();
		}
	}

	bool isValid () const
	{
		return		( newTexture && newTexture->isValid () )
				||	imgTexture.isValid ()
				||	lut3D.isValid ()
				||	! floatTexture.floatPalette.empty ();
	}
};
//-----------------------------------------------------------------------------
}