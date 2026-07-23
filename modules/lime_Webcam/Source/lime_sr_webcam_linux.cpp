#if defined (LINUX) || defined (__linux__)

#include "lime_sr_webcam_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <math.h>

typedef struct {
	void* start;
	long length;
} _sr_webcam_buffer;

typedef struct {
	int fid;
	sr_webcam_device* parent;
	int width;
	int height;
	int strideY;		// driver row stride in bytes (>= width for padded rows)
	int id;
	int framerate;
	__u32 pixelformat;
	pixFmt colorTags;	// negotiated matrix/range bits, OR'd into each frame
	_sr_webcam_buffer* buffers;
	int buffersCount;
	pthread_t thread;
	volatile int shouldStop;	// polled by the capture loop; stop() sets it and JOINS the thread
} _sr_webcam_v4lInfos;

// Map the V4L2 colour-space tags negotiated on the format struct to our packed
// pixFmt matrix/range bits. Matches the Windows/macOS rule: an absent or
// unrecognised OS tag leaves the corresponding bits unset (= unknown), and we
// never infer matrix or range from resolution.
static pixFmt _sr_webcam_color_tags ( __u32 colorspace, __u32 ycbcr_enc, __u32 quantization )
{
	int tags = 0;

	// Matrix: from ycbcr_enc; if DEFAULT, derive from colorspace, but only when
	// colorspace itself is a real value (else leave the matrix unknown).
	__u32 enc = ycbcr_enc;
	if ( enc == V4L2_YCBCR_ENC_DEFAULT
		 && colorspace != V4L2_COLORSPACE_DEFAULT
		 && colorspace != V4L2_COLORSPACE_RAW )
		enc = V4L2_MAP_YCBCR_ENC_DEFAULT ( colorspace );

	if ( enc == V4L2_YCBCR_ENC_601 || enc == V4L2_YCBCR_ENC_XV601 )
		tags |= matrixBT601;
	else if ( enc == V4L2_YCBCR_ENC_709 || enc == V4L2_YCBCR_ENC_XV709 )
		tags |= matrixBT709;
	// else SMPTE240M / BT2020 / SYCC / unresolved DEFAULT -> matrix unknown.

	// Range: from quantization. A DEFAULT value's meaning depends on colorspace
	// and format; our format is always YCbCr (NV12/YUYV) so is_rgb_or_hsv=false,
	// and the kernel rule reduces to "JPEG colorspace -> full, else limited".
	// We only resolve DEFAULT when colorspace is a real value; if colorspace is
	// itself DEFAULT/RAW the range is genuinely unresolvable -> leave unset.
	__u32 quant = quantization;
	if ( quant == V4L2_QUANTIZATION_DEFAULT
		 && colorspace != V4L2_COLORSPACE_DEFAULT
		 && colorspace != V4L2_COLORSPACE_RAW )
		quant = V4L2_MAP_QUANTIZATION_DEFAULT ( false, colorspace, enc );

	if ( quant == V4L2_QUANTIZATION_FULL_RANGE )
		tags |= rangeFull;
	else if ( quant == V4L2_QUANTIZATION_LIM_RANGE )
		tags |= rangeLimited;
	// else unresolvable DEFAULT -> range unknown.

	return pixFmt ( tags );
}

int _sr_webcam_wait_ioctl(int fid, int request, void* arg) {
	int r;
	do {
		r = ioctl(fid, request, arg);
	} while(r == -1 && EINTR == errno);
	return r;
}

void* _sr_webcam_callback_loop ( void* arg )
{
	_sr_webcam_v4lInfos* stream = (_sr_webcam_v4lInfos*)arg;

	while ( ! stream->shouldStop )
	{
		fd_set	fds;
		FD_ZERO ( &fds );
		FD_SET ( stream->fid, &fds );

		// Short timeout so sr_webcam_stop can join promptly
		struct	timeval tv { .tv_sec = 0, .tv_usec = 200000 };

		int	res = select ( stream->fid + 1, &fds, NULL, NULL, &tv );
		if ( res == -1 && errno != EINTR )
			return NULL;

		// Timeout/interruption: nothing to dequeue, re-check the stop flag
		if ( res <= 0 )
			continue;

		struct	v4l2_buffer buf;
		memset ( &buf, 0, sizeof ( buf ) );
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;

		if ( _sr_webcam_wait_ioctl ( stream->fid, VIDIOC_DQBUF, &buf ) == -1 )
			if ( errno != EIO )
				return NULL;

		// The NV12 UV plane starts at strideY * height (rows may be padded)
		auto*	bufStart = (unsigned char*)stream->buffers[ buf.index ].start;
		auto*	uvStart = stream->pixelformat == V4L2_PIX_FMT_NV12 ? bufStart + stream->strideY * stream->height : nullptr;
		stream->parent->callback ( stream->parent, bufStart, uvStart, stream->width, stream->height, stream->strideY, stream->strideY, pixFmt ( ( stream->pixelformat == V4L2_PIX_FMT_NV12 ? NV12 : YUY2 ) | stream->colorTags ) );

		_sr_webcam_wait_ioctl ( stream->fid, VIDIOC_QBUF, &buf );
	}
	return NULL;
}

