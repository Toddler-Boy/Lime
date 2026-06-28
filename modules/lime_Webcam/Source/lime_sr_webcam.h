#pragma once

struct _sr_webcam_device;
using sr_webcam_device = struct _sr_webcam_device;

// Packed colour-space bitfield. Three independent axes occupy non-overlapping
// bit ranges, so they can be OR'd together and masked apart. The two "unknown"
// states (matrix, range) are the absence of the relevant bits, not values.
enum pixFmt : int
{
	// ---- Pixel-format axis (bits 0-1). Exactly one bit is set. ----
	// NB: these were the values 0 and 1; they are now DISTINCT BITS, so a bare
	// 0 is no longer a valid format and "format == NV12/YUY2" no longer works.
	NV12            = 1 << 0,                    // 0x01
	YUY2            = 1 << 1,                    // 0x02

	// ---- Colour-matrix axis (bits 2-3). Neither set = unknown. ----
	matrixBT601     = 1 << 2,                    // 0x04
	matrixBT709     = 1 << 3,                    // 0x08

	// ---- Range axis (bits 4-5). Neither set = unknown. ----
	rangeLimited    = 1 << 4,                    // 0x10
	rangeFull       = 1 << 5,                    // 0x20

	// ---- Masks to extract a single axis cleanly. ----
	maskPixelFormat = NV12 | YUY2,               // 0x03
	maskMatrix      = matrixBT601 | matrixBT709, // 0x0C
	maskRange       = rangeLimited | rangeFull,  // 0x30
};

using sr_webcam_callback = void ( * )( sr_webcam_device* device, void* dataY, void* dataUV, int width, int height, int strideY, int strideUV, pixFmt format );

bool sr_webcam_create ( sr_webcam_device** device, int deviceId );

void sr_webcam_set_format ( sr_webcam_device* device, int width, int height, int framerate );
void sr_webcam_set_callback ( sr_webcam_device* device, sr_webcam_callback callback );
void sr_webcam_set_user ( sr_webcam_device* device, void* user );
void* sr_webcam_get_user ( sr_webcam_device* device );
void sr_webcam_get_dimensions ( sr_webcam_device* device, int* width, int* height );
void sr_webcam_get_framerate ( sr_webcam_device* device, int* fps );

bool sr_webcam_open ( sr_webcam_device* device );

void sr_webcam_start ( sr_webcam_device* device );
void sr_webcam_stop ( sr_webcam_device* device );
void sr_webcam_delete ( sr_webcam_device* device );
