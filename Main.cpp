#include <sstream>
#include <iostream>
#include <string>
#include "VideoEncoder.hpp"
#include "WebStream.hpp"


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

    HWND wnd;
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

    HDC     dc;
    HBITMAP bmp;
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
    std::cout << "WebAppStream: Sample application for streaming a window to a web browser." << std::endl;
    if (argc < 2) {
        std::cout << "Usage  : WebAppStream.exe port [window handle]\n";
        std::cout << "Example: WebAppStream.exe 8080\n";
        std::cout << "Use Spy++ (included with Visual Studio) to determine window handles.\n" << std::flush;
        return 1;
    }

    // parse arguments
    HWND win_handle = nullptr;
    if (argc > 2) {
        std::stringstream ss;
        ss << std::hex << argv[2];
        ss >> reinterpret_cast<size_t&>(win_handle);
    }
    char* port = argv[1];

    // check window handle
    window_dc wnd_dc(win_handle);
    std::array<unsigned short,2> dims = { static_cast<unsigned short>(wnd_dc.width()), static_cast<unsigned short>(wnd_dc.height()) };
    if (!wnd_dc.dc) {
        std::cerr << "ERROR: Invalid window handle" << std::flush;
        return -1;
    }

    const unsigned int FPS = 50;

    // create H.264/MPEG4 encoder
    std::cout << "Starting web server to stream window " << std::hex << win_handle << ". Please connect with a web browser on port " <<port << " to receive the stream" << std::endl;
    auto ws = CreateLocalInstance<WebStream>();
#ifdef ENABLE_FFMPEG
    ws->SetPortAndWindowHandle(port, win_handle, 1); // blocking call
    VideoEncoderFF encoder(dims, FPS, ws);
#else
    ws->SetPortAndWindowHandle(port, win_handle, FPS); // blocking call
    VideoEncoderMF encoder(dims, 1, ws);
#endif
    std::cout << "Connecting to client..." << std::endl;

    // encode & transmit frames
    HRESULT hr = S_OK;
    while (SUCCEEDED(hr)) {
        hr = EncodeFrame(encoder, wnd_dc);

        // synchronize framerate
        Sleep(1000/FPS);
    }

    return 0;
}

class AppWebStreamModule : public ATL::CAtlExeModuleT<AppWebStreamModule> {
public:
};

AppWebStreamModule _AtlModule;
