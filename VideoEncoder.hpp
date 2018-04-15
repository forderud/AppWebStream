#pragma once
#include <stdexcept>
#include <iostream>
#include <vector>
#include <array>
#include <cassert>

#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <Mfreadwrite.h>
#include <Ks.h>
#include <Codecapi.h>
#include <Dshow.h>
#include <mferror.h>
#include <comdef.h>  // COM smart-ptr with "Ptr" suffix
#include <atlbase.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Strmiids.lib")


/** 32bit color value. */
struct R8G8B8A8 {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

_COM_SMARTPTR_TYPEDEF(IMFSinkWriter,  __uuidof(IMFSinkWriter));
_COM_SMARTPTR_TYPEDEF(IMFMediaBuffer, __uuidof(IMFMediaBuffer));
_COM_SMARTPTR_TYPEDEF(IMFSample,      __uuidof(IMFSample));
_COM_SMARTPTR_TYPEDEF(IMFMediaType,   __uuidof(IMFMediaType));


/** Converts unicode string to ASCII */
static inline std::string ToAscii (const std::wstring& w_str) {
#pragma warning(push)
#pragma warning(disable: 4996) // function or variable may be unsafe
    size_t N = w_str.size();
    std::string s_str;
    s_str.resize(N);
    wcstombs(const_cast<char*>(s_str.data()), w_str.c_str(), N);

    return s_str;
#pragma warning(pop)
}


/** Class for encoding image frames into a H.264 video. */
class VideoEncoder {
public:
    /** Grow size to become a multiple of the MEPG macroblock size (typ. 8). */
    static unsigned int Align (unsigned int size, unsigned int block_size = 8) {
        if ((size % block_size) == 0)
            return size;
        else
            return size + block_size - (size % block_size);
    }

    /** Stream-based video encoding. 
        The underlying MFCreateFMPEG4MediaSink system call require Windows 8 or newer. */
    VideoEncoder (std::array<unsigned short, 2> dimensions, unsigned int fps, IMFByteStream * stream) : VideoEncoder(dimensions, fps) {
        const unsigned int bit_rate = static_cast<unsigned int>(0.78f*fps*Align(m_width)*Align(m_height)); // yields 40Mb/s for 1920x1080@25fps (max blu-ray quality)

        CComPtr<IMFAttributes> attribs;
        COM_CHECK(MFCreateAttributes(&attribs, 0));
        COM_CHECK(attribs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_FMPEG4));
        COM_CHECK(attribs->SetUINT32(MF_LOW_LATENCY, TRUE));
        COM_CHECK(attribs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));

        // create sink writer with specified output format
        IMFMediaTypePtr mediaTypeOut = MediaTypeutput(fps, bit_rate);
        COM_CHECK(MFCreateFMPEG4MediaSink(stream, mediaTypeOut, nullptr, &m_media_sink)); // "fragmented" MPEG4 does not require seekable byte-stream
        COM_CHECK(MFCreateSinkWriterFromMediaSink(m_media_sink, attribs, &m_sink_writer));

        // connect input to output
        IMFMediaTypePtr mediaTypeIn = MediaTypeInput(fps);
        COM_CHECK(m_sink_writer->SetInputMediaType(m_stream_index, mediaTypeIn, nullptr));

        {
            // access H.264 encoder directly (https://msdn.microsoft.com/en-us/library/windows/desktop/dd797816.aspx)
            CComPtr<ICodecAPI> codec;
            COM_CHECK(m_sink_writer->GetServiceForStream(m_stream_index, GUID_NULL, IID_ICodecAPI, (void**)&codec));
            CComVariant quality;
            codec->GetValue(&CODECAPI_AVEncCommonQuality, &quality); // not supported by Intel encoder (mfx_mft_h264ve_64.dll)
            CComVariant low_latency;
            COM_CHECK(codec->GetValue(&CODECAPI_AVLowLatencyMode, &low_latency));
            //assert(low_latency.boolVal != FALSE);
            // CODECAPI_AVEncAdaptiveMode not implemented
        }