bool sr_webcam_open(sr_webcam_device* device)
{
	// Already setup.
	if(device->stream) {
		return false;
	}
	if(device->deviceId < 0) {
		return false;
	}

	_sr_webcam_v4lInfos* stream = (_sr_webcam_v4lInfos*)malloc(sizeof(_sr_webcam_v4lInfos));
	memset(stream, 0, sizeof(_sr_webcam_v4lInfos));
	stream->parent = device;
	stream->fid	   = -1;
	// Scan for a valid video capture device.
	{
		int found = 0;
		int seen = 0;
		for(int i = 0; i < 64 && !found; ++i) {
			char file[256];
			snprintf(file, 255, "/dev/video%d", i);
			int fid = open(file, O_RDWR | O_NONBLOCK, 0);
			if(fid < 0)
				continue;
			// Check if this device supports video capture.
			struct v4l2_capability probeCap;
			if(_sr_webcam_wait_ioctl(fid, VIDIOC_QUERYCAP, &probeCap) == 0
				&& (probeCap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
				&& (probeCap.capabilities & V4L2_CAP_STREAMING)) {
				if(seen == device->deviceId) {
					stream->id  = i;
					stream->fid = fid;
					found = 1;
				} else {
					seen++;
					close(fid);
				}
			} else {
				close(fid);
			}
		}
	}
	// Failed to find any device.
	if(stream->fid < 0) {
		free(stream);
		return false;
	}
	int fid = stream->fid;

	// Configure the device.
	struct v4l2_capability cap;
	// If we can't query the device capabilities, or it doesn't support video streaming, skip.
	if(_sr_webcam_wait_ioctl(fid, VIDIOC_QUERYCAP, &cap) == -1 || !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
		close(fid);
		free(stream);
		return false;
	}

	// Select output crop.
	struct v4l2_cropcap cropCap;
	cropCap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	_sr_webcam_wait_ioctl(fid, VIDIOC_CROPCAP, &cropCap);
	struct v4l2_crop crop;
	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	crop.c	  = cropCap.defrect; // Default rectangle.
	_sr_webcam_wait_ioctl(fid, VIDIOC_S_CROP, &crop);

	// Find the highest resolution that supports >= 30fps.
	// Prefer NV12 (zero-copy), fall back to YUYV (cheap conversion).
	__u32 preferredFormats[] = { V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_YUYV };
	__u32 bestPixFmt = 0;
	int bestWidth = 0, bestHeight = 0;

	for(int fi = 0; fi < 2; ++fi) {
		__u32 pixFmt = preferredFormats[fi];
		struct v4l2_frmsizeenum frmsize;
		memset(&frmsize, 0, sizeof(frmsize));
		frmsize.pixel_format = pixFmt;
		frmsize.index = 0;

		while(_sr_webcam_wait_ioctl(fid, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
			if(frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
				int w = (int)frmsize.discrete.width;
				int h = (int)frmsize.discrete.height;

				// Check if this size supports >= 30fps.
				struct v4l2_frmivalenum frmival;
				memset(&frmival, 0, sizeof(frmival));
				frmival.pixel_format = pixFmt;
				frmival.width  = frmsize.discrete.width;
				frmival.height = frmsize.discrete.height;
				frmival.index  = 0;

				while(_sr_webcam_wait_ioctl(fid, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) == 0) {
					int fps = 0;
					if(frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE && frmival.discrete.numerator > 0)
						fps = (int)(frmival.discrete.denominator / frmival.discrete.numerator);

					if(fps >= 30 && w * h > bestWidth * bestHeight) {
						bestWidth  = w;
						bestHeight = h;
						bestPixFmt = pixFmt;
					}
					frmival.index++;
				}
			}
			frmsize.index++;
		}
		// If we found a good mode in the preferred format, stop looking.
		if(bestPixFmt == pixFmt)
			break;
	}

	struct v4l2_format fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if(bestPixFmt != 0) {
		fmt.fmt.pix.width  = bestWidth;
		fmt.fmt.pix.height = bestHeight;
		fmt.fmt.pix.pixelformat = bestPixFmt;
	} else {
		// Enumeration found nothing; try requesting what the caller asked for.
		fmt.fmt.pix.width  = device->width;
		fmt.fmt.pix.height = device->height;
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
	}

	if(_sr_webcam_wait_ioctl(fid, VIDIOC_S_FMT, &fmt) == -1 || (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_NV12 && fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV)) {
		close(fid);
		free(stream);
		return false;
	}
	stream->pixelformat = fmt.fmt.pix.pixelformat;
	// Capture the colour-space tags the driver negotiated (matrix + range). These
	// are fixed for the session, so read them once and OR them into every frame.
	stream->colorTags = _sr_webcam_color_tags(fmt.fmt.pix.colorspace, fmt.fmt.pix.ycbcr_enc, fmt.fmt.pix.quantization);
	// Update the size based on the format constraints
	stream->width  = fmt.fmt.pix.width;
	stream->height = fmt.fmt.pix.height;
	stream->strideY = (stream->pixelformat == V4L2_PIX_FMT_NV12 && fmt.fmt.pix.bytesperline > 0)
						? (int)fmt.fmt.pix.bytesperline
						: (int)fmt.fmt.pix.width;

	// Allocate buffers for video frames.
	struct v4l2_requestbuffers req;
	memset(&req, 0, sizeof(req));
	req.count  = 4;
	req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	// If we can't get at least two buffers, skip.
	if(_sr_webcam_wait_ioctl(fid, VIDIOC_REQBUFS, &req) == -1 || req.count < 2) {
		close(fid);
		free(stream);
		return false;
	}
	_sr_webcam_buffer* buffers = (_sr_webcam_buffer*)calloc(req.count, sizeof(_sr_webcam_buffer));
	if(!buffers) {
		close(fid);
		free(stream);
		return false;
	}
	stream->buffersCount = req.count;
	// Allocate the buffers.
	for(int bid = 0; bid < (int)(req.count); ++bid) {
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index  = bid;
		if(_sr_webcam_wait_ioctl(fid, VIDIOC_QUERYBUF, &buf) == -1) {
			for(int obid = 0; obid < bid; ++obid) {
				munmap(buffers[obid].start, buffers[obid].length);
			}
			free(buffers);
			close(fid);
			free(stream);
			return false;
		}
		buffers[bid].length = buf.length;
		buffers[bid].start	= mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fid, buf.m.offset);
		if(buffers[bid].start == MAP_FAILED) {
			for(int obid = 0; obid < bid; ++obid) {
				munmap(buffers[obid].start, buffers[obid].length);
			}
			free(buffers);
			close(fid);
			free(stream);
			return false;
		}
	}
	stream->buffers = buffers;

	// Try to set the framerate.
	struct v4l2_streamparm fpsParams;
	memset(&fpsParams, 0, sizeof(fpsParams));
	fpsParams.type									= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fpsParams.parm.capture.timeperframe.numerator	= 1;
	fpsParams.parm.capture.timeperframe.denominator = (unsigned int)(device->framerate);
	_sr_webcam_wait_ioctl(fid, VIDIOC_S_PARM, &fpsParams);
	_sr_webcam_wait_ioctl(fid, VIDIOC_G_PARM, &fpsParams);
	stream->framerate = (int)fpsParams.parm.capture.timeperframe.denominator;

	// Update the device infos.
	device->stream	  = stream;
	device->width	  = stream->width;
	device->height	  = stream->height;
	device->deviceId  = stream->id;
	device->framerate = stream->framerate;

	return true;
}

void sr_webcam_start(sr_webcam_device* device) {
	if(device->stream && device->running == 0) {
		_sr_webcam_v4lInfos* stream = (_sr_webcam_v4lInfos*)(device->stream);
		// Prepare all buffers.
		for(int bid = 0; bid < stream->buffersCount; ++bid) {
			struct v4l2_buffer buf;
			memset(&buf, 0, sizeof(buf));
			buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index  = bid;
			if(_sr_webcam_wait_ioctl(stream->fid, VIDIOC_QBUF, &buf) == -1) {
				return;
			}
		}
		enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if(_sr_webcam_wait_ioctl(stream->fid, VIDIOC_STREAMON, &type) == -1) {
			return;
		}
		stream->shouldStop = 0;
		pthread_create(&stream->thread, NULL, &_sr_webcam_callback_loop, device->stream);
		device->running = 1;
	}
}

void sr_webcam_stop(sr_webcam_device* device) {
	if(device->stream && device->running == 1) {
		_sr_webcam_v4lInfos* stream = (_sr_webcam_v4lInfos*)(device->stream);
		enum v4l2_buf_type type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
		_sr_webcam_wait_ioctl(stream->fid, VIDIOC_STREAMOFF, &type);
		// Teardown unmaps the buffers the loop reads, join before anything else
		stream->shouldStop = 1;
		pthread_join(stream->thread, NULL);
		device->running = 0;
	}
}

void sr_webcam_delete(sr_webcam_device* device) {
	if(device->running == 1) {
		sr_webcam_stop(device);
	}
	if(device->stream) {
		// Unmap and delete all buffers.
		_sr_webcam_v4lInfos* stream = (_sr_webcam_v4lInfos*)(device->stream);
		_sr_webcam_buffer* buffers	= stream->buffers;
		for(int bid = 0; bid < stream->buffersCount; ++bid) {
			munmap(buffers[bid].start, buffers[bid].length);
		}
		free(buffers);
		close(stream->fid);
		free(stream);
		device->stream = NULL;
	}
	delete device;
}

#endif
