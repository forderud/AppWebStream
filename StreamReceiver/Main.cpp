#include <stdio.h>
#include <string>
#include <stdexcept>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include "ComUtil.hpp"

#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

// define smart-pointers with "Ptr" suffix
_COM_SMARTPTR_TYPEDEF(IMFAttributes, __uuidof(IMFAttributes));
_COM_SMARTPTR_TYPEDEF(IMFSourceReader, __uuidof(IMFSourceReader));
_COM_SMARTPTR_TYPEDEF(IMFMediaType, __uuidof(IMFMediaType));
_COM_SMARTPTR_TYPEDEF(IMFSample, __uuidof(IMFSample));


EXTERN_GUID(WMMEDIATYPE_Video, 0x73646976, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71); // from https://learn.microsoft.com/en-us/windows/win32/wmformat/media-type-identifiers

HRESULT EnumerateTypesForStream(IMFSourceReader& reader, DWORD streamIdx) {
    HRESULT hr = S_OK;
    DWORD dwMediaTypeIndex = 0;

    while (SUCCEEDED(hr)) {
        IMFMediaTypePtr type;
        hr = reader.GetNativeMediaType(streamIdx, dwMediaTypeIndex, &type);
        if (hr == MF_E_NO_MORE_TYPES) {
            hr = S_OK;
            break;
        }
        
        if (SUCCEEDED(hr)) {
            // Examine the media type
            printf("Stream detected...\n");
            printf("* index: %u\n", streamIdx);
            GUID guid{};
            COM_CHECK(type->GetMajorType(&guid));
            if (guid == WMMEDIATYPE_Video) {
                printf("* MajorType: WMMEDIATYPE_Video\n");
            } else {
                wchar_t guid_str[39] = {};
                int ok = StringFromGUID2(guid, guid_str, (int)std::size(guid_str));
                assert(ok);
                wprintf(L"* MajorType: %s\n", guid_str);
            }

            wprintf(L"\n");
            return S_OK;
        }

        ++dwMediaTypeIndex;
    }
    return hr;
}

DWORD GetFirstVideoStream (IMFSourceReader& reader) {
    HRESULT hr = S_OK;
    DWORD streamIdx = 0;

    while (SUCCEEDED(hr)) {
        hr = EnumerateTypesForStream(reader, streamIdx);
        if (SUCCEEDED(hr))
            return streamIdx;

        ++streamIdx;
    }

    printf("ERROR: Unable to detect any video stream.\n");
    abort();
}


HRESULT ConfigureDecoder(IMFSourceReader& reader, DWORD dwStreamIndex) {
    // Find the native format of the stream.
    IMFMediaTypePtr pNativeType;
    HRESULT hr = reader.GetNativeMediaType(dwStreamIndex, 0, &pNativeType);
    if (FAILED(hr))
        return hr;

    // Find the major type.
    GUID majorType{};
    hr = pNativeType->GetGUID(MF_MT_MAJOR_TYPE, &majorType);
    if (FAILED(hr))
        return hr;

    // Define the output type.
    IMFMediaTypePtr pType;
    hr = MFCreateMediaType(&pType);
    if (FAILED(hr))
        return hr;

    hr = pType->SetGUID(MF_MT_MAJOR_TYPE, majorType);
    if (FAILED(hr))
        return hr;

    // Select a subtype.
    GUID subtype{};
    if (majorType == MFMediaType_Video) {
        subtype = MFVideoFormat_RGB32;
    } else if (majorType == MFMediaType_Audio) {
        subtype = MFAudioFormat_PCM;
    } else {
        // Unrecognized type. Skip.
        return hr;
    }

    hr = pType->SetGUID(MF_MT_SUBTYPE, subtype);
    if (FAILED(hr))
        return hr;

    // Set the uncompressed format.
    hr = reader.SetCurrentMediaType(dwStreamIndex, NULL, pType);
    if (FAILED(hr))
        return hr;

    return hr;
}

void ProcessFrames(IMFSourceReader& reader) {
    HRESULT hr = S_OK;
    unsigned int frameCount = 0;

    bool quit = false;
    while (!quit) {
        DWORD streamIdx = 0, flags = 0;
        LONGLONG timeStamp = 0;
        IMFSamplePtr frame;
        hr = reader.ReadSample(
            (DWORD)MF_SOURCE_READER_ANY_STREAM, /*flags*/0, &streamIdx, &flags, &timeStamp, &frame);
        if (FAILED(hr))
            break;

        // TODO: Convert to timeStamp to wall-time by adding the MPEG4 CreationTime attribute
        wprintf(L"Stream %d frame time (%I64d)\n", streamIdx, timeStamp);
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            wprintf(L"\tEnd of stream\n");
            quit = true;
        }
        if (flags & MF_SOURCE_READERF_NEWSTREAM) {
            wprintf(L"\tNew stream\n");
        }
        if (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED) {
            wprintf(L"\tNative type changed\n");
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            wprintf(L"\tCurrent type changed\n");
        }
        if (flags & MF_SOURCE_READERF_STREAMTICK) {
            wprintf(L"\tStream tick\n");
        }

        if (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED) {
            // The format changed. Reconfigure the decoder.
            hr = ConfigureDecoder(reader, streamIdx);
            if (FAILED(hr))
                break;
        }

        // TODO:
        // * Print frame resolution
        // * Figure out how to extract per-frame DPI

        if (frame)
            ++frameCount;
    }

    if (FAILED(hr)) {
        wprintf(L"ProcessSamples FAILED, hr = 0x%x\n", hr);
    } else {
        wprintf(L"Processed %u frames\n", frameCount);
    }
}


int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: StreamReceiver.exe URL (e.g. StreamReceiver.exe http://localhost:8080/movie.mp4)\n");
        return -1;
    }

    COM_CHECK(MFStartup(MF_VERSION));

    _bstr_t url = argv[1];

    // connect to the MPEG4 H.264 stream
    IMFAttributesPtr attribs;
    {
        // DOC: https://learn.microsoft.com/en-us/windows/win32/medfound/source-reader-attributes
        COM_CHECK(MFCreateAttributes(&attribs, 0));
        COM_CHECK(attribs->SetUINT32(MF_LOW_LATENCY, TRUE)); // low latency mode
        COM_CHECK(attribs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE)); // GPU accelerated
        COM_CHECK(attribs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE)); // enable YUV to RGB-32 conversion
    }

    IMFSourceReaderPtr reader;
    // If needed, replace with MFCreateSourceReaderFromByteStream or MFCreateSourceReaderFromMediaSource for explicit socket handling to allow parsing of the underlying bitstream
    COM_CHECK(MFCreateSourceReaderFromURL(url, attribs, &reader));

    DWORD streamIdx = GetFirstVideoStream(reader);
    ConfigureDecoder(reader, streamIdx);

    ProcessFrames(reader);
}
