#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <variant>
#include <vector>

namespace lime
{

class YamlConfig
{
public:
	using vec2f = std::pair<float, float>;
	using vec2i = std::pair<int, int>;

	using ConfigValue = std::variant<
		bool,

		int,
		vec2i,

		float,
		vec2f,

		std::string
	>;

	struct ConfigEntry
	{
		std::string		originalKey;
		std::string		lookupKey;
		ConfigValue		current;
		ConfigValue		defaultValue;
	};

	YamlConfig ( const std::vector<std::pair<std::string, ConfigValue>>& initialSetup );

	void load ( const juce::File& file );
	void save ( const juce::File& file ) const;

	void fromYamlString ( const juce::String& yaml );
	[[ nodiscard ]] juce::String toYamlString () const;

	//------------------------------------------------------------------------------

	template <typename T>
	void set ( const std::string_view& key, T newValue )
	{
		if ( auto* entry = findEntry ( key ) )
			entry->current = newValue;
	}
	//------------------------------------------------------------------------------

	template <typename T>
	T get ( const std::string_view& key ) const
	{
		if ( const auto* entry = findEntry ( key ) )
		{
			auto*	val = std::get_if<T> ( &entry->current );
			jassert ( val != nullptr ); // Type must match
			return *val;
		}
		return {};
	}
	//------------------------------------------------------------------------------

	template <typename T>
	T getDefault ( const std::string_view& key ) const
	{
		if ( const auto* entry = findEntry ( key ) )
		{
			auto*	val = std::get_if<T> ( &entry->defaultValue );
			jassert ( val != nullptr ); // Type must match
			return *val;
		}
		return {};
	}
	//------------------------------------------------------------------------------

private:
	[[ nodiscard ]] ConfigEntry* findEntry ( const std::string_view& key ) const
	{
		auto	it = std::ranges::lower_bound ( lookupIndex, key, {}, &ConfigEntry::lookupKey );
		if ( it != lookupIndex.end () && ( *it )->lookupKey == key )
			return *it;

		jassertfalse; // Key not found

		return nullptr;
	}
	//------------------------------------------------------------------------------

	std::vector<ConfigEntry>    masterData;
	std::vector<ConfigEntry*>   lookupIndex;
};
//------------------------------------------------------------------------------
}
