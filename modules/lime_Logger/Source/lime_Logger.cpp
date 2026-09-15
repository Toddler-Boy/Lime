#include <ctime>

#include "lime_LoggerWindow.h"

//-------------------------------------------------------------------------------------------------

namespace lime
{
JUCE_IMPLEMENT_SINGLETON ( Logger )

//-------------------------------------------------------------------------------------------------

juce::String LogMessage::formatTimestamp ( const time_t t )
{
	// Reentrant localtime: this runs on whatever thread logs
	std::tm	tmBuf {};

	#if JUCE_WINDOWS
		localtime_s ( &tmBuf, &t );
	#else
		localtime_r ( &t, &tmBuf );
	#endif

	char	dstTime[ 100 ] = { 0 };
	std::strftime ( dstTime, sizeof ( dstTime ), "%T", &tmBuf );

	return juce::String ( dstTime );
}
//-------------------------------------------------------------------------------------------------

juce::String LogMessage::toString ()
{
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

	return timeText + ": " + code + " - " + description;
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
	auto	tagged = true;

	if (		messageText.startsWith ( "[E]" ) )	level = LogLevel::error;
	else if (	messageText.startsWith ( "[W]" ) )	level = LogLevel::warning;
	else if (	messageText.startsWith ( "[I]" ) )	level = LogLevel::info;
	else if (	messageText.startsWith ( "[D]" ) )	level = LogLevel::debuglog;
	else if (	messageText.startsWith ( "[L]" ) )	level = LogLevel::log;
	else		tagged = false;	// e.g. JUCE-internal "[..." lines: keep them intact

	LogMessage	msg = { tagged ? messageText.substring ( 3 ) : messageText, level };

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

constexpr auto	runHeader = "===== ";
constexpr auto	keepDays = 10;
constexpr auto	keepMinLines = 300;
constexpr auto	keepMaxLines = 2000;

// The date a run header carries, or an invalid Time for anything else
static juce::Time runDate ( const juce::String& line )
{
	const auto	date = line.startsWith ( runHeader ) ? line.substring ( 6, 16 ) : juce::String ();

	if ( date.length () != 10 || date[ 4 ] != '-' || date[ 7 ] != '-' || ! date.removeCharacters ( "-" ).containsOnly ( "0123456789" ) )
		return {};

	return juce::Time::fromISO8601 ( date );
}
//-------------------------------------------------------------------------------------------------

// Keeps the newest whole runs: every run from the last keepDays, more until
// keepMinLines are kept, none once keepMaxLines would be passed
static juce::StringArray trimmedRuns ( const juce::StringArray& lines )
{
	// Lines before the first header count as one run of unknown age
	juce::Array<int>	starts;

	for ( auto i = 0; i < lines.size (); ++i )
		if ( lines[ i ].startsWith ( runHeader ) || i == 0 )
			starts.add ( i );

	const auto	oldest = juce::Time::getCurrentTime () - juce::RelativeTime::days ( keepDays );

	auto	keepFrom = lines.size ();
	auto	kept = 0;

	for ( auto r = starts.size () - 1; r >= 0; --r )
	{
		const auto	start = starts[ r ];
		const auto	size = keepFrom - start;
		const auto	recent = runDate ( lines[ start ] ) >= oldest;

		if ( ! recent && kept >= keepMinLines )
			break;

		if ( kept + size > keepMaxLines )
			break;

		keepFrom = start;
		kept += size;
	}

	juce::StringArray	result;

	// A single run past the cap keeps its tail
	for ( auto i = std::max ( keepFrom, lines.size () - keepMaxLines ); i < lines.size (); ++i )
		result.add ( lines[ i ] );

	return result;
}
//-------------------------------------------------------------------------------------------------

void Logger::setLogFile ( const juce::File& file, const juce::String& sessionInfo )
{
	juce::ScopedLock	sl ( lock );

	logStream = nullptr;
	logHistory.clear ();

	if ( file == juce::File () )
		return;

	auto	lines = juce::StringArray::fromLines ( file.loadFileAsString () );
	lines.removeEmptyStrings ();

	const auto	kept = trimmedRuns ( lines );

	if ( kept.size () > 0 )
		logHistory = kept.joinIntoString ( "\r\n" ) + "\r\n";

	file.getParentDirectory ().createDirectory ();

	// A trimmed file is rewritten before it reopens for append
	if ( kept.size () < lines.size () && ! file.replaceWithText ( logHistory ) )
	{
		Z_WARN ( "Couldn't rewrite the log file: " << file.getFullPathName () );
		return;
	}

	auto	stream = std::make_unique<juce::FileOutputStream> ( file );

	if ( stream->failedToOpen () )
	{
		Z_WARN ( "Couldn't open the log file: " << file.getFullPathName () );
		return;
	}

	const auto	header = runHeader + juce::Time::getCurrentTime ().formatted ( "%Y-%m-%d %H:%M:%S" ) + " " + sessionInfo + " =====\r\n";

	stream->writeText ( header, false, false, nullptr );
	stream->flush ();

	logHistory += header;
	logStream = std::move ( stream );
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

	juce::ScopedLock	sl ( lock );

	text += logHistory;

	for ( auto& msg : messages )
		text += msg.toString () + "\r\n";

	return text;
}
//-------------------------------------------------------------------------------------------------

}
