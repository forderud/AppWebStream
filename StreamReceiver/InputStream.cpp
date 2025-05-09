#define WIN32_LEAN_AND_MEAN
#include <stdexcept>
#include <tuple>
#include <comdef.h> // for _com_error
#include <Mfapi.h>
#include <ws2tcpip.h>
#include "InputStream.hpp"

#pragma comment (lib, "Ws2_32.lib")

static std::tuple<std::string, std::string, std::string> ParseURL(std::string url) {
    size_t idx1 = url.find("://");
    size_t idx2 = url.find(":", idx1 + 3);
    size_t idx3 = url.find("/", idx2 + 1);

    std::string servername = url.substr(idx1 + 3, idx2 - idx1 - 3);
    std::string port = url.substr(idx2 + 1, idx3 - idx2 - 1);
    std::string resource = url.substr(idx3);
    return std::tie(servername, port, resource);
}


class ClientSocket {
public:
    ClientSocket(const char* servername, const char* port) {
        WSAData wsaData = {};
        int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (res)
            throw std::runtime_error("WSAStartup failure");

        addrinfo* result = nullptr;
        {
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC; // allow both IPv4 & IPv6
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            // resolve server address & port
            res = getaddrinfo(servername, port, &hints, &result);
            if (res != 0)
                throw std::runtime_error("getaddrinfo failed.");
        }

        m_sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (m_sock == INVALID_SOCKET) {
            freeaddrinfo(result);
            throw std::runtime_error("socket failure");
        }

        res = connect(m_sock, result->ai_addr, (int)result->ai_addrlen);
        freeaddrinfo(result);
        if (res == SOCKET_ERROR) {
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
            throw std::runtime_error("connect failure");
        }
    }

    ~ClientSocket() {
        if (m_sock != INVALID_SOCKET) {
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }

        WSACleanup();
    }

    uint32_t Read(/*out*/BYTE* pb, /*in*/ULONG cb) {
        // socket read
        int res = recv(m_sock, (char*)pb, cb, 0);
        if (res == SOCKET_ERROR)
            throw std::runtime_error("recv failure");

        m_cur_pos += res;
        return res;  // bytes read

    }

    uint32_t Write(std::string& message) {
        int res = send(m_sock, message.data(), static_cast<int>(message.size()), 0);
        if (res == SOCKET_ERROR)
            throw std::runtime_error("send failure");
        return res;
    }

    uint64_t CurPos() const {
        return m_cur_pos;
    }

private:
    uint64_t m_cur_pos = 0;
    SOCKET   m_sock = INVALID_SOCKET;
};

InputStream::InputStream() {
}

InputStream::~InputStream() {
}

HRESULT InputStream::Initialize(std::string url) {
    auto [servername, port, resource] = ParseURL(url);

    m_socket = std::make_unique<ClientSocket>(servername.c_str(), port.c_str());

    // request HTTP video
    std::string request = "GET " + resource + " HTTP/1.1\r\n";
    request += "Host: " + servername + "\r\n";
    request += "User-Agent: StreamReceiver\r\n";
    request += "Accept: */*\r\n";
    request += "\r\n";
    m_socket->Write(request);

    return S_OK;
}


HRESULT InputStream::GetCapabilities(/*out*/DWORD *capabilities) {
    *capabilities = MFBYTESTREAM_IS_READABLE | MFBYTESTREAM_IS_REMOTE | MFBYTESTREAM_IS_PARTIALLY_DOWNLOADED;
    return S_OK;
}

HRESULT InputStream::GetLength(/*out*/QWORD* length) {
    *length = (QWORD)(-1); // unknown
    return S_OK;
}

HRESULT InputStream::SetLength(/*in*/QWORD /*length*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::GetCurrentPosition(/*out*/QWORD* position) {
    *position = m_socket->CurPos();
    return S_OK;
}

HRESULT InputStream::SetCurrentPosition(/*in*/QWORD /*position*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::IsEndOfStream(/*out*/BOOL* /*endOfStream*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::Read(/*out*/BYTE* pb, /*in*/ULONG cb, /*out*/ULONG* bRead) {
    // socket read
    uint32_t res = m_socket->Read(pb, cb);
    if (res == SOCKET_ERROR)
        return E_FAIL;

    *bRead = res;
    return S_OK;
}

HRESULT InputStream::BeginRead(/*out*/BYTE* /*pb*/, /*in*/ULONG /*cb*/, /*in*/IMFAsyncCallback* /*callback*/, /*in*/IUnknown* /*unkState*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::EndRead(/*in*/IMFAsyncResult* /*result*/, /*out*/ULONG* /*cbRead*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::Write(/*in*/const BYTE* /*pb*/, /*in*/ULONG /*cb*/, /*out*/ULONG* /*cbWritten*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::BeginWrite(/*in*/const BYTE* /*pb*/, /*in*/ULONG /*cb*/, /*in*/IMFAsyncCallback* /*callback*/, /*in*/IUnknown* /*unkState*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::EndWrite(/*in*/IMFAsyncResult* /*result*/, /*out*/ULONG* /*cbWritten*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::Seek(/*in*/MFBYTESTREAM_SEEK_ORIGIN /*SeekOrigin*/, /*in*/LONGLONG /*SeekOffset*/,/*in*/DWORD /*SeekFlags*/, /*out*/QWORD* /*CurrentPosition*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::Flush() {
    return E_NOTIMPL;
}

HRESULT InputStream::Close() {
    return E_NOTIMPL;
}
