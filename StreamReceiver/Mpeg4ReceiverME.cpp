#pragma once
#include <stdexcept>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <wincodec.h> // IWICImagingFactory2
#include "../AppWebStream/ComUtil.hpp"
#include "StreamWrapper.hpp"
#include "Mpeg4ReceiverME.hpp"


struct MediaEngineNotify : public IMFMediaEngineNotify {
    MediaEngineNotify(Mpeg4ReceiverME* parent) : m_parent(parent) {
    }

    ~MediaEngineNotify() = default;

    HRESULT EventNotify(DWORD event_, uint64_t param1, DWORD param2) override {
        auto event = (MF_MEDIA_ENGINE_EVENT)event_;

        if (event == MF_MEDIA_ENGINE_EVENT_TIMEUPDATE) {
            assert(!param1);
            assert(!param2);
            m_parent->OnFrameArrived();
        }

        return S_OK;
    }

    HRESULT QueryInterface(const IID& iid, void** ptr) override {
        if (!ptr)
            return E_INVALIDARG;

        *ptr = NULL;
        if (iid == IID_IUnknown) {
            *ptr = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        } else if (iid == IID_IMFMediaEngineNotify) {
            *ptr = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
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
    unsigned int     m_ref_cnt = 0;
    Mpeg4ReceiverME* m_parent = nullptr;
};


/** Connect to requested MPEG4 URL. */
Mpeg4ReceiverME::Mpeg4ReceiverME(_bstr_t url, NewFrameCb frame_cb) :Mpeg4Receiver(frame_cb) {
    MFStartup(MF_VERSION);

    CComPtr<IMFMediaEngineNotify> engine_cb = new MediaEngineNotify(this);

    CComPtr<IMFMediaEngineClassFactory> factory;
    HRESULT hr = factory.CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER);
    assert(SUCCEEDED(hr));

    CComPtr<IMFAttributes> attribs;
    {
        hr = MFCreateAttributes(&attribs, 0);
        assert(SUCCEEDED(hr));

        hr = attribs->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, engine_cb);
        assert(SUCCEEDED(hr));

        // TODO: Investigate the following attributes from https://github.com/chromium/chromium/blob/main/media/renderers/win/media_foundation_renderer.cc
        // MF_MEDIA_ENGINE_CONTENT_PROTECTION_FLAGS
        // MF_MEDIA_ENGINE_AUDIO_CATEGORY
        // MF_MEDIA_ENGINE_OPM_HWND output protection window handle
        // MF_MEDIA_ENGINE_DXGI_MANAGER for GPU acceleration
        // MF_MEDIA_ENGINE_EXTENSION to load custom media resources
        // MF_MEDIA_ENGINE_PLAYBACK_HWND legacy window handle
    }

    hr = factory->CreateInstance(MF_MEDIA_ENGINE_REAL_TIME_MODE, attribs, &m_engine);
    assert(SUCCEEDED(hr));

#if 0
    hr = m_engine->SetDefaultPlaybackRate(0.0);
    assert(SUCCEEDED(hr));
    hr = m_engine->SetPlaybackRate(1.0);
    assert(SUCCEEDED(hr));
#endif

#if 1
    {
        IMFByteStreamPtr innerStream = CreateByteStreamFromUrl(url);

        // wrap innerStream om byteStream-wrapper to allow parsing of the underlying MPEG4 bitstream
        auto wrapper = CreateLocalInstance<StreamWrapper>();
        using namespace std::placeholders;
        wrapper->Initialize(innerStream, std::bind(&Mpeg4ReceiverME::OnStartTimeDpiChanged, this, _1, _2, _3));

        CComPtr<IMFMediaEngineEx> engine_ex;
        engine_ex = m_engine;
        engine_ex->SetSourceFromByteStream(wrapper, url);
    }
#else
    hr = m_engine->SetSource(url);
    if (FAILED(hr))
        throw std::runtime_error("SetSource failed");
#endif

    //m_engine->SetCurrentTime(current_time);

    hr = m_engine->Play();
    if (FAILED(hr))
        throw std::runtime_error("Play failed");
}

Mpeg4ReceiverME::~Mpeg4ReceiverME() {
}

void Mpeg4ReceiverME::Stop() {
    m_engine->Shutdown();
}

/** Receive frames. The "frame_cb" callback will be called from the same thread when new frames are received. */
HRESULT Mpeg4ReceiverME::ReceiveFrame() {
    return E_NOTIMPL;
}

void Mpeg4ReceiverME::OnFrameArrived() {
    {
        // update m_resolution
        DWORD width = 0, height = 0;
        HRESULT hr = m_engine->GetNativeVideoSize(&width, &height);
        if (FAILED(hr))
            abort();

        if ((width != m_resolution[0]) || (height != m_resolution[1]))
            m_metadata_changed = true;

        m_resolution[0] = width;
        m_resolution[1] = height;
    }

    // get frame time & duration
    double time = m_engine->GetCurrentTime(); // in seconds
    double duration = m_engine->GetDuration(); // in seconds (might be inf or nan)

    if (!m_bitmap || m_metadata_changed) {
        m_bitmap.Release();

        CComPtr<IWICImagingFactory2> factory;
        HRESULT hr = factory.CoCreateInstance(CLSID_WICImagingFactory2);
        assert(SUCCEEDED(hr));

        hr = factory->CreateBitmap(m_resolution[0], m_resolution[1], GUID_WICPixelFormat32bppBGR, WICBitmapCacheOnDemand, &m_bitmap);
        assert(SUCCEEDED(hr));
    }

    LONGLONG presentation_time = 0;
    HRESULT hr = m_engine->OnVideoStreamTick(&presentation_time);
    assert(SUCCEEDED(hr));

    // copy frame to DXGI surface or WIC bitmap
    RECT dst_rect = { 0 ,0, (LONG)m_resolution[0], (LONG)m_resolution[1] };
    hr = m_engine->TransferVideoFrame(m_bitmap, nullptr, &dst_rect, nullptr);
    if (FAILED(hr))
        throw std::runtime_error("TransferVideoFrame failed");

    CComPtr<IWICBitmapLock> frame_lock; // lock for frame_buffer content
    std::string_view frame_buffer;
    {
        WICRect rect = { 0, 0, (INT)m_resolution[0], (INT)m_resolution[1] };
        hr = m_bitmap->Lock(&rect, WICBitmapLockRead, &frame_lock);
        assert(SUCCEEDED(hr));

        UINT size = 0;
        WICInProcPointer ptr = nullptr;
        hr = frame_lock->GetDataPointer(&size, &ptr);
        assert(SUCCEEDED(hr));

        frame_buffer = std::string_view((char*)ptr, size);
    }

    m_frame_cb(*this, time, duration, frame_buffer, m_metadata_changed);

    m_metadata_changed = false; // clear flag after m_frame_cb have been called
}
