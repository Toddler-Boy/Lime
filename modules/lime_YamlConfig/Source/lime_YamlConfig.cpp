#include "lime_YamlConfig.h"

#include <ranges>

//------------------------------------------------------------------------------

lime::YamlConfig::YamlConfig ( const std::vector<std::pair<std::string, ConfigValue>>& initialSetup )
{
	masterData.reserve ( initialSetup.size () );
	for ( const auto& item : initialSetup )
	{
		auto	lower = item.first;

		std::ranges::transform ( lower, lower.begin (), [] ( unsigned char c ) {return std::tolower ( c );} );

		masterData.push_back ( { item.first, lower, item.second, item.second } );
	}

	lookupIndex.clear ();
	lookupIndex.reserve ( masterData.size () );

	for ( auto& entry : masterData )
		lookupIndex.emplace_back ( &entry );

	std::ranges::sort ( lookupIndex, {}, &ConfigEntry::lookupKey );
}
//------------------------------------------------------------------------------

void lime::YamlConfig::load ( const juce::File& file )
{
	if ( ! file.existsAsFile () )
		return;

	if ( auto content = file.loadFileAsString (); content.isNotEmpty () )
		fromYamlString ( content );
}
//------------------------------------------------------------------------------

void lime::YamlConfig::save ( const juce::File& file ) const
{
	juce::FileOutputStream	out ( file );

	if ( out.failedToOpen () )
		return;

	out.setPosition ( 0 );
	out.writeText ( toYamlString (), false, false, nullptr );
	out.truncate ();
}
//------------------------------------------------------------------------------

void lime::YamlConfig::fromYamlString ( const juce::String& yaml )
{
	juce::StringArray   lines;
	juce::StringArray   pathStack;

	lines.addLines ( yaml );

	for ( auto& line : lines )
	{
		if ( line.trim ().isEmpty () || line.trim ().startsWith ( "#" ) )
			continue;

		auto	level = 0;
		while ( level < line.length () && line[ level ] == ' ' )
			level++;
		level /= 2;

		auto	key = line.upToFirstOccurrenceOf ( ":", false, false ).trim ().toLowerCase ();
		auto	valPart = line.fromFirstOccurrenceOf ( ":", false, false ).trim ();

		while ( pathStack.size () > level )
			pathStack.remove ( pathStack.size () - 1 );

		if ( valPart.isEmpty () )
		{
			pathStack.add ( key );
		}
		else
		{
			auto	fullPath = pathStack.joinIntoString ( "/" ) + ( pathStack.isEmpty () ? "" : "/" ) + key;

			if ( auto* entry = findEntry ( fullPath.toStdString () ) )
			{
				// Use the existing type to decide how to parse
				std::visit ( [ & ] ( auto&& arg ) {
					using T = std::decay_t<decltype( arg )>;

					if constexpr ( std::is_same_v<T, bool> )
						entry->current = ( valPart == "true" );
					else if constexpr ( std::is_same_v<T, int> )
						entry->current = valPart.getIntValue ();
					else if constexpr ( std::is_same_v<T, float> )
						entry->current = valPart.getFloatValue ();
					else if constexpr ( std::is_same_v<T, std::string> )
						entry->current = valPart.unquoted ().toStdString ();
					else if constexpr ( std::is_same_v<T, vec2f> || std::is_same_v<T, vec2i> )
					{
						auto	inner = valPart.substring ( valPart.indexOf ( "[" ) + 1, valPart.lastIndexOf ( "]" ) );
						auto	v1 = inner.upToFirstOccurrenceOf ( ",", false, false ).trim ();
						auto	v2 = inner.fromFirstOccurrenceOf ( ",", false, false ).trim ();

						if constexpr ( std::is_same_v<T, vec2f> )
							entry->current = vec2f { v1.getFloatValue (), v2.getFloatValue () };
						else
							entry->current = vec2i { v1.getIntValue (), v2.getIntValue () };
					}
				}, entry->current );
			}
		}
	}
}
//------------------------------------------------------------------------------

juce::String lime::YamlConfig::toYamlString () const
{
	struct YamlNode
	{
		std::map<juce::String, std::unique_ptr<YamlNode>> children;
		const ConfigEntry* entry = nullptr;
	};

	YamlNode	root;
	for ( const auto& entry : masterData )
	{
		auto	parts = juce::StringArray::fromTokens ( entry.originalKey, "/", "" );
		auto*	current = &root;
		for ( auto& part : parts )
		{
			if ( ! current->children.contains ( part ) )
				current->children[ part ] = std::make_unique<YamlNode> ();

			current = current->children[ part ].get ();
		}
		current->entry = &entry;
	}

	juce::String	output;
	std::function<void ( const YamlNode&, int )> printNode = [ & ] ( const YamlNode& node, int indent )
	{
		for ( auto it = node.children.begin (); it != node.children.end (); ++it )
		{
			output << juce::String::repeatedString ( "  ", indent ) << it->first << ":";
			if ( it->second->entry )
			{
				std::visit ( [ & ] ( auto&& arg ) {

					using	T = std::decay_t<decltype( arg )>;

					if constexpr ( std::is_same_v<T, juce::String> )
						output << " " << arg.quoted () << "\n";
					else if constexpr ( std::is_same_v<T, bool> )
						output << ( arg ? " true\n" : " false\n" );
					else if constexpr ( std::is_same_v<T, vec2f> || std::is_same_v<T, vec2i> )
						output << " [ " << arg.first << ", " << arg.second << " ]\n";
					else
						output << " " << juce::String ( arg ) << "\n";

				}, it->second->entry->current );
			}
			else
			{
				output << "\n";
				printNode ( *it->second, indent + 1 );
				if ( std::next ( it ) != node.children.end () )
					output << "\n";
			}
		}
	};

	printNode ( root, 0 );
	return output;
}
//------------------------------------------------------------------------------
