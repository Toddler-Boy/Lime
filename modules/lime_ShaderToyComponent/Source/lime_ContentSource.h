#pragma once

#include <functional>

//-----------------------------------------------------------------------------

// All file access of the shader stack funnels through here. By default every
// call reads the real file system; a host that ships its data inside an
// archive installs a Loader once and delivers the bytes itself - the
// juce::Files the components build then act as pure lookup keys.
//
// With a loader installed there are no real files, so the components skip
// their hot-reload watchers; without one, nothing changes at all.

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
	[[ nodiscard ]] juce::Image loadImage ( const juce::File& file );
	[[ nodiscard ]] bool exists ( const juce::File& file );

	[[ nodiscard ]] juce::Array<juce::File> listFiles ( const juce::File& dir, const bool recursive, const juce::String& wildcard );
	[[ nodiscard ]] juce::Array<juce::File> listFolders ( const juce::File& dir );
}
//-----------------------------------------------------------------------------
