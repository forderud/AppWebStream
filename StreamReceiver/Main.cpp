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



HRESULT EnumerateTypesForStream(IMFSourceReader* pReader, DWORD dwStreamIndex) {
    HRESULT hr = S_OK;
    DWORD dwMediaTypeIndex = 0;

    while (SUCCEEDED(hr)) {
        IMFMediaTypePtr type;
        hr = pReader->GetNativeMediaType(dwStreamIndex, dwMediaTypeIndex, &type);
        if (hr == MF_E_NO_MORE_TYPES) {
            hr = S_OK;
            break;
        }
        
        if (SUCCEEDED(hr)) {
            // Examine the media type
            printf("Stream detected...\n");
            GUID guid{};
            COM_CHECK(type->GetMajorType(&guid));
            wchar_t guid_str[39] = {};
            int ok = StringFromGUID2(guid, guid_str, (int)std::size(guid_str));
            assert(ok);
            wprintf(L"* MajorType: %s\n", guid_str);
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

    // TODO:
    // * Process frames as they arrive.
    // * Print frmae metadata to console (time-stamp, DPI, resolution etc.)
    // DOC: https://learn.microsoft.com/en-us/windows/win32/medfound/processing-media-data-with-the-source-reader
}
