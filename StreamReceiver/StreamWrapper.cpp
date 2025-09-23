#define WIN32_LEAN_AND_MEAN
#include <tuple>
#include <comdef.h> // for _com_error
#include <Mfapi.h>
#include "StreamWrapper.hpp"
#include "Mpeg4ReceiverSR.hpp"


StreamWrapper::StreamWrapper() {
}

StreamWrapper::~StreamWrapper() {
}

void StreamWrapper::Initialize(IMFByteStream* socket, StartTimeDpiChangedCb notifier) {
    m_socket = socket;
    m_notifier = notifier;
}

HRESULT StreamWrapper::GetCapabilities(/*out*/DWORD *capabilities) {
    return m_socket->GetCapabilities(capabilities);
}

HRESULT StreamWrapper::GetLength(/*out*/QWORD* length) {
    return m_socket->GetLength(length);
}

HRESULT StreamWrapper::SetLength(/*in*/QWORD length) {
    return m_socket->SetLength(length);
}

HRESULT StreamWrapper::GetCurrentPosition(/*out*/QWORD* position) {
    return m_socket->GetCurrentPosition(position);
}

HRESULT StreamWrapper::SetCurrentPosition(/*in*/QWORD position) {
    return m_socket->SetCurrentPosition(position);
}

HRESULT StreamWrapper::IsEndOfStream(/*out*/BOOL* endOfStream) {
    return m_socket->IsEndOfStream(endOfStream);
}

HRESULT StreamWrapper::Read(/*out*/BYTE* pb, /*in*/ULONG cb, /*out*/ULONG* bRead) {
    return m_socket->Read(pb, cb, bRead);
}

HRESULT StreamWrapper::BeginRead(/*out*/BYTE* pb, /*in*/ULONG cb, /*in*/IMFAsyncCallback* callback, /*in*/IUnknown* unkState) {
    HRESULT hr = m_socket->BeginRead(pb, cb, callback, unkState);
    m_read_buf = std::string_view((char*)pb, cb);
    return hr;
}

HRESULT StreamWrapper::EndRead(/*in*/IMFAsyncResult* result, /*out*/ULONG* cbRead) {
    HRESULT hr = m_socket->EndRead(result, cbRead);
    if (SUCCEEDED(hr)) {
        // inspect MPEG4 bitstream
        bool updated = m_stream_editor.ParseStream(m_read_buf.substr(0, *cbRead));
        if (m_notifier && updated) {
            double xform[6]{};
            m_stream_editor.GetXform(xform);
            m_notifier(m_stream_editor.GetStartTime(), m_stream_editor.GetDPI(), xform);
        }
    }
    return hr;
}

HRESULT StreamWrapper::Write(/*in*/const BYTE* pb, /*in*/ULONG cb, /*out*/ULONG* cbWritten) {
    return m_socket->Write(pb, cb, cbWritten);
}

HRESULT StreamWrapper::BeginWrite(/*in*/const BYTE* pb, /*in*/ULONG cb, /*in*/IMFAsyncCallback* callback, /*in*/IUnknown* unkState) {
    return m_socket->BeginWrite(pb, cb, callback, unkState);
}

HRESULT StreamWrapper::EndWrite(/*in*/IMFAsyncResult* result, /*out*/ULONG* cbWritten) {
    return m_socket->EndWrite(result, cbWritten);
}

HRESULT StreamWrapper::Seek(/*in*/MFBYTESTREAM_SEEK_ORIGIN SeekOrigin, /*in*/LONGLONG SeekOffset,/*in*/DWORD SeekFlags, /*out*/QWORD* CurrentPosition) {
    return m_socket->Seek(SeekOrigin, SeekOffset, SeekFlags, CurrentPosition);
}

HRESULT StreamWrapper::Flush() {
    return m_socket->Flush();
}

HRESULT StreamWrapper::Close() {
    return m_socket->Close();
}
