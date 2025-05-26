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
    typedef std::function<void(Mpeg4Receiver& receiver, int64_t frameTime, int64_t frameDuration, std::string_view buffer, bool metadataChanged)> NewFrameCb;

    /** Connect to requested MPEG4 URL. */
    Mpeg4Receiver(_bstr_t url, NewFrameCb frame_cb);

    ~Mpeg4Receiver();

    void Stop();

    /** Receive frames. The "frame_cb" callback will be called from the same thread when new frames are received. */
    HRESULT ReceiveFrame();

    uint64_t GetStartTime() const {
        return m_startTime;
    }

    double GetDpi() const {
        return m_dpi;
    }

    /** Get coordinate system mapping for transferring pixel coordinates in [0,1) x [0,1) to (x,y) world coordinates.
        x' = a*x + c*y + tx
        y' = b*x + d*y + ty
        where xform = [a,b, c, d, tx, ty] */
    void GetXform(double xform[6]) const;

    std::array<uint32_t, 2> GetResolution() const;

private:
    void OnStartTimeDpiChanged(uint64_t startTime, double dpi, double xform[6]);
    HRESULT ConfigureOutputType(IMFSourceReader& reader, DWORD dwStreamIndex);

    CComPtr<IMFSourceReader> m_reader = nullptr;
    uint64_t                 m_startTime = 0;   // SECONDS since midnight, Jan. 1, 1904
    double                   m_dpi = 0;         // pixel spacing
    double                   m_xform[6] = { 1, 0, 0, 1, 0, 0 }; // initialize with default identity transform
    std::array<uint32_t, 2>  m_resolution; // horizontal & vertical pixel count
    NewFrameCb               m_frame_cb = nullptr;
    bool                     m_metadata_changed = false; // metadata changed since previous frame
    bool                     m_active = true;
};
