#if defined (_WIN32) || defined (_WIN64)

#include "lime_sr_webcam_internal.h"

#include <windows.h>
#include <mfapi.h>

#include <mfplay.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <cmath>
#include <shlwapi.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

struct IMFMediaType;
struct IMFActivate;
struct IMFMediaSource;
struct IMFAttributes;

//-----------------------------------------------------------------------------

class SRWebcamMFContext final
{
public:
	static SRWebcamMFContext& getContext ()
	{
		static SRWebcamMFContext instance;
		return instance;
	}

	~SRWebcamMFContext ()
	{
		CoUninitialize ();
	}

private:
	SRWebcamMFContext ()
	{
		CoInitialize ( 0 );
		SUCCEEDED ( MFStartup ( MF_VERSION ) );
	}
};
//-----------------------------------------------------------------------------

struct SRWebcamFormat final
{
	SRWebcamFormat () = default;

	SRWebcamFormat ( IMFMediaType* pType )
	{
		// Extract the properties we need
		UINT32	count = 0;
		if ( SUCCEEDED ( pType->GetCount ( &count ) ) && SUCCEEDED ( pType->LockStore () ) )
		{
			for ( UINT32 i = 0; i < count; i++ )
			{
				// Value of the property
				PROPVARIANT	var = {};
				GUID		guid = {};

				if ( FAILED ( pType->GetItemByIndex ( i, &guid, &var ) ) )
					continue;

				// Extract the properties we need
				if ( guid == MF_MT_FRAME_RATE && var.vt == VT_UI8 )
				{
					// Framerate
					UINT32	frameRateNum = 0;
					UINT32	frameRateDenom = 0;

					Unpack2UINT32AsUINT64 ( var.uhVal.QuadPart, &frameRateNum, &frameRateDenom );

					if ( frameRateDenom )
						framerate = double ( frameRateNum ) / double ( frameRateDenom );
				}
				else if ( guid == MF_MT_MAJOR_TYPE && var.vt == VT_CLSID )
				{
					// Type
					type = *var.puuid;
				}
				else if ( guid == MF_MT_FRAME_SIZE && var.vt == VT_UI8 )
				{
					// Width and height
					Unpack2UINT32AsUINT64 ( var.uhVal.QuadPart, &width, &height );
				}

				PropVariantClear ( &var );
			}
			pType->UnlockStore ();
		}
	}

	UINT32	width = 0;
	UINT32	height = 0;
	double	framerate = 0.0;
	GUID	type = {};
};
//-----------------------------------------------------------------------------

template <class T> void SafeRelease ( T** ppT )
{
	if ( *ppT )
	{
		( *ppT )->Release ();
		*ppT = NULL;
	}
}
//-----------------------------------------------------------------------------

class SRWebcamVideoStreamMF final : public IMFSourceReaderCallback
{
public:
	//-----------------------------------------------------------------------------

	STDMETHODIMP QueryInterface ( REFIID riid, _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject ) override
	{
		static const QITAB qit[] =
		{
			QITABENT ( SRWebcamVideoStreamMF, IMFSourceReaderCallback ),
			{ 0 },
		};
		return QISearch ( this, qit, riid, ppvObject );
	}
	//-----------------------------------------------------------------------------

	STDMETHODIMP_ ( ULONG )	AddRef () override
	{
		return InterlockedIncrement ( &refCount );
	}
	//-----------------------------------------------------------------------------

	STDMETHODIMP_ ( ULONG )	Release () override
	{
		ULONG uCount = InterlockedDecrement ( &refCount );

		if ( uCount == 0 )
			delete this;

		return uCount;
	}
	//-----------------------------------------------------------------------------

	STDMETHODIMP OnEvent ( DWORD, IMFMediaEvent* ) override
	{
		return S_OK;
	}
	//-----------------------------------------------------------------------------

