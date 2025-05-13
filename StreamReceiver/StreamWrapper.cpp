#define WIN32_LEAN_AND_MEAN
#include <stdexcept>
#include <tuple>
#include <comdef.h> // for _com_error
#include <Mfapi.h>
#include "StreamWrapper.hpp"


StreamWrapper::StreamWrapper() {
}

StreamWrapper::~StreamWrapper() {
}

void StreamWrapper::Initialize(IMFByteStream* obj) {
    m_obj = obj;
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
    m_read_buf = (char*)pb;
    return m_obj->BeginRead(pb, cb, callback, unkState);
}

HRESULT StreamWrapper::EndRead(/*in*/IMFAsyncResult* result, /*out*/ULONG* cbRead) {
    HRESULT hr = m_obj->EndRead(result, cbRead);
    // Inspect m_read_buf bitstream
    bool updated = m_stream_editor.ParseStream(std::string_view(m_read_buf, *cbRead));
    if (updated) {
        wprintf(L"Frame DPI: %f\n", m_stream_editor.GetDPI());
        wprintf(L"Start time: %hs (UTC)\n", TimeString1904(m_stream_editor.GetStartTime()).c_str());
    }
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
