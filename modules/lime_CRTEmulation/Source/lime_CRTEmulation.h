#pragma once

#include <chrono>

//-----------------------------------------------------------------------------

namespace lime
{

class CRTEmulation : public lime::ShaderToyComponent, private juce::Thread
{
public:
	struct resolutions
	{
		int	nativeWidth;
		int	nativeHeight;
		int	scaledWidth;
		int	scaledHeight;
	};

	CRTEmulation ( const bool canHaveChildren, const int idleTimeout, const juce::File& root, const resolutions& res );
	~CRTEmulation () override;

	struct settings
	{
		// Overlay
		bool			overlay = true;
		juce::String	overlayProfile = "C1702 Bedroom";
		int8_t			overlayDaytime = 35;
		int8_t			overlayZoom = 0;

		int8_t			overlayBezel = 80;
		int8_t			overlayShadow = 80;
		int8_t			overlayDust = 50;
		int8_t			overlayBloom = 50;
		int8_t			overlayChromaticAberration = 50;
		int8_t			overlayGrain = 50;

		// TV
		bool			isNTSC = false;

		int8_t			brightness = 50;
		int8_t			contrast = 80;
		int8_t			saturation = 50;
		int8_t			overscan = 0;

		// Emulation itself. With this off, it's just an overlay on top of an unprocessed image
		bool			crtEmulation = true;

		// Encoder
		int8_t			encJailbars = 50;

		// Decoder
		int8_t			decNoise = 15;
		int8_t			decSharpening = 30;
		int8_t			decLumaBlur = 50;
		int8_t			decChromaBlur = 50;
		int8_t			decCrosstalk = 20;
		int8_t			decHannover = 80;
		int8_t			decRainbowing = 50;
		int8_t			decPhaseError = 20;

		// CRT
		int8_t			crtCurve = 20;
		int8_t			crtBleed = 20;
		std::pair<int8_t, int8_t> crtBleedRed = { 50, -10 };
		std::pair<int8_t, int8_t> crtBleedGreen = { 50, -10 };
		std::pair<int8_t, int8_t> crtBleedBlue = { 50, -10 };
		int8_t			crtConvergence = 20;
		int8_t			crtHwave = 50;
		int8_t			crtBloomExpansion = 100;
		int8_t			crtScanlines = 50;
		int8_t			crtMask = 50;
		juce::String	crtMaskBitmap = "Shadow Mask EDP";
		int8_t			crtGlow = 66;
		int8_t			crtPhosphorDecay = 60;
		int8_t			crtReflections = 50;
		int8_t			crtAmbient = 60;
		int8_t			crtVignette = 60;

		// Webcam
		bool			webcam = true;
		int8_t			webcamBrightness = 50;
		int8_t			webcamContrast = 50;
		int8_t			webcamSaturation = 50;

		[[ nodiscard ]] bool needsNewEncoders ( const settings& other ) const
		{
			return isNTSC != other.isNTSC || needsNewSimulation ( other );
		}

		[[ nodiscard ]] bool needsNewSimulation ( const settings& other ) const
		{
			return crtEmulation != other.crtEmulation;
		}
	};

	// juce::Component
	void resized () override;

	// ShaderToyComponent
	void renderFrame () override;

	// this
	void setRoot ( const juce::File& _root );
	void updateZoom ();

	[[ nodiscard ]] const juce::StringArray& getOverlays () const	{	return overlayProfiles;	}
	[[ nodiscard ]] const juce::StringArray& getCRTMasks () const	{	return crtMasks;		}

	void loadOverlayProfile ( const juce::String& profileName );
	void reloadOverlayProfile ();
	void updateOverlay ();

	void setSettings ( const settings& set );
	[[ nodiscard ]] settings& getSettings () { return curSettings; }

	void setBackgroundColor ( const juce::Colour bckCol );

	void setIndexTextureSource ( const juce::Image& img );
	void setIndexTextureSource ( const openGL_Image& img );
	void setLumaChromaPalette ( const std::span<float>& palette );

	void triggerIndexTextureUpdate () { indexSourceTexture->textureUpdated = true; }

	void fileChanged ( const juce::File& file, gin::FileSystemWatcher::FileSystemEvent event ) override;

private:
	// this
	[[ nodiscard ]] bool isBezelEnabled () const;
	[[ nodiscard ]] bool isOverlayEnabled () const;
	[[ nodiscard ]] bool isDustOrBloomEnabled () const;
	[[ nodiscard ]] bool isGlassEnabled () const;

	juce::Colour	bckCol = juce::Colours::black;

	resolutions		res;

	juce::Rectangle<float>	overlayImgRect;