	STDMETHODIMP OnFlush ( DWORD ) override
	{
		// Runs on the MF work-queue thread; stop() waits for this before the
		// reader may be released (MF's async source-reader contract)
		{
			const std::lock_guard	sl ( flushMutex );
			flushDone = true;
		}
		flushCV.notify_all ();

		return S_OK;
	}
	//-----------------------------------------------------------------------------

	STDMETHODIMP OnReadSample ( HRESULT hrStatus, DWORD dwStreamIndex, DWORD /*dwStreamFlags*/, LONGLONG /*llTimestamp*/, IMFSample* pSample ) override
	{
		if ( !_parent || FAILED ( hrStatus ) )
			return S_OK;

		// Extract data and pass it
		if ( pSample )
		{
			IMFMediaBuffer*	buffer = nullptr;

			// Generate data buffer from sample
			if ( SUCCEEDED ( pSample->GetBufferByIndex ( 0, &buffer ) ) )
			{
				BYTE*			ptr = nullptr;
				LONG			pitchY = 0;
				IMF2DBuffer2*	buffer2_2d = nullptr;
				BYTE*			bufferStart = nullptr;
				DWORD			bufferLength = 0;

				// Lock 2D buffer
				if (	SUCCEEDED ( buffer->QueryInterface ( __uuidof( IMF2DBuffer2 ), reinterpret_cast<void**>( &buffer2_2d ) ) )
					 && SUCCEEDED ( buffer2_2d->Lock2DSize ( MF2DBuffer_LockFlags_Read, &ptr, &pitchY, &bufferStart, &bufferLength ) )
					)
				{
					// Transmit data to user (pixel format OR'd with the negotiated colour-space tags)
					_parent->callback ( _parent, ptr, ptr + pitchY * captureFormat.height, captureFormat.width, captureFormat.height, pitchY, pitchY, pixFmt ( NV12 | colorTags ) );

					// Unlock 2D buffer
					buffer2_2d->Unlock2D ();
				}

				SafeRelease ( &buffer2_2d );
				SafeRelease ( &buffer );
			}
		}

		// Schedule next sample (the reader can be gone on error/teardown paths)
		if ( videoReader )
			videoReader->ReadSample ( dwStreamIndex, 0, NULL, NULL, NULL, NULL );

		return S_OK;
	}
	//-----------------------------------------------------------------------------

