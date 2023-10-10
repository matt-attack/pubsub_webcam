#pragma once

#include <cstdint>

struct Image
{
	int width;
	int height;
	uint8_t* data;
	uint32_t data_length;
};

enum class ImageFormat
{
	YUYV = 1,
	MJPEG = 2,
	RGB = 3
};

class ICamera
{
public:

	virtual ~ICamera() {}

	// returns false if device doesnt exist
	virtual bool Initialize(int device_id) = 0;

	// returns false if format cannot be selected or device failed
	virtual bool SetFormat(ImageFormat format, int desired_width, int desired_height) = 0;

	// prepare to capture images
	virtual bool StartCapture() = 0;

	// note the image must be released before another can be grabbed, at most one can be out at a time
	virtual Image GrabImage(bool& failed) = 0;

	virtual void ReleaseImage(Image img) = 0;

	virtual void EnumerateFormats() = 0;
};