#include <stdio.h>
#include <string>
#include <stdexcept>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include "../AppWebStream/ComUtil.hpp"
#include "StreamWrapper.hpp"

#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

// define smart-pointers with "Ptr" suffix
_COM_SMARTPTR_TYPEDEF(IMFAttributes, __uuidof(IMFAttributes));
_COM_SMARTPTR_TYPEDEF(IMFSourceReader, __uuidof(IMFSourceReader));
_COM_SMARTPTR_TYPEDEF(IMFMediaType, __uuidof(IMFMediaType));
_COM_SMARTPTR_TYPEDEF(IMFSample, __uuidof(IMFSample));
_COM_SMARTPTR_TYPEDEF(IMFByteStream, __uuidof(IMFByteStream));
_COM_SMARTPTR_TYPEDEF(IMFSourceResolver, __uuidof(IMFSourceResolver));
_COM_SMARTPTR_TYPEDEF(IMFMediaBuffer, __uuidof(IMFMediaBuffer));


EXTERN_GUID(WMMEDIATYPE_Video, 0x73646976, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71); // video stream from https://learn.microsoft.com/en-us/windows/win32/wmformat/media-type-identifiers

HRESULT IsVideoStream(IMFSourceReader& reader, DWORD streamIdx, /*out*/bool& isVideo) {
    // iterate over all media types
    for (DWORD mediaTypeIdx = 0;; mediaTypeIdx++) {
        IMFMediaTypePtr mediaType;
        HRESULT hr = reader.GetNativeMediaType(streamIdx, mediaTypeIdx, &mediaType);
        if (FAILED(hr)) {
            isVideo = false;
            return hr; // MF_E_NO_MORE_TYPES if out of bounds
        }
        
        // examine media type
        GUID guid{};
        COM_CHECK(mediaType->GetMajorType(&guid));
        if (guid == WMMEDIATYPE_Video) {
            isVideo = true; // found video stream
            return S_OK;
        }

#if 0
        // other major type
        wchar_t guid_str[39] = {};
        int ok = StringFromGUID2(guid, guid_str, (int)std::size(guid_str));
        assert(ok);
        wprintf(L"* MajorType: %s\n", guid_str);
#endif
    }
}

DWORD GetFirstVideoStream (IMFSourceReader& reader) {
    // iterate over all streams
    for (DWORD streamIdx = 0; ; streamIdx++) {
        bool isVideo = false;
        HRESULT hr = IsVideoStream(reader, streamIdx, isVideo);
        if (hr == MF_E_NO_MORE_TYPES) // out of bounds
            break;

        if (SUCCEEDED(hr) && isVideo)
            return streamIdx;
    }

    wprintf(L"ERROR: Unable to detect any video stream.\n");
    abort();
}


HRESULT ConfigureOutputType(IMFSourceReader& reader, DWORD dwStreamIndex) {
    GUID majorType{};
    GUID subType{};
    {
        // get the native stream format
        IMFMediaTypePtr nativeType;
        COM_CHECK(reader.GetNativeMediaType(dwStreamIndex, 0, &nativeType));

        // get major type
        COM_CHECK(nativeType->GetGUID(MF_MT_MAJOR_TYPE, &majorType));

        // select matching subtype
        if (majorType == MFMediaType_Video)
            subType = MFVideoFormat_RGB32;
        else if (majorType == MFMediaType_Audio)
            subType = MFAudioFormat_PCM;
        else
            return E_FAIL; // unrecognized type
    }

    // configure RGB32 output
    IMFMediaTypePtr mediaType;
    COM_CHECK(MFCreateMediaType(&mediaType));
    COM_CHECK(mediaType->SetGUID(MF_MT_MAJOR_TYPE, majorType));
    COM_CHECK(mediaType->SetGUID(MF_MT_SUBTYPE, subType));

    COM_CHECK(reader.SetCurrentMediaType(dwStreamIndex, NULL, mediaType));
    return S_OK;
}

static unsigned int Align16(unsigned int size) {
    if ((size % 16) == 0)
        return size;
    else
        return size + 16 - (size % 16);
}