	[[ nodiscard ]] juce::Rectangle<float> getTubeRect ( const juce::Rectangle<float>& target );
	[[ nodiscard ]] std::vector<juce::Rectangle<float>> calcRects ();
	[[ nodiscard ]] juce::Rectangle<float> getCRTRect ();

	settings		curSettings;
	float			currentZoom = 0.0f;
	float			currentOverscan = 0.0f;

	// Webcam raw-data NV12 (Windows & macOS)
	openGL_Image	camImageNV12_Y { 1, 1920, 1080 };
	openGL_Image	camImageNV12_UV { 2, 1920 / 2, 1080 / 2 };

	// Palette object
	shaderFloatTexture	lumaChromaPaletteSrc;

	// Index buffer & YUV/YIQ palette -> lumaChromaTarget
	shaderTarget*	indexTarget;
	openGL_Image	indexBuffer;
	shaderTexture*	indexSourceTexture;
	shaderTexture*	lumaChromaPalette;

	// YUV/YIQ image -> crtTarget (RGB)
	shaderTarget*	lumaChromaTarget;
	shaderTexture*	lumaChromaSourceTexture;

	shaderTexture*	crtMaskTexture;

	shaderTarget*	crtTarget;
	shaderTexture*	crtSourceTexture;
	shaderTexture*	crtProcessedTexture[ 2 ];
	int				crtProcessedTextureIndex = 0;

	shaderTarget*	crtBloomCalcTarget;
	shaderTexture*	crtBloomCalcTexture[ 2 ];
	int				crtBloomCalcTextureIndex = 0;

	shaderTexture*	glassTexture;
	shaderTexture*	webcamTextureNV12_Y = nullptr;
	shaderTexture*	webcamTextureNV12_UV = nullptr;

	shaderTarget*	crtTargetProcessed;

	shaderTarget*	overlayTarget;
	shaderTexture*	overlayTexture;
	shaderTexture*	overlayLUT_dusk;
	shaderTexture*	overlayLUT_night;

	shaderTarget*	bezelTarget;
	shaderTexture*	bezelTexture;
	juce::Rectangle<float>	bezelBounds;

	shaderTarget*	lightTarget;
	shaderTexture*	lightTexture;
	juce::Rectangle<float>	lightBounds;

	shaderTarget*	overlayDustTarget;
	shaderTexture*	overlayDustTexture;

	//
	// Webcam stuff
	//
	[[ nodiscard ]] bool isWebcamNeeded () const;
	void addWebcamListener ();
	void removeWebcamListener ();

	// Multipliers
	float	mulDaytime = 1.0f;
	float	mulBezel = 1.0f;
	float	mulShadow = 1.0f;
	float	mulReflection = 1.0f;
	float	mulDust = 1.0f;
	float	mulBloom = 1.0f;
	float	mulLightBloom = 1.0f;
	float	mulGrain = 1.0f;

	// juce::Thread (used for webcam)
	void run () override;

	bool	isCamInUse = false;
	std::unique_ptr<Webcam>		camera;
	std::atomic<pixFmt>			camPixFmt = pixFmt ( NV12 | matrixBT601 | rangeLimited );

	//
	// Root directory for CRT emulation resources (overlays, CRT masks, etc.)
	//
	juce::File			root;

	//
	// Overlays
	//
	juce::File			rootOverlays;
	juce::StringArray	overlayProfiles;

	juce::String		ovlyProfileName = "";

	juce::Point<float>	ovlyCenter {};
	float				ovlyWidth {};
	float				ovlyHeight {};
	float				ovlyScreenZoom { 1.0f };
	juce::Point<float>	ovlyScreenShift {};

	//
	// CRT Masks
	//
	juce::File			rootCRTMasks;
	juce::StringArray	crtMasks;

	//
	// Dust particles
	//
	shaderTarget*		dustTarget;

	//
	// Helpers
	//
	[[ nodiscard ]] bool parseOverlayProfile ( const juce::String& name );
	[[ nodiscard ]] juce::Rectangle<int> loadPartialTexture ( lime::shaderTexture* dst, const juce::File& root, const int expansion = 0 );
	[[ nodiscard ]] static juce::Rectangle<int> getCropBounds ( juce::Image& img );
	[[ nodiscard ]] static juce::Rectangle<int> getHoleBounds ( juce::Image& img );
	[[ nodiscard ]] static juce::Rectangle<float> expandHoleBounds ( const juce::Rectangle<int>& hole, const float targetRatio, const float expansionPixels );

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR ( CRTEmulation )
};
//-----------------------------------------------------------------------------
}
