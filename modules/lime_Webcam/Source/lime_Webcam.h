#pragma once

#include "lime_sr_webcam.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

//-----------------------------------------------------------------------------

namespace lime
{

class Webcam final
{
public:
	Webcam ( int w, int h, int fps, int deviceIndex = 0 );
	~Webcam ();

	// Capture-device names; their index is the ctor's deviceIndex
	[[ nodiscard ]] static std::vector<std::string> getDeviceNames ()	{	return sr_webcam_list_devices ();	}

	void start ();
	void stop ();

	std::string& getError () { return error; }

	std::function<void ( uint8_t* dataY, uint8_t* dataUV, int width, int height, int strideY, int strideUV, pixFmt format )>	onDataReceived;

private:
	static void callback ( sr_webcam_device* device, void* dataY, void* dataUV, int width, int height, int strideY, int strideUV, pixFmt format );

	sr_webcam_device*	device = nullptr;
	std::string			error;
};
//-----------------------------------------------------------------------------

} // namespace lime