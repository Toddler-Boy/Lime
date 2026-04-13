#pragma once

#include <chrono>

namespace lime
{
//-------------------------------------------------------------------------------------------------

class LoggerWindow
	: public juce::DocumentWindow
{
public:
	LoggerWindow ( Logger&, const LoggerOptions& );
	~LoggerWindow () override;

	void update ();
	void visibilityChanged () override;

private:
	void closeButtonPressed () override;
	float getDesktopScaleFactor () const override;

	//-------------------------------------------------------------------------------------------------

	class Content
		: public juce::Component
		, public juce::ListBoxModel
	{
	public:
		Content ( LoggerWindow& owner );

		int getNumRows () override;
		void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
		void resized () override;
		void paint ( juce::Graphics& g ) override;
		juce::String getNameForRow ( int row ) override;

		LoggerWindow&		owner;

		juce::ListBox		dbc;
		juce::TextButton	clearButton { "Clear" };
		juce::TextButton	saveButton { "Save to Desktop" };
	   #if JUCE_DEBUG
		juce::TextButton	levelButton { "Level" };
	   #endif
	};

	Logger&			logging;
	juce::File			settingsFile;
	std::unique_ptr<juce::LookAndFeel>	laf;
	juce::Array<LogMessage> messages;

	time_t 				logClearedTime = 0;
	bool				everShown = false;
	LoggerOptions		opts;

	Content				content { *this };

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoggerWindow)
};
//-------------------------------------------------------------------------------------------------
}
