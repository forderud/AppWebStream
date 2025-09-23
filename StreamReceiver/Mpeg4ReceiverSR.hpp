#pragma once
#include "Mpeg4Receiver.hpp"

struct IMFSourceReader; // forward decl.

/** Receiver for fragmented MPEG4 streams over a network.
    Does internally use the Media Foundation Source Reader API, but that can change in the future. */
class Mpeg4ReceiverSR : public Mpeg4Receiver {
public:
    /** Connect to requested MPEG4 URL. */
    Mpeg4ReceiverSR(_bstr_t url, NewFrameCb frame_cb);

    ~Mpeg4ReceiverSR() override;

    void Stop() override;

    /** Receive frames. The "frame_cb" callback will be called from the same thread when new frames are received. */
    HRESULT ReceiveFrame() override;

private:
    void OnStartTimeDpiChanged(uint64_t startTime, double dpi, double xform[6]);
    HRESULT ConfigureOutputType(IMFSourceReader& reader, DWORD dwStreamIndex);

    CComPtr<IMFSourceReader> m_reader;
    NewFrameCb               m_frame_cb = nullptr;
    bool                     m_metadata_changed = false; // metadata changed since previous frame
    bool                     m_active = true;
};
