#pragma once
#include <array>
#include <functional>
#include <string_view>
#include <comdef.h> // for __uuidof
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>

// define smart-pointers with "Ptr" suffix
_COM_SMARTPTR_TYPEDEF(IMFSourceReader, __uuidof(IMFSourceReader));


class IStartTimeDPIReceiver {
public:
    virtual ~IStartTimeDPIReceiver() = default;
    virtual void OnStartTimeDpiChanged(uint64_t startTime, double dpi) = 0;
};

class Mpeg4Receiver; // forward decl.

/** frameTime is in 100-nanosecond units since startTime. frameDuration is also in 100-nanosecond units. */
typedef std::function<void(Mpeg4Receiver& receiver, int64_t frameTime, int64_t frameDuration, std::string_view buffer, bool metadataChanged)> ProcessFrameCb;

class Mpeg4Receiver : public IStartTimeDPIReceiver {
public:
    /** Connect to requested MPEG4 URL. The "frame_cb" callback will be called from the same thread for every frame received. */
    Mpeg4Receiver(_bstr_t url, ProcessFrameCb frame_cb);

    ~Mpeg4Receiver() override;

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
    void OnStartTimeDpiChanged(uint64_t startTime, double dpi) override;
    HRESULT ConfigureOutputType(IMFSourceReader& reader, DWORD dwStreamIndex);

    IMFSourceReaderPtr m_reader;
    uint64_t           m_startTime = 0;   // SECONDS since midnight, Jan. 1, 1904
    double             m_dpi = 0;         // pixel spacing
    std::array<uint32_t, 2> m_resolution; // horizontal & vertical pixel count
    ProcessFrameCb     m_frame_cb = nullptr;
    bool               m_metadata_changed = false; // metadata changed since previous frame
};
