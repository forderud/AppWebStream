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
    WSACleanup();
}

HRESULT InputStream::Initialize(char* servername, char* port) {
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

    return S_OK;
}


HRESULT InputStream::GetCapabilities(/*out*/DWORD *capabilities) {
    *capabilities = MFBYTESTREAM_IS_READABLE | MFBYTESTREAM_IS_REMOTE;
    return S_OK;
}

HRESULT InputStream::GetLength(/*out*/QWORD* /*length*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::SetLength(/*in*/QWORD /*length*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::GetCurrentPosition(/*out*/QWORD* /*position*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::SetCurrentPosition(/*in*/QWORD /*position*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::IsEndOfStream(/*out*/BOOL* /*endOfStream*/) {
    return E_NOTIMPL;
}

HRESULT InputStream::Read(/*out*/BYTE* /*pb*/, /*in*/ULONG /*cb*/, /*out*/ULONG* /*bRead*/) {
    return E_NOTIMPL;
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
