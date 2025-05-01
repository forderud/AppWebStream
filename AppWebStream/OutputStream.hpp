#pragma once
#include <atlbase.h>
#include <atlcom.h>
#include <iostream>
#include <fstream>
#include <mutex>
#include <string_view>
#include <MFidl.h>
#include <Mfreadwrite.h>
#include "Resource.h"
#include "MP4FragmentEditor.hpp"


class ByteWriter {
public:
    virtual ~ByteWriter() = default;
    virtual int WriteBytes(const std::string_view buffer) = 0;
    virtual void Flush() = 0;
};


class ATL_NO_VTABLE OutputStream :
    public CComObjectRootEx<CComMultiThreadModel>,
    public CComCoClass<OutputStream>,
    public IMFByteStream {
public:
    OutputStream();
    /*NOT virtual*/ ~OutputStream();

    void SetPortOrFilename(const char * port_or_filename);

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

    BEGIN_COM_MAP(OutputStream)
        COM_INTERFACE_ENTRY(IMFByteStream)
    END_COM_MAP()

private:
    HRESULT WriteImpl(std::string_view buffer);

    mutable std::mutex m_mutex;
    unsigned long      m_tmp_bytes_written = 0;

    std::unique_ptr<ByteWriter> m_writer;

    unsigned __int64      m_cur_pos = 0;
    MP4FragmentEditor     m_stream_editor;
};
