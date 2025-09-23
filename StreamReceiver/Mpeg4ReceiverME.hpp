#pragma once
#include "Mpeg4Receiver.hpp"

struct IMFMediaEngine; // forward decl.


/** Receiver for fragmented MPEG4 streams over a network.
    Does internally use the Media Foundation Media Engine API. */
class Mpeg4ReceiverME : public Mpeg4Receiver {
public:
    /** Connect to requested MPEG4 URL. */
    Mpeg4ReceiverME(_bstr_t url, NewFrameCb frame_cb);

    ~Mpeg4ReceiverME() override;

    void Stop() override;

    /** Receive frames. The "frame_cb" callback will be called from the same thread when new frames are received. */
    HRESULT ReceiveFrame() override;

private:
    CComPtr<IMFMediaEngine> m_engine;
    NewFrameCb              m_frame_cb = nullptr;
    bool                    m_metadata_changed = false; // metadata changed since previous frame
    bool                    m_active = true;
};
