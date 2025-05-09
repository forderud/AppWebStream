#define WIN32_LEAN_AND_MEAN
#include <stdexcept>
#include <comdef.h> // for _com_error
#include "InputStream2.hpp"
#include "ClientSocket.hpp"


InputStream2::InputStream2() {
}

InputStream2::~InputStream2() {
}

HRESULT InputStream2::Initialize(std::string url) {
    auto [servername, port, resource] = ParseURL(url);
    m_socket = std::make_unique<ClientSocket>(servername.c_str(), port.c_str());

    // request HTTP video
    m_socket->WriteHttpGet(resource);

    return S_OK;
}

// IStream interface
HRESULT InputStream2::Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, /*out*/ULARGE_INTEGER* plibNewPosition) {
    if (dwOrigin == STREAM_SEEK_SET)
        m_cur_pos = dlibMove.QuadPart;
    else if (dwOrigin == STREAM_SEEK_CUR)
        m_cur_pos += dlibMove.QuadPart;
    assert(dwOrigin != STREAM_SEEK_END);

    // TODO: Figure out how to combine with m_socket->CurPos()
    if (plibNewPosition)
        plibNewPosition->QuadPart = m_cur_pos;
    return S_OK;
}

HRESULT InputStream2::SetSize(ULARGE_INTEGER /*libNewSize*/) {
    return E_NOTIMPL;
}

HRESULT InputStream2::CopyTo(/*in*/IStream* /*pstm*/, ULARGE_INTEGER /*cb*/, /*out*/ULARGE_INTEGER* /*pcbRead*/, /*out*/ULARGE_INTEGER* /*pcbWritten*/) {
    return E_NOTIMPL;
}

HRESULT InputStream2::Commit(DWORD /*grfCommitFlags*/) {
    return E_NOTIMPL;
}

HRESULT InputStream2::Revert() {
    return E_NOTIMPL;
}

HRESULT InputStream2::LockRegion(ULARGE_INTEGER /*libOffset*/, ULARGE_INTEGER /*cb*/, DWORD /*dwLockType*/) {
    return E_NOTIMPL;
}

HRESULT InputStream2::UnlockRegion(ULARGE_INTEGER /*libOffset*/, ULARGE_INTEGER /*cb*/, DWORD /*dwLockType*/) {
    return E_NOTIMPL;
}

HRESULT InputStream2::Stat(/*out*/STATSTG* statstg, DWORD grfStatFlag) {
    const wchar_t name[] = L"StreamReceiver buffer";
    auto* nameBuffer = (wchar_t*)CoTaskMemAlloc(sizeof(name));
    memcpy(nameBuffer, name, sizeof(name));

    assert((grfStatFlag == STATFLAG_DEFAULT) || (grfStatFlag == STATFLAG_NONAME));
    statstg->pwcsName = nameBuffer; // transfer ownership
    statstg->type = STGTY_STREAM;
    statstg->cbSize;
    statstg->mtime;
    statstg->ctime;
    statstg->atime;
    statstg->grfMode;
    statstg->grfLocksSupported;
    statstg->clsid = CLSID_NULL;
    statstg->grfStateBits;
    return S_OK; // can also return STG_E_ACCESSDENIED
}

HRESULT InputStream2::Clone(/*out*/IStream** /*ppstm*/) {
    return E_NOTIMPL;
}

// ISequentialStream  interface
HRESULT InputStream2::Read(/*out*/void* pv, ULONG cb, /*our*/ULONG* pcbRead) {
    uint32_t res = m_socket->Read((BYTE*)pv, cb);
    *pcbRead = res; // bytes read

    printf("Read %u bytes.\n", res);
    return S_OK;
}

HRESULT InputStream2::Write(/*in*/const void* /*pv*/, ULONG /*cb*/, /*out*/ULONG* /*pcbWritten*/) {
    return E_NOTIMPL;
}
