#include "Mpeg4Transmitter.hpp"
#include "OutputStream.hpp"
#include "VideoEncoder.hpp"


Mpeg4Transmitter::Mpeg4Transmitter(unsigned int dimensions[2], unsigned int fps, FILETIME startTime, const char* port_filename) {
    m_stream = CreateLocalInstance<OutputStream>();

    m_stream->Initialize(startTime); // start time

    m_stream->SetPortOrFilename(port_filename); // blocking call

#ifdef ENABLE_FFMPEG
    m_encoder = std::make_unique<VideoEncoderFF>(dimensions, fps, m_stream);
#else
    m_encoder = std::make_unique<VideoEncoderMF>(dimensions, fps, m_stream);
#endif
}

Mpeg4Transmitter::~Mpeg4Transmitter() {
}

void Mpeg4Transmitter::SetDPI(double dpi) {
    double prevDpi = m_stream->SetNextFrameDPI(dpi);
    if (prevDpi && (dpi != prevDpi))
        m_encoder->StartNewStream();
}

R8G8B8A8* Mpeg4Transmitter::WriteFrameBegin(FILETIME curTime) {
    if (curTime.dwHighDateTime || curTime.dwLowDateTime)
        m_stream->SetNextFrameTime(curTime);

    return m_encoder->WriteFrameBegin();
}

HRESULT Mpeg4Transmitter::WriteFrameEnd() {
    HRESULT hr = m_encoder->WriteFrameEnd();
    if (FAILED(hr))
        return hr;

    return m_stream->Flush();
}

void Mpeg4Transmitter::AbortWrite() {
    return m_encoder->AbortWrite();
}
