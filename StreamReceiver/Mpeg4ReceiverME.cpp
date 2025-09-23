#pragma once
#include <mfapi.h>
#include <mfmediaengine.h>
#include "Mpeg4ReceiverME.hpp"


/** Connect to requested MPEG4 URL. */
Mpeg4ReceiverME::Mpeg4ReceiverME(_bstr_t url, NewFrameCb frame_cb) :Mpeg4Receiver(frame_cb) {
    CComPtr<IMFMediaEngineClassFactory> factory;
    HRESULT hr = factory.CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER);
    assert(SUCCEEDED(hr));

    CComPtr<IMFAttributes> attribs;
    hr = MFCreateAttributes(&attribs, 0);
    assert(SUCCEEDED(hr));
    // TODO: Investigate the following attributes from https://github.com/chromium/chromium/blob/main/media/renderers/win/media_foundation_renderer.cc
    // MF_MEDIA_ENGINE_CALLBACK
    // MF_MEDIA_ENGINE_AUDIO_CATEGORY
    // MF_MEDIA_ENGINE_OPM_HWND
    // MF_MEDIA_ENGINE_DXGI_MANAGER
    // MF_MEDIA_ENGINE_EXTENSION

    hr = factory->CreateInstance(MF_MEDIA_ENGINE_REAL_TIME_MODE, attribs, &m_engine);
    assert(SUCCEEDED(hr));
}

Mpeg4ReceiverME::~Mpeg4ReceiverME() {

}

void Mpeg4ReceiverME::Stop() {

}

/** Receive frames. The "frame_cb" callback will be called from the same thread when new frames are received. */
HRESULT Mpeg4ReceiverME::ReceiveFrame() {
    return E_NOTIMPL;
}
