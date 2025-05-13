#pragma once
#include <comdef.h> // for __uuidof
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>

// define smart-pointers with "Ptr" suffix
_COM_SMARTPTR_TYPEDEF(IMFAttributes, __uuidof(IMFAttributes));
_COM_SMARTPTR_TYPEDEF(IMFSourceReader, __uuidof(IMFSourceReader));
_COM_SMARTPTR_TYPEDEF(IMFMediaType, __uuidof(IMFMediaType));
_COM_SMARTPTR_TYPEDEF(IMFSample, __uuidof(IMFSample));
_COM_SMARTPTR_TYPEDEF(IMFByteStream, __uuidof(IMFByteStream));
_COM_SMARTPTR_TYPEDEF(IMFSourceResolver, __uuidof(IMFSourceResolver));
_COM_SMARTPTR_TYPEDEF(IMFMediaBuffer, __uuidof(IMFMediaBuffer));


class IStartTimeDPIReceiver {
public:
    virtual ~IStartTimeDPIReceiver() = default;
    virtual void OnStartTimeDpiChanged(uint64_t startTime, double dpi) = 0;
};


class Mpeg4Receiver : public IStartTimeDPIReceiver {
public:
    Mpeg4Receiver(_bstr_t url);

    ~Mpeg4Receiver() override;

    HRESULT ReceiveFrame();

private:
    void ParseFrame(IMFSample& frame);

    void OnStartTimeDpiChanged(uint64_t startTime, double dpi) override;

    IMFSourceReaderPtr m_reader;
    uint64_t           m_startTime = 0; // SECONDS since midnight, Jan. 1, 1904
    double             m_dpi = 0;       // pixel spacing
    uint32_t           m_width = 0;
    uint32_t           m_height = 0;
};
