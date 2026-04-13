#include "lime_sr_webcam_internal.h"

#include <memory.h>

//-----------------------------------------------------------------------------

bool sr_webcam_create ( sr_webcam_device** device, int deviceId )
{
    auto	pdev = new sr_webcam_device ();
	if ( ! pdev )
		return false;

	pdev->deviceId = deviceId;
	*device = pdev;

	return true;
}
//-----------------------------------------------------------------------------

void sr_webcam_set_format ( sr_webcam_device* device, int width, int height, int framerate )
{
	device->width = width;
	device->height = height;
	device->framerate = framerate;
}
//-----------------------------------------------------------------------------

void sr_webcam_set_callback ( sr_webcam_device* device, sr_webcam_callback callback )
{
	device->callback = callback;
}
//-----------------------------------------------------------------------------

void sr_webcam_set_user ( sr_webcam_device* device, void* user )
{
	device->user = user;
}
//-----------------------------------------------------------------------------

void sr_webcam_get_dimensions ( sr_webcam_device* device, int* width, int* height )
{
	*width = device->width;
	*height = device->height;
}
//-----------------------------------------------------------------------------

void sr_webcam_get_framerate ( sr_webcam_device* device, int* fps )
{
	*fps = device->framerate;
}
//-----------------------------------------------------------------------------

void* sr_webcam_get_user ( sr_webcam_device* device )
{
	return device->user;
}
//-----------------------------------------------------------------------------
