#pragma once
#include <functional>
#include <memory>
#include <atlbase.h>
#include <atlcom.h>
#include <MFidl.h>
#include <Mfreadwrite.h>
#include "../AppWebStream/MP4StreamEditor.hpp"

_COM_SMARTPTR_TYPEDEF(IMFByteStream, __uuidof(IMFByteStream));

typedef std::function<void(uint64_t startTime, double dpi, double xform[6])> StartTimeDpiChangedCb;


/** IMFByteStream wrapper to allow parsing of the underlying MPEG4 bitstream.
    Used to access CreationTime & DPI parameters that doesn't seem to be exposed through the MediaFoundation API. */
class ATL_NO_VTABLE StreamWrapper :
    public CComObjectRootEx<CComMultiThreadModel>,
    public CComCoClass<StreamWrapper>,
    public IMFByteStream {
public:
    StreamWrapper();
    /*NOT virtual*/ ~StreamWrapper();

    void Initialize(IMFByteStream * socket, StartTimeDpiChangedCb notifier);

    HRESULT GetCapabilities(/*out*/DWORD *capabilities) override;

    HRESULT GetLength(/*out*/QWORD* length) override;

    HRESULT SetLength(/*in*/QWORD length) override;

    HRESULT GetCurrentPosition(/*out*/QWORD* position) override;

    HRESULT SetCurrentPosition(/*in*/QWORD position) override;

    HRESULT IsEndOfStream(/*out*/BOOL* endOfStream) override;

    HRESULT Read(/*out*/BYTE* pb, /*in*/ULONG cb, /*out*/ULONG* bRead) override;

    HRESULT BeginRead(/*out*/BYTE* pb, /*in*/ULONG cb, /*in*/IMFAsyncCallback* callback, /*in*/IUnknown* unkState) override;

    HRESULT EndRead(/*in*/IMFAsyncResult* result, /*out*/ULONG* cbRead) override;

    HRESULT Write(/*in*/const BYTE* pb, /*in*/ULONG cb, /*out*/ULONG* cbWritten) override;

    HRESULT BeginWrite(/*in*/const BYTE* pb, /*in*/ULONG cb, /*in*/IMFAsyncCallback* callback, /*in*/IUnknown* unkState) override;

    HRESULT EndWrite(/*in*/IMFAsyncResult* result, /*out*/ULONG* cbWritten) override;

    HRESULT Seek(/*in*/MFBYTESTREAM_SEEK_ORIGIN SeekOrigin, /*in*/LONGLONG SeekOffset,/*in*/DWORD SeekFlags, /*out*/QWORD* CurrentPosition) override;

    HRESULT Flush() override;

    HRESULT Close() override;

    BEGIN_COM_MAP(StreamWrapper)
        COM_INTERFACE_ENTRY(IMFByteStream)
    END_COM_MAP()

private:
    IMFByteStreamPtr      m_socket;   // network socket stream to intercept
    MP4StreamEditor       m_stream_editor;
    std::string_view      m_read_buf; // set by BeginRead
    StartTimeDpiChangedCb m_notifier;
};

IMFByteStreamPtr CreateByteStreamFromUrl(_bstr_t url);
