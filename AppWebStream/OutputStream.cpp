#define WIN32_LEAN_AND_MEAN
#include <atomic>
#include <comdef.h> // for _com_error
#include <Mfapi.h>
#include "OutputStream.hpp"
#include "WebSocket.hpp"


class WebStream : public ByteWriter, StreamSockSetter {
public:
    WebStream(const char * port_str) : m_server(port_str), m_block_ctor(true) {
        // start server thread
        m_thread = std::thread(&WebStream::WaitForClientsThread, this);

        // wait for video request or socket failure
        std::mutex mutex;
        std::unique_lock<std::mutex> lock(mutex);
        while (m_block_ctor)
            m_cond_var.wait(lock);

        // start streaming video
    }

    void WaitForClientsThread() {
        SetThreadDescription(GetCurrentThread(), L"WaitForClientsThread");

        for (;;) {
            auto current = m_server.WaitForClient();
            if (!current)
                break;

            // create a thread to the handle client connection
            current->Start(this);
            m_clients.push_back(std::move(current));
        }

        Unblock();
    }

    void SetStreamSocket(ClientSock & s) override {
        for (size_t i = 0; i < m_clients.size(); ++i) {
            if (m_clients[i].get() == &s) {
                m_stream_client = std::move(m_clients[i]);
                return;
            }
        }
    }

    void Unblock() override {
        m_block_ctor = false;
        m_cond_var.notify_all();
    }

    ~WebStream() override {
        // close open sockets (forces blocking calls to complete)
        m_server  = ServerSock();
        m_thread.join();

        m_stream_client.reset();

        // wait for client threads to complete
        for (auto & c : m_clients) {
            if (c)
                c.reset();
        }
    }

    int WriteBytes(const std::string_view buffer) override {
        // transmit data over socket
        int byte_count = send(m_stream_client->Socket(), buffer.data(), (int)buffer.size(), 0);
        if (byte_count == SOCKET_ERROR) {
            // WSAECONNABORTED expected on client disconnect
            int err = WSAGetLastError();
            _com_error err_str(err);
            wprintf(L"Socket send error %u: %s\n", err, err_str.ErrorMessage());
            assert((err == WSAECONNABORTED) || (err == WSAECONNRESET));

            // destroy failing client socket (typ. caused by client-side closing)
            m_stream_client.reset();
            return -1;
        }

#ifndef _NDEBUG
        printf("."); // log "x" to signal that a TCP packet have been transmitted
#endif
        return byte_count;
    }

    void Flush() override {
    }

private:
    ServerSock              m_server;  ///< listens for new connections
    std::unique_ptr<ClientSock> m_stream_client;  ///< video streaming socket
    std::atomic<bool>       m_block_ctor;
    std::condition_variable m_cond_var;
    std::thread             m_thread;
    std::vector<std::unique_ptr<ClientSock>> m_clients; // heap allocated objects to ensure that they never change addreess
};


class FileStream : public ByteWriter {
public:
    FileStream(const char* filename) : m_file(filename, std::ofstream::binary) {
    }

    ~FileStream() override {
    }

    int WriteBytes(const std::string_view buffer) override {
        m_file.write(buffer.data(), buffer.size());
        return (int)buffer.size();
    }

    void Flush() override {
        m_file.flush();
    }

private:
    std::ofstream m_file;
};


OutputStream::OutputStream() {
}

OutputStream::~OutputStream() {
}


void OutputStream::Initialize(FILETIME startTime) {
    uint64_t mpegStartTime = WindowsTimeToMpeg4Time(startTime); // rounds down to nearest second
    m_startTime = Mpeg4TimeToWindowsTime(mpegStartTime);
    m_stream_editor = std::make_unique<MP4StreamEditor>(mpegStartTime);
}


void OutputStream::SetPortOrFilename(const char * port_or_filename) {
    if (atoi(port_or_filename)) {
        printf("Please open http://localhost:%s/ in a web browser or directly open the MPEG4 stream on http://localhost:%s/movie.mp4\n", port_or_filename, port_or_filename);
        printf("\n");
        m_writer = std::make_unique<WebStream>(port_or_filename);
    } else {
        printf("Storing movie to file %s\n", port_or_filename);
        printf("\n");
        m_writer = std::make_unique<FileStream>(port_or_filename);
    }
}

