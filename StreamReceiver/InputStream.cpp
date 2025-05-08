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

HRESULT InputStream::Initialize(char* hostname, char* port) {
    WSADATA wsaData = {};
    int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (res)
        throw std::runtime_error("WSAStartup failure");

    ADDRINFOA hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port
    ADDRINFOA* result = nullptr;
    res = getaddrinfo(hostname, port, &hints, &result);
    if (res != 0) {
        printf("getaddrinfo failed: %d\n", res);
        WSACleanup();
        return E_FAIL;;
    }

    freeaddrinfo(result);

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
