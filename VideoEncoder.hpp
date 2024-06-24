#pragma once
#include <stdexcept>
#include <iostream>
#include <vector>
#include <array>
#include <cassert>
#include <Windows.h>
#include <mfapi.h>
#include <atlbase.h>

/** 32bit color value. */
struct R8G8B8A8 {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

#ifndef ENABLE_FFMPEG
#include <mfidl.h>
#include <Mfreadwrite.h>
#include <Ks.h>
#include <Codecapi.h>
#include <Dshow.h>
#include <mferror.h>
#include <comdef.h>  // COM smart-ptr with "Ptr" suffix

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Strmiids.lib")


_COM_SMARTPTR_TYPEDEF(IMFSinkWriter,  __uuidof(IMFSinkWriter));
_COM_SMARTPTR_TYPEDEF(IMFMediaBuffer, __uuidof(IMFMediaBuffer));
_COM_SMARTPTR_TYPEDEF(IMFSample,      __uuidof(IMFSample));
_COM_SMARTPTR_TYPEDEF(IMFMediaType,   __uuidof(IMFMediaType));
#else

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")

#include <cassert>
#include <stdexcept>
#include <fstream>
#include <tuple>
#include <vector>

extern "C" {
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
}

#endif


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

class VideoEncoder {
public:
    /** Grow size to become a multiple of the MEPG macroblock size (typ. 8). */
    static unsigned int Align (unsigned int size, unsigned int block_size = 8) {
        if ((size % block_size) == 0)
            return size;
        else
            return size + block_size - (size % block_size);
    }

    VideoEncoder (std::array<unsigned short, 2> dimensions) : m_width(dimensions[0]), m_height(dimensions[1]) {
    }

    virtual ~VideoEncoder () = default;

    std::array<unsigned short, 2> Dims() const {
        return {m_width, m_height};
    }

    virtual R8G8B8A8* WriteFrameBegin () = 0;
    virtual HRESULT   WriteFrameEnd () = 0;


    size_t GetRGBABuffersize()  {
        return 4 * Align(m_width) * Align(m_height);
    }

    size_t GetYUV420Buffersize()  {
        return 3 * Align(m_width) * Align(m_height)/ 2 ;
    }


    size_t GetAlignedSize(unsigned short dim) {
        return Align(dim);
    }
    

    HRESULT WriteFrame (const R8G8B8A8* src_data, bool swap_rb) {
        R8G8B8A8 * buffer_ptr = WriteFrameBegin();

        for (unsigned int j = 0; j < m_height; j++) {
#ifdef ENABLE_FFMPEG
            const R8G8B8A8 * src_row = &src_data[j*m_width];
#else
            const R8G8B8A8 * src_row = &src_data[(m_height-1-j)*m_width]; // flip upside down
#endif
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

    static R8G8B8A8 SwapRGBAtoBGRA (R8G8B8A8 in) {
        return{ in.b, in.g, in.r, in.a };
    }

    static void RGBfromYUV(double& R, double& G, double& B, double Y, double U, double V)
    {
        Y -= 16;
        U -= 128;
        V -= 128;
        R = 1.164 * Y + 1.596 * V;
        G = 1.164 * Y - 0.392 * U - 0.813 * V;
        B = 1.164 * Y + 2.017 * U;
    }

    static void YUVfromRGB(double& Y, double& U, double& V, const double R, const double G, const double B)
    {
        Y = 0.257 * R + 0.504 * G + 0.098 * B + 16;
        U = -0.148 * R - 0.291 * G + 0.439 * B + 128;
        V = 0.439 * R - 0.368 * G - 0.071 * B + 128;
    }

protected:
    const unsigned short m_width;  ///< horizontal img. resolution (excluding padding)
    const unsigned short m_height; ///< vertical img. resolution (excluding padding)
};


#ifndef ENABLE_FFMPEG
/** Media-Foundation-based H.264 video encoder. */
class VideoEncoderMF : public VideoEncoder {
public:
    /** File-based video encoding. */
    VideoEncoderMF (std::array<unsigned short, 2> dimensions, unsigned int fps, const wchar_t * filename) : VideoEncoderMF(dimensions, fps) {
        const unsigned int bit_rate = static_cast<unsigned int>(0.78f*fps*Align(m_width)*Align(m_height)); // yields 40Mb/s for 1920x1080@25fps (max blu-ray quality)

        CComPtr<IMFAttributes> attribs;
        COM_CHECK(MFCreateAttributes(&attribs, 0));
        COM_CHECK(attribs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4));
        COM_CHECK(attribs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));

        // create sink writer with specified output format
        COM_CHECK(MFCreateSinkWriterFromURL(filename, nullptr, attribs, &m_sink_writer));
        IMFMediaTypePtr mediaTypeOut = MediaTypeutput(fps, bit_rate);
        COM_CHECK(m_sink_writer->AddStream(mediaTypeOut, &m_stream_index));

        // connect input to output
        IMFMediaTypePtr mediaTypeIn = MediaTypeInput(fps);
        COM_CHECK(m_sink_writer->SetInputMediaType(m_stream_index, mediaTypeIn, nullptr));
        COM_CHECK(m_sink_writer->BeginWriting());
    }

