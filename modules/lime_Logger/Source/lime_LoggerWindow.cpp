#include <ctime>

#include "lime_LoggerWindow.h"

//-------------------------------------------------------------------------------------------------

namespace lime
{

LoggerWindow::Content::Content ( LoggerWindow& o )
	: owner ( o )
{
	addAndMakeVisible ( dbc );
	dbc.setModel ( this );
	dbc.setOpaque ( true );
	dbc.setRowHeight ( owner.opts.rowHeight );
	dbc.setOutlineThickness ( 4 );
	dbc.setColour ( juce::ListBox::backgroundColourId, juce::Colour ( 0xff'13161B ) );
	dbc.setColour ( juce::ListBox::outlineColourId, juce::Colours::transparentBlack );

	const auto	txtCol = juce::Colour ( 0xff'ACBDD5 );

	addAndMakeVisible ( clearButton );
	clearButton.setColour ( juce::TextButton::textColourOnId, txtCol );
	clearButton.setColour ( juce::TextButton::textColourOffId, txtCol );
	clearButton.onClick = [ this ]
	{
		owner.logClearedTime = std::chrono::system_clock::to_time_t ( std::chrono::system_clock::now () );
		owner.update ();
	};

	addAndMakeVisible ( saveButton );
	saveButton.setColour ( juce::TextButton::textColourOnId, txtCol );
	saveButton.setColour ( juce::TextButton::textColourOffId, txtCol );
	saveButton.onClick = [ this ]
	{
		auto f = juce::File::getSpecialLocation ( juce::File::userDesktopDirectory ).getChildFile ( owner.getName ().replace ( "logging window", "Support Info" ) + ".txt" );
		f.replaceWithText ( owner.logging.getAsString () );
	};

   #if JUCE_DEBUG
	addAndMakeVisible ( levelButton );
	levelButton.setButtonText ( Logger::getLogLevelName ( owner.logging.getLogLevel () ) );
	levelButton.setColour ( juce::TextButton::textColourOnId, txtCol );
	levelButton.setColour ( juce::TextButton::textColourOffId, txtCol );
	levelButton.onClick = [ this ]
	{
		juce::PopupMenu	m;

		for ( auto i = int ( LogLevel::error ); i >= int ( LogLevel::debuglog ); --i )
		{
			auto	l = LogLevel ( i );
			m.addItem ( Logger::getLogLevelName ( l ),	true, owner.logging.getLogLevel () == l, [ this, l ]
			{
				levelButton.setButtonText ( Logger::getLogLevelName ( l ) );
				owner.logging.setLogLevel ( l );
				owner.update ();
			} );
		}

		m.showMenuAsync ( juce::PopupMenu::Options ().withDeletionCheck ( *this ).withTargetComponent ( levelButton ) );
	};
   #endif
	owner.update ();
}
//-------------------------------------------------------------------------------------------------

void LoggerWindow::Content::resized ()
{
	auto bounds = getLocalBounds ();

	auto rc = bounds.removeFromTop ( 40 ).reduced ( 5 );

	clearButton.setBounds ( rc.removeFromLeft ( 60 ).reduced ( 2 ) );
	saveButton.setBounds ( rc.removeFromRight ( 120 ).reduced ( 2 ) );
   #if JUCE_DEBUG
	rc.removeFromRight ( 4 );
	levelButton.setBounds ( rc.removeFromRight ( 120 ).reduced ( 2 ) );
   #endif

	dbc.setBounds ( bounds );
}
//-------------------------------------------------------------------------------------------------

void LoggerWindow::Content::paint ( juce::Graphics& g )
{
	g.fillAll ( juce::Colour ( 0xff'13161B ).overlaidWith ( juce::Colours::white.withAlpha ( 0.05f ) ) );
}
//-------------------------------------------------------------------------------------------------

int LoggerWindow::Content::getNumRows ()
{
	return owner.messages.size ();
}
//-------------------------------------------------------------------------------------------------

juce::String LoggerWindow::Content::getNameForRow ( int row )
{
	if ( juce::isPositiveAndBelow ( row, owner.messages.size () ) )
	{
		const auto message = owner.messages[ row ];

		// Compose final message
		char dstTime[ 100 ] = { 0 };
		std::strftime ( dstTime, sizeof ( dstTime ), "%T", std::localtime ( &message.timeStamp ) );

		return juce::String ( dstTime ) + " - " + message.description;
	}

	return {};
}
//-------------------------------------------------------------------------------------------------

void LoggerWindow::Content::paintListBoxItem ( int row, juce::Graphics& g, int width, int height, bool /*rowIsSelected*/ )
{
	if ( juce::isPositiveAndBelow ( row, owner.messages.size () ) )
	{
		const auto message = owner.messages[ row ];

		static juce::Colour	levels[][ 2 ] = {
			{	juce::Colour ( 0xff'EBFD5A ),		juce::Colours::black	},	// dlog
			{	juce::Colours::transparentBlack,	juce::Colours::white	},	// log
			{   juce::Colour ( 0xFF'43A047 ),		juce::Colours::white	},	// info
			{   juce::Colour ( 0xff'ECBF54 ),		juce::Colours::black	},	// warn
			{	juce::Colour ( 0xff'FC5454 ),		juce::Colours::black	},	// err
		};

		// Compose final message
		char dstTime[ 100 ] = { 0 };
		std::strftime ( dstTime, sizeof ( dstTime ), "%T ", std::localtime ( &message.timeStamp ) );

		const auto	text = juce::String ( dstTime ) + message.description;

		g.setFont ( owner.opts.font );

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

LoggerWindow::LoggerWindow ( Logger& l, const LoggerOptions& opts_ )
	: juce::DocumentWindow ( opts_.name, juce::Colours::coral, TitleBarButtons::closeButton )
	, logging ( l )
	, opts ( opts_ )
{
	if ( opts.lookAndFeelFactory )
		laf = opts.lookAndFeelFactory ();

	setUsingNativeTitleBar ( true );
	setLookAndFeel ( laf.get () );
	setContentNonOwned ( &content, false );
	setResizable ( true, true );

   #if JUCE_MAC
	juce::String settingsSubDir = "Application Support/" + opts.settingsFolder;
   #else
	juce::String settingsSubDir = opts.settingsFolder;
   #endif
	juce::String appName = juce::File::getSpecialLocation ( juce::File::currentExecutableFile ).getFileNameWithoutExtension ().replace ( " ", "_" );
	settingsFile = juce::File::getSpecialLocation ( juce::File::userApplicationDataDirectory ).getChildFile ( settingsSubDir + "/logging_window_" + opts.name + ".json" );
	settingsFile.getParentDirectory ().createDirectory ();

	auto json = juce::JSON::parse ( settingsFile );

	juce::String pos = json.getProperty ( "/window_pos", "" ).toString ();

	//
	// Get saved window rect
	//
	auto tokens = juce::StringArray::fromTokens ( pos, false );
	tokens.removeEmptyStrings();
	tokens.trim();
	if ( tokens[ 0 ] == "fs" )
		tokens.remove ( 0 );

	//
	// Make sure it has a decent size
	//
	juce::Rectangle<int> newPos ( tokens[ 0 ].getIntValue (), tokens[ 1 ].getIntValue (), tokens[ 2 ].getIntValue (), tokens[ 3 ].getIntValue () );
	if ( newPos.getWidth () < 200 || newPos.getHeight () < 200 )
		newPos = { 100, 100, 600, 800 };

	//
	// Make sure it's on a display
	//
	std::optional<juce::Displays::Display> display;
	for ( auto d : juce::Desktop::getInstance ().getDisplays ().displays )
	{
		auto area = d.userBounds / opts.scale;

		if ( area.contains ( newPos.toFloat () ) )
		{
			display = d;
			break;
		}
	}

	if ( ! display.has_value () )
	{
		if ( auto d = juce::Desktop::getInstance ().getDisplays ().getPrimaryDisplay () )
		{
			display = *d;
			auto area = display->userBounds / opts.scale;
			newPos = juce::Rectangle<int> ( int ( area.getX () ) + 100, int ( area.getY () ) + 100, 600, 800 );
		}
	}

	//
	// Restore position
	//
	setBounds ( newPos );

	logging.setLogLevel ( ( LogLevel ) ( int ) json.getProperty ( "/log_level", ( int ) LogLevel::debuglog ) );

	update ();
}
//-------------------------------------------------------------------------------------------------

LoggerWindow::~LoggerWindow ()
{
	setLookAndFeel ( nullptr );

	if ( everShown )
	{
		auto json = juce::JSON::parse ( settingsFile );
		auto obj = json.getDynamicObject ();

		if ( ! obj )
			obj = new juce::DynamicObject ();

		obj->setProperty ( juce::String ( "/window_pos" ), getWindowStateAsString () );
	   #if JUCE_DEBUG
		obj->setProperty ( "/log_level", ( int ) logging.getLogLevel () );
	   #endif
		settingsFile.replaceWithText ( juce::JSON::toString ( juce::var ( obj ) ) );
	}
}
//-------------------------------------------------------------------------------------------------

void LoggerWindow::closeButtonPressed ()
{
	setVisible ( false );
}
//-------------------------------------------------------------------------------------------------

void LoggerWindow::visibilityChanged ()
{
	juce::DocumentWindow::visibilityChanged ();
	everShown = true;
}
//-------------------------------------------------------------------------------------------------

void LoggerWindow::update ()
{
	messages.clear ();

	for ( auto m : logging.getMessages () )
		if ( m.timeStamp >= logClearedTime && m.level >= logging.getLogLevel () )
			messages.add ( m );

	content.dbc.updateContent ();
	content.dbc.scrollToEnsureRowIsOnscreen ( messages.size () - 1 );
}
//-------------------------------------------------------------------------------------------------

float LoggerWindow::getDesktopScaleFactor () const
{
	return opts.scale * juce::Desktop::getInstance ().getGlobalScaleFactor ();
}
//-------------------------------------------------------------------------------------------------

}
