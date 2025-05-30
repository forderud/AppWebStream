#pragma once
#include <iostream>
#include <vector>
#include <cassert>
#include <Windows.h>
#include <mfapi.h>
#include "ComUtil.hpp"

#ifndef ENABLE_FFMPEG
#include <mfidl.h>
#include <Mfreadwrite.h>
#include <Ks.h>
#include <Codecapi.h>
#include <Dshow.h>
#include <mferror.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Strmiids.lib")

// define smart-pointers with "Ptr" suffix
_COM_SMARTPTR_TYPEDEF(IMFSinkWriter,  __uuidof(IMFSinkWriter));
_COM_SMARTPTR_TYPEDEF(IMFMediaBuffer, __uuidof(IMFMediaBuffer));
_COM_SMARTPTR_TYPEDEF(IMFSample,      __uuidof(IMFSample));
_COM_SMARTPTR_TYPEDEF(IMFMediaType,   __uuidof(IMFMediaType));
_COM_SMARTPTR_TYPEDEF(IMFAttributes,  __uuidof(IMFAttributes));
#else

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")

#include <cassert>
#include <fstream>
#include <tuple>
#include <vector>

extern "C" {
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#endif


class VideoEncoder {
public:
    VideoEncoder (unsigned int dimensions[2], unsigned int fps) : m_width(dimensions[0]), m_height(dimensions[1]), m_fps(fps) {
    }

    virtual ~VideoEncoder () = default;

    virtual void StartNewStream(IMFByteStream* stream) = 0;

    virtual R8G8B8A8* WriteFrameBegin () = 0;
    virtual HRESULT   WriteFrameEnd () = 0;
    virtual void      AbortWrite() = 0;

protected:
    const unsigned int m_width = 0;  ///< horizontal img. resolution (excluding padding)
    const unsigned int m_height = 0; ///< vertical img. resolution (excluding padding)
    unsigned int       m_fps = 0;
};


#ifndef ENABLE_FFMPEG
/** Media-Foundation-based H.264 video encoder. */
class VideoEncoderMF : public VideoEncoder {
    /* configure RGBA input */
    IMFMediaTypePtr GetInputType() {
        IMFMediaTypePtr mediaTypeIn;
        COM_CHECK(MFCreateMediaType(&mediaTypeIn));
        COM_CHECK(mediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        COM_CHECK(mediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)); // X8R8G8B8 format
        COM_CHECK(mediaTypeIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        //COM_CHECK(mediaTypeIn->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
        // Frame size is aligned to avoid crash
        COM_CHECK(MFSetAttributeSize(mediaTypeIn, MF_MT_FRAME_SIZE, Align2(m_width), Align2(m_height)));
        COM_CHECK(MFSetAttributeRatio(mediaTypeIn, MF_MT_FRAME_RATE, m_fps, 1));
        COM_CHECK(MFSetAttributeRatio(mediaTypeIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
        return mediaTypeIn;
    }

    static IMFAttributesPtr SinkWriterAttribs() {
        IMFAttributesPtr attribs;
        COM_CHECK(MFCreateAttributes(&attribs, 0));
        COM_CHECK(attribs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_FMPEG4)); // fragmented MPEG4
        COM_CHECK(attribs->SetUINT32(MF_LOW_LATENCY, TRUE)); // zero frame encoding latency
        COM_CHECK(attribs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE)); // GPU accelerated encoding
        return attribs;
    }

    /* configure H.264 output */
    IMFMediaTypePtr GetOutputType() {
        // doc: https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-encoder
        IMFMediaTypePtr mediaTypeOut;
        COM_CHECK(MFCreateMediaType(&mediaTypeOut));
        COM_CHECK(mediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        COM_CHECK(mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)); // H.264 format
        COM_CHECK(mediaTypeOut->SetUINT32(MF_MT_AVG_BITRATE, m_bitrate));
        COM_CHECK(mediaTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        // Frame size is aligned to avoid crash
        COM_CHECK(MFSetAttributeSize(mediaTypeOut, MF_MT_FRAME_SIZE, Align2(m_width), Align2(m_height)));
        COM_CHECK(MFSetAttributeRatio(mediaTypeOut, MF_MT_FRAME_RATE, m_fps, 1));
        COM_CHECK(MFSetAttributeRatio(mediaTypeOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
        return mediaTypeOut;
    }

public:
    /** Stream-based video encoding. */
    VideoEncoderMF (unsigned int dimensions[2], unsigned int fps, IMFByteStream * stream) : VideoEncoder(dimensions, fps) {
        COM_CHECK(MFStartup(MF_VERSION));
        COM_CHECK(MFFrameRateToAverageTimePerFrame(fps, 1, const_cast<unsigned long long*>(&m_frame_duration)));

        m_bitrate = static_cast<unsigned int>(0.78f*fps*m_width*m_height); // yields 40Mb/s for 1920x1080@25fps

        // create fragmented MPEG4 sink
        COM_CHECK(MFCreateFMPEG4MediaSink(stream, /*videoType*/GetOutputType(), /*audioType*/nullptr, &m_media_sink));

        // create sink writer with specified output format
        COM_CHECK(MFCreateSinkWriterFromMediaSink(m_media_sink, SinkWriterAttribs(), &m_sink_writer));

        COM_CHECK(m_sink_writer->SetInputMediaType(m_stream_index, GetInputType(), /*encParams*/nullptr));

        {
#if 0
            // access H.264 encoder directly (https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-encoder)
            CComPtr<ICodecAPI> codec;
            COM_CHECK(m_sink_writer->GetServiceForStream(m_stream_index, GUID_NULL, IID_ICodecAPI, (void**)&codec));
            CComVariant quality;
            codec->GetValue(&CODECAPI_AVEncCommonQuality, &quality); // not supported by Intel encoder (mfx_mft_h264ve_64.dll)
            CComVariant low_latency;
            COM_CHECK(codec->GetValue(&CODECAPI_AVLowLatencyMode, &low_latency));
            //assert(low_latency.boolVal != FALSE);
            // CODECAPI_AVEncAdaptiveMode not implemented

            // query group-of-pictures (GoP) size
            CComVariant gop_size;
            COM_CHECK(codec->GetValue(&CODECAPI_AVEncMPVGOPSize, &gop_size));
            //gop_size = (unsigned int)1; // VT_UI4 type
            //COM_CHECK(codec->SetValue(&CODECAPI_AVEncMPVGOPSize, &gop_size));

            // query number of bidirectional (B) frames between intra (I) & predicted (P) frames
            CComVariant b_picture_count;
            COM_CHECK(codec->GetValue(&CODECAPI_AVEncMPVDefaultBPictureCount, &b_picture_count));
#endif
        }

        COM_CHECK(m_sink_writer->BeginWriting());
    }

    ~VideoEncoderMF () noexcept {
        HRESULT hr = m_sink_writer->Finalize(); // fails on prior I/O errors
        hr; // discard error
        // delete objects before shutdown-call
        m_buffer.Release();
        m_sink_writer.Release();

        if (m_media_sink) {
            COM_CHECK(m_media_sink->Shutdown());
            m_media_sink.Release();
        }

        COM_CHECK(MFShutdown());
    }

    /** Must be called after DPI changes. */
    void StartNewStream(IMFByteStream* stream) override {
        // TODO: Investigate sample code on https://github.com/microsoft/MixedRealityCompanionKit

#if 1
        // Recreate media-sink & sink-writer objects to trigger re-tranmittion of MPEG4 "moov" atoms with updated DPI value.
        // This is admittedly a more extensive reset than adding a new stream that should in theory be sufficient. However, I've
        // so far been unable to get IMFSinkWriter::AddStream to work.
        m_buffer.Release();

        // create fragmented MPEG4 sink
        m_media_sink.Release();
        COM_CHECK(MFCreateFMPEG4MediaSink(stream, /*videoType*/GetOutputType(), /*audioType*/nullptr, &m_media_sink));

        // create sink writer with specified output format
        m_sink_writer.Release();
        COM_CHECK(MFCreateSinkWriterFromMediaSink(m_media_sink, SinkWriterAttribs(), &m_sink_writer));

        COM_CHECK(m_sink_writer->SetInputMediaType(m_stream_index, GetInputType(), /*encParams*/nullptr));
        COM_CHECK(m_sink_writer->BeginWriting());
#else
        // add new stream (will increment m_stream_index)
        COM_CHECK(m_sink_writer->AddStream(GetOutputType(), &m_stream_index));
        COM_CHECK(m_sink_writer->SetInputMediaType(m_stream_index, GetInputType(), /*encParams*/nullptr));
#endif
    }

    R8G8B8A8* WriteFrameBegin () override {
        const DWORD frame_size = 4*Align2(m_width)*Align2(m_height);

        // Create a new memory buffer.
        if (!m_buffer)
            COM_CHECK(MFCreateMemoryBuffer(frame_size, &m_buffer));

        // Lock buffer to get data pointer
        R8G8B8A8 * buffer_ptr = nullptr;
        COM_CHECK(m_buffer->Lock(reinterpret_cast<BYTE**>(&buffer_ptr), NULL, NULL));
        return buffer_ptr;
    }

    HRESULT WriteFrameEnd () override {
        const DWORD frame_size = 4*Align2(m_width)*Align2(m_height);

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

        // transmit frame immediately
        COM_CHECK(m_sink_writer->NotifyEndOfSegment(m_stream_index));
        //COM_CHECK(m_sink_writer->Flush(m_stream_index));

        // increment time
        m_time_stamp += m_frame_duration;
        return S_OK;
    }

    void AbortWrite() override {
        COM_CHECK(m_buffer->Unlock());
    }

private:
    unsigned int             m_bitrate = 0;
    const uint64_t           m_frame_duration = 0; // frame duration in 100-nanosecond units
    int64_t                  m_time_stamp = 0;

    CComPtr<IMFMediaSink>    m_media_sink;
    IMFSinkWriterPtr         m_sink_writer;
    IMFMediaBufferPtr        m_buffer;
    unsigned long            m_stream_index = 0; // default video stream index
};

#else

/** FFMPEG-based H.264 video encoder. */
class VideoEncoderFF : public VideoEncoder {
public:
    VideoEncoderFF (unsigned int dimensions[2], unsigned int fps, IMFByteStream * socket) : VideoEncoder(dimensions, fps) {
        //av_log_set_level(AV_LOG_VERBOSE);

        /* allocate the output media context */
        avformat_alloc_output_context2(/*out*/&m_out_ctx, nullptr, "mp4", nullptr);
        if (!m_out_ctx)
            throw std::runtime_error("avformat_alloc_output_context2 failure");

        m_rgb_buf.resize(Align2(m_width)* Align2(m_height));

        // find the encoder
        const AVCodec* video_codec = avcodec_find_encoder(m_out_ctx->oformat->video_codec);
        if (!video_codec)
            throw std::runtime_error("Could not find encoder");
        assert(video_codec->type == AVMEDIA_TYPE_VIDEO);

        // Add the video streams using the default format codecs and initialize the codecs
        m_codec_ctx = configure_context(video_codec);

        {
            m_stream = avformat_new_stream(m_out_ctx, NULL);
            if (!m_stream)
                throw std::runtime_error("Could not allocate stream");

            m_stream->id = m_out_ctx->nb_streams - 1;
            m_stream->time_base = m_codec_ctx->time_base; // affect baseMediaDecodeTime increments
        }

        // REF: https://ffmpeg.org/ffmpeg-formats.html#Options-8 (-movflags arguments)
        // REF: https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/movenc.c
        AVDictionary *opt = nullptr;
        int ret = av_dict_set(&opt, "movflags", "empty_moov+default_base_moof+frag_every_frame", 0); // fragmented MP4
        assert(ret >= 0);
        ret = av_dict_set_int(&opt, "movie_timescale", 1000*m_fps, 0); // match MediaFoundation timescale
        assert(ret >= 0);
        ret = av_dict_set(&opt, "fflags", "nobuffer+flush_packets", 0); // don't know if this helps
        assert(ret >= 0);
        ret = av_dict_set(&opt, "mpegts", "omit_video_pes_length", 0); // must also set val=0, don't know if this helps
        assert(ret >= 0);
#if 0
        ret = av_opt_set_int(m_out_ctx->priv_data, "omit_video_pes_length", 0, 0); // fails with AVERROR_OPTION_NOT_FOUND (0xabafb008)
        if (ret)
            throw std::runtime_error("omit_video_pes_length failed");
#endif

        {
            // open the video codecs and allocate the necessary encode buffers
            AVDictionary* opt_cpy = nullptr;
            av_dict_copy(/*out*/&opt_cpy, /*in*/opt, 0);

            // open the codec
            ret = avcodec_open2(m_codec_ctx, /*in*/video_codec, &opt_cpy);
            av_dict_free(&opt_cpy);
            if (ret < 0)
                throw std::runtime_error("Could not open video codec");
        }

        m_frame = allocate_frame(m_codec_ctx);

        // copy the stream parameters to the muxer
        ret = avcodec_parameters_from_context(/*out*/m_stream->codecpar, /*in*/m_codec_ctx);
        if (ret < 0)
            throw std::runtime_error("Could not copy the stream parameters");

        constexpr int out_buf_size = 16 * 1024 * 1024; // 16MB
        m_out_buf = (unsigned char*)av_malloc(out_buf_size);

        m_socket = socket; // prevent socket from being destroyed before this object
        m_out_ctx->pb = avio_alloc_context(m_out_buf, out_buf_size, 1/*writable*/, socket, nullptr/*read*/, WritePackage, nullptr/*seek*/);
        //out_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

#ifndef NDEBUG
        // log encoder info to console
        av_dump_format(m_out_ctx, 0, nullptr, 1);
#endif

        // Write the stream header, if any
        ret = avformat_write_header(m_out_ctx, &opt);
        if (ret < 0)
            throw std::runtime_error("avformat_write_header failed");
    }

    ~VideoEncoderFF() {
        // flush encoder to mark end of stream
        WriteFrameImpl(false);

        // write file ending (discard error codes)
        av_write_trailer(m_out_ctx);

        av_frame_free(&m_frame);

        avcodec_free_context(&m_codec_ctx);

        avio_context_free(&m_out_ctx->pb);
        avformat_free_context(m_out_ctx);

        av_free(m_out_buf);
        m_out_buf = nullptr;
    }

    void StartNewStream(IMFByteStream* /*stream*/) override {
        throw std::runtime_error("StartNewStream not implemented");
    }

    R8G8B8A8* WriteFrameBegin () override {
        return m_rgb_buf.data();
    }

    HRESULT   WriteFrameEnd () override {
        return WriteFrameImpl(true);
    }

    HRESULT WriteFrameImpl (bool has_frame) {
        if (has_frame) {
            if (av_frame_make_writable(m_frame) < 0)
                exit(1);

            assert(m_codec_ctx->pix_fmt == AV_PIX_FMT_YUV420P);

            // RGB to YCbCR conversion
            for (unsigned int y = 0; y < m_height; y++) {
                for (unsigned int x = 0; x < m_width; x++) {
                    R8G8B8A8 rgb = m_rgb_buf[y* m_codec_ctx->width + x];
                    unsigned char Y=0, Cb=0, Cr=0;
                    RGB_to_YCbCr(rgb, Y, Cb, Cr);
                    // write Y value
                    m_frame->data[0][y* m_frame->linesize[0] + x] = Y;
                    // write subsambled Cb,Cr values
                    if (((x % 2) == 0) && ((y % 2) == 0)) {
                        m_frame->data[1][y/2* m_frame->linesize[1] + x/2] = Cr/4;
                        m_frame->data[2][y/2* m_frame->linesize[2] + x/2] = Cb/4;
                    } else {
                        m_frame->data[1][y/2* m_frame->linesize[1] + x/2] += Cr/4;
                        m_frame->data[2][y/2* m_frame->linesize[2] + x/2] += Cb/4;
                    }
                }
            }

            m_frame->pts = m_next_pts;
            m_next_pts += 4; // gives sample_dur=4*256=1024 to almost match MediaFoundation
        }

        // encode frame
        int ret = avcodec_send_frame(m_codec_ctx, has_frame ? m_frame : nullptr);
        if (ret < 0)
            throw std::runtime_error("Error encoding video frame");

        std::unique_ptr<AVPacket, decltype(&av_packet_unref)> pkt(av_packet_alloc(), av_packet_unref);

        // process packages
        for (;;) {
            ret = avcodec_receive_packet(m_codec_ctx, pkt.get());
            if (ret == AVERROR(EAGAIN))
                break; // not yet available
            else if (!has_frame && (ret == AVERROR_EOF))
                break; // end of stream
            else if (ret < 0)
                throw std::runtime_error("avcodec_receive_packet failed");

            // rescale output packet timestamp values from codec to stream timebase
            av_packet_rescale_ts(pkt.get(), m_codec_ctx->time_base, m_stream->time_base);
            pkt->stream_index = m_stream->index;

            // write compressed frame to stream
            ret = av_interleaved_write_frame(m_out_ctx, pkt.get());
            if (ret < 0)
                return E_FAIL;
        }

        return S_OK;
    }

    void AbortWrite() override {
        // do nothing
    }

private:
    /* Add an output stream. */
    AVCodecContext* configure_context (const AVCodec* codec) {
        // setup context
        AVCodecContext *enc = avcodec_alloc_context3(codec);
        if (!enc)
            throw std::runtime_error("Could not alloc an encoding context");
        {
            enc->bit_rate = static_cast<unsigned int>(0.78f*m_fps*m_width*m_height); // yields 40Mb/s for 1920x1080@25fps
            // Resolution must be a multiple of two
            enc->width    = Align2(m_width);
            enc->height   = Align2(m_height);
            /* timebase: This is the fundamental unit of time (in seconds) in terms
            * of which frame timestamps are represented. For fixed-fps content,
            * timebase should be 1/framerate and timestamp increments should be
            * identical to 1. */
            enc->time_base = { 1, static_cast<int>(m_fps) };
            enc->framerate = { static_cast<int>(m_fps), 1 };

            //enc->gop_size = 0; // group of pictures size
            enc->pix_fmt  = AV_PIX_FMT_YUV420P; // default pix_fmt

            int res = av_opt_set(enc->priv_data, "tune", "zerolatency", 0);
            if (res)
                throw std::runtime_error("zerolatency tuning failed");

            // Some formats want stream headers to be separate
            if (m_out_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        return enc;
    }

    static AVFrame* allocate_frame(/*in*/const AVCodecContext *ctx) {
        // allocate and init a re-usable frame
        AVFrame* frame = av_frame_alloc();
        assert(frame);

        assert(ctx->pix_fmt == AV_PIX_FMT_YUV420P);
        frame->format = ctx->pix_fmt;
        frame->width = ctx->width;
        frame->height = ctx->height;

        // allocate the buffers for the frame data
        int ret = av_frame_get_buffer(frame, /*align*/32);
        if (ret < 0)
            throw std::runtime_error("Could not allocate frame data");

        return frame;
    }

    /** Manual RGB to YCbCr conversion, based on rgb_to_yuv in https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/movenc.c */
    static void RGB_to_YCbCr(const R8G8B8A8 rgb, unsigned char& Y, unsigned char& Cb, unsigned char& Cr) {
        Y = av_clip_uint8((  16000 + 257*rgb.r + 504*rgb.g +  98*rgb.b)/1000);
        Cb = av_clip_uint8((128000 - 148*rgb.r - 291*rgb.g + 439*rgb.b)/1000);
        Cr = av_clip_uint8((128000 + 439*rgb.r - 368*rgb.g -  71*rgb.b)/1000);
    }

    /** Stream writing callback. */
    static int WritePackage(void* opaque, const uint8_t* buf, int buf_size) {
        IMFByteStream* stream = reinterpret_cast<IMFByteStream*>(opaque);
        ULONG bytes_written = 0;
        if (FAILED(stream->Write(buf, buf_size, &bytes_written)))
            return -1;

        return buf_size; // don't want to return bytes_written here in case bitstream have grown or shrunk during editing
    }

    int64_t                m_next_pts = 0; // presentation timestamp (PTS) [time_base unit] for the next frame
    AVFormatContext*       m_out_ctx = nullptr;
    AVCodecContext*        m_codec_ctx = nullptr;
    AVStream*              m_stream = nullptr;
    AVFrame*               m_frame = nullptr;

    std::vector<R8G8B8A8>  m_rgb_buf;
    unsigned char*         m_out_buf = nullptr;
    CComPtr<IMFByteStream> m_socket;
};

#endif
