#pragma once
#include <array>
#include <functional>
#include <memory>
#include <string_view>
#include <atlbase.h> // for CComPtr
#include <comdef.h>  // for __uuidof, _bstr_t

static unsigned int Align16(unsigned int size) {
    if ((size % 16) == 0)
        return size;
    else
        return size + 16 - (size % 16);
}

/** Base-class for receiving for fragmented MPEG4 streams over a network. */
class Mpeg4Receiver {
public:
    enum DecoderType {
        MediaFoundation_MediaEngine,
        MediaFoundation_SourceReader,
    };

    /** frameTime is in 100-nanosecond units since startTime. frameDuration is also in 100-nanosecond units. */
    typedef std::function<void(Mpeg4Receiver& receiver, int64_t frameTime, int64_t frameDuration, std::string_view buffer, bool metadataChanged)> NewFrameCb;

    /** Factory function. */
    static std::unique_ptr< Mpeg4Receiver> Create(DecoderType type, _bstr_t url, NewFrameCb frame_cb);

    Mpeg4Receiver(NewFrameCb frame_cb) : m_frame_cb(frame_cb) {
    }

    virtual ~Mpeg4Receiver() = default;

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
    void GetXform(double xform[6]) const {
        for (size_t i = 0; i < 6; i++)
            xform[i] = m_xform[i];
    }

    std::array<uint32_t, 2> GetResolution() const {
        // return resolution of output buffer, that's a multiple of MPEG4 16x16 macroblocks 
        std::array<uint32_t, 2> result;
        result[0] = Align16(m_resolution[0]);
        result[1] = Align16(m_resolution[1]);
        return result;
    }

    virtual void Stop() = 0;

    /** Receive frames. The "frame_cb" callback will be called from the same thread when new frames are received. */
    virtual HRESULT ReceiveFrame() = 0;

protected:
    void OnStartTimeDpiChanged(uint64_t startTime, double dpi, double xform[6]);

private:
    uint64_t                 m_startTime = 0;   // SECONDS since midnight, Jan. 1, 1904
    double                   m_dpi = 0;         // pixel spacing
    double                   m_xform[6] = { 1, 0, 0, 1, 0, 0 }; // initialize with default identity transform
protected:
    std::array<uint32_t, 2>  m_resolution; // horizontal & vertical pixel count
    bool                     m_metadata_changed = false; // metadata changed since previous frame
    NewFrameCb               m_frame_cb = nullptr;
};
