#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfapi.h>
#include <mferror.h>
#include <strmif.h>
#include <codecapi.h>
#include "Mpeg4ReceiverSR.hpp"
#include "StreamWrapper.hpp"
#include "../AppWebStream/ComUtil.hpp"

#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Propsys.lib")
#pragma comment(lib, "Strmiids.lib")

// define smart-pointers with "Ptr" suffix
_COM_SMARTPTR_TYPEDEF(IMFAttributes, __uuidof(IMFAttributes));
_COM_SMARTPTR_TYPEDEF(IMFMediaType, __uuidof(IMFMediaType));
_COM_SMARTPTR_TYPEDEF(IMFSample, __uuidof(IMFSample));
_COM_SMARTPTR_TYPEDEF(IMFByteStream, __uuidof(IMFByteStream));
_COM_SMARTPTR_TYPEDEF(IMFSourceResolver, __uuidof(IMFSourceResolver));
_COM_SMARTPTR_TYPEDEF(IMFMediaBuffer, __uuidof(IMFMediaBuffer));
_COM_SMARTPTR_TYPEDEF(IMFSourceReader, __uuidof(IMFSourceReader));
_COM_SMARTPTR_TYPEDEF(IPropertyStore, __uuidof(IPropertyStore));
_COM_SMARTPTR_TYPEDEF(IMFMediaSource, __uuidof(IMFMediaSource));


Mpeg4ReceiverSR::Mpeg4ReceiverSR(_bstr_t url, NewFrameCb frame_cb) : Mpeg4Receiver(frame_cb) {
    m_resolution.fill(0); // clear array

    COM_CHECK(MFStartup(MF_VERSION));

    IMFAttributesPtr attribs;
    {
        // DOC: https://learn.microsoft.com/en-us/windows/win32/medfound/source-reader-attributes
        COM_CHECK(MFCreateAttributes(&attribs, 0));
        COM_CHECK(attribs->SetUINT32(MF_LOW_LATENCY, TRUE)); // low latency mode
        COM_CHECK(attribs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE)); // GPU accelerated
        COM_CHECK(attribs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE)); // enable YUV to RGB-32 conversion
    }

    IMFByteStreamPtr byteStream;
    {
        IMFSourceResolverPtr resolver;
        COM_CHECK(MFCreateSourceResolver(&resolver));

        IPropertyStorePtr props;
        COM_CHECK(PSCreateMemoryPropertyStore(__uuidof(IPropertyStore), (void**)&props));
        {
            // reduce startup latency
            PROPERTYKEY key{};
            key.fmtid = MFNETSOURCE_ACCELERATEDSTREAMINGDURATION;
            key.pid = 0;

            PROPVARIANT val{};
            val.vt = VT_I4;
            val.lVal = 100; // 100 milliseconds (10,000 is default)

            COM_CHECK(props->SetValue(key, val));
            //COM_CHECK(props->Commit());
        }
        {
            // reduce network buffering
            PROPERTYKEY key{};
            key.fmtid = MFNETSOURCE_BUFFERINGTIME;
            key.pid = 0;

            PROPVARIANT val{};
            val.vt = VT_I4;
            val.lVal = 1; // 1 second (5 is default)

            COM_CHECK(props->SetValue(key, val));
            //COM_CHECK(props->Commit());
        }
        {
            // reduce max buffering
            PROPERTYKEY key{};
            key.fmtid = MFNETSOURCE_MAXBUFFERTIMEMS;
            key.pid = 0;

            PROPVARIANT val{};
            val.vt = VT_I4;
            val.lVal = 100; // 100ms (40,000 is default)

            COM_CHECK(props->SetValue(key, val));
            //COM_CHECK(props->Commit());
        }

        // create innerStream that connects to the URL
        DWORD createObjFlags = MF_RESOLUTION_BYTESTREAM; // MF_RESOLUTION_BYTESTREAM for IMFByteStream and MF_RESOLUTION_MEDIASOURCE for IMFMediaSource
        MF_OBJECT_TYPE objectType = MF_OBJECT_INVALID;
        IUnknownPtr obj;
        COM_CHECK(resolver->CreateObjectFromURL(url, createObjFlags, props, &objectType, &obj));
        assert(objectType == MF_OBJECT_BYTESTREAM);
        IMFByteStreamPtr innerStream = obj;

        // wrap innerStream om byteStream-wrapper to allow parsing of the underlying MPEG4 bitstream
        auto wrapper = CreateLocalInstance<StreamWrapper>();
        using namespace std::placeholders;
        wrapper->Initialize(innerStream, std::bind(&Mpeg4ReceiverSR::OnStartTimeDpiChanged, this, _1, _2, _3));
        COM_CHECK(wrapper.QueryInterface(&byteStream));
    }
    COM_CHECK(MFCreateSourceReaderFromByteStream(byteStream, attribs, &m_reader));

    COM_CHECK(ConfigureOutputType(*m_reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM));
}

Mpeg4ReceiverSR::~Mpeg4ReceiverSR() {
    m_reader.Release();

    COM_CHECK(MFShutdown());
}

void Mpeg4ReceiverSR::Stop() {
    m_active = false;
}

