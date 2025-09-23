#pragma once
#include "Mpeg4ReceiverSR.hpp"


std::unique_ptr<Mpeg4Receiver> Mpeg4Receiver::Create(_bstr_t url, NewFrameCb frame_cb) {
    return std::make_unique<Mpeg4ReceiverSR>(url, frame_cb);
}
