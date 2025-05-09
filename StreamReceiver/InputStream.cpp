#define WIN32_LEAN_AND_MEAN
#include <stdexcept>
#include <tuple>
#include <comdef.h> // for _com_error
#include <Mfapi.h>
#include "InputStream.hpp"
#include "ClientSocket.hpp"


InputStream::InputStream() {
}

InputStream::~InputStream() {
}

HRESULT InputStream::Initialize(std::string url) {
    auto [servername, port, resource] = ParseURL(url);
    m_socket = std::make_unique<ClientSocket>(servername.c_str(), port.c_str());

    // request HTTP video
    m_socket->WriteHttpGet(resource);

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
