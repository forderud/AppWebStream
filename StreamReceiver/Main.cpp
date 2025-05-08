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

    // TODO: Connect to MPEG4 H.264 stream
    IMFAttributesPtr attribs = nullptr;
    IMFSourceReaderPtr reader;
    // TODO: Replace with MFCreateSourceReaderFromByteStream for explicit socket handling to allow parsing of the underlying bitstream
    COM_CHECK(MFCreateSourceReaderFromURL(url, attribs, &reader));

    // TODO:
    // * Process frames as they arrive.
    // * Print frmae metadata to console (time-stamp, resolution etc.)
}
