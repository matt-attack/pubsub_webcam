
#include <cstdlib>

#include <pubsub/Node.h>
#include <pubsub/Publisher.h>
#include <pubsub/Image.msg.h>

#include <pubsub_cpp/arg_parse.h>

#include <pubsub_cpp/Node.h>


#include <pubsub/TCPTransport.h>

#ifdef WIN32
#include "windows_support.h"
#else
#include "linux_support.h"
#endif

int main(int argc, char** argv)
{
	pubsub::ArgParser parser;
	parser.SetUsage("Usage: video -d DEVICE\n\nProduces a video stream from the given camera device.");
	parser.AddMulti({ "d", "device" }, "Device name for camera to stream.", "/dev/video0");
	parser.AddMulti({ "l" }, "List supported outputs for the given device.");
	parser.AddMulti({ "width" }, "Desired output width.", "640");
	parser.AddMulti({ "height" }, "Desired output height.", "480");
	//parser.AddMulti({ "rgb"}, "Convert output to RGB format.");
	//parser.AddMulti({ "mjpeg"}, "Use mjpeg format.");
	parser.AddMulti({ "f", "format" }, "Image format to output.", "YUYV");

	//todo add enum parser and have a format argument

	parser.Parse(argv, argc, 0);

#ifdef WIN32
	ICamera* camera = new WindowsCamera();
#else
	ICamera* camera = new LinuxCamera;// todo
#endif

	if (camera->Initialize(parser.GetDouble("d")) == false)
	{
		printf("Cannot open device\n");
		return 1;
	}

	if (parser.GetBool("l"))
	{
		camera->EnumerateFormats();
		return 0;
	}

	int desired_h = parser.GetDouble("height");
	int desired_w = parser.GetDouble("width");
	bool mjpeg = true;// parser.GetBool("mjpeg");
	todo fix format selection
	// todo allow selecting formats
	if (!camera->SetFormat(ImageFormat::MJPEG, desired_w, desired_h))
	{
		printf("Failed to set image format.\n");
		return 1;
	}

	pubsub::Node node("camera");

	struct ps_transport_t tcp_transport;
	ps_tcp_transport_init(&tcp_transport, node.getNode());
	ps_node_add_transport(node.getNode(), &tcp_transport);

	pubsub::Publisher<pubsub::msg::Image> image_pub(node, "/image");

	bool rgb = false;// parser.GetBool("rgb");

	if (!camera->StartCapture())
	{
		printf("Failed to start capture\n");
		return 1;
	}


	while (ps_okay())
	{
		bool failed = false;
		Image image = camera->GrabImage(failed);
		if (failed)
		{
			printf("Capture device lost\n");
			break;
		}

		if (image.data == 0)
		{
			continue;
		}

		// image is good to work with

		/*do
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
			perror("select");
			return errno;
		}

		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		xioctl(fd, VIDIOC_DQBUF, &buf);*/

		//printf("%i\n", buf.bytesused);

		// V4L2_PIX_FMT_YUYV
		// in this format, luminance is every even byte, then cr and cb are in alternative odd
		// to test just grab luminance
		auto img = pubsub::msg::ImageSharedPtr(new pubsub::msg::Image);
		img->width = image.width;// fmt.fmt.pix.width;
		img->height = image.height;// fmt.fmt.pix.height;
		if (rgb)
		{
			img->type = pubsub::msg::Image::R8G8B8;
			img->data_length = img->width * img->height * 3;
			img->data = (uint8_t*)malloc(img->data_length);
			uint8_t* data = image.data;// (uint8_t*)buffers[buf.index].start;
			// todo fill out pixel 0
			int len = img->height * img->width;
			for (int i = 1; i < len; i++)
			{
				auto y = data[i * 2];
				auto u = data[i * 2 - 1];
				auto v = data[i * 2 + 1];
				if ((i & 0b1) == 0)
					std::swap(u, v);
				int8_t c = (int8_t)(y - 16);
				int8_t d = (int8_t)(u - 128);
				int8_t e = (int8_t)(v - 128);

				uint8_t r = (uint8_t)(c + (1.370705f * (e)));
				uint8_t g = (uint8_t)(c - (0.698001f * (d)) - (0.337633f * (e)));
				uint8_t b = (uint8_t)(c + (1.732446f * (d)));
				img->data[i * 3 + 0] = r;
				img->data[i * 3 + 1] = g;
				img->data[i * 3 + 2] = b;
			}
		}
		else if (mjpeg)
		{
			img->type = pubsub::msg::Image::JPEG;
			img->data_length = image.data_length;// buf.bytesused;
			img->data = (uint8_t*)malloc(img->data_length);
			uint8_t* data = image.data;// (uint8_t*)buffers[buf.index].start;
			memcpy(img->data, data, img->data_length);
		}
		else
		{
			img->type = pubsub::msg::Image::YUYV;
			img->data_length = img->width * img->height * 2;
			img->data = (uint8_t*)malloc(img->data_length);
			uint8_t* data = image.data;// (uint8_t*)buffers[buf.index].start;
			memcpy(img->data, data, img->data_length);
		}
		image_pub.publish(img);
		node.spin();


		camera->ReleaseImage(image);

		//xioctl(fd, VIDIOC_QBUF, &buf);
	}

	/*type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	xioctl(fd, VIDIOC_STREAMOFF, &type);
	for (i = 0; i < n_buffers; ++i)
	{
		v4l2_munmap(buffers[i].start, buffers[i].length);
	}
	v4l2_close(fd);*/

	return 0;
}
