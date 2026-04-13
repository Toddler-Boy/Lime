#pragma once

#include "lime_sr_webcam.h"
#include <string>

struct _sr_webcam_device
{
	int		deviceId = 0;

	std::string		friendlyName;

	int		width = 0;
	int		height = 0;
	int		framerate = 0;
	int		running = 0;

	void*	stream = nullptr;
	sr_webcam_callback	callback;
	void*	user = nullptr;
};
