#define WIN32_LEAN_AND_MEAN
#include <atomic>
#include <Mfapi.h>
#include "OutputStream.hpp"
#include "WebSocket.hpp"


class WebStream : public StreamSockSetter {
public:
    WebStream(const char * port_str) : m_server(port_str), m_block_ctor(true) {
        // start server thread
        m_thread = std::thread(&WebStream::WaitForClients, this);

        // wait for video request or socket failure
        std::mutex mutex;
        std::unique_lock<std::mutex> lock(mutex);
        while (m_block_ctor)
            m_cond_var.wait(lock);

        // start streaming video
    }

    void WaitForClients () {
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

    int WriteBytes(/*in*/const BYTE* buf, /*in*/ULONG size) {
        // transmit data over socket
        int byte_count = send(m_stream_client->Socket(), reinterpret_cast<const char*>(buf), size, 0);
        if (byte_count == SOCKET_ERROR) {
            //_com_error error(WSAGetLastError());
            //const TCHAR* msg = error.ErrorMessage();

            // destroy failing client socket (typ. caused by client-side closing)
            m_stream_client.reset();
            return -1;
        }

        return byte_count;
    }

    ServerSock              m_server;  ///< listens for new connections
    std::unique_ptr<ClientSock> m_stream_client;  ///< video streaming socket
    std::atomic<bool>       m_block_ctor;
    std::condition_variable m_cond_var;
    std::thread             m_thread;
    std::vector<std::unique_ptr<ClientSock>> m_clients; // heap allocated objects to ensure that they never change addreess
};


OutputStream::OutputStream() {
}

OutputStream::~OutputStream() {
}

void OutputStream::SetNetworkPort(const char * port_str) {
    m_impl = std::make_unique<WebStream>(port_str);
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

HRESULT OutputStream::WriteImpl(/*in*/const BYTE* buf, ULONG size) {
#ifndef ENABLE_FFMPEG
    std::tie(buf,size) = m_stream_editor.EditStream(buf, size);
#endif

    int byte_count = m_impl->WriteBytes(buf, size);
    if (byte_count < 0)
        return E_FAIL;

    m_cur_pos += byte_count;
    return S_OK;
}

HRESULT OutputStream::Write(/*in*/const BYTE* pb, /*in*/ULONG cb, /*out*/ULONG* cbWritten) {
    std::lock_guard<std::mutex> lock(m_mutex);

     HRESULT hr = WriteImpl(pb, cb);
     if (FAILED(hr))
         return E_FAIL;
     
    *cbWritten = cb;
    return S_OK;
}

HRESULT OutputStream::BeginWrite(/*in*/const BYTE* pb, /*in*/ULONG cb, /*in*/IMFAsyncCallback* callback, /*in*/IUnknown* unkState) {
    m_mutex.lock();

     HRESULT hr = WriteImpl(pb, cb);
     if (FAILED(hr)) {
         m_mutex.unlock();
         return E_FAIL;
     }

     m_tmp_bytes_written = cb;

    CComPtr<IMFAsyncResult> async_res;
#ifndef ENABLE_FFMPEG
    if (FAILED(MFCreateAsyncResult(nullptr, callback, unkState, &async_res)))
        throw std::runtime_error("MFCreateAsyncResult failed");
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
    return S_OK;
}

HRESULT OutputStream::Close() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_impl.reset();
    return S_OK;
}
