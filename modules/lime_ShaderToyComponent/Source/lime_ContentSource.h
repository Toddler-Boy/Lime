#pragma once

#include <functional>

//-----------------------------------------------------------------------------

// All shader-stack file access funnels through here: the real file system by
// default, or a host-installed Loader that delivers archived bytes for the
// juce::Files the components build (then mere lookup keys, no watchers).

namespace lime::content
{
	struct Loader
	{
		std::function<juce::MemoryBlock ( const juce::File& file )>	load;
		std::function<bool ( const juce::File& file )>				exists;

		// Files or immediate sub-folders of dir; the wildcard applies to files
		std::function<juce::Array<juce::File> ( const juce::File& dir, const bool recursive, const juce::String& wildcard, const bool folders )>	list;
	};

	void setLoader ( Loader loader );
	[[ nodiscard ]] bool hasLoader ();

	[[ nodiscard ]] juce::MemoryBlock loadData ( const juce::File& file );
	[[ nodiscard ]] juce::String loadText ( const juce::File& file );
	[[ nodiscard ]] bool exists ( const juce::File& file );

	// PNG/JPEG decoded straight into an upload-ready buffer (BGRA8,
	// premultiplied); no juce::Image, so nothing lands on the GPU twice
	[[ nodiscard ]] openGL_Image decodeTexture ( const void* data, const size_t size );
	[[ nodiscard ]] openGL_Image loadTexture ( const juce::File& file );

	[[ nodiscard ]] juce::Array<juce::File> listFiles ( const juce::File& dir, const bool recursive, const juce::String& wildcard );
	[[ nodiscard ]] juce::Array<juce::File> listFolders ( const juce::File& dir );
}
//-----------------------------------------------------------------------------