void OutputStream::SetNextFrameTime(FILETIME timeStamp) {
    // compute 100-nanosecond intervals since startTime
    uint64_t duration = FileTimeToU64(timeStamp) - FileTimeToU64(m_startTime);

    uint32_t timeScale = m_stream_editor->GetTimeScale();

    uint64_t mpegTime = (duration * timeScale)/FILETIME_PER_SECONDS;
    m_stream_editor->SetNextFrameTime(mpegTime);
}

double OutputStream::SetNextFrameDPI(double dpi) {
    double prevDpi = m_stream_editor->GetDPI();
    m_stream_editor->SetDPI(dpi);
    return prevDpi;
}

void OutputStream::SetXform(const double xform[6]) {
    m_stream_editor->SetXform(xform);
}

HRESULT OutputStream::GetCapabilities(/*out*/DWORD *capabilities) {
    *capabilities = MFBYTESTREAM_IS_WRITABLE | MFBYTESTREAM_IS_REMOTE;
    return S_OK;
}

HRESULT OutputStream::GetLength(/*out*/QWORD* /*length*/) {
    return E_NOTIMPL;
}

HRESULT OutputStream::SetLength(/*in*/QWORD /*length*/) {
    return E_NOTIMPL;
}

HRESULT OutputStream::GetCurrentPosition(/*out*/QWORD* position) {
    *position = m_cur_pos;
    return S_OK;
}

HRESULT OutputStream::SetCurrentPosition(/*in*/QWORD /*position*/) {
    return E_NOTIMPL;
}

HRESULT OutputStream::IsEndOfStream(/*out*/BOOL* /*endOfStream*/) {
    return E_NOTIMPL;
}

HRESULT OutputStream::Read(/*out*/BYTE* /*pb*/, /*in*/ULONG /*cb*/, /*out*/ULONG* /*bRead*/) {
    return E_NOTIMPL;
}

HRESULT OutputStream::BeginRead(/*out*/BYTE* /*pb*/, /*in*/ULONG /*cb*/, /*in*/IMFAsyncCallback* /*callback*/, /*in*/IUnknown* /*unkState*/) {
    return E_NOTIMPL;
}

HRESULT OutputStream::EndRead(/*in*/IMFAsyncResult* /*result*/, /*out*/ULONG* /*cbRead*/) {
    return E_NOTIMPL;
}

HRESULT OutputStream::WriteImpl(std::string_view buffer) {
    buffer = m_stream_editor->EditStream(buffer);

    int byte_count = m_writer->WriteBytes(buffer);
    if (byte_count < 0)
        return E_FAIL;

    m_cur_pos += byte_count;
    return S_OK;
}

HRESULT OutputStream::Write(/*in*/const BYTE* pb, /*in*/ULONG cb, /*out*/ULONG* cbWritten) {
    std::lock_guard<std::mutex> lock(m_mutex);

     HRESULT hr = WriteImpl(std::string_view((char*)pb, cb));
     if (FAILED(hr))
         return E_FAIL;
     
    *cbWritten = cb;
    return S_OK;
}

HRESULT OutputStream::BeginWrite(/*in*/const BYTE* pb, /*in*/ULONG cb, /*in*/IMFAsyncCallback* callback, /*in*/IUnknown* unkState) {
    m_mutex.lock();

     HRESULT hr = WriteImpl(std::string_view((char*)pb, cb));
     if (FAILED(hr)) {
         m_mutex.unlock();
         return E_FAIL;
     }

     m_tmp_bytes_written = cb;

    CComPtr<IMFAsyncResult> async_res;
#ifndef ENABLE_FFMPEG
    if (FAILED(MFCreateAsyncResult(nullptr, callback, unkState, &async_res)))
        throw std::runtime_error("MFCreateAsyncResult failed");
#else
    (void)unkState; // mute unreferenced variable warning
#endif
    
    hr = callback->Invoke(async_res); // will trigger EndWrite
    return hr;
}

HRESULT OutputStream::EndWrite(/*in*/IMFAsyncResult* /*result*/, /*out*/ULONG* cbWritten) {
    *cbWritten = m_tmp_bytes_written;
    m_tmp_bytes_written = 0;

    m_mutex.unlock();
    return S_OK;
}

HRESULT OutputStream::Seek(/*in*/MFBYTESTREAM_SEEK_ORIGIN /*SeekOrigin*/, /*in*/LONGLONG /*SeekOffset*/,/*in*/DWORD /*SeekFlags*/, /*out*/QWORD* /*CurrentPosition*/) {
    return E_NOTIMPL;
}

HRESULT OutputStream::Flush() {
    m_writer->Flush();
    return S_OK;
}

HRESULT OutputStream::Close() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_writer.reset();
    return S_OK;
}