	bool setupWith ( int id, int framerate, int w, int h )
	{
		// All the COM objects used during setup, released on EVERY exit path.
		// The reader keeps its own references to what it needs (media source,
		// callback), so releasing these after creation is correct
		IMFAttributes*	msAttr = nullptr;
		IMFMediaSource*	mSrc = nullptr;
		IMFAttributes*	srAttr = nullptr;
		IMFMediaType*	typeOut = nullptr;

		const auto	releaseLocals = [ & ] () noexcept
		{
			SafeRelease ( &typeOut );
			SafeRelease ( &srAttr );
			SafeRelease ( &mSrc );
			SafeRelease ( &msAttr );
		};

		// Prepare video devices query
		if ( FAILED ( MFCreateAttributes ( &msAttr, 1 ) ) )
			return false;

		if ( FAILED ( msAttr->SetGUID ( MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID ) ) )
		{
			releaseLocals ();
			return false;
		}

		IMFActivate**	ppDevices = nullptr;
		UINT32			count = 0;

		// Enumerate devices
		if ( FAILED ( MFEnumDeviceSources ( msAttr, &ppDevices, &count ) ) || count == 0 || id < 0 )
		{
			CoTaskMemFree ( ppDevices );
			releaseLocals ();
			return false;
		}

		_id = min ( int ( count ) - 1, id );

		// Release all the others
		for ( auto i = 0; i < int ( count ); ++i )
		{
			if ( i == _id || ! ppDevices[ i ] )
				continue;

			ppDevices[ i ]->Release ();
		}

		// If the device is null, not available
		if ( ! ppDevices[ _id ] )
		{
			CoTaskMemFree ( ppDevices );
			releaseLocals ();
			return false;
		}

		// Get name of device
		{
			auto getDeviceString = [] ( IMFActivate* dev, REFGUID guid ) -> std::string
			{
				WCHAR* szFriendlyName = nullptr;
				UINT32	szFriendlyLength = 0;

				if ( FAILED ( dev->GetAllocatedString ( guid, &szFriendlyName, &szFriendlyLength ) ) )
					return {};

				auto	sizeNeeded = WideCharToMultiByte ( CP_UTF8, 0, szFriendlyName, szFriendlyLength, nullptr, 0, nullptr, nullptr );
				auto	res = std::string ( sizeNeeded, 0 );
				WideCharToMultiByte ( CP_UTF8, 0, szFriendlyName, szFriendlyLength, &res[ 0 ], sizeNeeded, nullptr, nullptr );

				return res;
			};

			friendlyName = getDeviceString ( ppDevices[ _id ], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME );
		}

		// Set source reader parameters
		if ( FAILED ( ppDevices[ _id ]->ActivateObject ( __uuidof( IMFMediaSource ), (void**)&mSrc ) ) || ! mSrc )
		{
			ppDevices[ _id ]->Release ();
			CoTaskMemFree ( ppDevices );
			releaseLocals ();
			return false;
		}

		// Create attributes
		if ( FAILED ( MFCreateAttributes ( &srAttr, 6 ) ) )
		{
			ppDevices[ _id ]->Release ();
			CoTaskMemFree ( ppDevices );
			releaseLocals ();
			return false;
		}

		srAttr->SetUINT32 ( MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE );
		srAttr->SetUINT32 ( MF_SOURCE_READER_DISABLE_DXVA, FALSE );
		srAttr->SetUINT32 ( MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, FALSE );
		srAttr->SetUINT32 ( MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, FALSE );
		srAttr->SetUINT32 ( MF_LOW_LATENCY, TRUE );

		// Define callback
		if ( FAILED ( srAttr->SetUnknown ( MF_SOURCE_READER_ASYNC_CALLBACK, (IMFSourceReaderCallback*)this ) ) )
		{
			ppDevices[ _id ]->Release ();
			CoTaskMemFree ( ppDevices );
			releaseLocals ();
			return false;
		}

		// Create the reader from the attributes and device
		if ( FAILED ( MFCreateSourceReaderFromMediaSource ( mSrc, srAttr, &videoReader ) ) )
		{
			ppDevices[ _id ]->Release ();
			CoTaskMemFree ( ppDevices );
			releaseLocals ();
			return false;
		}

		// Iterate over streams and media types to find the best fit
		HRESULT			hr = S_OK;
		int				bestStream = -1;
		SRWebcamFormat	bestFormat;
		float			bestFit = 1e9;
		DWORD			streamId = 0;
		DWORD			typeId = 0;

		while ( SUCCEEDED ( hr ) )
		{
			IMFMediaType*	pType = nullptr;
			hr = videoReader->GetNativeMediaType ( streamId, typeId, &pType );

			// If we reached the end of format types for this stream, move to the next
			if ( hr == MF_E_NO_MORE_TYPES )
			{
				hr = S_OK;
				++streamId;
				typeId = 0;
				continue;
			}

			// If hr is neither no_more_types nor success, we reached the end of the streams list
			if ( FAILED ( hr ) )
				continue;

			SRWebcamFormat	format ( pType );
			SafeRelease ( &pType );	// one native type per iteration, release each

			// We only care about video types
			if ( format.type != MFMediaType_Video )
			{
				++typeId;
				continue;
			}

			// Init with the first available video format
			if ( bestStream < 0 )
			{
				const auto	dw = float ( w - format.width );
				const auto	dh = float ( h - format.height );
				bestFit = sqrtf ( dw * dw + dh * dh );
				bestStream = int ( streamId );
				bestFormat = format;
				++typeId;
				continue;
			}

			// If the current best already has the same size, replace it only if the framerate is closer to the requested one
			if ( format.width == bestFormat.width && format.height == bestFormat.height )
			{
				if ( abs ( framerate - format.framerate ) < abs ( framerate - bestFormat.framerate ) )
				{
					bestStream = int ( streamId );
					bestFormat = format;
				}
				++typeId;
				continue;
			}
			// Else, replace the format if its size is closer to the required one
			const auto	dw = float ( w - format.width );
			const auto	dh = float ( h - format.height );
			const auto	fit = sqrtf ( dw * dw + dh * dh );
			if ( fit < bestFit )
			{
				bestFit = fit;
				bestStream = int ( streamId );
				bestFormat = format;
			}
			++typeId;
		}

		// If we didn't find anything, fail
		if ( bestStream < 0 )
		{
			ppDevices[ _id ]->Release ();
			CoTaskMemFree ( ppDevices );
			releaseLocals ();
			return false;
		}

		// We found the best available stream and format, configure the output format
		if ( FAILED ( MFCreateMediaType ( &typeOut ) ) )
		{
			ppDevices[ _id ]->Release ();
			CoTaskMemFree ( ppDevices );
			releaseLocals ();
			return false;
		}

		// Tell Windows which output format we want
		typeOut->SetGUID ( MF_MT_MAJOR_TYPE, MFMediaType_Video );

		// NV12 seems to be the fastest and the preferred format for Windows video
		typeOut->SetGUID ( MF_MT_SUBTYPE, MFVideoFormat_NV12 );

		typeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		MFSetAttributeRatio ( typeOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1 );

		MFSetAttributeSize ( typeOut, MF_MT_FRAME_SIZE, bestFormat.width, bestFormat.height );
		MFSetAttributeRatio ( typeOut, MF_MT_FRAME_RATE, min ( framerate, int ( bestFormat.framerate ) ), 1 );

		// Set the selected stream and the output format
		if (	FAILED ( videoReader->SetStreamSelection ( (DWORD)MF_SOURCE_READER_ALL_STREAMS, false ) )
			 || FAILED ( videoReader->SetStreamSelection ( (DWORD)bestStream, true ) )
			 || FAILED ( videoReader->SetCurrentMediaType ( (DWORD)bestStream, NULL, typeOut ) )
			 )
		{
			SafeRelease ( &videoReader );
			ppDevices[ _id ]->Release ();
			CoTaskMemFree ( ppDevices );
			releaseLocals ();
			return false;
		}

		// Read the colour-space tags the driver actually negotiated (matrix + range).
		// MF carries these on the media type, not per-sample, so we read them once
		// here and OR them into every frame. Absent/unrecognised tags leave the
		// corresponding bits unset (= unknown). We do not infer from resolution.
		colorTags = pixFmt ( 0 );
		IMFMediaType*	actualType = NULL;
		if ( SUCCEEDED ( videoReader->GetCurrentMediaType ( (DWORD)bestStream, &actualType ) ) && actualType )
		{
			UINT32	matrix = 0;
			if ( SUCCEEDED ( actualType->GetUINT32 ( MF_MT_YUV_MATRIX, &matrix ) ) )
			{
				if ( matrix == MFVideoTransferMatrix_BT709 )
					colorTags = pixFmt ( colorTags | matrixBT709 );
				else if ( matrix == MFVideoTransferMatrix_BT601 )
					colorTags = pixFmt ( colorTags | matrixBT601 );
			}

			UINT32	range = 0;
			if ( SUCCEEDED ( actualType->GetUINT32 ( MF_MT_VIDEO_NOMINAL_RANGE, &range ) ) )
			{
				if ( range == MFNominalRange_0_255 )
					colorTags = pixFmt ( colorTags | rangeFull );
				else if ( range == MFNominalRange_16_235 )
					colorTags = pixFmt ( colorTags | rangeLimited );
			}

			SafeRelease ( &actualType );
		}

		// Store infos for callback
		selectedStream = (DWORD)bestStream;
		captureFormat = SRWebcamFormat ( typeOut );
		ppDevices[ _id ]->Release ();
		CoTaskMemFree ( ppDevices );
		releaseLocals ();

		return true;
	}
	//-----------------------------------------------------------------------------

