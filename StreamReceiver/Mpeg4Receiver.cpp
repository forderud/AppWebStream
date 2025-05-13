#include "Mpeg4Receiver.hpp"
#include "StreamWrapper.hpp"
#include "../AppWebStream/ComUtil.hpp"

#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")


EXTERN_GUID(WMMEDIATYPE_Video, 0x73646976, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71); // video stream from https://learn.microsoft.com/en-us/windows/win32/wmformat/media-type-identifiers

HRESULT IsVideoStream(IMFSourceReader& reader, DWORD streamIdx, /*out*/bool& isVideo) {
    // iterate over all media types
    for (DWORD mediaTypeIdx = 0;; mediaTypeIdx++) {
        IMFMediaTypePtr mediaType;
        HRESULT hr = reader.GetNativeMediaType(streamIdx, mediaTypeIdx, &mediaType);
        if (FAILED(hr)) {
            isVideo = false;
            return hr; // MF_E_NO_MORE_TYPES if out of bounds
        }

        // examine media type
        GUID guid{};
        COM_CHECK(mediaType->GetMajorType(&guid));
        if (guid == WMMEDIATYPE_Video) {
            isVideo = true; // found video stream
            return S_OK;
        }
    }
}

DWORD GetFirstVideoStream(IMFSourceReader& reader) {
    // iterate over all streams
    for (DWORD streamIdx = 0; ; streamIdx++) {
        bool isVideo = false;
        HRESULT hr = IsVideoStream(reader, streamIdx, isVideo);
        if (hr == MF_E_NO_MORE_TYPES) // out of bounds
            break;

        if (SUCCEEDED(hr) && isVideo)
            return streamIdx;
    }

    wprintf(L"ERROR: Unable to detect any video stream.\n");
    abort();
}


Mpeg4Receiver::Mpeg4Receiver(_bstr_t url, ProcessFrameCb frame_cb) : m_frame_cb(frame_cb) {
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

    // Create intermediate IMFByteStream object allow parsing of the underlying MPEG4 bitstream.
    // Needed to access CreationTime & DPI parameters that doesn't seem to be exposed through the MediaFoundation API.
    IMFByteStreamPtr byteStream;
    {
        IMFSourceResolverPtr resolver;
        COM_CHECK(MFCreateSourceResolver(&resolver));

        DWORD createObjFlags = MF_RESOLUTION_READ | MF_RESOLUTION_BYTESTREAM | MF_RESOLUTION_CONTENT_DOES_NOT_HAVE_TO_MATCH_EXTENSION_OR_MIME_TYPE;
        MF_OBJECT_TYPE objectType = MF_OBJECT_INVALID;
        IUnknownPtr source;
        COM_CHECK(resolver->CreateObjectFromURL(url, createObjFlags, nullptr, &objectType, &source));
        IMFByteStreamPtr innerStream = source;

        auto tmp = CreateLocalInstance<StreamWrapper>();
        tmp->Initialize(innerStream, this);
        COM_CHECK(tmp.QueryInterface(&byteStream));
    }
    COM_CHECK(MFCreateSourceReaderFromByteStream(byteStream, attribs, &m_reader));

    DWORD streamIdx = GetFirstVideoStream(m_reader);
    ConfigureOutputType(m_reader, streamIdx);
}

Mpeg4Receiver::~Mpeg4Receiver() {
}

HRESULT Mpeg4Receiver::ReceiveFrame() {
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

    if (m_frame_cb)
        m_frame_cb(*this, *frame, m_metadata_changed);

    m_metadata_changed = false; // clear flag after m_frame_cb have been called

    return S_OK;
}

void Mpeg4Receiver::OnStartTimeDpiChanged(uint64_t startTime, double dpi) {
    if (startTime != m_startTime)
        m_metadata_changed = true;
    if (dpi != m_dpi)
        m_metadata_changed = true;

    m_startTime = startTime;
    m_dpi = dpi;
}

HRESULT Mpeg4Receiver::ConfigureOutputType(IMFSourceReader& reader, DWORD dwStreamIndex) {
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
