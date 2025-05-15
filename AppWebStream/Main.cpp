#include <sstream>
#include <iostream>
#include "VideoEncoder.hpp"
#include "OutputStream.hpp"
#include "ScreenCapture.hpp"


static HRESULT EncodeFrame (VideoEncoder& encoder, window_dc& wnd_dc, unsigned int dims[2]) {
    // create offscreen bitmap for screen capture (pad window size to be compatible with FFMPEG encoder)
    offscreen_bmp bmp(wnd_dc.dc, dims[0], dims[1]);

    // copy window content encoder buffer
    auto * img_ptr = encoder.WriteFrameBegin();
    int scan_lines = bmp.CopyToRGBABuffer(wnd_dc, (uint32_t*)img_ptr);
    if (scan_lines != (int)dims[1]) {
        encoder.AbortWrite(); // still need to unlock buffer
        return E_FAIL;
    }

    // encode frame
    HRESULT hr = encoder.WriteFrameEnd();

#ifndef _NDEBUG
    printf("f"); // log "f" to signal that a frame have been encoded
#endif
    return hr;
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
    unsigned int dims[2] = {VideoEncoder::Align2(wnd_dc.width()), VideoEncoder::Align2(wnd_dc.height())};
    if (!wnd_dc.dc) {
        fprintf(stderr, "ERROR: Invalid window handle\n");
        return -1;
    }

    if (!win_handle) {
        // clamp resolution to 1280x720 if capturing entire desktop
        dims[0] = min(dims[0], 1280);
        dims[1] = min(dims[1], 720);
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
        HRESULT hr = EncodeFrame(encoder, wnd_dc, dims);
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
