#pragma once
#include <array>
#include <functional>
#include <string_view>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
#include <atlbase.h> // for CComPtr
#include <comdef.h>  // for __uuidof, _bstr_t

struct IMFSourceReader; // forward decl.

/** Frame data structure for async processing */
struct FrameData {
    int64_t frameTime;
    int64_t frameDuration;
    std::vector<uint8_t> buffer;
    bool metadataChanged;
    uint64_t startTime;
    double dpi;
    double xform[6];
    std::array<uint32_t, 2> resolution;
    
    FrameData() = default;
    FrameData(int64_t ft, int64_t fd, const std::string_view& buf, bool mc, 
              uint64_t st, double d, const double* xf, const std::array<uint32_t, 2>& res)
        : frameTime(ft), frameDuration(fd), buffer(buf.begin(), buf.end()), 
          metadataChanged(mc), startTime(st), dpi(d), resolution(res) {
        for (int i = 0; i < 6; i++) xform[i] = xf[i];
    }
};

/** Thread-safe frame queue for async processing */
class AsyncFrameQueue {
public:
    void Push(std::unique_ptr<FrameData> frame) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // Limit queue size to prevent memory buildup
        while (m_queue.size() >= MAX_QUEUE_SIZE) {
            m_queue.pop(); // Drop oldest frame
        }
        
        m_queue.push(std::move(frame));
        m_cv.notify_one();
    }
    
    std::unique_ptr<FrameData> Pop(bool blocking = true) {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        if (blocking) {
            m_cv.wait(lock, [this] { return !m_queue.empty() || m_shutdown; });
        }
        
        if (m_queue.empty() || m_shutdown) {
            return nullptr;
        }
        
        auto frame = std::move(m_queue.front());
        m_queue.pop();
        return frame;
    }
    
    void Shutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shutdown = true;
        m_cv.notify_all();
    }
    
    size_t Size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }
    
private:
    static constexpr size_t MAX_QUEUE_SIZE = 10; // Limit queue size for low latency
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::unique_ptr<FrameData>> m_queue;
    std::atomic<bool> m_shutdown{false};
};

/** Receiver for fragmented MPEG4 streams over a network.
    Does internally use the Media Foundation API, but that can change in the future. */
class Mpeg4Receiver {
public:
    /** frameTime is in 100-nanosecond units since startTime. frameDuration is also in 100-nanosecond units. */
    typedef std::function<void(Mpeg4Receiver& receiver, int64_t frameTime, int64_t frameDuration, std::string_view buffer, bool metadataChanged)> NewFrameCb;

    /** Connect to requested MPEG4 URL. */
    Mpeg4Receiver(_bstr_t url, NewFrameCb frame_cb, bool enableAsyncProcessing = true);

    ~Mpeg4Receiver();

    void Stop();

    /** Receive frames. In async mode, frames are queued for processing. In sync mode, callback is called immediately. */
    HRESULT ReceiveFrame();
    
    /** Get current queue size (for monitoring) */
    size_t GetQueueSize() const {
        return m_asyncQueue ? m_asyncQueue->Size() : 0;
    }
    
    /** Enable/disable async processing */
    void SetAsyncProcessing(bool enable);
    
    /** Check if async processing is enabled */
    bool IsAsyncProcessingEnabled() const {
        return m_asyncProcessingEnabled;
    }

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
    
    /** Async frame processing thread function */
    void AsyncProcessingThread();
    
    /** Process a single frame (called from async thread or directly) */
    void ProcessFrame(const FrameData& frameData);

    CComPtr<IMFSourceReader> m_reader = nullptr;
    uint64_t                 m_startTime = 0;   // SECONDS since midnight, Jan. 1, 1904
    double                   m_dpi = 0;         // pixel spacing
    double                   m_xform[6] = { 1, 0, 0, 1, 0, 0 }; // initialize with default identity transform
    std::array<uint32_t, 2>  m_resolution; // horizontal & vertical pixel count
    NewFrameCb               m_frame_cb = nullptr;
    bool                     m_metadata_changed = false; // metadata changed since previous frame
    bool                     m_active = true;
    
    // Async processing components
    bool                     m_asyncProcessingEnabled = true;
    std::unique_ptr<AsyncFrameQueue> m_asyncQueue;
    std::unique_ptr<std::thread> m_processingThread;
    std::atomic<bool>        m_stopProcessing{false};
};
