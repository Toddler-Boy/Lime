#include "lime_Webcam.h"

namespace lime
{
//-----------------------------------------------------------------------------

Webcam::Webcam ( int w, int h, int fps )
{
	sr_webcam_create ( &device, 0 );
	sr_webcam_set_format ( device, w, h, fps );
	sr_webcam_set_callback ( device, &Webcam::callback );
	sr_webcam_set_user ( device, this );

	if ( ! sr_webcam_open ( device ) )
		error = "Unable to open device";
}
//-----------------------------------------------------------------------------

Webcam::~Webcam ()
{
	sr_webcam_delete ( device );
}
//-----------------------------------------------------------------------------

void Webcam::start ()
{
	sr_webcam_start ( device );
}
//-----------------------------------------------------------------------------

void Webcam::stop ()
{
	sr_webcam_stop ( device );
}
//-----------------------------------------------------------------------------

void Webcam::callback ( sr_webcam_device* device, void* dataY, void* dataUV, int width, int height, int strideY, int strideUV, pixFmt format )
{
	if ( auto stream = static_cast<Webcam*>( sr_webcam_get_user ( device ) ); stream->onDataReceived )
		stream->onDataReceived ( (uint8_t*)dataY, (uint8_t*)dataUV, width, height, strideY, strideUV, format );
}
//-----------------------------------------------------------------------------

} // namespace lime
