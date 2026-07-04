#pragma once

#include <mutex>
#include <optional>
#include <span>
#include <variant>

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
	// Define the possible types the texture source can be
	using TextureSource = std::variant<
		std::monostate,         // Represents "empty/unloaded"
		openGL_Image,			// Simple all purpose buffer
		juce::Image,            // JUCE Image (standard or 3D LUT)
		shaderFloatTexture      // Custom float palette
	>;

	std::mutex			lock;
	std::atomic<bool>	textureUpdated = false;
	juce::String		name;

	TextureSource       source;

	openGL_Texture		glTexture;

	std::function<void ( shaderTexture* dst, const juce::File& root )>		load;

	bool				yFlipped = true;
	bool				generateMipmaps = false;
	bool				isUint = false;
	int					pixLen = 0;
	bool				is3DLUT = false;
	int					targetFormat = juce::gl::GL_RGBA;

	void unload ()
	{
		source = std::monostate {};
		textureUpdated = true;
	}

	template<typename T>
	void setSource ( T&& newSrc, bool _yFlipped = true, bool _genMipmaps = false, bool _isUint = false, int _pixLen = 0, bool _is3DLUT = false )
	{
		if ( lock.try_lock () )
		{
			source = std::forward<T> ( newSrc );
			yFlipped = _yFlipped;
			generateMipmaps = _genMipmaps;
			isUint = _isUint;
			pixLen = _pixLen;
			is3DLUT = _is3DLUT;
			textureUpdated = true;
			lock.unlock ();
		}
	}

	void fromImage ( const openGL_Image& img, bool _yFlipped = true, bool _genMip = true, bool _isUint = false )
	{
		setSource ( img, _yFlipped, _genMip, _isUint, img.pixLen );
	}

	// Make sure we don't accept temporary openGL_Image objects
	void fromImage ( const openGL_Image&& img, bool = true, bool = true, bool = false ) = delete;

	void fromImage ( const juce::Image& img, bool _yFlipped = true, bool _genMip = true, bool _isUint = false, int _pixLen = 0 )
	{
		setSource ( img, _yFlipped, _genMip, _isUint, _pixLen );
	}

	void from3DLUT ( const juce::Image& lut )
	{
		setSource ( lut, true, false, false, 0, true );
	}

	void fromFloatVector ( const shaderFloatTexture& txt, bool _yFlipped = false )
	{
		setSource ( txt, _yFlipped );
	}

	// Make sure we don't accept temporary shaderFloatTexture objects
	void fromFloatVector ( const shaderFloatTexture&& txt, bool = false ) = delete;

	bool isValid () const
	{
		if ( std::holds_alternative<std::monostate> ( source ) )
			return false;

		return std::visit ( [] ( auto&& arg ) -> bool {
			using T = std::decay_t<decltype( arg )>;

			if constexpr ( std::is_same_v<T, openGL_Image> )
				return arg.isValid ();
			else if constexpr ( std::is_same_v<T, juce::Image> )
				return arg.isValid ();
			else if constexpr ( std::is_same_v<T, shaderFloatTexture> )
				return ! arg.floatPalette.empty ();
			return false;

		}, source );
	}
};
//-----------------------------------------------------------------------------

} // namespace lime
