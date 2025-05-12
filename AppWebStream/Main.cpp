#include <sstream>
#include <iostream>
#include <string>
#include "VideoEncoder.hpp"
#include "OutputStream.hpp"
#include "ScreenCapture.hpp"


static HRESULT EncodeFrame (VideoEncoder & encoder, window_dc & wnd_dc) {
    // create offscreen bitmap with device context
    // make bitmap width compatible with VideoEncoder, so that image buffer pointers can be shared
    offscreen_bmp bmp(wnd_dc.dc, VideoEncoder::Align2(wnd_dc.width()), wnd_dc.height());

    // copy window content to bitmap
    if (!BitBlt(/*dst*/bmp.dc, 0, 0, wnd_dc.width(), wnd_dc.height(), /*src*/wnd_dc.dc, 0, 0, SRCCOPY))
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
#ifdef ENABLE_FFMPEG
        bmp_info.bmiHeader.biHeight = -abs(bmp_info.bmiHeader.biHeight); // request bitmap with origin in top-left corner
#else
        bmp_info.bmiHeader.biHeight = abs(bmp_info.bmiHeader.biHeight); // request bottom-up bitmap, with origin in lower-left corner
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
    unsigned short dims[2] = {static_cast<unsigned short>(wnd_dc.width()), static_cast<unsigned short>(wnd_dc.height())};
    if (!wnd_dc.dc) {
        fprintf(stderr, "ERROR: Invalid window handle\n");
        return -1;
    }

    const unsigned int FPS = 50;

    // create H.264/MPEG4 encoder
    printf("Window handle: %08llx\n", (size_t)win_handle);
    auto os = CreateLocalInstance<OutputStream>();
    os->Initialize(96.0, CurrentTime1904()); // DPI & start time
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
