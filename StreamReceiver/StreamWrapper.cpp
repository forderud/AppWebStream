#define WIN32_LEAN_AND_MEAN
#include <tuple>
#include <comdef.h> // for _com_error
#include <mfidl.h>
#include <mfreadwrite.h>
#include <Mfapi.h>
#include <strmif.h>
#include "StreamWrapper.hpp"
#include "Mpeg4Receiver.hpp"
#include "../AppWebStream/ComUtil.hpp"

// define smart-pointers with "Ptr" suffix
_COM_SMARTPTR_TYPEDEF(IMFSourceResolver, __uuidof(IMFSourceResolver));
_COM_SMARTPTR_TYPEDEF(IPropertyStore, __uuidof(IPropertyStore));


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


IMFByteStreamPtr CreateByteStreamFromUrl(_bstr_t url) {
    IMFSourceResolverPtr resolver;
    COM_CHECK(MFCreateSourceResolver(&resolver));

    IPropertyStorePtr props;
    COM_CHECK(PSCreateMemoryPropertyStore(__uuidof(IPropertyStore), (void**)&props));
    {
        // reduce startup latency
        PROPERTYKEY key{};
        key.fmtid = MFNETSOURCE_ACCELERATEDSTREAMINGDURATION;
        key.pid = 0;

        PROPVARIANT val{};
        val.vt = VT_I4;
        val.lVal = 100; // 100 milliseconds (10,000 is default)

        COM_CHECK(props->SetValue(key, val));
        //COM_CHECK(props->Commit());
    }
    {
        // reduce network buffering
        PROPERTYKEY key{};
        key.fmtid = MFNETSOURCE_BUFFERINGTIME;
        key.pid = 0;

        PROPVARIANT val{};
        val.vt = VT_I4;
        val.lVal = 1; // 1 second (5 is default)

        COM_CHECK(props->SetValue(key, val));
        //COM_CHECK(props->Commit());
    }
    {
        // reduce max buffering
        PROPERTYKEY key{};
        key.fmtid = MFNETSOURCE_MAXBUFFERTIMEMS;
        key.pid = 0;

        PROPVARIANT val{};
        val.vt = VT_I4;
        val.lVal = 100; // 100ms (40,000 is default)

        COM_CHECK(props->SetValue(key, val));
        //COM_CHECK(props->Commit());
    }

    // create innerStream that connects to the URL
    DWORD createObjFlags = MF_RESOLUTION_BYTESTREAM; // MF_RESOLUTION_BYTESTREAM for IMFByteStream and MF_RESOLUTION_MEDIASOURCE for IMFMediaSource
    MF_OBJECT_TYPE objectType = MF_OBJECT_INVALID;
    IUnknownPtr obj;
    COM_CHECK(resolver->CreateObjectFromURL(url, createObjFlags, props, &objectType, &obj));
    assert(objectType == MF_OBJECT_BYTESTREAM);
    IMFByteStreamPtr byteStream = obj;
    return byteStream;
}