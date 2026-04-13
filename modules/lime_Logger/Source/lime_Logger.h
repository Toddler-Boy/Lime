#pragma once

#include <chrono>

#define	Z_ERR(_m)		{ juce::String zTempDbgBuf ( "[E]" ); zTempDbgBuf << _m; juce::Logger::writeToLog ( zTempDbgBuf ); }
#define	Z_WARN(_m)		{ juce::String zTempDbgBuf ( "[W]" ); zTempDbgBuf << _m; juce::Logger::writeToLog ( zTempDbgBuf ); }
#define	Z_INFO(_m)		{ juce::String zTempDbgBuf ( "[I]" ); zTempDbgBuf << _m; juce::Logger::writeToLog ( zTempDbgBuf ); }
#define Z_LOG(_m)		{ juce::String zTempDbgBuf ( "[L]" ); zTempDbgBuf << _m; juce::Logger::writeToLog ( zTempDbgBuf ); }

#ifdef _DEBUG
	#define Z_DLOG(_m)	{ juce::String zTempDbgBuf ( "[D]" ); zTempDbgBuf << _m; juce::Logger::writeToLog ( zTempDbgBuf ); }
#else
	#define Z_DLOG(_m)
#endif

namespace lime
{
//-------------------------------------------------------------------------------------------------

class LoggerWindow;

enum class LogLevel : int
{
	error		= 4,
	warning		= 3,
	info		= 2,
	log			= 1,
	debuglog	= 0,
};
//-------------------------------------------------------------------------------------------------

struct LoggerOptions
{
	juce::String	name = "logging window";
	juce::String	settingsFolder = "lime_Logger";
	float 			scale = 1.0f;
	int				rowHeight = 22;
	juce::Font		font = juce::Font ( juce::FontOptions () );

	std::function<std::unique_ptr<juce::LookAndFeel>()>	lookAndFeelFactory;
};
//-------------------------------------------------------------------------------------------------

struct LogMessage
{
	LogMessage ( const juce::String& d = "", const LogLevel l = LogLevel::debuglog )
		: description ( d ), level ( l ) {}

	juce::String toString ();

	const time_t		timeStamp = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now ());
	const juce::String	description;
	const LogLevel 		level = LogLevel::debuglog;
};
//-------------------------------------------------------------------------------------------------

class Logger
	: public juce::AsyncUpdater
	, public juce::DeletedAtShutdown
	, public juce::Logger
{
public:
	Logger () = default;
	~Logger () override;

	juce::String					creatorString;
	std::function<juce::String ()>	additionalSystemStats;

	void setLogFolder ( const juce::File& );

	LogLevel getLogLevel ()				{ return level; }
	void setLogLevel ( LogLevel l )		{ level = l;	}

	static juce::String getLogLevelName ( LogLevel l );

	void closeLoggingWindow ();
	LoggerWindow& getLoggingWindow ( const LoggerOptions& );
	bool isLoggingWindowVisible ();

	void logMessage ( const juce::String& message ) override;

	juce::String getAsString ();

	class Listener
	{
	public:
		virtual ~Listener () = default;

		virtual void messageLogged ( const LogMessage& ) {}
	};

	void addListener ( Listener* l );
	void removeListener ( Listener* l );

	//-------------------------------------------------------------------------------------------------
	JUCE_DECLARE_SINGLETON (Logger, false)

private:
	friend class LoggerWindow;

	juce::Array<LogMessage> getMessages ();

	void handleAsyncUpdate () override;

	juce::String getSystemStats ();
	juce::String mergeLogFiles ();

	juce::CriticalSection 	lock;
	juce::Array<LogMessage>	messages;
	LogLevel				level = LogLevel::debuglog;

	juce::File								logFolder;
	std::unique_ptr<juce::FileOutputStream>	logStream;
	std::unique_ptr<LoggerWindow> 			loggerWindow;

	juce::ListenerList<Listener>			listeners;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR ( Logger )
};
//-------------------------------------------------------------------------------------------------
}
