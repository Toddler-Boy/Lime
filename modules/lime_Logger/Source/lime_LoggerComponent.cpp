#include <ctime>

#include "lime_LoggerComponent.h"

namespace lime {

//-------------------------------------------------------------------------------------------------

LoggerComponent::LoggerComponent ()
{
	addAndMakeVisible ( dbc );
	dbc.setModel ( this );
	dbc.setOpaque ( true );
	dbc.setRowHeight ( 22 );
	dbc.setOutlineThickness ( 4 );
	dbc.setColour ( juce::ListBox::backgroundColourId, juce::Colour ( 0xff'13161B ) );
	dbc.setColour ( juce::ListBox::outlineColourId, juce::Colours::transparentBlack );

	Logger::getInstance ()->addListener ( this );
}
//-------------------------------------------------------------------------------------------------

LoggerComponent::~LoggerComponent ()
{
	Logger::getInstance ()->removeListener ( this );
}
//-------------------------------------------------------------------------------------------------

void LoggerComponent::resized ()
{
	auto bounds = getLocalBounds ();

	dbc.setBounds ( bounds );
}
//-------------------------------------------------------------------------------------------------

int LoggerComponent::getNumRows ()
{
	return messages.size ();
}
//-------------------------------------------------------------------------------------------------

juce::String LoggerComponent::getNameForRow ( int row )
{
	if ( juce::isPositiveAndBelow ( row, messages.size () ) )
	{
		const auto message = messages[ row ];

		return message.timeText + " - " + message.description;
	}

	return {};
}
//-------------------------------------------------------------------------------------------------

void LoggerComponent::paintListBoxItem ( int row, juce::Graphics& g, int width, int height, bool /*rowIsSelected*/ )
{
	if ( juce::isPositiveAndBelow ( row, messages.size () ) )
	{
		const auto message = messages[ row ];

		static juce::Colour	levels[][ 2 ] = {
			{	juce::Colour ( 0xff'EBFD5A ),		juce::Colours::black	},	// dlog
			{	juce::Colours::transparentBlack,	juce::Colours::white	},	// log
			{   juce::Colour ( 0xFF'43A047 ),		juce::Colours::white	},	// info
			{   juce::Colour ( 0xff'ECBF54 ),		juce::Colours::black	},	// warn
			{	juce::Colour ( 0xff'FC5454 ),		juce::Colours::black	},	// err
		};

		const auto	text = message.timeText + " " + message.description;

		g.setFont ( juce::FontOptions () );

		const auto	b = juce::Rectangle<int> ( width, height );

		const auto	msgLevel = int ( message.level );
		if ( const auto bckCol = levels[ msgLevel ][ 0 ]; ! bckCol.isTransparent () )
		{
			g.setColour ( bckCol );
			g.fillRect ( b );
		}

		g.setColour ( levels[ msgLevel ][ 1 ] );
		g.drawText ( text, b.reduced ( 4, 0 ), juce::Justification::centredLeft, true );
	}
}
//-------------------------------------------------------------------------------------------------

void LoggerComponent::messageLogged ( const LogMessage& msg )
{
	if ( msg.level >= LogLevel::info )
	{
		messages.add ( msg );
		dbc.updateContent ();
		dbc.setVerticalPosition ( 1.0f );
	}
}
//-------------------------------------------------------------------------------------------------

}
