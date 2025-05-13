#define WIN32_LEAN_AND_MEAN
#include <tuple>
#include <comdef.h> // for _com_error
#include <Mfapi.h>
#include "StreamWrapper.hpp"


StreamWrapper::StreamWrapper() {
}

StreamWrapper::~StreamWrapper() {
}

void StreamWrapper::Initialize(IMFByteStream* obj, IStartTimeDPIReceiver* notifier) {
    m_obj = obj;
    m_notifier = notifier;
}


HRESULT StreamWrapper::GetCapabilities(/*out*/DWORD *capabilities) {
    return m_obj->GetCapabilities(capabilities);
}

HRESULT StreamWrapper::GetLength(/*out*/QWORD* length) {
    return m_obj->GetLength(length);
}

HRESULT StreamWrapper::SetLength(/*in*/QWORD length) {
    return m_obj->SetLength(length);
}

HRESULT StreamWrapper::GetCurrentPosition(/*out*/QWORD* position) {
    return m_obj->GetCurrentPosition(position);
}

HRESULT StreamWrapper::SetCurrentPosition(/*in*/QWORD position) {
    return m_obj->SetCurrentPosition(position);
}

HRESULT StreamWrapper::IsEndOfStream(/*out*/BOOL* endOfStream) {
    return m_obj->IsEndOfStream(endOfStream);
}

HRESULT StreamWrapper::Read(/*out*/BYTE* pb, /*in*/ULONG cb, /*out*/ULONG* bRead) {
    return m_obj->Read(pb, cb, bRead);
}

HRESULT StreamWrapper::BeginRead(/*out*/BYTE* pb, /*in*/ULONG cb, /*in*/IMFAsyncCallback* callback, /*in*/IUnknown* unkState) {
    HRESULT hr = m_obj->BeginRead(pb, cb, callback, unkState);
    m_read_buf = std::string_view((char*)pb, cb);
    return hr;
}

HRESULT StreamWrapper::EndRead(/*in*/IMFAsyncResult* result, /*out*/ULONG* cbRead) {
    HRESULT hr = m_obj->EndRead(result, cbRead);

    // inspect MPEG4 bitstream
    bool updated = m_stream_editor.ParseStream(m_read_buf.substr(0, *cbRead));
    if (m_notifier && updated)
        m_notifier->OnStartTimeDpiChanged(m_stream_editor.GetStartTime(), m_stream_editor.GetDPI());

    return hr;
}

HRESULT StreamWrapper::Write(/*in*/const BYTE* pb, /*in*/ULONG cb, /*out*/ULONG* cbWritten) {
    return m_obj->Write(pb, cb, cbWritten);
}

HRESULT StreamWrapper::BeginWrite(/*in*/const BYTE* pb, /*in*/ULONG cb, /*in*/IMFAsyncCallback* callback, /*in*/IUnknown* unkState) {
    return m_obj->BeginWrite(pb, cb, callback, unkState);
}

HRESULT StreamWrapper::EndWrite(/*in*/IMFAsyncResult* result, /*out*/ULONG* cbWritten) {
    return m_obj->EndWrite(result, cbWritten);
}

HRESULT StreamWrapper::Seek(/*in*/MFBYTESTREAM_SEEK_ORIGIN SeekOrigin, /*in*/LONGLONG SeekOffset,/*in*/DWORD SeekFlags, /*out*/QWORD* CurrentPosition) {
    return m_obj->Seek(SeekOrigin, SeekOffset, SeekFlags, CurrentPosition);
}

HRESULT StreamWrapper::Flush() {
    return m_obj->Flush();
}

HRESULT StreamWrapper::Close() {
    return m_obj->Close();
}
