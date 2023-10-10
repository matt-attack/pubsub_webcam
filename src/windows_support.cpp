#include "windows_support.h"
#include <cstdio>

#define CHECK_HR(a, b) a

HRESULT CreateVideoDeviceSource(IMFMediaSource** ppSource, int index)
{
    *ppSource = NULL;

    IMFMediaSource* pSource = NULL;
    IMFAttributes* pAttributes = NULL;
    IMFActivate** ppDevices = NULL;

    // Create an attribute store to specify the enumeration parameters.
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr))
    {
        goto done;
    }

    // Source type: video capture devices
    hr = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );
    if (FAILED(hr))
    {
        goto done;
    }

    // Enumerate devices.
    UINT32 count;
    hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);
    if (FAILED(hr))
    {
        goto done;
    }

    if (count == 0)
    {
        hr = E_FAIL;
        goto done;
    }

    if (index >= count)
    {
        hr = E_FAIL;
        goto done;
    }

    // Create the media source object.
    CoInitialize(nullptr);
    hr = ppDevices[index]->ActivateObject(IID_PPV_ARGS(&pSource));
    if (FAILED(hr))
    {
        goto done;
    }

    *ppSource = pSource;
    (*ppSource)->AddRef();

done:
    SafeRelease(&pAttributes);

    for (DWORD i = 0; i < count; i++)
    {
        SafeRelease(&ppDevices[i]);
    }
    CoTaskMemFree(ppDevices);
    SafeRelease(&pSource);
    return hr;
}

bool WindowsCamera::Initialize(int device_id)
{
    if (FAILED(CreateVideoDeviceSource(&source_, device_id)))
    {
        return false;
    }

	return true;
}

int FindCaptureFormat(IMFMediaSource* pSource, int desired_width, int desired_height, GUID desired_format, int* found_width, int* found_height)
{
    IMFPresentationDescriptor* pPD = NULL;
    IMFStreamDescriptor* pSD = NULL;
    IMFMediaTypeHandler* pHandler = NULL;
    IMFMediaType* pType = NULL;

    HRESULT hr = pSource->CreatePresentationDescriptor(&pPD);
    if (FAILED(hr))
    {
        goto done;
    }

    BOOL fSelected;
    hr = pPD->GetStreamDescriptorByIndex(0, &fSelected, &pSD);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pSD->GetMediaTypeHandler(&pHandler);
    if (FAILED(hr))
    {
        goto done;
    }

    DWORD cTypes = 0;
    hr = pHandler->GetMediaTypeCount(&cTypes);
    if (FAILED(hr))
    {
        goto done;
    }

    int best_type = -1;
    int best_width = -1;
    int best_height = -1;
    for (DWORD i = 0; i < cTypes; i++)
    {
        hr = pHandler->GetMediaTypeByIndex(i, &pType);
        if (FAILED(hr))
        {
            goto done;
        }

        //printf("Type %i:\n");
        //LogMediaType(pType);
        //printf("\n");

        // get the format and the resolution so we can pick the best match
        PROPVARIANT type, size;
        pType->GetItem(MF_MT_SUBTYPE, &type);
        pType->GetItem(MF_MT_FRAME_SIZE, &size);

        UINT32 uwidth = 0, uheight = 0;
        Unpack2UINT32AsUINT64(size.uhVal.QuadPart, &uwidth, &uheight);

        int width = uwidth;
        int height = uheight;
        if (*type.puuid == desired_format)
        {
            // good, try and see if it fits our size request
            if (width <= desired_width && height <= desired_height && (height > best_height || width > best_width))
            {
                printf("Found best so far at: %i x %i\n", width, height);
                best_type = i;
                best_height = height;
                best_width = width;
            }

            // find the best size smaller than desired
        }
        /*if (*type.puuid == MFVideoFormat_MJPG)
        {
            printf("mjpeg\n");
        }
        else if (*type.puuid == MFVideoFormat_YUY2)
        {
            printf("yuyv\n");
        }*/
        SafeRelease(&pType);
    }

done:
    SafeRelease(&pPD);
    SafeRelease(&pSD);
    SafeRelease(&pHandler);
    SafeRelease(&pType);
    *found_width = best_width;
    *found_height = best_height;
    return best_type;
}

