#define WIN32_LEAN_AND_MEAN
#include <stdexcept>
#include <comdef.h> // for _com_error
#include <Mfapi.h>
#include <ws2tcpip.h>
#include "InputStream.hpp"

#pragma comment (lib, "Ws2_32.lib")


InputStream::InputStream() {
}

InputStream::~InputStream() {
    if (m_sock != INVALID_SOCKET) {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }

    WSACleanup();
}

HRESULT InputStream::Initialize(std::string url) {
    std::string servername;
    std::string port;
    std::string resource;
    {
        // parse URL
        size_t idx1 = url.find("://");
        size_t idx2 = url.find(":", idx1+3);
        size_t idx3 = url.find("/", idx2 + 1);
        servername = url.substr(idx1 + 3, idx2 - idx1 - 3);
        port = url.substr(idx2 + 1, idx3 - idx2 - 1);
        resource = url.substr(idx3);
    }

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
        res = getaddrinfo(servername.c_str(), port.c_str(), &hints, &result);
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

    // request HTTP video
    std::string request = "GET " + resource + " HTTP/1.1\r\n";
    request += "Host: " + servername + "\r\n";
    request += "User-Agent: StreamReceiver\r\n";
    request += "Accept: */*\r\n";
    request += "\r\n";
    res = send(m_sock, request.data(), static_cast<int>(request.size()), 0);
    if (res == SOCKET_ERROR)
        return E_FAIL;

    return S_OK;
}


HRESULT InputStream::GetCapabilities(/*out*/DWORD *capabilities) {
    *capabilities = MFBYTESTREAM_IS_READABLE | MFBYTESTREAM_IS_REMOTE;
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
    *position = m_cur_pos;
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
    int res = recv(m_sock, (char*)pb, cb, 0);
    if (res == SOCKET_ERROR)
        return E_FAIL;

    m_cur_pos += res;
    *bRead = res; // bytes read
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