	void start ()
	{
		// Schedule first sample
		if ( videoReader && SUCCEEDED ( videoReader->ReadSample ( selectedStream, 0, NULL, NULL, NULL, NULL ) ) )
			return;

		removeReader ();
	}
	//-----------------------------------------------------------------------------

	void stop ()
	{
		if ( ! videoReader )
			return;

		{
			const std::lock_guard	sl ( flushMutex );
			flushDone = false;
		}

		if ( SUCCEEDED ( videoReader->Flush ( selectedStream ) ) )
		{
			std::unique_lock	lk ( flushMutex );
			flushCV.wait_for ( lk, std::chrono::seconds ( 2 ), [ this ] { return flushDone; } );

			return;
		}

		removeReader ();
	}
	//-----------------------------------------------------------------------------

	void removeReader ()
	{
		SafeRelease ( &videoReader );
	}
	//-----------------------------------------------------------------------------

public:
	sr_webcam_device*	_parent = NULL;
	int					_id = -1;
	SRWebcamFormat		captureFormat;
	pixFmt				colorTags = pixFmt ( 0 );	// negotiated matrix/range bits, OR'd into each frame
	std::string			friendlyName;

private:
	SRWebcamMFContext&	context = SRWebcamMFContext::getContext ();
	IMFSourceReader*	videoReader = nullptr;
	DWORD				selectedStream = 0;