        COM_CHECK(m_sink_writer->BeginWriting());
    }

    VideoEncoder (std::array<unsigned short, 2> dimensions, unsigned int fps) : m_width(dimensions[0]), m_height(dimensions[1]) {
        COM_CHECK(MFStartup(MF_VERSION));
        COM_CHECK(MFFrameRateToAverageTimePerFrame(fps, 1, const_cast<unsigned long long*>(&m_frame_duration)));
    }

    ~VideoEncoder () noexcept {
        HRESULT hr = m_sink_writer->Finalize(); // fails on prior I/O errors
        hr; // discard error
        // delete objects before shutdown-call
        m_buffer = nullptr;
        m_sink_writer = nullptr;

        if (m_media_sink) {
            COM_CHECK(m_media_sink->Shutdown());
            m_media_sink = nullptr;
        }

        COM_CHECK(MFShutdown());
    }

    IMFMediaTypePtr MediaTypeInput (unsigned int fps) {
        // configure input format. Frame size is aligned to avoid crash
        IMFMediaTypePtr mediaTypeIn;
        COM_CHECK(MFCreateMediaType(&mediaTypeIn));
        COM_CHECK(mediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        COM_CHECK(mediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)); // X8R8G8B8 format
        COM_CHECK(mediaTypeIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        COM_CHECK(mediaTypeIn->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
        // Frame size is aligned to avoid crash
        COM_CHECK(MFSetAttributeSize(mediaTypeIn, MF_MT_FRAME_SIZE, Align(m_width), Align(m_height)));
        COM_CHECK(MFSetAttributeRatio(mediaTypeIn, MF_MT_FRAME_RATE, fps, 1));
        COM_CHECK(MFSetAttributeRatio(mediaTypeIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
        return mediaTypeIn;
    }

    IMFMediaTypePtr MediaTypeutput (unsigned int fps, unsigned int bit_rate) {
        IMFMediaTypePtr mediaTypeOut;
        COM_CHECK(MFCreateMediaType(&mediaTypeOut));
        COM_CHECK(mediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        COM_CHECK(mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)); // H.264 format
        COM_CHECK(mediaTypeOut->SetUINT32(MF_MT_AVG_BITRATE, bit_rate));
        COM_CHECK(mediaTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        // Frame size is aligned to avoid crash
        COM_CHECK(MFSetAttributeSize(mediaTypeOut, MF_MT_FRAME_SIZE, Align(m_width), Align(m_height)));
        COM_CHECK(MFSetAttributeRatio(mediaTypeOut, MF_MT_FRAME_RATE, fps, 1));
        COM_CHECK(MFSetAttributeRatio(mediaTypeOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
        return mediaTypeOut;
    }

    R8G8B8A8* WriteFrameBegin () {
        const DWORD frame_size = 4*Align(m_width)*Align(m_height);

        // Create a new memory buffer.
        if (!m_buffer)
            COM_CHECK(MFCreateMemoryBuffer(frame_size, &m_buffer));

        // Lock buffer to get data pointer
        R8G8B8A8 * buffer_ptr = nullptr;
        COM_CHECK(m_buffer->Lock(reinterpret_cast<BYTE**>(&buffer_ptr), NULL, NULL));
        return buffer_ptr;
    }

    HRESULT WriteFrame (R8G8B8A8* src_data, bool swap_rb) {
        R8G8B8A8 * buffer_ptr = WriteFrameBegin();

        for (unsigned int j = 0; j < m_height; j++) {
            R8G8B8A8 * src_row = &src_data[j*m_width];
            R8G8B8A8 * dst_row = &buffer_ptr[j*Align(m_width)];
            if (swap_rb) {
                for (unsigned int i = 0; i < m_width; i++)
                    dst_row[i] = SwapRGBAtoBGRA(src_row[i]);
            } else {
                // copy scanline as-is
                memcpy(dst_row, src_row, 4*m_width);
            }

            // clear padding at end of scanline
            size_t hor_padding = Align(m_width) - m_width;
            if (hor_padding)
                std::memset(&dst_row[m_width], 0, 4*hor_padding);
        }

        // clear padding after last scanline
        size_t vert_padding = Align(m_height) - m_height;
        if (vert_padding)
            std::memset(&buffer_ptr[m_height*Align(m_width)], 0, 4*Align(m_width)*vert_padding);

        return WriteFrameEnd();
    }

    HRESULT WriteFrameEnd () {
        const DWORD frame_size = 4*Align(m_width)*Align(m_height);

        COM_CHECK(m_buffer->Unlock());

        // Set the data length of the buffer.
        COM_CHECK(m_buffer->SetCurrentLength(frame_size));

        // Create a media sample and add the buffer to the sample.
        IMFSamplePtr sample;
        COM_CHECK(MFCreateSample(&sample));
        COM_CHECK(sample->AddBuffer(m_buffer));

        // Set the time stamp and the duration.
        COM_CHECK(sample->SetSampleTime(m_time_stamp));
        COM_CHECK(sample->SetSampleDuration(m_frame_duration));

        // send sample to Sink Writer.
        HRESULT hr = m_sink_writer->WriteSample(m_stream_index, sample); // fails on I/O error
        if (FAILED(hr))
            return hr;

        // increment time
        m_time_stamp += m_frame_duration;
        return S_OK;
    }

    std::array<unsigned short, 2> Dims() const {
        return {m_width, m_height};
    }

private:
    static R8G8B8A8 SwapRGBAtoBGRA (R8G8B8A8 in) {
        return{ in.b, in.g, in.r, in.a };
    }

    static void COM_CHECK (HRESULT hr) {
        if (FAILED(hr)) {
            _com_error err(hr);
#ifdef _UNICODE
            const wchar_t * msg = err.ErrorMessage(); // weak ptr.
            throw std::runtime_error(ToAscii(msg));
#else
            const char * msg = err.ErrorMessage(); // weak ptr.
            throw std::runtime_error(msg);
#endif
        }
    }

    const unsigned short     m_width,
                             m_height;
    const unsigned long long m_frame_duration = 0;
    long long                m_time_stamp = 0;

    CComPtr<IMFMediaSink>    m_media_sink;
    IMFSinkWriterPtr         m_sink_writer;
    IMFMediaBufferPtr        m_buffer;
    unsigned long            m_stream_index = 0;
};
