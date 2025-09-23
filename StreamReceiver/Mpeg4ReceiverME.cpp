#pragma once
#include <mfmediaengine.h>
#include "Mpeg4ReceiverME.hpp"


/** Connect to requested MPEG4 URL. */
Mpeg4ReceiverME::Mpeg4ReceiverME(_bstr_t url, NewFrameCb frame_cb) :Mpeg4Receiver(frame_cb) {

}

Mpeg4ReceiverME::~Mpeg4ReceiverME() {

}

void Mpeg4ReceiverME::Stop() {

}

/** Receive frames. The "frame_cb" callback will be called from the same thread when new frames are received. */
HRESULT Mpeg4ReceiverME::ReceiveFrame() {
    return E_NOTIMPL;
}
