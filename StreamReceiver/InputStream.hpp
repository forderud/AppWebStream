#pragma once
#include <atlbase.h>
#include <atlcom.h>
#include <MFidl.h>
#include <Mfreadwrite.h>


class ATL_NO_VTABLE InputStream :
    public CComObjectRootEx<CComMultiThreadModel>,
    public CComCoClass<InputStream>,
    public IMFByteStream {
public:
    InputStream();
    /*NOT virtual*/ ~InputStream();

    HRESULT Initialize(char* hostName, char* port);

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

    BEGIN_COM_MAP(InputStream)
        COM_INTERFACE_ENTRY(IMFByteStream)
    END_COM_MAP()

private:
};
