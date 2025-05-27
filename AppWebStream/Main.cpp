#include <sstream>
#include <iostream>
#include "Mpeg4Transmitter.hpp"
#include "ScreenCapture.hpp"


static double G_XFORM[] = { 0.10, 0.00, 0.00, 0.10, -0.05, 0.00 }; // 10cm width, 10cm height, top-left corner at (-0.05, 0)


inline FILETIME CurrentTime() {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return now;
}

/** Convert samples per meters to DPI. */
inline double ComputeDPI(unsigned int samples, double distance) {
    constexpr double INCH = 0.0254; // 2.54cm
    double distance_inches = distance / INCH;
    return samples / distance_inches;
}

static HRESULT EncodeFrame (Mpeg4Transmitter& encoder, window_dc& wnd_dc, unsigned int dims[2]) {
    // create offscreen bitmap for screen capture (pad window size to be compatible with FFMPEG encoder)
    offscreen_bmp bmp(wnd_dc.m_dc, dims[0], dims[1]);

    // copy window content encoder buffer
    auto* img_ptr = encoder.WriteFrameBegin();
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

#ifdef SIMULATE_GEOM_CHANGES
    // simulate xform and DPI change every 100 frames
    static int s_counter = 0;
    if (++s_counter % 100 == 0) {
        for (size_t i = 0; i < 4; i++)
            G_XFORM[i] *= 0.90; // zoom in 10%
        G_XFORM[5] += +0.001; // move image view 1mm sideways
        double dpi = ComputeDPI(dims[0], G_XFORM[0]);
        printf("New DPI: %f\n", dpi);
        encoder.SetDPI(dpi);
        encoder.SetXform(G_XFORM);
    }
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
    unsigned int dims[2] = {Align2(wnd_dc.width()), Align2(wnd_dc.height())};
    if (!wnd_dc.m_dc) {
        fprintf(stderr, "ERROR: Invalid window handle\n");
        return -1;
    }
    printf("Window handle: %08llx\n", (size_t)win_handle);

    if (!win_handle) {
        // clamp resolution to 1280x720 if capturing entire desktop
        dims[0] = min(dims[0], 1280);
        dims[1] = min(dims[1], 720);
    }

    constexpr unsigned int FPS = 25;

    // create H.264/MPEG4 encoder
    Mpeg4Transmitter encoder(dims, FPS, CurrentTime(), port_filename);
    encoder.SetDPI(ComputeDPI(dims[0], G_XFORM[0]));
    encoder.SetXform(G_XFORM);
    printf("Connecting to client...\n");

    // encode & transmit frames
    for (;;) {
        HRESULT hr = EncodeFrame(encoder, wnd_dc, dims);
        if (FAILED(hr))
            break;

        // synchronize framerate
        Sleep(1000/FPS);
    }

    return 0;
}


class AppWebStreamModule : public ATL::CAtlExeModuleT<AppWebStreamModule> {
public:
};

AppWebStreamModule _AtlModule;
