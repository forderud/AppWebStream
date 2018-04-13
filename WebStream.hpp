#pragma once
#include <atlbase.h>
#include <atlcom.h>
#include <iostream>
#include <fstream>
#include <mutex>
#include <MFidl.h>
#include <Mfreadwrite.h>
#include "Resource.h"

class ATL_NO_VTABLE WebStream :
    public CComObjectRootEx<CComMultiThreadModel>,
    public CComCoClass<WebStream>,
    public IMFByteStream {
public:
    WebStream();
    /*NOT virtual*/ ~WebStream();

    void SetPortAndWindowHandle(const char * port_str, HWND wnd);

    HRESULT STDMETHODCALLTYPE GetCapabilities(/*out*/DWORD *capabilities) override;

    HRESULT STDMETHODCALLTYPE GetLength(/*out*/QWORD* length) override;

    HRESULT STDMETHODCALLTYPE SetLength(/*in*/QWORD length) override;

    HRESULT STDMETHODCALLTYPE GetCurrentPosition(/*out*/QWORD* position) override;

    HRESULT STDMETHODCALLTYPE SetCurrentPosition(/*in*/QWORD position) override;

    HRESULT STDMETHODCALLTYPE IsEndOfStream(/*out*/BOOL* endOfStream) override;

    HRESULT STDMETHODCALLTYPE Read(/*out*/BYTE* pb, /*in*/ULONG cb, /*out*/ULONG* bRead) override;

    HRESULT STDMETHODCALLTYPE BeginRead(/*out*/BYTE* pb, /*in*/ULONG cb, /*in*/IMFAsyncCallback* callback, /*in*/IUnknown* unkState) override;

    HRESULT STDMETHODCALLTYPE EndRead(/*in*/IMFAsyncResult* result, /*out*/ULONG* cbRead) override;

    HRESULT STDMETHODCALLTYPE Write(/*in*/const BYTE* pb, /*in*/ULONG cb, /*out*/ULONG* cbWritten) override;

    HRESULT STDMETHODCALLTYPE BeginWrite(/*in*/const BYTE* pb, /*in*/ULONG cb, /*in*/IMFAsyncCallback* callback, /*in*/IUnknown* unkState) override;

    HRESULT STDMETHODCALLTYPE EndWrite(/*in*/IMFAsyncResult* result, /*out*/ULONG* cbWritten) override;

    HRESULT STDMETHODCALLTYPE Seek(/*in*/MFBYTESTREAM_SEEK_ORIGIN SeekOrigin, /*in*/LONGLONG SeekOffset,/*in*/DWORD SeekFlags, /*out*/QWORD* CurrentPosition) override;

    HRESULT STDMETHODCALLTYPE Flush() override;

    HRESULT STDMETHODCALLTYPE Close() override;

    BEGIN_COM_MAP(WebStream)
        COM_INTERFACE_ENTRY(IMFByteStream)
    END_COM_MAP()

private:
    HRESULT WriteImpl(/*in*/const BYTE* pb, /*in*/ULONG cb);

    mutable std::mutex m_mutex;
    unsigned long  m_tmp_bytes_written = 0;

    struct impl;
    std::unique_ptr<impl> m_impl;
};
