#pragma once
#include <atlbase.h>
#include <atlcom.h>
#include <MFidl.h>
#include <Mfreadwrite.h>


class ATL_NO_VTABLE InputStream2 :
    public CComObjectRootEx<CComMultiThreadModel>,
    public CComCoClass<InputStream2>,
    public IStream {
public:
    InputStream2();
    /*NOT virtual*/ ~InputStream2();

    // IStream interface
    HRESULT Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, /*out*/ULARGE_INTEGER* plibNewPosition) override;

    HRESULT SetSize(ULARGE_INTEGER libNewSize) override;

    HRESULT CopyTo(/*in*/IStream* pstm, ULARGE_INTEGER cb, /*out*/ULARGE_INTEGER* pcbRead, /*out*/ULARGE_INTEGER* pcbWritten) override;

    HRESULT Commit(DWORD grfCommitFlags) override;

    HRESULT Revert() override;

    HRESULT LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override;

    HRESULT UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override;

    HRESULT Stat(/*out*/STATSTG* pstatstg, DWORD grfStatFlag) override;

    HRESULT Clone(/*out*/IStream** ppstm) override;

    // ISequentialStream  interface
    HRESULT Read(/*out*/void* pv, ULONG cb, /*our*/ULONG* pcbRead) override;

    HRESULT Write(/*in*/const void* pv, ULONG cb, /*out*/ULONG* pcbWritten) override;


    BEGIN_COM_MAP(InputStream2)
        COM_INTERFACE_ENTRY(IStream)
    END_COM_MAP()

private:
};
