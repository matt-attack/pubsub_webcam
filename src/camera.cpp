
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
	parser.AddMulti({ "f", "format" }, "Image format to output. YUYV, MJPEG or RGB.", "YUYV");

	//todo add enum parser and have a format argument

	parser.Parse(argv, argc, 0);

#ifdef WIN32
	ICamera* camera = new WindowsCamera();
#else
	ICamera* camera = new LinuxCamera();
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
	std::string desired_format = parser.GetString("f");
	if (desired_format != "RGB" && desired_format != "MJPEG" && desired_format != "YUYV")
	{
		printf("'%s' is not a supported output format.\n", desired_format.c_str());
		return 1;
	}
	bool mjpeg = (desired_format == "MJPEG");

	ImageFormat internal_format = mjpeg ? ImageFormat::MJPEG : ImageFormat::YUYV;
	if (!camera->SetFormat(internal_format, desired_w, desired_h))
	{
		printf("Failed to set image format.\n");
		return 1;
	}

	pubsub::Node node("camera");

	struct ps_transport_t tcp_transport;
	ps_tcp_transport_init(&tcp_transport, node.getNode());
	ps_node_add_transport(node.getNode(), &tcp_transport);

	pubsub::Publisher<pubsub::msg::Image> image_pub(node, "/image");

	bool rgb = (desired_format == "RGB");

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

		// in this format, luminance is every even byte, then cr and cb are in alternative odd
		// to test just grab luminance
		auto img = pubsub::msg::ImageSharedPtr(new pubsub::msg::Image);
		img->width = image.width;
		img->height = image.height;
		if (rgb)
		{
			img->type = pubsub::msg::Image::R8G8B8;
			img->data_length = img->width * img->height * 3;
			img->data = (uint8_t*)malloc(img->data_length);
			uint8_t* data = image.data;
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
			img->data_length = image.data_length;
			img->data = (uint8_t*)malloc(img->data_length);
			uint8_t* data = image.data;
			memcpy(img->data, data, img->data_length);
		}
		else
		{
			img->type = pubsub::msg::Image::YUYV;
			img->data_length = img->width * img->height * 2;
			img->data = (uint8_t*)malloc(img->data_length);
			uint8_t* data = image.data;
			memcpy(img->data, data, img->data_length);
		}
		image_pub.publish(img);
		node.spin();


		camera->ReleaseImage(image);
	}

	return 0;
}
