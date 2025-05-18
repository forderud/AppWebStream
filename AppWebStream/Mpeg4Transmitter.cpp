#include "Mpeg4Transmitter.hpp"
#include "OutputStream.hpp"
#include "VideoEncoder.hpp"


static CComPtr<OutputStream> CreateOutputStream(const char* port_filename) {
    auto os = CreateLocalInstance<OutputStream>();
    os->Initialize(96.0, CurrentTime1904()); // DPI & start time
    os->SetPortOrFilename(port_filename); // blocking call
    return os;
}

Mpeg4Transmitter::Mpeg4Transmitter(unsigned int dimensions[2], unsigned int fps, const char* port_filename) : m_stream(CreateOutputStream(port_filename)) {

#ifdef ENABLE_FFMPEG
    m_encoder = std::make_unique<VideoEncoderFF>(dimensions, fps, m_stream);
#else
    m_encoder = std::make_unique<VideoEncoderMF>(dimensions, fps, m_stream);
#endif
}

Mpeg4Transmitter::~Mpeg4Transmitter() {
}

R8G8B8A8* Mpeg4Transmitter::WriteFrameBegin() {
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
