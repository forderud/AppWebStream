#include <stdio.h>
#include <string>
#include <stdexcept>
#include <windows.h>
#include <comdef.h> // for _com_error
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>


#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")


/** Converts unicode string to ASCII */
static inline std::string ToAscii(const std::wstring& w_str) {
#pragma warning(push)
#pragma warning(disable: 4996) // function or variable may be unsafe
    size_t N = w_str.size();
    std::string s_str;
    s_str.resize(N);
    wcstombs(const_cast<char*>(s_str.data()), w_str.c_str(), N);

    return s_str;
#pragma warning(pop)
}

static void COM_CHECK(HRESULT hr) {
    if (FAILED(hr)) {
        _com_error err(hr);
        const wchar_t* msg = err.ErrorMessage(); // weak ptr.
        throw std::runtime_error(ToAscii(msg));
    }
}

// define smart-pointers with "Ptr" suffix
_COM_SMARTPTR_TYPEDEF(IMFAttributes, __uuidof(IMFAttributes));
_COM_SMARTPTR_TYPEDEF(IMFSourceReader, __uuidof(IMFSourceReader));


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

    // TODO:
    // * Process frames as they arrive.
    // * Print frmae metadata to console (time-stamp, resolution etc.)
}