	// The creating sr_webcam_open holds the initial reference; MF's refs come
	// and go on top of it, sr_webcam_delete releases it (COM delete-on-zero)
	long				refCount = 1;

	// OnFlush handshake, see stop()
	std::mutex				flushMutex;
	std::condition_variable	flushCV;
	bool					flushDone = false;
};
//-----------------------------------------------------------------------------

bool sr_webcam_open ( sr_webcam_device* device )
{
	// Already setup
	if ( device->stream )
		return false;

	auto	stream = new SRWebcamVideoStreamMF;
	stream->_parent = device;
	if ( auto res = stream->setupWith ( device->deviceId, device->framerate, device->width, device->height ); ! res )
	{
		device->stream = nullptr;
		stream->Release ();	// drop the creation reference, frees the stream
		return false;
	}

	device->stream = stream;
	device->friendlyName = stream->friendlyName;
	device->width = stream->captureFormat.width;
	device->height = stream->captureFormat.height;
	device->framerate = int ( stream->captureFormat.framerate );
	device->deviceId = stream->_id;

	return true;
}
//-----------------------------------------------------------------------------

void sr_webcam_start ( sr_webcam_device* device )
{
	if ( ! device->stream || device->running == 1 )
		return;

	auto	stream = (SRWebcamVideoStreamMF*)( device->stream );
	stream->start ();
	device->running = 1;
}
//-----------------------------------------------------------------------------

void sr_webcam_stop ( sr_webcam_device* device )
{
	if ( ! device->stream || device->running == 0 )
		return;

	auto	stream = (SRWebcamVideoStreamMF*)( device->stream );
	stream->stop ();
	device->running = 0;
}
//-----------------------------------------------------------------------------

void sr_webcam_delete ( sr_webcam_device* device )
{
	// stop() is synchronous (waits for OnFlush), so after it returns no
	// callback references `device` or the reader anymore
	sr_webcam_stop ( device );

	if ( auto stream = (SRWebcamVideoStreamMF*)( device->stream ) )
	{
		stream->removeReader ();
		stream->Release ();	// drop the creation reference, frees the stream
		device->stream = nullptr;
	}

	delete device;
}
//-----------------------------------------------------------------------------

#endif
