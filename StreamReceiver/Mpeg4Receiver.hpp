#pragma once
#include <array>
#include <functional>
#include <string_view>
#include <atlbase.h> // for CComPtr
#include <comdef.h>  // for __uuidof, _bstr_t

struct IMFSourceReader; // forward decl.

/** Receiver for fragmented MPEG4 streams over a network.
    Does internally use the Media Foundation API, but that can change in the future. */
class Mpeg4Receiver {
public:
    /** frameTime is in 100-nanosecond units since startTime. frameDuration is also in 100-nanosecond units. */
    typedef std::function<void(Mpeg4Receiver& receiver, int64_t frameTime, int64_t frameDuration, std::string_view buffer, bool metadataChanged)> ProcessFrameCb;

    /** Connect to requested MPEG4 URL. */
    Mpeg4Receiver(_bstr_t url, ProcessFrameCb frame_cb);

    ~Mpeg4Receiver();

    /** Receive frames. The "frame_cb" callback will be called from the same thread when new frames are received. */
    HRESULT ReceiveFrame();

    uint64_t GetStartTime() const {
        return m_startTime;
    }

    double GetDpi() const {
        return m_dpi;
    }

    std::array<uint32_t, 2> GetResolution() const {
        return m_resolution;
    }

private:
    void OnStartTimeDpiChanged(uint64_t startTime, double dpi);
    HRESULT ConfigureOutputType(IMFSourceReader& reader, DWORD dwStreamIndex);

    CComPtr<IMFSourceReader> m_reader = nullptr;
    uint64_t                 m_startTime = 0;   // SECONDS since midnight, Jan. 1, 1904
    double                   m_dpi = 0;         // pixel spacing
    std::array<uint32_t, 2>  m_resolution; // horizontal & vertical pixel count
    ProcessFrameCb           m_frame_cb = nullptr;
    bool                     m_metadata_changed = false; // metadata changed since previous frame
};
