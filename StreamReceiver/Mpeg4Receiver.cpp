#pragma once
#include "Mpeg4ReceiverME.hpp"
#include "Mpeg4ReceiverSR.hpp"


std::unique_ptr<Mpeg4Receiver> Mpeg4Receiver::Create(_bstr_t url, NewFrameCb frame_cb) {
#if 0
    return std::make_unique<Mpeg4ReceiverME>(url, frame_cb);
#else
    return std::make_unique<Mpeg4ReceiverSR>(url, frame_cb);
#endif
}
