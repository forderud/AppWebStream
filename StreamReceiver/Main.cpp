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

HRESULT EnumerateTypesForStream(IMFSourceReader* pReader, DWORD streamIdx) {
    HRESULT hr = S_OK;
    DWORD dwMediaTypeIndex = 0;

    while (SUCCEEDED(hr)) {
        IMFMediaTypePtr type;
        hr = pReader->GetNativeMediaType(streamIdx, dwMediaTypeIndex, &type);
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
        }

        ++dwMediaTypeIndex;
    }
    return hr;
}

HRESULT EnumerateMediaTypes(IMFSourceReader* pReader) {
    HRESULT hr = S_OK;
    DWORD dwStreamIndex = 0;

    while (SUCCEEDED(hr)) {
        hr = EnumerateTypesForStream(pReader, dwStreamIndex);
        if (hr == MF_E_INVALIDSTREAMNUMBER) {
            hr = S_OK;
            break;
        }

        ++dwStreamIndex;
    }
    return hr;
}


HRESULT ConfigureDecoder(IMFSourceReader* pReader, DWORD dwStreamIndex) {
    // Find the native format of the stream.
    IMFMediaTypePtr pNativeType;
    HRESULT hr = pReader->GetNativeMediaType(dwStreamIndex, 0, &pNativeType);
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
    hr = pReader->SetCurrentMediaType(dwStreamIndex, NULL, pType);
    if (FAILED(hr))
        return hr;

    return hr;
}

HRESULT ProcessFrames(IMFSourceReader* pReader) {
    HRESULT hr = S_OK;
    IMFSamplePtr pSample;
    unsigned int cSamples = 0;

    bool quit = false;
    while (!quit) {
        DWORD streamIndex, flags;
        LONGLONG llTimeStamp;

        hr = pReader->ReadSample(
            MF_SOURCE_READER_ANY_STREAM,    // Stream index.
            0,                              // Flags.
            &streamIndex,                   // Receives the actual stream index. 
            &flags,                         // Receives status flags.
            &llTimeStamp,                   // Receives the time stamp.
            &pSample                        // Receives the sample or NULL.
        );
        if (FAILED(hr))
            break;

        wprintf(L"Stream %d (%I64d)\n", streamIndex, llTimeStamp);
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
            hr = ConfigureDecoder(pReader, streamIndex);
            if (FAILED(hr))
            {
                break;
            }
        }

        if (pSample)
            ++cSamples;
    }

    if (FAILED(hr)) {
        wprintf(L"ProcessSamples FAILED, hr = 0x%x\n", hr);
    } else {
        wprintf(L"Processed %u samples\n", cSamples);
    }
    return hr;
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
    // TODO: Replace with MFCreateSourceReaderFromByteStream or MFCreateSourceReaderFromMediaSource for explicit socket handling to allow parsing of the underlying bitstream
    COM_CHECK(MFCreateSourceReaderFromURL(url, attribs, &reader));

    EnumerateMediaTypes(reader);
    DWORD streamIdx = 0; // TODO: Use parsed value
    ConfigureDecoder(reader, streamIdx);

    ProcessFrames(reader);

    // TODO:
    // * Process frames as they arrive.
    // * Print frmae metadata to console (time-stamp, DPI, resolution etc.)
    // DOC: https://learn.microsoft.com/en-us/windows/win32/medfound/processing-media-data-with-the-source-reader
}
