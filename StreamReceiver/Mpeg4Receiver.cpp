#pragma once
#include "Mpeg4ReceiverME.hpp"
#include "Mpeg4ReceiverSR.hpp"


std::unique_ptr<Mpeg4Receiver> Mpeg4Receiver::Create(DecoderType type, _bstr_t url, NewFrameCb frame_cb) {
    if (type == MediaFoundation_MediaEngine)
        return std::make_unique<Mpeg4ReceiverME>(url, frame_cb);
    else if (type == MediaFoundation_SourceReader)
        return std::make_unique<Mpeg4ReceiverSR>(url, frame_cb);

    abort();
}

void Mpeg4Receiver::OnStartTimeDpiChanged(uint64_t startTime, double dpi, double xform[6]) {
    if (startTime != m_startTime)
        m_metadata_changed = true;
    if (dpi != m_dpi)
        m_metadata_changed = true;

    m_startTime = startTime;
    m_dpi = dpi;

    for (size_t i = 0; i < 6; i++)
        m_xform[i] = xform[i];
}
