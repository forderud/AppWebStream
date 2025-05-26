#pragma once
#include <Windows.h>
#include <atlbase.h>
#include <memory>

/** 32bit color value. */
struct R8G8B8A8 {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

/** FFMPEG only: Grow size to become a multiple of 2 (libx264 requirement). */
static unsigned int Align2(unsigned int size) {
#ifdef ENABLE_FFMPEG
    constexpr unsigned int block_size = 2;
    if ((size % block_size) == 0)
        return size;
    else
        return size + block_size - (size % block_size);
#else
    return size;
#endif
}

class OutputStream; // forward decl.
class VideoEncoderFF;
class VideoEncoderMF;


class Mpeg4Transmitter {
public:
    Mpeg4Transmitter(unsigned int dimensions[2], unsigned int fps, FILETIME startTime, const char* port_filename);
    ~Mpeg4Transmitter();

    /** Update DPI for the next frame.
        WARNING: DPI changes not yet implemented. */
    void SetDPI(double dpi);

    /** Set coordinate system mapping for transferring pixel coordinates in [0,1) x [0,1) to (x,y) world coordinates.
        x' = a*x + c*y + tx
        y' = b*x + d*y + ty
        where xform = [a,b, c, d, tx, ty] */
    void SetXform(double xform[6]);

    R8G8B8A8* WriteFrameBegin(FILETIME curTime = {});
    HRESULT   WriteFrameEnd();
    void      AbortWrite();

private:
    CComPtr<OutputStream>           m_stream;
#ifdef ENABLE_FFMPEG
    std::unique_ptr<VideoEncoderFF> m_encoder;
#else
    std::unique_ptr<VideoEncoderMF> m_encoder;
#endif
};
