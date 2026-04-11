#pragma once

#include "lime_shaderTarget.h"

namespace lime
{
//-----------------------------------------------------------------------------

class ShaderToyComponent : public juce::Component,	private juce::OpenGLRenderer, public gin::FileSystemWatcher::Listener
{
public:
	ShaderToyComponent ( const bool canHaveChildren = false );
	~ShaderToyComponent () override;

	// juce::OpenGLRenderer
	void newOpenGLContextCreated () override;
	void openGLContextClosing () override;
	void renderOpenGL () override;

	// juce::Component
	void resized () override;
	void paint ( juce::Graphics& /*g*/ ) override {}

	// this
	bool isReady () const { return openGLContext.isAttached (); }

	void setRoot ( const juce::File& _root, const juce::File& _local, const juce::String& shaderFolderName = "!Shaders", const juce::String& textureFolderName = "!Textures" );

	void setPipeline ( const juce::File& _file );
	void parsePipeline ();
	void resetPipeline ();

	shaderTarget* addTarget ( const juce::String& name );
	shaderTexture* addTexture ( const juce::String& name, std::function<void ( shaderTexture *dst, const juce::File& )> load = nullptr );

	shaderTarget* getTarget ( const juce::String& name );
	shaderTexture* getTexture ( const juce::String& name );

	void setTargetShader ( shaderTarget* dst, const juce::String& name );
	void setTextureSource ( shaderTexture* dst, const juce::String& name );

	void setBackgroundColor ( const juce::Colour col );
	void setFrameAndTime ( const int iFrame, const float iTime );

	void setGlobalUniform ( const std::string& name, const std::initializer_list<const float>& values );
	void setGlobalUniform ( const std::string& name, const std::span<const float>& values );
	void setGlobalUniform ( const std::string& name, const float value );
	void setGlobalUniform ( const std::string& name, const int value );

	void setCaptureAddress ( void* addr );
	float getRenderingScale () const { return float ( openGLContext.getRenderingScale () ); }
	bool hasFinished () const { return lastFrameRendered == lastFrameRequested; }

	// Helpers
	double getDeltaTime ();

	// gin::FileSystemWatcher::Listener
	void fileChanged ( const juce::File& file, gin::FileSystemWatcher::FileSystemEvent event ) override;

private:
	// OpenGL Variables
	GLfloat					backgroundColor[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };

	// Helpers
	std::chrono::steady_clock::time_point	lastTime;

	// Frame
	void*				captureAddress = nullptr;
	std::atomic<int>	lastFrameRequested = -1;
	std::atomic<int>	lastFrameRendered = -1;

	// Roots
	juce::File			root;
	juce::File			localRoot;
	juce::String		shaderFolderName = "!Shaders";
	juce::String		textureFolderName = "!Textures";
	juce::File			renderPipeline;

	juce::File findFile ( const juce::String& name );

protected:
	juce::OpenGLContext		openGLContext;

	// gin::FileSystemWatcher
	gin::FileSystemWatcher	fsWatcher;

	size_t		hardcodedTextureCount = 0;
	std::vector<std::unique_ptr<shaderTexture>>			textures;

	size_t		hardcodedShaderCount = 0;
	std::vector<std::unique_ptr<shaderTarget>>			targets;

private:
	//
	// Helpers
	//
	juce::String loadFragmentShader ( juce::String name );
};
//-----------------------------------------------------------------------------
}