#pragma once
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include "../AppWebStream/ComUtil.hpp"
#include "StreamWrapper.hpp"

// define smart-pointers with "Ptr" suffix
_COM_SMARTPTR_TYPEDEF(IMFAttributes, __uuidof(IMFAttributes));
_COM_SMARTPTR_TYPEDEF(IMFSourceReader, __uuidof(IMFSourceReader));
_COM_SMARTPTR_TYPEDEF(IMFMediaType, __uuidof(IMFMediaType));
_COM_SMARTPTR_TYPEDEF(IMFSample, __uuidof(IMFSample));
_COM_SMARTPTR_TYPEDEF(IMFByteStream, __uuidof(IMFByteStream));
_COM_SMARTPTR_TYPEDEF(IMFSourceResolver, __uuidof(IMFSourceResolver));
_COM_SMARTPTR_TYPEDEF(IMFMediaBuffer, __uuidof(IMFMediaBuffer));


class Mpeg4Receiver : public IStartTimeDPIReceiver {
public:
    Mpeg4Receiver(_bstr_t url);

    ~Mpeg4Receiver() override;

    void ReceiveFrames();

private:
    void OnStartTimeDpiChanged(uint64_t startTime, double dpi) override;

    IMFSourceReaderPtr m_reader;
};
