#pragma once
#include <stdexcept>
#include <mfapi.h>
#include <mfmediaengine.h>
#include "Mpeg4ReceiverME.hpp"


struct MediaEngineNotify : public IMFMediaEngineNotify {
    MediaEngineNotify() = default;
    ~MediaEngineNotify() = default;

    HRESULT EventNotify(DWORD event, uint64_t /*param1*/, DWORD /*param2*/) override {
        auto type = (MF_MEDIA_ENGINE_EVENT)event;
        type;
        return E_NOTIMPL;
    }

    HRESULT QueryInterface(const IID& riid, void** ppvObj) override {
        if (!ppvObj)
            return E_INVALIDARG;

        *ppvObj = NULL;
        if (riid == IID_IUnknown || riid == IID_IMFMediaEngineNotify) {
            // Increment the reference count and return the pointer.
            *ppvObj = (void*)this;
            AddRef();
            return NOERROR;
        }
        return E_NOINTERFACE;
    }

    ULONG AddRef() override {
        return ++m_ref_cnt;
    }

    ULONG Release() override {
        ULONG ref_cnt = --m_ref_cnt;
        if (!ref_cnt)
            delete this;

        return ref_cnt;
    }


private:
    unsigned int m_ref_cnt = 0;
};


/** Connect to requested MPEG4 URL. */
Mpeg4ReceiverME::Mpeg4ReceiverME(_bstr_t url, NewFrameCb frame_cb) :Mpeg4Receiver(frame_cb) {
    m_frame_cb = new MediaEngineNotify;

    CComPtr<IMFMediaEngineClassFactory> factory;
    HRESULT hr = factory.CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER);
    assert(SUCCEEDED(hr));

    CComPtr<IMFAttributes> attribs;
    {
        hr = MFCreateAttributes(&attribs, 0);
        assert(SUCCEEDED(hr));

        hr = attribs->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, m_frame_cb);
        assert(SUCCEEDED(hr));

        // TODO: Investigate the following attributes from https://github.com/chromium/chromium/blob/main/media/renderers/win/media_foundation_renderer.cc
        // MF_MEDIA_ENGINE_AUDIO_CATEGORY
        // MF_MEDIA_ENGINE_OPM_HWND
        // MF_MEDIA_ENGINE_DXGI_MANAGER
        // MF_MEDIA_ENGINE_EXTENSION
    }

    hr = factory->CreateInstance(MF_MEDIA_ENGINE_REAL_TIME_MODE, attribs, &m_engine);
    assert(SUCCEEDED(hr));

    //m_engine->SetDefaultPlaybackRate(0.0);

    hr = m_engine->SetSource(url);
    if (FAILED(hr))
        throw std::runtime_error("SetSource failed");

    DWORD width = 0, height = 0;
    hr = m_engine->GetNativeVideoSize(&width, &height);
    if (FAILED(hr))
        throw std::runtime_error("GetNativeVideoSize failed");

    //m_engine->SetCurrentTime(current_time);

    hr = m_engine->Play();
    if (FAILED(hr))
        throw std::runtime_error("Play failed");
}

Mpeg4ReceiverME::~Mpeg4ReceiverME() {
    m_engine->Shutdown();

}

void Mpeg4ReceiverME::Stop() {

}

/** Receive frames. The "frame_cb" callback will be called from the same thread when new frames are received. */
HRESULT Mpeg4ReceiverME::ReceiveFrame() {
    return E_NOTIMPL;
}
