namespace lime::content
{

//-----------------------------------------------------------------------------

namespace
{
	Loader	activeLoader;
}
//-----------------------------------------------------------------------------

void setLoader ( Loader loader )
{
	activeLoader = std::move ( loader );
}
//-----------------------------------------------------------------------------

bool hasLoader ()
{
	return activeLoader.load != nullptr;
}
//-----------------------------------------------------------------------------

juce::MemoryBlock loadData ( const juce::File& file )
{
	if ( activeLoader.load )
		return activeLoader.load ( file );

	juce::MemoryBlock	mb;
	file.loadFileAsData ( mb );

	return mb;
}
//-----------------------------------------------------------------------------

juce::String loadText ( const juce::File& file )
{
	if ( ! activeLoader.load )
		return file.loadFileAsString ();

	const auto	mb = activeLoader.load ( file );

	return juce::String::createStringFromData ( mb.getData (), int ( mb.getSize () ) );
}
//-----------------------------------------------------------------------------

openGL_Image loadTexture ( const juce::File& file )
{
	const auto	mb = loadData ( file );

	return decodeTexture ( mb.getData (), mb.getSize () );
}
//-----------------------------------------------------------------------------

bool exists ( const juce::File& file )
{
	if ( activeLoader.exists )
		return activeLoader.exists ( file );

	return file.existsAsFile ();
}
//-----------------------------------------------------------------------------

juce::Array<juce::File> listFiles ( const juce::File& dir, const bool recursive, const juce::String& wildcard )
{
	if ( activeLoader.list )
		return activeLoader.list ( dir, recursive, wildcard, false );

	return dir.findChildFiles ( juce::File::findFiles | juce::File::ignoreHiddenFiles, recursive, wildcard.isEmpty () ? "*" : wildcard );
}
//-----------------------------------------------------------------------------

juce::Array<juce::File> listFolders ( const juce::File& dir )
{
	if ( activeLoader.list )
		return activeLoader.list ( dir, false, {}, true );

	return dir.findChildFiles ( juce::File::findDirectories | juce::File::ignoreHiddenFiles, false, "*" );
}
//-----------------------------------------------------------------------------

}