    /** Stream-based video encoding. 
        The underlying MFCreateFMPEG4MediaSink system call require Windows 8 or newer. */
    VideoEncoderMF (std::array<unsigned short, 2> dimensions, unsigned int fps, IMFByteStream * stream) : VideoEncoderMF(dimensions, fps) {
        const unsigned int bit_rate = static_cast<unsigned int>(0.78f*fps*m_width*m_height); // yields 40Mb/s for 1920x1080@25fps

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

            // query group-of-pictures (GoP) size
            CComVariant gop_size;
            COM_CHECK(codec->GetValue(&CODECAPI_AVEncMPVGOPSize, &gop_size));
            //gop_size = (unsigned int)1; // VT_UI4 type
            //COM_CHECK(codec->SetValue(&CODECAPI_AVEncMPVGOPSize, &gop_size));
        }

        COM_CHECK(m_sink_writer->BeginWriting());
    }

    VideoEncoderMF (std::array<unsigned short, 2> dimensions, unsigned int fps) : VideoEncoder(dimensions) {
        COM_CHECK(MFStartup(MF_VERSION));
        COM_CHECK(MFFrameRateToAverageTimePerFrame(fps, 1, const_cast<unsigned long long*>(&m_frame_duration)));
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

    R8G8B8A8* WriteFrameBegin () override {
        const DWORD frame_size = 3*Align(m_width)*Align(m_height)/2;
        //const DWORD frame_size = 4 * Align(m_width) * Align(m_height);

        // Create a new memory buffer.
        if (!m_buffer)
            COM_CHECK(MFCreateMemoryBuffer(frame_size, &m_buffer));

        // Lock buffer to get data pointer
        R8G8B8A8 * buffer_ptr = nullptr;
        COM_CHECK(m_buffer->Lock(reinterpret_cast<BYTE**>(&buffer_ptr), NULL, NULL));
        return buffer_ptr;
    }

    HRESULT WriteFrameEnd () override {
        const DWORD frame_size = 3 / 2 * Align(m_width)*Align(m_height);
        //const DWORD frame_size = 4 * Align(m_width) * Align(m_height);

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

        // increment time
        m_time_stamp += m_frame_duration;
        return S_OK;
    }

private:
    IMFMediaTypePtr MediaTypeInput (unsigned int fps) {
        // configure input format. Frame size is aligned to avoid crash
        IMFMediaTypePtr mediaTypeIn;
        COM_CHECK(MFCreateMediaType(&mediaTypeIn));
        COM_CHECK(mediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        COM_CHECK(mediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_IYUV)); // X8R8G8B8 format
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

    const unsigned long long m_frame_duration = 0;
    long long                m_time_stamp = 0;

    CComPtr<IMFMediaSink>    m_media_sink;
    IMFSinkWriterPtr         m_sink_writer;
    IMFMediaBufferPtr        m_buffer;
    unsigned long            m_stream_index = 0;
};

#else

/** FFMPEG-based H.264 video encoder. */
class VideoEncoderFF : public VideoEncoder {
public:
    /** Stream writing callback. */
    static int WritePackage (void *opaque, uint8_t *buf, int buf_size) {
        IMFByteStream * stream = reinterpret_cast<IMFByteStream*>(opaque);
        ULONG bytes_written = 0;
        if (FAILED(stream->Write(buf, buf_size, &bytes_written)))
            return -1;

        return buf_size;
    }

    VideoEncoderFF (std::array<unsigned short, 2> dimensions, unsigned int fps) : VideoEncoder(dimensions), m_fps(fps) {
#if LIBAVFORMAT_VERSION_MAJOR < 58
        av_register_all();
#endif

        /* allocate the output media context */
        avformat_alloc_output_context2(&out_ctx, nullptr, "mp4", nullptr);
        if (!out_ctx)
            throw std::runtime_error("avformat_alloc_output_context2 failure");

        m_rgb_buf.resize(Align(m_width)*Align(m_height));
    }

    VideoEncoderFF (std::array<unsigned short, 2> dimensions, unsigned int fps, const wchar_t * _filename) : VideoEncoderFF(dimensions, fps) {
        // Add the video streams using the default format codecs and initialize the codecs
        AVCodec * video_codec = nullptr;
        std::tie(video_codec, stream, enc) = add_stream(out_ctx->oformat->video_codec, out_ctx);

        // open the video codecs and allocate the necessary encode buffers
        frame = open_video(video_codec, nullptr, enc, stream->codecpar);

        // open the output file
        assert(!(out_ctx->oformat->flags & AVFMT_NOFILE));
        auto filename = ToAscii(_filename);
        int ret = avio_open(&out_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            throw std::runtime_error("avio_open failure");
        }

        WriteHeader(nullptr);
    }

    VideoEncoderFF (std::array<unsigned short, 2> dimensions, unsigned int fps, IMFByteStream * socket) : VideoEncoderFF(dimensions, fps) {
        // Add the video streams using the default format codecs and initialize the codecs
        AVCodec * video_codec = nullptr;
        std::tie(video_codec, stream, enc) = add_stream(out_ctx->oformat->video_codec, out_ctx);

        // REF: https://ffmpeg.org/ffmpeg-formats.html#Options-8 (-movflags arguments)
        // REF: https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/movenc.c
        AVDictionary *opt = nullptr;
        av_dict_set(&opt, "movflags", "empty_moov+default_base_moof+frag_every_frame", 0); // fragmented MP4

        // open the video codecs and allocate the necessary encode buffers
        frame = open_video(video_codec, opt, enc, stream->codecpar);

        m_out_buf.resize(16*1024*1024); // 16MB
        m_socket = socket; // prevent socket from being destroyed before this object
        out_ctx->pb = avio_alloc_context(m_out_buf.data(), static_cast<int>(m_out_buf.size()), 1/*writable*/, socket, nullptr/*read*/, WritePackage, nullptr/*seek*/);
        //out_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

        WriteHeader(opt);
    }

    ~VideoEncoderFF() {
        // flush encoder to mark end of stream
        WriteFrameImpl(false);

        // write file ending (discard error codes)
        av_write_trailer(out_ctx);

        avcodec_close(enc);
        avcodec_free_context(&enc);

        av_frame_free(&frame);

        avio_context_free(&out_ctx->pb);
        avformat_free_context(out_ctx);
    }

    void WriteHeader (AVDictionary *opt) {
#ifndef NDEBUG
        // write info to console
        av_dump_format(out_ctx, 0, nullptr, 1);
#endif

        // Write the stream header, if any
        int ret = avformat_write_header(out_ctx, &opt);
        if (ret < 0)
            throw std::runtime_error("avformat_write_header failed");
    }

    R8G8B8A8* WriteFrameBegin () override {
        return m_rgb_buf.data();
    }

    HRESULT   WriteFrameEnd () override {
        return WriteFrameImpl(true);
    }

    HRESULT WriteFrameImpl (bool has_frame) {
        if (has_frame) {
            if (av_frame_make_writable(frame) < 0)
                exit(1);

            assert(enc->pix_fmt == AV_PIX_FMT_YUV420P);

            // RGB to YUV conversion
            for (int y = 0; y < m_height; y++) {
                for (int x = 0; x < m_width; x++) {
                    R8G8B8A8 rgb = m_rgb_buf[y*enc->width + x];
                    // convert to YUV
                    unsigned char Y=0, U=0, V=0;
                    YUVfromRGB(rgb, Y, U, V);
                    // write Y value
                    frame->data[0][y*frame->linesize[0] + x] = Y;
                    // write subsambled Cb,Cr values
                    if (((x % 2) == 0) && ((y % 2) == 0)) {
                        frame->data[1][y/2*frame->linesize[1] + x/2] = V/4;
                        frame->data[2][y/2*frame->linesize[2] + x/2] = U/4;
                    } else {
                        frame->data[1][y/2*frame->linesize[1] + x/2] += V/4;
                        frame->data[2][y/2*frame->linesize[2] + x/2] += U/4;
                    }
                }
            }

            frame->pts = next_pts;
            next_pts++; // increment next pts
        }

        // encode frame
        int ret = avcodec_send_frame(enc, has_frame ? frame : nullptr);
        if (ret < 0)
            throw std::runtime_error("Error encoding video frame");

        AVPacket pkt = {};
        av_init_packet(&pkt);

        // process packages
        for (;;) {
            ret = avcodec_receive_packet(enc, &pkt);
            if (ret == AVERROR(EAGAIN))
                break; // not yet available
            else if (!has_frame && (ret == AVERROR_EOF))
                break; // end of stream
            else if (ret < 0)
                throw std::runtime_error("avcodec_receive_packet failed");

            // rescale output packet timestamp values from codec to stream timebase
            av_packet_rescale_ts(&pkt, enc->time_base, stream->time_base);
            pkt.stream_index = stream->index;

            // write compressed frame to stream
            ret = av_interleaved_write_frame(out_ctx, &pkt);
            if (ret < 0)
                return E_FAIL;
        }

        return S_OK;
    }

private:
    /* Add an output stream. */
    std::tuple<AVCodec*,AVStream*, AVCodecContext*> add_stream (AVCodecID codec_id, /*in/out*/AVFormatContext *out_ctx) {
        // find the encoder
        AVCodec *codec = avcodec_find_encoder(codec_id);
        if (!codec) {
            const char * name = avcodec_get_name(codec_id);
            throw std::runtime_error("Could not find encoder for");
        }
        assert(codec->type == AVMEDIA_TYPE_VIDEO);

        AVStream * stream = avformat_new_stream(out_ctx, NULL);
        if (!stream)
            throw std::runtime_error("Could not allocate stream");

        stream->id = out_ctx->nb_streams-1;

        // setup context
        AVCodecContext *enc = avcodec_alloc_context3(codec);
        if (!enc)
            throw std::runtime_error("Could not alloc an encoding context");
        {
            enc->codec_id = codec_id;
            enc->bit_rate = static_cast<unsigned int>(0.78f*m_fps*m_width*m_height); // yields 40Mb/s for 1920x1080@25fps
            // Resolution must be a multiple of two
            enc->width    = Align(m_width);
            enc->height   = Align(m_height);
            /* timebase: This is the fundamental unit of time (in seconds) in terms
            * of which frame timestamps are represented. For fixed-fps content,
            * timebase should be 1/framerate and timestamp increments should be
            * identical to 1. */
            enc->time_base = { 1, static_cast<int>(m_fps) };

            enc->gop_size = 12; // group of pictures size
            enc->pix_fmt  = AV_PIX_FMT_YUV420P; // default pix_fmt

            int res = av_opt_set(enc->priv_data, "tune", "zerolatency", 0);
            if (res < 0)
                throw std::runtime_error("zerolatency tuning failed");

            // Some formats want stream headers to be separate
            if (out_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        stream->time_base = enc->time_base;
        return std::tie(codec, stream, enc);
    }

    static AVFrame *alloc_frame (enum AVPixelFormat pix_fmt, int width, int height) {
        AVFrame* picture = av_frame_alloc();
        assert(picture);

        picture->format = pix_fmt;
        picture->width  = width;
        picture->height = height;

        // allocate the buffers for the frame data
        int ret = av_frame_get_buffer(picture, 32);
        if (ret < 0)
            throw std::runtime_error("Could not allocate frame data");

        return picture;
    }

    static AVFrame* open_video (const AVCodec *codec, const AVDictionary *opt_arg, /*in/out*/AVCodecContext *c, /*in/out*/AVCodecParameters *codecpar) {
        AVDictionary *opt = nullptr;
        av_dict_copy(&opt, opt_arg, 0);

        /* open the codec */
        int ret = avcodec_open2(c, codec, &opt);
        av_dict_free(&opt);
        if (ret < 0)
            throw std::runtime_error("Could not open video codec");

        /* allocate and init a re-usable frame */
        AVFrame* frame = alloc_frame(c->pix_fmt, c->width, c->height);
        if (!frame)
            throw std::runtime_error("Could not allocate video frame");

        assert(c->pix_fmt == AV_PIX_FMT_YUV420P);

        // copy the stream parameters to the muxer
        ret = avcodec_parameters_from_context(codecpar, c);
        if (ret < 0)
            throw std::runtime_error("Could not copy the stream parameters");

        return frame;
    }

    /** "Homemade" RGB to YUV conversion. Please replace with more authoritative alternative if/when possible.
        REF: http://www.fourcc.org/fccyvrgb.php */
    static void YUVfromRGB (const R8G8B8A8 rgb, unsigned char& Y, unsigned char& U, unsigned char& V) {
        Y = static_cast<unsigned char>( 0.257f*rgb.r + 0.504f*rgb.g + 0.098f*rgb.b +  16);
        U = static_cast<unsigned char>(-0.148f*rgb.r - 0.291f*rgb.g + 0.439f*rgb.b + 128);
        V = static_cast<unsigned char>( 0.439f*rgb.r - 0.368f*rgb.g - 0.071f*rgb.b + 128);
    }

    unsigned int       m_fps = 0;
    int64_t         next_pts = 0; // pts of the next frame that will be generated
    AVFormatContext *out_ctx = nullptr;
    AVStream         *stream = nullptr;
    AVCodecContext      *enc = nullptr;
    AVFrame           *frame = nullptr;

    std::vector<R8G8B8A8>      m_rgb_buf;
    std::vector<unsigned char> m_out_buf;
    CComPtr<IMFByteStream>     m_socket;
};

#endif
