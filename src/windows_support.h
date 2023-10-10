#pragma once

#include "camera.h"

#include <mfapi.h>
#include <mfidl.h>

#include <mferror.h>
#include <mfobjects.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <cstdint>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

template <class T> void SafeRelease(T** ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}
class WindowsCamera: public ICamera
{
	IMFMediaSource* source_ = 0;
	IMFSourceReader* reader_ = 0;
	IMFPresentationDescriptor* pres_ = 0;

	IMFSample* current_sample_ = 0;
	IMFMediaBuffer* current_buffer_ = 0;

	int real_width_, real_height_;
public:

	WindowsCamera()
	{

	}

	~WindowsCamera()
	{
		SafeRelease(&pres_);
		SafeRelease(&reader_);
		SafeRelease(&source_);
	}

	// returns false if device doesnt exist
	virtual bool Initialize(int device_id);

	// returns false if format cannot be selected or device failed
	virtual bool SetFormat(ImageFormat format, int desired_width, int desired_height);

	// prepare to capture images
	virtual bool StartCapture();

	// note the image must be released before another can be grabbed, at most one can be out at a time
	virtual Image GrabImage(bool& failed);

	virtual void ReleaseImage(Image img);

	// list supported image formats, used if we fail to find one or the user just wants to list them
	// todo have standard output of format + res rather than per platform
	virtual void EnumerateFormats() {};
};