#pragma once

#include "camera.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             

#include <fcntl.h>              
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string>

#define v4l2_close close
#define v4l2_ioctl ioctl
#define v4l2_open open
#define v4l2_mmap mmap
#define v4l2_munmap munmap

#define CLEAR(x) memset(&(x), 0, sizeof(x))

struct buffer
{
	void* start;
	size_t length;
};

static void xioctl(int fh, int request, void* arg)
{
	int r;

	do
	{
		r = v4l2_ioctl(fh, request, arg);
	} while (r == -1 && ((errno == EINTR) || (errno == EAGAIN)));

	if (r == -1)
	{
		fprintf(stderr, "ioctl error %d, %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

class LinuxCamera : public ICamera
{
	//int n_buffers;

	struct v4l2_format              fmt;
	struct v4l2_buffer              buf;
	fd_set                          fds;
	struct timeval                  tv;
	int                             r = -1;
	unsigned int                    i, n_buffers;

public:

	LinuxCamera()
	{

	}

	~LinuxCamera()
	{
		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		xioctl(fd, VIDIOC_STREAMOFF, &type);
		for (i = 0; i < n_buffers; ++i)
		{
			v4l2_munmap(buffers[i].start, buffers[i].length);
		}
		v4l2_close(fd);
	}

	// returns false if device doesnt exist
	virtual bool Initialize(int device_id)
	{
		std::string dev_name = "/dev/video" + std::to_string(device_id);
		int fd = v4l2_open(dev_name.c_str(), O_RDWR | O_NONBLOCK, 0);
		if (fd < 0)
		{
			perror("Cannot open device");
			return false;
		}
		return true;
	}

	// returns false if format cannot be selected or device failed
	virtual bool SetFormat(ImageFormat format, int desired_width, int desired_height)
	{
		int desired_pixel_format = format == ImageFormat::MJPEG ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV;

		CLEAR(fmt);
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		fmt.fmt.pix.width = desired_height;
		fmt.fmt.pix.height = desired_width;
		fmt.fmt.pix.pixelformat = desired_pixel_format;
		fmt.fmt.pix.field = V4L2_FIELD_NONE;
		xioctl(fd, VIDIOC_S_FMT, &fmt);
		if (fmt.fmt.pix.pixelformat != desired_pixel_format)
		{
			printf("Libv4l didn't accept desired pixel format format. Can't proceed.\n");
			exit(EXIT_FAILURE);
		}
		if ((fmt.fmt.pix.width != desired_width) || (fmt.fmt.pix.height != desired_height))
		{
			printf("Warning: driver is sending image at %dx%d\n",
				fmt.fmt.pix.width, fmt.fmt.pix.height);
		}
	}

	// prepare to capture images
	virtual bool StartCapture()
	{
		struct v4l2_requestbuffers req;
		CLEAR(req);
		req.count = 2;
		req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		req.memory = V4L2_MEMORY_MMAP;
		xioctl(fd, VIDIOC_REQBUFS, &req);

		struct buffer* buffers = (buffer*)calloc(req.count, sizeof(*buffers));
		for (n_buffers = 0; n_buffers < req.count; ++n_buffers)
		{
			CLEAR(buf);

			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index = n_buffers;

			xioctl(fd, VIDIOC_QUERYBUF, &buf);

			buffers[n_buffers].length = buf.length;
			buffers[n_buffers].start = v4l2_mmap(NULL, buf.length,
				PROT_READ | PROT_WRITE, MAP_SHARED,
				fd, buf.m.offset);

			if (MAP_FAILED == buffers[n_buffers].start)
			{
				perror("mmap");
				exit(EXIT_FAILURE);
			}
		}

		for (i = 0; i < n_buffers; ++i)
		{
			CLEAR(buf);
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index = i;
			xioctl(fd, VIDIOC_QBUF, &buf);
		}
		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		xioctl(fd, VIDIOC_STREAMON, &type);
	}

	// note the image must be released before another can be grabbed, at most one can be out at a time
	virtual Image GrabImage(bool& failed)
	{
		failed = false;
		do
		{
			FD_ZERO(&fds);
			FD_SET(fd, &fds);

			// Timeout.
			tv.tv_sec = 2;
			tv.tv_usec = 0;

			r = select(fd + 1, &fds, NULL, NULL, &tv);
		} while ((r == -1 && (errno = EINTR)));
		if (r == -1)
		{
			failed = true;
			perror("select");
			Image img;
			img.data = 0;
			img.data_length = 0;
			return img;
		}

		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		xioctl(fd, VIDIOC_DQBUF, &buf);

		Image img;
		img.data = buffers[buf.index].start;
		img.data_length = buf.bytesused;
		img.height = ;
		img.width = ;
		return img;
	}

	virtual void ReleaseImage(Image img)
	{
		xioctl(fd, VIDIOC_QBUF, &buf);
	}

	// list supported image formats, used if we fail to find one or the user just wants to list them
	// todo have standard output of format + res rather than per platform
	virtual void EnumerateFormats()
	{
		struct v4l2_fmtdesc fmtdesc;
		memset(&fmtdesc, 0, sizeof(fmtdesc));
		fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0)
		{
			printf("%s\n", fmtdesc.description);

			struct v4l2_frmsizeenum fmtdesc2;
			memset(&fmtdesc2, 0, sizeof(fmtdesc2));
			fmtdesc2.pixel_format = V4L2_PIX_FMT_YUYV;
			while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fmtdesc2) == 0)
			{
				if (fmtdesc2.type == V4L2_FRMSIZE_TYPE_DISCRETE)
				{
					printf("  %ix%i\n", fmtdesc2.discrete.width, fmtdesc2.discrete.height);
				}
				fmtdesc2.index++;
			}

			fmtdesc.index++;
		}
	}
};