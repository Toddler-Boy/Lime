#pragma once

struct _sr_webcam_device;
using sr_webcam_device = struct _sr_webcam_device;

enum pixFmt : int
{
	NV12,
	YUY2,
};

using sr_webcam_callback = void ( * )( sr_webcam_device* device, void* data, int width, int height, int strideY, int strideUV, pixFmt format );

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
