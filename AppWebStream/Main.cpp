#include <sstream>
#include <iostream>
#include <string>
#include "VideoEncoder.hpp"
#include "OutputStream.hpp"


struct window_dc {
    window_dc(HWND h) : wnd(h) {
        dc = GetDC(wnd);
        if (wnd) {
            if (!GetClientRect(h, &rect))
                throw std::runtime_error("GetClientRect failed");
        } else {
            // primary monitor resolution
            rect.right  = GetSystemMetrics(SM_CXSCREEN);
            rect.bottom = GetSystemMetrics(SM_CYSCREEN);
        }
    }
    ~window_dc() {
        ReleaseDC(wnd, dc);
    }

    LONG width() {
        return rect.right - rect.left;
    }
    LONG height() {
        return rect.bottom - rect.top;
    }

    HWND wnd  = nullptr;
    HDC  dc   = nullptr;
    RECT rect = {};
};


struct offscreen_bmp {
    offscreen_bmp(HDC ref_dc, LONG width, LONG height) {
        dc = CreateCompatibleDC(ref_dc);
        if (!dc)
            throw std::runtime_error("CreateCompatibleDC has failed");

        bmp = CreateCompatibleBitmap(ref_dc, width, height);
        if (!bmp)
            throw std::runtime_error("CreateCompatibleBitmap Failed");

        // make bitmap current for dc
        prev = SelectObject(dc, bmp);
    }
    ~offscreen_bmp() {
        SelectObject(dc, prev);
        DeleteObject(bmp);
        DeleteDC(dc);
    }

    HDC     dc   = nullptr;
    HBITMAP bmp  = nullptr;
    HGDIOBJ prev = nullptr;
};


/** Convenience function to create a locally implemented COM instance without the overhead of CoCreateInstance.
The COM class does not need to be registred for construction to succeed. However, lack of registration can
cause problems if transporting the class out-of-process. */
template <class T>
static CComPtr<T> CreateLocalInstance () {
    // create an object (with ref. count zero)
    CComObject<T> * tmp = nullptr;
    if (FAILED(CComObject<T>::CreateInstance(&tmp)))
        throw std::runtime_error("CreateInstance failed");

    // move into smart-ptr (will incr. ref. count to one)
    return CComPtr<T>(static_cast<T*>(tmp));
}


static HRESULT EncodeFrame (VideoEncoder & encoder, window_dc & wnd_dc) {
    // create offscreen bitmap with device context
    // make bitmap width compatible with VideoEncoder, so that image buffer pointers can be shared
    offscreen_bmp bmp(wnd_dc.dc, VideoEncoder::Align(wnd_dc.width()), wnd_dc.height());

    // copy window content to bitmap
    if (!BitBlt(/*dst*/bmp.dc, 0, 0, wnd_dc.width(), wnd_dc.height(),
        /*src*/wnd_dc.dc, 0, 0, SRCCOPY))
        return E_FAIL;

    BITMAPINFO bmp_info = {};
    bmp_info.bmiHeader.biSize = sizeof(bmp_info.bmiHeader);
    {
        // call GetDIBits to fill "bmp_info" struct 
        int ok = GetDIBits(bmp.dc, bmp.bmp, 0, wnd_dc.height(), nullptr, &bmp_info, DIB_RGB_COLORS);
        if (!ok)
            return E_FAIL;
        bmp_info.bmiHeader.biBitCount    = 32;     // request 32bit RGBA image 
        bmp_info.bmiHeader.biCompression = BI_RGB; // disable compression
#ifndef ENABLE_FFMPEG
        bmp_info.bmiHeader.biHeight = abs(bmp_info.bmiHeader.biHeight); // request bottom-up bitmap, with origin in lower-left corner
#else
        bmp_info.bmiHeader.biHeight = -abs(bmp_info.bmiHeader.biHeight); // request bitmap with origin in top-left corner
#endif
    }

    // call GetDIBits to get image data
    auto * img_ptr = encoder.WriteFrameBegin();
    int scan_lines = GetDIBits(bmp.dc, bmp.bmp, 0, wnd_dc.height(), img_ptr, &bmp_info, DIB_RGB_COLORS);
    if (scan_lines != wnd_dc.height()) {
        encoder.WriteFrameEnd(); // needed to unlock buffer
        return E_FAIL;
    }

    // encode frame
    return encoder.WriteFrameEnd();
}


int main (int argc, char *argv[]) {
    printf("WebAppStream: Sample application for streaming a window to a web browser.\n");
    if (argc < 2) {
        printf("Usage  : WebAppStream.exe port-or-filename [window handle]\n");
        printf("Example: WebAppStream.exe 8080\n");
        printf("Example: WebAppStream.exe movie.mp4\n");
        printf("Use Spy++ (included with Visual Studio) to determine window handles.\n");
        return 1;
    }

    // parse arguments
    HWND win_handle = nullptr;
    if (argc > 2) {
        std::stringstream ss;
        ss << std::hex << argv[2];
        ss >> reinterpret_cast<size_t&>(win_handle);
    }
    char* port_filename = argv[1];

    // check window handle
    window_dc wnd_dc(win_handle);
    std::array<unsigned short,2> dims = { static_cast<unsigned short>(wnd_dc.width()), static_cast<unsigned short>(wnd_dc.height()) };
    if (!wnd_dc.dc) {
        fprintf(stderr, "ERROR: Invalid window handle\n");
        return -1;
    }

    const unsigned int FPS = 50;

    // create H.264/MPEG4 encoder
    printf("Window handle: %08llx\n", (size_t)win_handle);
    auto os = CreateLocalInstance<OutputStream>();
#ifdef ENABLE_FFMPEG
    os->SetPortOrFilename(port_filename); // blocking call
    VideoEncoderFF encoder(dims, FPS, os);
#else
    os->SetPortOrFilename(port_filename); // blocking call
    VideoEncoderMF encoder(dims, FPS, os);
#endif
    printf("Connecting to client...\n");

    // encode & transmit frames
    for (;;) {
        HRESULT hr = EncodeFrame(encoder, wnd_dc);
        if (FAILED(hr))
            break;

        os->Flush();

        // synchronize framerate
        Sleep(1000/FPS);
    }

    return 0;
}

class AppWebStreamModule : public ATL::CAtlExeModuleT<AppWebStreamModule> {
public:
};

AppWebStreamModule _AtlModule;