void ProcessFrames(IMFSourceReader& reader) {
    HRESULT hr = S_OK;
    unsigned int frameCount = 0;

    bool quit = false;
    while (!quit) {
        DWORD streamIdx = 0;
        DWORD flags = 0;       // MF_SOURCE_READER_FLAG bitmask
        int64_t timeStamp = 0; // in 100-nanosecond units
        IMFSamplePtr frame;    // NULL if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        hr = reader.ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, /*flags*/0, &streamIdx, &flags, &timeStamp, &frame);
        if (FAILED(hr))
            break;

        wprintf(L"Stream event on idx: %u\n", streamIdx);

#if 0
        PROPVARIANT val{};
        PropVariantClear(&val);
        COM_CHECK(reader.GetPresentationAttribute(streamIdx, MF_PD_LAST_MODIFIED_TIME, &val)); // fails with "The requested attribute was not found."
#endif

        bool printAll = false; // print all frame parameters
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            wprintf(L"  End of stream\n");
            quit = true;
        }
        if (flags & MF_SOURCE_READERF_NEWSTREAM) {
            wprintf(L"  New stream\n");
            printAll = true;
        }
        if (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED) {
            wprintf(L"  Native type changed\n");
            printAll = true;
            // The format changed. Reconfigure the decoder.
            hr = ConfigureOutputType(reader, streamIdx);
            if (FAILED(hr))
                break;
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            wprintf(L"  Current type changed\n");
            printAll = true;
        }
        if (flags & MF_SOURCE_READERF_STREAMTICK) {
            wprintf(L"  Stream tick\n");
            printAll = true;
        }

        uint32_t width = 0, height = 0;
        {
            IMFMediaTypePtr nativeType;
            COM_CHECK(reader.GetNativeMediaType(streamIdx, 0, &nativeType));

            COM_CHECK(MFGetAttributeSize(nativeType, MF_MT_FRAME_SIZE, &width, &height));
            if (printAll)
                wprintf(L"  Frame resolution: %u x %u\n", width, height);
        }

        wprintf(L"  Frame time:     %f ms\n", timeStamp*0.1f/1000); // convert to milliseconds

        if (!frame)
            continue;

        int64_t frameTime = 0; // in 100-nanosecond units
        COM_CHECK(frame->GetSampleTime(&frameTime));
        assert(frameTime == timeStamp);

        int64_t frameDuration = 0; // in 100-nanosecond units
        COM_CHECK(frame->GetSampleDuration(&frameDuration));
        wprintf(L"  Frame duration: %f ms\n", frameDuration*0.1f/1000); // convert to milliseconds

        DWORD bufferCount = 0;
        COM_CHECK(frame->GetBufferCount(&bufferCount));
        for (DWORD idx = 0; idx < bufferCount; idx++) {
            IMFMediaBufferPtr buffer;
            COM_CHECK(frame->GetBufferByIndex(idx, &buffer));

            DWORD bufLen = 0;
            COM_CHECK(buffer->GetCurrentLength(&bufLen));
            //wprintf(L"  Frame buffer #%u length: %u\n", idx, bufLen);
            assert(bufLen == 4 * Align16(width) * Align16(height)); // buffer size is a multiple of MPEG4 16x16 macroblocks

            // Call buffer->Lock()... Unlock() to access RGBA pixel data
        }

        ++frameCount;
    }

    if (FAILED(hr)) {
        wprintf(L"ProcessSamples FAILED, hr = 0x%x\n", hr);
    } else {
        wprintf(L"Processed %u frames\n", frameCount);
    }
}


class Mpeg4Receiver : public IStartTimeDPIReceiver {
public:
    Mpeg4Receiver(_bstr_t url) {
        COM_CHECK(MFStartup(MF_VERSION));

        IMFAttributesPtr attribs;
        {
            // DOC: https://learn.microsoft.com/en-us/windows/win32/medfound/source-reader-attributes
            COM_CHECK(MFCreateAttributes(&attribs, 0));
            COM_CHECK(attribs->SetUINT32(MF_LOW_LATENCY, TRUE)); // low latency mode
            COM_CHECK(attribs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE)); // GPU accelerated
            COM_CHECK(attribs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE)); // enable YUV to RGB-32 conversion
        }

#if 1
        // Create intermediate IMFByteStream object allow parsing of the underlying MPEG4 bitstream.
        // Needed to access CreationTime & DPI parameters that doesn't seem to be exposed through the MediaFoundation API.
        IMFByteStreamPtr byteStream;
        {
            IMFSourceResolverPtr resolver;
            COM_CHECK(MFCreateSourceResolver(&resolver));

            DWORD createObjFlags = MF_RESOLUTION_READ | MF_RESOLUTION_BYTESTREAM | MF_RESOLUTION_CONTENT_DOES_NOT_HAVE_TO_MATCH_EXTENSION_OR_MIME_TYPE;
            MF_OBJECT_TYPE objectType = MF_OBJECT_INVALID;
            IUnknownPtr source;
            COM_CHECK(resolver->CreateObjectFromURL(url, createObjFlags, nullptr, &objectType, &source));
            IMFByteStreamPtr innerStream = source;

            auto tmp = CreateLocalInstance<StreamWrapper>();
            tmp->Initialize(innerStream, this);
            COM_CHECK(tmp.QueryInterface(&byteStream));
        }
        COM_CHECK(MFCreateSourceReaderFromByteStream(byteStream, attribs, &m_reader));
#else
        COM_CHECK(MFCreateSourceReaderFromURL(url, attribs, &m_reader));
#endif

        DWORD streamIdx = GetFirstVideoStream(m_reader);
        ConfigureOutputType(m_reader, streamIdx);
    }

    ~Mpeg4Receiver() override {
    }

    void ReceiveFrames() {
        ProcessFrames(m_reader);
    }

    void OnStartTimeDpiChanged(uint64_t startTime, double dpi) override {
        wprintf(L"Frame DPI:  %f\n", dpi);
        wprintf(L"Start time: %hs (UTC)\n", TimeString1904(startTime).c_str());
    }

private:
    IMFSourceReaderPtr m_reader;
};


int main(int argc, char* argv[]) {
    if (argc < 2) {
        wprintf(L"Usage: StreamReceiver.exe URL (e.g. StreamReceiver.exe http://localhost:8080/movie.mp4)\n");
        return -1;
    }

    _bstr_t url = argv[1];
    // connect to MPEG4 H.264 stream
    Mpeg4Receiver receiver(url);
    // blocking call
    receiver.ReceiveFrames();
}


class StreamReceiverModule : public ATL::CAtlExeModuleT<StreamReceiverModule> {
public:
};

StreamReceiverModule _AtlModule;