HRESULT Mpeg4ReceiverSR::ReceiveFrame() {
    if (!m_active)
        return E_FAIL;

    DWORD streamIdx = 0;
    DWORD flags = 0;       // MF_SOURCE_READER_FLAG bitmask
    int64_t timeStamp = 0; // in 100-nanosecond units
    IMFSamplePtr frame;    // NULL if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
    HRESULT hr = m_reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, /*flags*/0, &streamIdx, &flags, &timeStamp, &frame);
    if (FAILED(hr))
        return hr;

#if 0
    PROPVARIANT val{};
    PropVariantClear(&val);
    COM_CHECK(reader.GetPresentationAttribute(streamIdx, MF_PD_LAST_MODIFIED_TIME, &val)); // fails with "The requested attribute was not found."
#endif

#if 0
    if (frame) {
        // access H.264 decoder directly (https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-decoder)
        CComPtr<ICodecAPI> codec;
        COM_CHECK(m_reader->GetServiceForStream(streamIdx, GUID_NULL, IID_ICodecAPI, (void**)&codec));

        // verify that low latency mode is enabled
        CComVariant low_latency;
        COM_CHECK(codec->GetValue(&CODECAPI_AVLowLatencyMode, &low_latency));
        assert(low_latency.boolVal != FALSE);
    }
#endif

    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        wprintf(L"INFO: End of stream\n");
        return E_FAIL;
    }
    if (flags & MF_SOURCE_READERF_NEWSTREAM) {
        wprintf(L"INFO: New stream created\n");
        hr = ConfigureOutputType(*m_reader, streamIdx);
        if (FAILED(hr))
            return E_FAIL;
    }
    if (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED) {
        wprintf(L"ERROR: Native media type changed\n");
        // The format changed. Reconfigure the decoder.
        hr = ConfigureOutputType(*m_reader, streamIdx);
        if (FAILED(hr))
            return E_FAIL;
    }
    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
        wprintf(L"ERROR: Current media type changed\n");
    }
    if (flags & MF_SOURCE_READERF_STREAMTICK) {
        wprintf(L"WARNING: Gap in stream\n");
    }

    if (!frame)
        return E_FAIL;

    if (m_frame_cb) {
        int64_t frameTime = 0; // in 100-nanosecond units since startTime
        COM_CHECK(frame->GetSampleTime(&frameTime));

        int64_t frameDuration = 0; // in 100-nanosecond units
        COM_CHECK(frame->GetSampleDuration(&frameDuration));

        {
            DWORD bufferCount = 0;
            COM_CHECK(frame->GetBufferCount(&bufferCount));
            assert(bufferCount == 1); // one buffer per frame for video
        }

        {
            IMFMediaBufferPtr buffer;
            COM_CHECK(frame->GetBufferByIndex(0, &buffer)); // only one buffer per frame for video

            BYTE* bufferPtr = nullptr;
            DWORD bufferSize = 0;
            COM_CHECK(buffer->Lock(&bufferPtr, nullptr, &bufferSize));
            assert(bufferSize == 4 * Align16(m_resolution[0]) * Align16(m_resolution[1])); // buffer size is a multiple of MPEG4 16x16 macroblocks

            // call frame data callback function for client-side processing
            m_frame_cb(*this, frameTime, frameDuration, std::string_view((char*)bufferPtr, bufferSize), m_metadata_changed);

            COM_CHECK(buffer->Unlock());
        }

        m_metadata_changed = false; // clear flag after m_frame_cb have been called
    }

    return S_OK;
}

void Mpeg4ReceiverSR::OnStartTimeDpiChanged(uint64_t startTime, double dpi, double xform[6]) {
    if (startTime != m_startTime)
        m_metadata_changed = true;
    if (dpi != m_dpi)
        m_metadata_changed = true;

    m_startTime = startTime;
    m_dpi = dpi;

    for (size_t i = 0; i < 6; i++)
        m_xform[i] = xform[i];
}

HRESULT Mpeg4ReceiverSR::ConfigureOutputType(IMFSourceReader& reader, DWORD dwStreamIndex) {
    GUID majorType{};
    GUID subType{};
    {
        // get the native stream format
        IMFMediaTypePtr nativeType;
        COM_CHECK(reader.GetNativeMediaType(dwStreamIndex, 0, &nativeType));

        // get major type
        COM_CHECK(nativeType->GetGUID(MF_MT_MAJOR_TYPE, &majorType));

        // select matching subtype
        if (majorType == MFMediaType_Video)
            subType = MFVideoFormat_RGB32;
        else if (majorType == MFMediaType_Audio)
            subType = MFAudioFormat_PCM;
        else
            return E_FAIL; // unrecognized type

        // update frame resolution
        uint32_t width = 0, height = 0;
        COM_CHECK(MFGetAttributeSize(nativeType, MF_MT_FRAME_SIZE, &width, &height));
        if ((width != m_resolution[0]) || (height != m_resolution[1]))
            m_metadata_changed = true;

        m_resolution[0] = width;
        m_resolution[1] = height;
    }

    // configure RGB32 output
    IMFMediaTypePtr mediaType;
    COM_CHECK(MFCreateMediaType(&mediaType));
    COM_CHECK(mediaType->SetGUID(MF_MT_MAJOR_TYPE, majorType));
    COM_CHECK(mediaType->SetGUID(MF_MT_SUBTYPE, subType));

    COM_CHECK(reader.SetCurrentMediaType(dwStreamIndex, NULL, mediaType));
    return S_OK;
}