HRESULT SetDeviceFormat(IMFMediaSource* pSource, DWORD dwFormatIndex)
{
    IMFPresentationDescriptor* pPD = NULL;
    IMFStreamDescriptor* pSD = NULL;
    IMFMediaTypeHandler* pHandler = NULL;
    IMFMediaType* pType = NULL;

    HRESULT hr = pSource->CreatePresentationDescriptor(&pPD);
    if (FAILED(hr))
    {
        goto done;
    }

    BOOL fSelected;
    hr = pPD->GetStreamDescriptorByIndex(0, &fSelected, &pSD);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pSD->GetMediaTypeHandler(&pHandler);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pHandler->GetMediaTypeByIndex(dwFormatIndex, &pType);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pHandler->SetCurrentMediaType(pType);

done:
    SafeRelease(&pPD);
    SafeRelease(&pSD);
    SafeRelease(&pHandler);
    SafeRelease(&pType);
    return hr;
}

// returns false if format cannot be selected or device failed
bool WindowsCamera::SetFormat(ImageFormat desired_format, int desired_width, int desired_height)
{
    GUID format = MFVideoFormat_MJPG;
    if (desired_format == ImageFormat::YUYV || desired_format == ImageFormat::RGB)
    {
        format = MFVideoFormat_YUY2;
    }
    int found_width, found_height;
    int found_format = FindCaptureFormat(source_, desired_width, desired_height, MFVideoFormat_MJPG, &found_width, &found_height);
    if (found_format < 0)
    {
        return false;
    }

    if (FAILED(SetDeviceFormat(source_, found_format)))
    {
        return false;
    }

    real_width_ = found_width;
    real_height_ = found_height;

    return true;
}

// prepare to capture images
bool WindowsCamera::StartCapture()
{
    if (FAILED(source_->CreatePresentationDescriptor(&pres_)))
    {
        return false;
    }
    PROPVARIANT p;
    p.vt = VT_EMPTY;
    if (FAILED(source_->Start(pres_, 0, &p)))
    {
        return false;
    }

    if (FAILED(MFCreateSourceReaderFromMediaSource(source_, NULL, &reader_)))
    {
        return false;
    }

    return true;
}

// note the image must be released before another can be grabbed, at most one can be out at a time
Image WindowsCamera::GrabImage(bool& failed)
{
    failed = false;

    IMFMediaBuffer* pDstBuffer = NULL;
    IMF2DBuffer* p2DBuffer = NULL;
    BYTE* pbBuffer = NULL;
    DWORD streamIndex, flags;
    LONGLONG llTimeStamp;
    UINT32 uiAttribute = 0;
    DWORD dwBuffer = 0;
    DWORD d2dBufferLen = 0;

    auto res = reader_->ReadSample(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,                              // Flags.
        &streamIndex,                   // Receives the actual stream index. 
        &flags,                         // Receives status flags.
        &llTimeStamp,                   // Receives the time stamp.
        &current_sample_                // Receives the sample or NULL.
    );

    if (current_sample_ == 0)
    {
        // got nothing
        Image img;
        img.data = 0;
        img.data_length = 0;

        if (res == 0xc00d3ea2)
        {
            // device lost
            printf("Device lost.\n");
            failed = true;
        }

        return img;
    }

    IMFMediaBuffer* buf = NULL;
    DWORD bufLength = 0, lockedBufLength = 0;
    BYTE* pByteBuf = NULL;

    CHECK_HR(current_sample_->ConvertToContiguousBuffer(&current_buffer_), "ConvertToContiguousBuffer failed.");
    CHECK_HR(current_buffer_->GetCurrentLength(&bufLength), "Get buffer length failed.");
    CHECK_HR(current_buffer_->Lock(&pByteBuf, NULL, &lockedBufLength), "Failed to lock sample buffer.");

    Image img;
    img.data = pByteBuf;
    img.data_length = lockedBufLength;
    img.width = real_width_;
    img.height = real_height_;
    return img;
}

void WindowsCamera::ReleaseImage(Image img)
{
    if (current_buffer_)
    {
        CHECK_HR(current_buffer_->Unlock(), "Failed to unlock source buffer.");
    }
    SafeRelease(&current_buffer_);
    SafeRelease(&current_sample_);
}