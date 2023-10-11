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
	int current_width_, current_height_;
	struct v4l2_buffer current_buf_;
	struct buffer* buffers_;
	unsigned int n_buffers_;
	int fd_;

public:

	LinuxCamera()
	{

	}

	~LinuxCamera()
	{
		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		xioctl(fd_, VIDIOC_STREAMOFF, &type);
		for (int i = 0; i < n_buffers_; ++i)
		{
			v4l2_munmap(buffers_[i].start, buffers_[i].length);
		}
		v4l2_close(fd_);
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
		fd_ = fd;
		return true;
	}

	// returns false if format cannot be selected or device failed
	virtual bool SetFormat(ImageFormat format, int desired_width, int desired_height)
	{
		int desired_pixel_format = format == ImageFormat::MJPEG ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV;
		
		struct v4l2_format fmt;
		CLEAR(fmt);
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		fmt.fmt.pix.width = desired_width;
		fmt.fmt.pix.height = desired_height;
		fmt.fmt.pix.pixelformat = desired_pixel_format;
		fmt.fmt.pix.field = V4L2_FIELD_NONE;
		xioctl(fd_, VIDIOC_S_FMT, &fmt);
		if (fmt.fmt.pix.pixelformat != desired_pixel_format)
		{
			printf("Libv4l didn't accept desired pixel format format. Can't proceed.\n");
			return false;
		}
		if ((fmt.fmt.pix.width != desired_width) || (fmt.fmt.pix.height != desired_height))
		{
			printf("Warning: driver is sending image at %dx%d\n",
				fmt.fmt.pix.width, fmt.fmt.pix.height);
		}
		current_width_ = fmt.fmt.pix.width;
		current_height_ = fmt.fmt.pix.height;
		return true;
	}

	// prepare to capture images
	virtual bool StartCapture()
	{
		struct v4l2_requestbuffers req;
		CLEAR(req);
		req.count = 2;
		req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		req.memory = V4L2_MEMORY_MMAP;
		xioctl(fd_, VIDIOC_REQBUFS, &req);

		buffers_ = (buffer*)calloc(req.count, sizeof(*buffers_));
		for (n_buffers_ = 0; n_buffers_ < req.count; ++n_buffers_)
		{
			struct v4l2_buffer buf;
			CLEAR(buf);

			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index = n_buffers_;

			xioctl(fd_, VIDIOC_QUERYBUF, &buf);

			buffers_[n_buffers_].length = buf.length;
			buffers_[n_buffers_].start = v4l2_mmap(NULL, buf.length,
				PROT_READ | PROT_WRITE, MAP_SHARED,
				fd_, buf.m.offset);

			if (MAP_FAILED == buffers_[n_buffers_].start)
			{
				perror("mmap");
				free(buffers_);
				// todo should probably free bufs
				return false;
			}
		}

		for (int i = 0; i < n_buffers_; ++i)
		{
			struct v4l2_buffer buf;
			CLEAR(buf);
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index = i;
			xioctl(fd_, VIDIOC_QBUF, &buf);
		}
		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		xioctl(fd_, VIDIOC_STREAMON, &type);
		return true;
	}

	// note the image must be released before another can be grabbed, at most one can be out at a time
	virtual Image GrabImage(bool& failed)
	{
		failed = false;
		int r = -1;
		do
		{
			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(fd_, &fds);

			// Timeout.
			struct timeval tv;
			tv.tv_sec = 2;
			tv.tv_usec = 0;

			r = select(fd_ + 1, &fds, NULL, NULL, &tv);
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

		CLEAR(current_buf_);
		current_buf_.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		current_buf_.memory = V4L2_MEMORY_MMAP;
		xioctl(fd_, VIDIOC_DQBUF, &current_buf_);

		Image img;
		img.data = (uint8_t*)buffers_[current_buf_.index].start;
		img.data_length = current_buf_.bytesused;
		img.height = current_height_;
		img.width = current_width_;
		return img;
	}

	virtual void ReleaseImage(Image img)
	{
		xioctl(fd_, VIDIOC_QBUF, &current_buf_);
	}

	// list supported image formats, used if we fail to find one or the user just wants to list them
	// todo have standard output of format + res rather than per platform
	virtual void EnumerateFormats()
	{
		struct v4l2_fmtdesc fmtdesc;
		memset(&fmtdesc, 0, sizeof(fmtdesc));
		fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		while (ioctl(fd_, VIDIOC_ENUM_FMT, &fmtdesc) == 0)
		{
			printf("%s\n", fmtdesc.description);

			struct v4l2_frmsizeenum fmtdesc2;
			memset(&fmtdesc2, 0, sizeof(fmtdesc2));
			fmtdesc2.pixel_format = fmtdesc.pixelformat;
			while (ioctl(fd_, VIDIOC_ENUM_FRAMESIZES, &fmtdesc2) == 0)
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
