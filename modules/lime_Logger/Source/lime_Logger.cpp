#include <ctime>

#include "lime_LoggerWindow.h"

//-------------------------------------------------------------------------------------------------

namespace lime
{
JUCE_IMPLEMENT_SINGLETON ( Logger )

//-------------------------------------------------------------------------------------------------

juce::String LogMessage::toString ()
{
	// Compose final message
	char	dstTime[ 100 ] = { 0 };
	std::strftime ( dstTime, sizeof ( dstTime ), "%T", std::localtime ( &timeStamp ) );

	juce::String code;
	switch ( level )
	{
		case LogLevel::log: 		code = "LOG "; break;
		case LogLevel::info: 		code = "INFO"; break;
		case LogLevel::warning: 	code = "WARN"; break;
		case LogLevel::error: 		code = "ERR "; break;
		case LogLevel::debuglog: 	code = "DLOG"; break;
		default: jassertfalse; 		code = "   "; break;
	}

	return juce::String ( dstTime ) + ": " + code + " - " + description;
}
//-------------------------------------------------------------------------------------------------

Logger::~Logger ()
{
	clearSingletonInstance ();
}
//-------------------------------------------------------------------------------------------------

void Logger::addListener ( Listener* l )
{
	listeners.add ( l );
}
//----------------------------------------------------------------------------------

void Logger::removeListener ( Listener* l )
{
	listeners.remove ( l );
}
//----------------------------------------------------------------------------------

void Logger::logMessage ( const juce::String& messageText )
{
	auto	self = Logger::getInstance ();

	juce::ScopedLock	sl ( self->lock );

	auto	level = LogLevel::log;

	if (		messageText.startsWith ( "[E]" ) )	level = LogLevel::error;
	else if (	messageText.startsWith ( "[W]" ) )	level = LogLevel::warning;
	else if (	messageText.startsWith ( "[I]" ) )	level = LogLevel::info;
	else if (	messageText.startsWith ( "[D]" ) )	level = LogLevel::debuglog;

	LogMessage	msg = { messageText.startsWithChar ( '[' ) ? messageText.substring ( 3 ) : messageText, level };

	self->messages.add ( msg );
	self->triggerAsyncUpdate ();

	if ( self->logStream )
	{
		self->logStream->writeText ( msg.toString () + "\r\n", false, false, nullptr );
		self->logStream->flush ();
	}

	outputDebugString ( msg.toString () );

	juce::MessageManager::callAsync ( [ self, msg ]
	{
		self->listeners.call ( [ msg ] ( Listener& l ) { l.messageLogged ( msg ); } );
	} );
}
//-------------------------------------------------------------------------------------------------

void Logger::handleAsyncUpdate ()
{
	if ( loggerWindow )
		loggerWindow->update ();
}
//-------------------------------------------------------------------------------------------------

juce::Array<LogMessage> Logger::getMessages ()
{
	juce::ScopedLock sl ( lock );

	return messages;
}
//-------------------------------------------------------------------------------------------------

void Logger::closeLoggingWindow ()
{
	loggerWindow = nullptr;
}
//-------------------------------------------------------------------------------------------------

LoggerWindow& Logger::getLoggingWindow ( const LoggerOptions& opts )
{
	if ( ! loggerWindow )
		loggerWindow = std::make_unique<LoggerWindow> ( *this, opts );

	return *loggerWindow;
}
//-------------------------------------------------------------------------------------------------

bool Logger::isLoggingWindowVisible ()
{
	if ( ! loggerWindow )
		return false;

	return loggerWindow->isVisible ();
}
//-------------------------------------------------------------------------------------------------

void Logger::setLogFolder ( const juce::File& f )
{
	juce::ScopedLock	sl ( lock );

	if ( f != juce::File () )
	{
		f.createDirectory ();

		auto	files = f.findChildFiles ( juce::File::findFiles, true, "*", juce::File::FollowSymlinks::noCycles );
		std::sort ( files.begin (), files.end (), [] ( const auto& lhs, const auto& rhs ) { return lhs.getCreationTime () < rhs.getCreationTime (); } );

		while ( files.size () > 3 )
			files.removeAndReturn ( 0 ).deleteFile ();

		auto	logFile = f.getChildFile ( juce::Time::getCurrentTime ().toISO8601 ( false ) + ".txt" );
		logStream = std::make_unique<juce::FileOutputStream> ( logFile );
	}
	else
	{
		logStream = nullptr;
	}

	logFolder = f;
}
//-------------------------------------------------------------------------------------------------

juce::String Logger::getLogLevelName ( LogLevel l )
{
	switch ( l )
	{
		case LogLevel::error:		return "Error";
		case LogLevel::warning:		return "Warning";
		case LogLevel::info:		return "Info";
		case LogLevel::log:			return "Log";
		case LogLevel::debuglog:	return "Debug Log";
		default:
			jassertfalse;
			return {};
	}
}
//-------------------------------------------------------------------------------------------------

juce::String Logger::getSystemStats ()
{
	juce::String	text;

	if ( creatorString.isNotEmpty () )
		text += "Creator:   " + creatorString + "\r\n";

	text += "Location:  " + juce::File::getSpecialLocation ( juce::File::currentApplicationFile ).getFullPathName () + "\r\n";
	text += "Timestamp: " + juce::Time::getCurrentTime ().toString ( true, true, true, true ) + "\r\n\r\n";

	//
	// Computer specific
	//
	auto isCurrentUserAdmin = [] () -> bool
	{
		#if JUCE_MAC || JUCE_LINUX
			juce::ChildProcess cp;
			cp.start ( { "id", "-u" } );
			cp.waitForProcessToFinish ( 100 );

			auto output = cp.readAllProcessOutput ().trim ();
			return output == "0";
		#elif JUCE_WINDOWS
			// Get authority information
			SID_IDENTIFIER_AUTHORITY	NtAuthority = SECURITY_NT_AUTHORITY;
			PSID						AdministratorsGroup;

			if ( AllocateAndInitializeSid ( &NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &AdministratorsGroup ) )
			{
				BOOL	isMember = false;

				CheckTokenMembership ( nullptr, AdministratorsGroup, &isMember );

				FreeSid ( AdministratorsGroup );

				return isMember;
			}
			return false;
		#else
			#error "Unknown platform!"
		#endif
	};

	text += "Computer:  " + juce::SystemStats::getComputerName () + "\r\n";
	text += "OS:        " + juce::SystemStats::getOperatingSystemName () + "\r\n";
	text += "Device:    " + ( juce::SystemStats::getDeviceManufacturer () + " " + juce::SystemStats::getDeviceDescription () ).trim () + "\r\n";
	text += "Admin:     " + juce::String ( isCurrentUserAdmin () ? "Yes" : "No" ) + "\r\n\r\n";

	//
	// CPU specific
	//
	text += "CPU:       " + juce::SystemStats::getCpuVendor () + " " + juce::SystemStats::getCpuModel () + " " + juce::String ( juce::SystemStats::getCpuSpeedInMegahertz () ) + " MHz\r\n";
	text += "Cores:     " + juce::String ( juce::SystemStats::getNumPhysicalCpus () ) + " / " + juce::String ( juce::SystemStats::getNumCpus () ) + "\r\n";
	text += "Memory:    " + juce::String ( juce::roundToInt ( juce::SystemStats::getMemorySizeInMegabytes () / 1024.0 ) ) + " GB" + "\r\n";

	//
	// Displays
	//
	for ( const auto& d : juce::Desktop::getInstance().getDisplays ().displays )
	{
		const auto	physRect = ( d.logicalBounds.toDouble () * d.scale ).toNearestIntEdges ();

		text += juce::String::formatted ( "Display:   %d x %d @ %d%%\r\n", physRect.getWidth (), physRect.getHeight (), juce::roundToInt ( d.scale * 100.0 ) );
	}

	if ( additionalSystemStats )
		text += additionalSystemStats ();

	text += "------------------------------------------------------------------------------\r\n\r\n";

	return text;
}
//-------------------------------------------------------------------------------------------------

juce::String Logger::getAsString ()
{
	auto	text = getSystemStats ();

	if ( logStream != nullptr )
		text += mergeLogFiles ();

	for ( auto& msg : messages )
		text += msg.toString () + "\r\n";

	return text;
}
//-------------------------------------------------------------------------------------------------

juce::String Logger::mergeLogFiles ()
{
	juce::String text;

	auto	files = logFolder.findChildFiles ( juce::File::findFiles, false );
	std::sort ( files.begin (), files.end (), [] ( const auto& lhs, const auto& rhs ) { return lhs.getCreationTime () < rhs.getCreationTime (); } );

	// Get all the files except the last (current) one
	for ( auto i = 0; i < files.size () - 1; i++ )
	{
		auto	f = files[ i ];

		text += f.loadFileAsString ();
		text += "------------------------------------------------------------------------------\r\n\r\n";
	}

	return text;
}
//-------------------------------------------------------------------------------------------------

}
