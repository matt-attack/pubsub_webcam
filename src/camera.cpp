
#include <cstdlib>

#include <pubsub/Node.h>
#include <pubsub/Publisher.h>
#include <pubsub/Image.msg.h>


#include <pubsub_cpp/arg_parse.h>

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

#include <pubsub_cpp/Node.h>


#include <pubsub/TCPTransport.h>

#define v4l2_close close
#define v4l2_ioctl ioctl
#define v4l2_open open
#define v4l2_mmap mmap
#define v4l2_munmap munmap

#define CLEAR(x) memset(&(x), 0, sizeof(x))



struct buffer
{
  void   *start;
  size_t length;
};

static void xioctl(int fh, int request, void *arg)
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

int main(int argc, char **argv)
{
  pubsub::ArgParser parser;
  parser.SetUsage("Usage: video -d DEVICE\n\nProduces a video stream from the given camera device.");
  parser.AddMulti({ "d", "device" }, "Device name for camera to stream.", "/dev/video0");
  parser.AddMulti({ "l" }, "List supported outputs for the given device.");
  parser.AddMulti({ "width" }, "Desired output width.", "640");
  parser.AddMulti({ "height" }, "Desired output height.", "480");
  parser.AddMulti({ "rgb"}, "Convert output to RGB format.");
  parser.AddMulti({ "mjpeg"}, "Use mjpeg format.");

  parser.Parse(argv, argc, 0);

  struct v4l2_format              fmt;
  struct v4l2_buffer              buf;
  fd_set                          fds;
  struct timeval                  tv;
  int                             r = -1;
  unsigned int                    i, n_buffers;
  std::string dev_name = parser.GetString("d");

  int fd = v4l2_open(dev_name.c_str(), O_RDWR | O_NONBLOCK, 0);
  if (fd < 0)
  {
    perror("Cannot open device");
    exit(EXIT_FAILURE);
  }

  if (parser.GetBool("l"))
  {
    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc,0,sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (ioctl(fd,VIDIOC_ENUM_FMT,&fmtdesc) == 0)
    {
      printf("%s\n", fmtdesc.description);

      struct v4l2_frmsizeenum fmtdesc2;
      memset(&fmtdesc2,0,sizeof(fmtdesc2));
	  fmtdesc2.pixel_format = V4L2_PIX_FMT_YUYV;
      while (ioctl(fd,VIDIOC_ENUM_FRAMESIZES,&fmtdesc2) == 0)
      {    
        if (fmtdesc2.type == V4L2_FRMSIZE_TYPE_DISCRETE)
	    {
          printf("  %ix%i\n", fmtdesc2.discrete.width, fmtdesc2.discrete.height);
	    }
        fmtdesc2.index++;
      }

      fmtdesc.index++;
    }

    v4l2_close(fd);
	exit(0);
  }

  int desired_h = parser.GetDouble("height");
  int desired_w = parser.GetDouble("width");
  bool mjpeg = parser.GetBool("mjpeg");
  int desired_pixel_format = mjpeg ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV;

  CLEAR(fmt);
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width       = desired_h;
  fmt.fmt.pix.height      = desired_w;
  fmt.fmt.pix.pixelformat = desired_pixel_format;
  fmt.fmt.pix.field       = V4L2_FIELD_NONE;
  xioctl(fd, VIDIOC_S_FMT, &fmt);
  if (fmt.fmt.pix.pixelformat != desired_pixel_format)
  {
    printf("Libv4l didn't accept desired pixel format format. Can't proceed.\n");
    exit(EXIT_FAILURE);
  }
  if ((fmt.fmt.pix.width != desired_w) || (fmt.fmt.pix.height != desired_h))
  {
    printf("Warning: driver is sending image at %dx%d\n",
      fmt.fmt.pix.width, fmt.fmt.pix.height);
  }

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

    buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory      = V4L2_MEMORY_MMAP;
    buf.index       = n_buffers;

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

  pubsub::Node node("camera");
	
  struct ps_transport_t tcp_transport;
  ps_tcp_transport_init(&tcp_transport, node.getNode());
  ps_node_add_transport(node.getNode(), &tcp_transport);

  pubsub::Publisher<pubsub::msg::Image> image_pub(node, "/image");

  bool rgb = parser.GetBool("rgb");

  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  xioctl(fd, VIDIOC_STREAMON, &type);
  while (ps_okay())
  {
    do
    {
      FD_ZERO(&fds);
      FD_SET(fd, &fds);

      /* Timeout. */
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
    xioctl(fd, VIDIOC_DQBUF, &buf);

    //printf("%i\n", buf.bytesused);

    // V4L2_PIX_FMT_YUYV
    // in this format, luminance is every even byte, then cr and cb are in alternative odd
    // to test just grab luminance
    auto img = pubsub::msg::ImageSharedPtr(new pubsub::msg::Image);
    img->width = fmt.fmt.pix.width;
    img->height = fmt.fmt.pix.height;
	if (rgb)
	{
      img->type = pubsub::msg::Image::R8G8B8;
      img->data_length = img->width*img->height*3;
      img->data = (uint8_t*)malloc(img->data_length);
      uint8_t* data = (uint8_t*)buffers[buf.index].start;
	  // todo fill out pixel 0
      int len = img->height*img->width;
      for (int i = 1; i < len; i++)
      {
        auto y = data[i*2];
        auto u = data[i*2-1];
        auto v = data[i*2+1];
        if ((i&0b1) == 0)
          std::swap(u,v);
        int8_t c = (int8_t) (y - 16);
        int8_t d = (int8_t) (u - 128);
        int8_t e = (int8_t) (v - 128);

        uint8_t r = (uint8_t) (c + (1.370705f * (e))); 
        uint8_t g = (uint8_t) (c - (0.698001f * (d)) - (0.337633f * (e)));
        uint8_t b = (uint8_t) (c + (1.732446f * (d)));
        img->data[i*3 + 0] = r;
        img->data[i*3 + 1] = g;
        img->data[i*3 + 2] = b;
      }
	}
	else if (mjpeg)
	{
	  img->type = pubsub::msg::Image::JPEG;
      img->data_length = buf.bytesused;
      img->data = (uint8_t*)malloc(img->data_length);
      uint8_t* data = (uint8_t*)buffers[buf.index].start;
      memcpy(img->data, data, img->data_length);
	}
    else
    {
      img->type = pubsub::msg::Image::YUYV;
      img->data_length = img->width*img->height*2;
      img->data = (uint8_t*)malloc(img->data_length);
      uint8_t* data = (uint8_t*)buffers[buf.index].start;
      memcpy(img->data, data, img->data_length);
    }
    image_pub.publish(img);
    node.spin();

    xioctl(fd, VIDIOC_QBUF, &buf);
  }

  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  xioctl(fd, VIDIOC_STREAMOFF, &type);
  for (i = 0; i < n_buffers; ++i)
  {
    v4l2_munmap(buffers[i].start, buffers[i].length);
  }
  v4l2_close(fd);

  return 0;
}
