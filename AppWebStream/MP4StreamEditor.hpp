#include <cassert>
#include <tuple>
#include <vector>
#include "MP4Utils.hpp"


struct TimeHandler {
    bool updateSampleDuration = false;

    uint64_t startTime = 0;       // creation- & modification time
    uint32_t sample_duration = 0; // frame duration (typ 1000)
    uint64_t cur_time = 0;
    uint32_t timeScale = 0;       // time units per second: 1000*fps (50000 = 50fps) [unused]
};

/** Process atoms within a MPEG4 MovieFragment (moof) to make the stream comply with ISO base media file format (https://b.goeswhere.com/ISO_IEC_14496-12_2015.pdf , https://github.com/MPEGGroup/isobmff).
    Work-around for shortcommings in the Media Foundation MPEG4 file sink (https://learn.microsoft.com/en-us/windows/win32/medfound/mpeg-4-file-sink).
    Please delete this class if a better alternative becomes available.
Expected atom hiearchy:
[moov] movie box
* [mvhd] movie header box
* [trak] track box
  - [tkhd] track header box
  - [mdia] media box
    * [mdhd] media header box
    * [hdlr] handler box
    * [minf] media information box
      - [vmhd] video media header box
      - [dinf] data information box
        * [dref] data reference box
          - [url] data entry url box
      - [stbl] ample table box
        * [stsd] sample description box
          - [avc1] (will be modified)
            * [avcC] AVC configuration box
[moof] movie fragment
* [mfhd] movie fragment header
* [traf] track fragment
  - [tfhd] track fragment header (will be modified)
  - [tfdt] track fragment decode timebox (will be added)
  - [trun] track run (will be modified)
[mdat] fragment with H.264 video data
*/
class MP4StreamEditor {
    static constexpr uint32_t HEADER_SIZE = 8; // atom header size (4bytes size + 4byte name)
    static constexpr uint32_t VERSION_FLAGS_SIZE = 4;  // version & flags size (1byte version + 3bytes flags)

    static constexpr uint32_t BASE_DATA_OFFSET_SIZE = 8; // size of tfhd flag to remove
    static constexpr uint32_t TFDT_SIZE = 20;    // size of new tfdt atom that is added

public:
    MP4StreamEditor() = default;

    MP4StreamEditor(double dpi, uint64_t startTime1904) {
        m_dpi = dpi;
        m_time.startTime = startTime1904;
    }

    /** Parse MPEG4 bitstream to extract parameters that are not directly accessible through the Media Foundation and/or FFMPEG APIs.
        Returns true if a new parameter have been extracted. */
    bool ParseStream (std::string_view buffer) {
        if (buffer.size() < HEADER_SIZE)
            return false; // buffer too small for MPEG atom header parsing

        const char* ptr = buffer.data();

        if (!IsAtomType(ptr, "ftyp"))
            return false;
        {
            // "ftyp" atom
            uint32_t ftyp_size = GetAtomSize(ptr);
            ptr += ftyp_size;
        }

        if (!IsAtomType(ptr, "uuid"))
            return false;
        {
            // "uuid" atom
            uint32_t uuid_size = GetAtomSize(ptr);
            ptr += uuid_size;
        }

        if (!IsAtomType(ptr, "pdin"))
            return false;
        {
            // "pdin" atom
            uint32_t pdin_size = GetAtomSize(ptr);
            ptr += pdin_size;
        }

        if (!IsAtomType(ptr, "moov"))
            return false;
        {
            // Movie box (moov)
            uint32_t moov_size = GetAtomSize(ptr);
            std::string_view moov_buf = buffer.substr(ptr - buffer.data());
            assert(moov_size <= moov_buf.size());
            return ParseMoov(moov_buf);
        }
    }

    /** Edit MPEG4 bitstream to update parameters that are not directly accessible through the Media Foundation and/or FFMPEG APIs.
        Returns a (ptr, size) tuple pointing to a potentially modified buffer. */
    std::string_view EditStream (std::string_view buffer) {
        if (buffer.size() < HEADER_SIZE)
            return buffer; // buffer too small for MPEG atom header parsing

        // REF: https://developer.apple.com/documentation/quicktime-file-format/movie_atom
        if (IsAtomType(buffer.data(), "moov")) {
            uint32_t atom_size = GetAtomSize(buffer.data());
            assert(atom_size == buffer.size());

            // Movie box (moov)
            ModifyMoov(buffer);
            return buffer;
        } else if (IsAtomType(buffer.data(), "moof")) {
            uint32_t atom_size = GetAtomSize(buffer.data());
            assert(atom_size <= buffer.size());

            // Movie Fragment (moof)
#ifdef ENABLE_FFMPEG
            return ModifyMoof(buffer.data(), (ULONG)buffer.size(), false);
#else
            assert(atom_size == buffer.size());
            return ModifyMoof(buffer.data(), (ULONG)buffer.size(), true);
#endif
        } else if (IsAtomType(buffer.data(), "mdat")) {
            //uint32_t atom_size = GetAtomSize(buffer.data());
            // don't check buffer size, since the payload arrives in a later call

            // Media Data (mdat) - only header data
            return buffer;
        } else {
            // leave other atoms and mdat payload with H.264 data unchanged
            return buffer;
        }
    }

    void SetNextFrameTime(uint64_t nextTime) {
        m_time.cur_time = nextTime;
    }

    double GetDPI() const {
        return m_dpi;
    }
    uint64_t GetStartTime() const {
        return m_time.startTime;
    }

private:
    bool ParseMoov(const std::string_view buffer) {
        const char* ptr = (char*)buffer.data();

        assert(IsAtomType(ptr, "moov"));
        assert(GetAtomSize(ptr) <= buffer.size());
        ptr += HEADER_SIZE; // skip size & type

        // NOTE: Optional "prfl" atom here

        {
            // entering "mvhd" atom
            // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/MovieHeaderBox.java
            assert(IsAtomType(ptr, "mvhd"));
            uint32_t mvhd_len = GetAtomSize(ptr);
            ptr += HEADER_SIZE; // skip size & type

            auto version = DeSerialize<uint8_t>(ptr);
            ptr += 1;

            //uint32_t flags = DeSerialize<uint24_t>(ptr);
            ptr += sizeof(uint24_t);

            uint64_t modifyTime = 0;
            std::tie(m_time.startTime, modifyTime, ptr) = ParseCreateModifyTime(ptr, version);

            // read timescale (number of time units per second)
            m_time.timeScale = DeSerialize<uint32_t>(ptr); // 1000*fps
            ptr += 4;

            uint64_t duration = 0;
            if (version == 1) {
                duration = DeSerialize<uint64_t>(ptr);
                ptr += 8;
            } else {
                duration = DeSerialize<uint32_t>(ptr);
                ptr += 4;
            }

            //double rate = ReadFixed1616(ptr); // preferred playback rate (16+16 fraction) (typ 1.0)
            ptr += 4;

            //double volume = ReadFixed88(ptr); // master volume of file (8+8 fraction) (typ 1.0)
            ptr += 2;

            ptr += sizeof(uint16_t); // reserved
            ptr += sizeof(uint32_t) * 2; // reserved

            // matrix to map points from one coordinate space into another
            //matrix mat(ptr);
            ptr += matrix::SIZE;

            ptr += sizeof(uint32_t) * 6; // reserved

            //auto nextTrackId = DeSerialize<uint32_t>(ptr); // (typ 2)
            ptr += 4;

            // end of "mvhd" atom
            assert(ptr == buffer.data() + HEADER_SIZE + mvhd_len);
        }

        // NOTE: Optional "clip" atom here

        //NOTE: It might be more than one "track" atom here
        {
            // entering "trak" atom
            assert(IsAtomType(ptr, "trak"));
            //uint32_t trak_len = GetAtomSize(ptr);
            ptr += HEADER_SIZE; // skip size & type

            // NOTE: Optional "prfl" atom here

            {
                // skip over "tkhd" atom
                // REF: https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mov.c#L5478
                assert(IsAtomType(ptr, "tkhd"));
                uint32_t tkhd_len = GetAtomSize(ptr);
                ptr += tkhd_len;
            }

            // NOTE: Optional "tapt", "clip", "matt", "edts", "tref", "txas", "load", "imap" atoms here

            {
                // entring "mdia" atom
                assert(IsAtomType(ptr, "mdia"));
                //uint32_t mdia_len = GetAtomSize(ptr);
                ptr += HEADER_SIZE; // skip size & type

                {
                    // skip over "mdhd" atom
                    // REF: https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mov.c#L1864
                    assert(IsAtomType(ptr, "mdhd"));
                    uint32_t mdhd_len = GetAtomSize(ptr);
                    ptr += mdhd_len;
                }

                // NOTE: Optional "elng" atom here

                {
                    // skip over "hdlr" atom
                    assert(IsAtomType(ptr, "hdlr"));
                    uint32_t hdlr_len = GetAtomSize(ptr);
                    ptr += hdlr_len;
                }

                {
                    // entering "minf" atom
                    assert(IsAtomType(ptr, "minf"));
                    //uint32_t minf_len = GetAtomSize(ptr);
                    ptr += HEADER_SIZE; // skip size & type

                    {
                        // skip over "vmhd" atom
                        assert(IsAtomType(ptr, "vmhd"));
                        uint32_t vmhd_len = GetAtomSize(ptr);
                        ptr += vmhd_len;
                    }

                    // NOTE: Optional "hdlr" atom here

                    {
                        // skip over "dinf" atom
                        assert(IsAtomType(ptr, "dinf"));
                        uint32_t dinf_len = GetAtomSize(ptr);
                        ptr += dinf_len;
                    }

                    {
                        // entering "stbl" atmom
                        assert(IsAtomType(ptr, "stbl"));
                        //uint32_t stbl_len = GetAtomSize(ptr);
                        ptr += HEADER_SIZE; // skip size & type

                        {
                            // entering "stsd" atom (see https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/movenc.c)
                            // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/SampleDescriptionBox.java
                            assert(IsAtomType(ptr, "stsd"));
                            //uint32_t stsd_len = GetAtomSize(ptr);
                            ptr += HEADER_SIZE; // skip size & type

                            auto version = DeSerialize<uint8_t>(ptr);
                            assert(version == 0);
                            ptr += sizeof(uint8_t);

                            uint32_t flags = DeSerialize<uint24_t>(ptr);
                            assert(flags == 0);
                            ptr += sizeof(uint24_t);

                            uint32_t entryCount = DeSerialize<uint32_t>(ptr);
                            ptr += sizeof(uint32_t);

                            for (uint32_t entry = 0; entry < entryCount; entry++) {
                                // entering "avc1" atom
                                // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/sampleentry/VisualSampleEntry.java
                                assert(IsAtomType(ptr, "avc1"));
                                //uint32_t avc1_len = GetAtomSize(ptr);
                                ptr += HEADER_SIZE; // skip size & type

                                ptr += 6; // skip first 6 bytes

                                //auto dataReferenceIdx = DeSerialize<uint16_t>(ptr);
                                ptr += sizeof(uint16_t);

                                auto reserved = DeSerialize<uint16_t>(ptr);
                                assert(reserved == 0);
                                ptr += sizeof(uint16_t);

                                reserved = DeSerialize<uint16_t>(ptr);
                                assert(reserved == 0);
                                ptr += sizeof(uint16_t);

                                ptr += 3 * sizeof(uint32_t); // skip 3 "predefined" values that should all be zero

                                //auto width = DeSerialize<uint16_t>(ptr);
                                ptr += 2;
                                //auto height = DeSerialize<uint16_t>(ptr);
                                ptr += 2;
                                //printf("avc1 atom resolution: (%u, %u)\n", width, height);

                                // read horizontal and vertical video DPI in fixed-point 16+16 format
                                m_dpi = ReadFixed1616(ptr);
                                double vdpi = ReadFixed1616(ptr + 4);
                                assert(m_dpi == vdpi); // same horizontal and vertical DPI

                                // ignore the remaining parameters
                            }
                        }

                        // ignore the remaining parameters
                    }
                }

                // NOTE: Optional "udta" atom here
            }
        }

        //NOTE: Ignore remaining "udta", "ctab", ,"cmov", "rmra" child atoms

        return true;
    }

    void ModifyMoov (std::string_view buffer) {
        char* ptr = (char*)buffer.data();
        // REF: https://developer.apple.com/documentation/quicktime-file-format/movie_atom
        assert(IsAtomType(ptr, "moov"));
        assert(GetAtomSize(ptr) == buffer.size());
        ptr += HEADER_SIZE; // skip size & type

        // NOTE: Optional "prfl" atom here

        {
            // entering "mvhd" atom
            // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/MovieHeaderBox.java
            assert(IsAtomType(ptr, "mvhd"));
            uint32_t mvhd_len = GetAtomSize(ptr);
            ptr += HEADER_SIZE; // skip size & type

            auto version = DeSerialize<uint8_t>(ptr);
            ptr += 1;

            //uint32_t flags = DeSerialize<uint24_t>(ptr);
            ptr += sizeof(uint24_t);

            ptr = UpdateCreateModifyTime(ptr, version, m_time.startTime);

            // read timescale (number of time units per second)
            m_time.timeScale = DeSerialize<uint32_t>(ptr); // 1000*fps
            ptr += 4;

            uint64_t duration = 0;
            if (version == 1) {
                duration = DeSerialize<uint64_t>(ptr);
                ptr += 8;
            } else {
                duration = DeSerialize<uint32_t>(ptr);
                ptr += 4;
            }
            
            //double rate = ReadFixed1616(ptr); // preferred playback rate (16+16 fraction) (typ 1.0)
            ptr += 4;

            //double volume = ReadFixed88(ptr); // master volume of file (8+8 fraction) (typ 1.0)
            ptr += 2;

            ptr += sizeof(uint16_t); // reserved
            ptr += sizeof(uint32_t) * 2; // reserved

            // matrix to map points from one coordinate space into another
            //matrix mat(ptr);
            ptr += matrix::SIZE;

            ptr += sizeof(uint32_t) * 6; // reserved

            //auto nextTrackId = DeSerialize<uint32_t>(ptr); // (typ 2)
            ptr += 4;

            // end of "mvhd" atom
            assert(ptr == buffer.data() + HEADER_SIZE + mvhd_len);
        }

        // NOTE: Optional "clip" atom here

        //NOTE: It might be more than one "track" atom here
        {
            // entering "trak" atom
            assert(IsAtomType(ptr, "trak"));
            //uint32_t trak_len = GetAtomSize(ptr);
            ptr += HEADER_SIZE; // skip size & type

            // NOTE: Optional "prfl" atom here

            {
                // partially parse "tkhd" atom
                // REF: https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mov.c#L5478
                assert(IsAtomType(ptr, "tkhd"));
                uint32_t tkhd_len = GetAtomSize(ptr);

                char* tkhd_ptr = ptr + HEADER_SIZE;

                auto version = DeSerialize<uint8_t>(tkhd_ptr);
                tkhd_ptr += 1;

                //uint32_t flags = DeSerialize<uint24_t>(tkhd_ptr);
                tkhd_ptr += sizeof(uint24_t);

                tkhd_ptr = UpdateCreateModifyTime(tkhd_ptr, version, m_time.startTime);

                ptr += tkhd_len;
            }

            // NOTE: Optional "tapt", "clip", "matt", "edts", "tref", "txas", "load", "imap" atoms here

            {
                // entring "mdia" atom
                assert(IsAtomType(ptr, "mdia"));
                //uint32_t mdia_len = GetAtomSize(ptr);
                ptr += HEADER_SIZE; // skip size & type

                {
                    // partially parse "mdhd" atom
                    // REF: https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mov.c#L1864
                    assert(IsAtomType(ptr, "mdhd"));
                    uint32_t mdhd_len = GetAtomSize(ptr);
                    char* mdhd_ptr = ptr + HEADER_SIZE;

                    auto version = DeSerialize<uint8_t>(mdhd_ptr);
                    mdhd_ptr += 1;

                    //uint32_t flags = DeSerialize<uint24_t>(mdhd_ptr);
                    mdhd_ptr += sizeof(uint24_t);

                    mdhd_ptr = UpdateCreateModifyTime(mdhd_ptr, version, m_time.startTime);

                    ptr += mdhd_len;
                }

                // NOTE: Optional "elng" atom here

                {
                    // skip over "hdlr" atom
                    assert(IsAtomType(ptr, "hdlr"));
                    uint32_t hdlr_len = GetAtomSize(ptr);
                    ptr += hdlr_len;
                }

                {
                    // entering "minf" atom
                    assert(IsAtomType(ptr, "minf"));
                    //uint32_t minf_len = GetAtomSize(ptr);
                    ptr += HEADER_SIZE; // skip size & type

                    {
                        // skip over "vmhd" atom
                        assert(IsAtomType(ptr, "vmhd"));
                        uint32_t vmhd_len = GetAtomSize(ptr);
                        ptr += vmhd_len;
                    }

                    // NOTE: Optional "hdlr" atom here

                    {
                        // skip over "dinf" atom
                        assert(IsAtomType(ptr, "dinf"));
                        uint32_t dinf_len = GetAtomSize(ptr);
                        ptr += dinf_len;
                    }

                    {
                        // entering "stbl" atmom
                        assert(IsAtomType(ptr, "stbl"));
                        //uint32_t stbl_len = GetAtomSize(ptr);
                        ptr += HEADER_SIZE; // skip size & type

                        {
                            // entering "stsd" atom (see https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/movenc.c)
                            // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/SampleDescriptionBox.java
                            assert(IsAtomType(ptr, "stsd"));
                            //uint32_t stsd_len = GetAtomSize(ptr);
                            ptr += HEADER_SIZE; // skip size & type

                            auto version = DeSerialize<uint8_t>(ptr);
                            assert(version == 0);
                            ptr += sizeof(uint8_t);

                            uint32_t flags = DeSerialize<uint24_t>(ptr);
                            assert(flags == 0);
                            ptr += sizeof(uint24_t);

                            uint32_t entryCount = DeSerialize<uint32_t>(ptr);
                            ptr += sizeof(uint32_t);

                            for (uint32_t entry = 0; entry < entryCount; entry++) {
                                // entering "avc1" atom
                                // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/sampleentry/VisualSampleEntry.java
                                assert(IsAtomType(ptr, "avc1"));
                                //uint32_t avc1_len = GetAtomSize(ptr);
                                ptr += HEADER_SIZE; // skip size & type

                                ptr += 6; // skip first 6 bytes

                                //auto dataReferenceIdx = DeSerialize<uint16_t>(ptr);
                                ptr += sizeof(uint16_t);

                                auto reserved = DeSerialize<uint16_t>(ptr);
                                assert(reserved == 0);
                                ptr += sizeof(uint16_t);

                                reserved = DeSerialize<uint16_t>(ptr);
                                assert(reserved == 0);
                                ptr += sizeof(uint16_t);

                                ptr += 3 * sizeof(uint32_t); // skip 3 "predefined" values that should all be zero

                                auto width = DeSerialize<uint16_t>(ptr);
                                ptr += 2;
                                auto height = DeSerialize<uint16_t>(ptr);
                                ptr += 2;
                                printf("avc1 atom resolution: (%u, %u)\n", width, height);

                                // check existing video DPI in fixed-point 16+16 format
                                // Resolution hardcoded to 72dpi (0x00480000) in FFMPEG encoder (https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/movenc.c)
                                // It also appear to be hardocded in the MF encoder. I've at least not found a parameter for adjusting it.
#if 0
                                double dpi = ReadFixed1616(ptr);
                                assert(dpi == 72); // 72.00 horizontal DPI
                                dpi = ReadFixed1616(ptr + 4);
                                assert(dpi == 72); // 72.00 vertical DPI
#endif
                                // update video DPI in fixed-point 16+16 format
                                WriteFixed1616(ptr, m_dpi);    // horizontal DPI
                                WriteFixed1616(ptr + 4, m_dpi);// vertical DPI

                                // ignore the remaining parameters
                            }
                        }

                        // ignore the remaining parameters
                    }
                }

                // NOTE: Optional "udta" atom here
            }
        }

        //NOTE: Ignore remaining "udta", "ctab", ,"cmov", "rmra" child atoms
    }

    /** REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/MovieFragmentBox.java */
    std::string_view ModifyMoof (const char* buf, const ULONG buf_size, bool add_tfdt) {
        static size_t s_frame_counter = 0;
        s_frame_counter++;
        m_paused = (s_frame_counter % 20) >= 10; // toggle every 10 frame
        printf("Paused: %u\n", m_paused);

        m_time.updateSampleDuration = true;
        if (m_paused)
            m_time.sample_duration = 1;
        else
            m_time.sample_duration = 1000;

        assert(IsAtomType(buf, "moof"));
        assert(GetAtomSize(buf) <= buf_size);

        uint32_t new_buf_size = buf_size;
        if (add_tfdt)
            new_buf_size = buf_size - BASE_DATA_OFFSET_SIZE + TFDT_SIZE;

        if (add_tfdt) {
            // copy to temporary buffer to make room for "tfdt" atom insertion
            m_moof_buf.resize(new_buf_size);
            memcpy(m_moof_buf.data()/*dst*/, buf, buf_size);
        }
        char* const moof_ptr = add_tfdt ? m_moof_buf.data() : (char*)buf;

        // "mfhd" atom immediately follows
        char* ptr = moof_ptr + HEADER_SIZE;
        {
            // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/MovieFragmentHeaderBox.java
            uint32_t mfhd_size = GetAtomSize(ptr);
            if (!IsAtomType(ptr, "mfhd")) // movie fragment header
                throw std::runtime_error("not a \"mfhd\" atom");
            ptr += HEADER_SIZE;

            ptr += 4; // skip version & flags

            auto seq_nr = DeSerialize<uint32_t>(ptr); // increases by one per fragment
            seq_nr;
            ptr += sizeof(uint32_t);

            assert(ptr == moof_ptr + HEADER_SIZE + mfhd_size);
        }

        // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackFragmentBox.java
        char* const traf_ptr = ptr;
        uint32_t traf_size = GetAtomSize(traf_ptr);
        if (!IsAtomType(traf_ptr, "traf")) // track fragment
            throw std::runtime_error("not a \"traf\" atom");

        // TrackFragmentHeaderAtom ("tfhd") immediately follows
        char* tfhd_ptr = traf_ptr + HEADER_SIZE;

        unsigned long tfhd_idx = static_cast<unsigned long>(tfhd_ptr - moof_ptr);
        ProcessTrackFrameChildren(moof_ptr+tfhd_idx, buf_size-tfhd_idx, new_buf_size, add_tfdt);

        if (add_tfdt) {
            // update "moof" parent atom size after size change
            Serialize<uint32_t>(moof_ptr, new_buf_size);
            Serialize<uint32_t>(traf_ptr, traf_size - BASE_DATA_OFFSET_SIZE + TFDT_SIZE);
        }
        return std::string_view(moof_ptr, new_buf_size);
    }


    /** Modify the FrackFrame (traf) child atoms to comply with https://www.w3.org/TR/mse-byte-stream-format-isobmff/#movie-fragment-relative-addressing
    Changes done:
    * Modify TrackFragmentHeader (tfhd):
      - remove base-data-offset parameter (reduces size by 8bytes)
      - set default-base-is-moof flag
    * Add missing track fragment decode timebox (tfdt) (increases size by 20bytes)
    * Modify track run box (trun):
      - modify data_offset

    Returns the relative size of the modified child atoms (bytes shrunk or grown). */
    void ProcessTrackFrameChildren (char* tfhd_ptr, const ULONG buf_size, const ULONG new_moof_size, bool add_tfdt) {
        assert(buf_size >= 2 * HEADER_SIZE + 8);

        // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackFragmentHeaderBox.java
        uint32_t tfhd_size = GetAtomSize(tfhd_ptr);
        {
            assert(IsAtomType(tfhd_ptr, "tfhd")); // TrackFragmentHeaderAtom
            // process tfhd content
            char* payload = tfhd_ptr + HEADER_SIZE;
            auto version = DeSerialize<uint8_t>(payload);
            assert(version == 0);
            payload += sizeof(uint8_t);

            {
                // TrackFragmentHeaderAtom ("tfhd") flags (from https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/isom.h)
                constexpr uint32_t MOV_TFHD_BASE_DATA_OFFSET = 0x01;
                //constexpr uint32_t MOV_TFHD_STSD_ID = 0x02;
#ifdef ENABLE_FFMPEG
                constexpr uint32_t MOV_TFHD_DEFAULT_DURATION = 0x08;
                constexpr uint32_t MOV_TFHD_DEFAULT_SIZE = 0x10;
                constexpr uint32_t MOV_TFHD_DEFAULT_FLAGS = 0x20;
#endif
                //constexpr uint32_t MOV_TFHD_DURATION_IS_EMPTY = 0x010000;
                constexpr uint32_t MOV_TFHD_DEFAULT_BASE_IS_MOOF = 0x020000;

                uint32_t flags = DeSerialize<uint24_t>(payload);
#ifdef ENABLE_FFMPEG
                assert(flags == (MOV_TFHD_DEFAULT_DURATION | MOV_TFHD_DEFAULT_SIZE | MOV_TFHD_DEFAULT_FLAGS | MOV_TFHD_DEFAULT_BASE_IS_MOOF)); // 0x00020038
#else
                assert(flags == MOV_TFHD_BASE_DATA_OFFSET);
#endif
                if (add_tfdt) {
                    // 1: set default-base-is-moof flag
                    flags |= MOV_TFHD_DEFAULT_BASE_IS_MOOF;
                    // 2: remove base-data-offset flag
                    flags &= ~MOV_TFHD_BASE_DATA_OFFSET;
                    Serialize<uint24_t>(payload, flags); // write back changes
                }
                payload += sizeof(uint24_t);
            }

            if (add_tfdt)
                Serialize<uint32_t>(tfhd_ptr, tfhd_size-BASE_DATA_OFFSET_SIZE); // shrink atom size

            //auto track_id = DeSerialize<uint32_t>(payload);
            payload += sizeof(uint32_t);          // skip track-ID field (4bytes)

            if (add_tfdt) {
                // move remaining tfhd fields over data_offset
                size_t remaining_size = tfhd_size-HEADER_SIZE-VERSION_FLAGS_SIZE-sizeof(uint32_t)-BASE_DATA_OFFSET_SIZE;
                MemMove(payload/*dst*/, payload+BASE_DATA_OFFSET_SIZE/*src*/, remaining_size/*size*/);
            }
        }
        // pointer to right after shrunken tfhd atom
        char* ptr = tfhd_ptr + tfhd_size;
        if (add_tfdt) {
            ptr -= BASE_DATA_OFFSET_SIZE;

            // move TrackRunAtom ("trun") to make room for a new TrackFragmentHeaderAtom ("tfhd")
            MemMove(ptr+TFDT_SIZE-BASE_DATA_OFFSET_SIZE/*dst*/, ptr/*src*/, buf_size-tfhd_size/*size*/);
        }

        if (add_tfdt) {
            // insert new TrackFragmentHeaderAtom ("tfdt") atom (20bytes)
            // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackFragmentBaseMediaDecodeTimeBox.java
            char* tfdt_ptr = ptr;

            Serialize<uint32_t>(tfdt_ptr, TFDT_SIZE);
            memcpy(tfdt_ptr+4/*dst*/, "tfdt", 4); // track fragment base media decode timebox
            tfdt_ptr += HEADER_SIZE;

            *tfdt_ptr = 1; // version 1 (no other flags)
            tfdt_ptr += VERSION_FLAGS_SIZE; // skip flags
            // write tfdt/baseMediaDecodeTime
            tfdt_ptr = Serialize<uint64_t>(tfdt_ptr, m_time.cur_time);

            assert(tfdt_ptr == ptr + TFDT_SIZE);
            ptr += TFDT_SIZE;
        } else {
            // inspect existing tfdt atom
            char* tfdt_ptr = ptr;
            assert(IsAtomType(tfdt_ptr, "tfdt"));
            uint32_t tfdt_size = GetAtomSize(tfdt_ptr);
            tfdt_ptr += HEADER_SIZE;

            assert(*tfdt_ptr == 1); // check version
            tfdt_ptr += VERSION_FLAGS_SIZE;

            // check baseMediaDecodeTime
            auto baseMediaDecodeTime = DeSerialize<uint64_t>(tfdt_ptr);
            assert(baseMediaDecodeTime == m_time.cur_time);
            tfdt_ptr += sizeof(uint64_t);

            assert(tfdt_ptr == ptr + tfdt_size);
            ptr += tfdt_size;
        }

        {
            // modify TrackRunAtom ("trun")
            // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackRunBox.java
            char* trun_ptr = ptr;
            uint32_t trun_size = GetAtomSize(trun_ptr);
            if (!IsAtomType(trun_ptr, "trun")) // track run box
                throw std::runtime_error("not a \"trun\" atom");
            char* payload = trun_ptr + HEADER_SIZE;

            auto version = DeSerialize<uint8_t>(payload);
            payload += sizeof(uint8_t);
#ifdef ENABLE_FFMPEG
            assert(version == 0);   // check version
#else
            assert(version == 1);   // check version
#endif

            // TrackRunAtom ("trun") flags (from https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/isom.h)
            constexpr uint32_t MOV_TRUN_DATA_OFFSET = 0x01;
            constexpr uint32_t MOV_TRUN_FIRST_SAMPLE_FLAGS = 0x04;
            constexpr uint32_t MOV_TRUN_SAMPLE_DURATION = 0x100;
            constexpr uint32_t MOV_TRUN_SAMPLE_SIZE = 0x200;
            constexpr uint32_t MOV_TRUN_SAMPLE_FLAGS = 0x400;
            constexpr uint32_t MOV_TRUN_SAMPLE_CTS = 0x800;

            uint32_t flags = DeSerialize<uint24_t>(payload);
            // verify that dataOffset, sampleDuration, sampleSize, sampleFlags & sampleCts are set
#ifdef ENABLE_FFMPEG
            assert((flags == MOV_TRUN_DATA_OFFSET) || (flags == (MOV_TRUN_DATA_OFFSET|MOV_TRUN_FIRST_SAMPLE_FLAGS)));
#else
            assert(flags == (MOV_TRUN_DATA_OFFSET | MOV_TRUN_SAMPLE_DURATION | MOV_TRUN_SAMPLE_SIZE | MOV_TRUN_SAMPLE_FLAGS | MOV_TRUN_SAMPLE_CTS));
#endif
            payload += sizeof(uint24_t);

            auto sample_count = DeSerialize<uint32_t>(payload); // frame count (typ 1)
            assert(sample_count > 0);
            payload += sizeof(uint32_t);

            if (flags & MOV_TRUN_DATA_OFFSET) {
                // overwrite data_offset field (https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-sstr/6d796f37-b4f0-475f-becd-13f1c86c2d1f)
                // offset from the beginning of the "moof" field
                // DataOffset field MUST be the sum of the lengths of the "moof" and all the fields in the "mdat" field
                if (add_tfdt)
                    Serialize<int32_t>(payload, new_moof_size + HEADER_SIZE); // add "mdat" header size
                payload += sizeof(int32_t);
            }

            if (flags & MOV_TRUN_FIRST_SAMPLE_FLAGS) {
                payload += sizeof(uint32_t);
            }

            for (uint32_t i = 0; i < sample_count; i++) {
                if (flags & MOV_TRUN_SAMPLE_DURATION) {
                    if (m_time.updateSampleDuration)
                        Serialize<uint32_t>(payload, m_time.sample_duration);
                    else
                        m_time.sample_duration = DeSerialize<uint32_t>(payload);
                    payload += sizeof(uint32_t);
                } else {
                    m_time.sample_duration = 1024; // almost matches MediaFoundation
                }

                // update baseMediaDecodeTime for next fragment
                m_time.cur_time += m_time.sample_duration;

                if (flags & MOV_TRUN_SAMPLE_SIZE) {
                    //auto sample_size = DeSerialize<uint32_t>(payload);
                    payload += sizeof(uint32_t);
                }

                if (flags & MOV_TRUN_SAMPLE_FLAGS) {
                    //auto sample_flags = DeSerialize<uint32_t>(payload);
                    payload += sizeof(uint32_t);
                }

                if (flags & MOV_TRUN_SAMPLE_CTS) {
                    //auto sample_composition_time_offset = DeSerialize<int32_t>(payload); // uint32_t for version==0, int32_t for version > 0
                    payload += sizeof(int32_t);
                }
            }

            assert(payload == trun_ptr + trun_size);
        }
    }

private:
    double            m_dpi = 0;
    TimeHandler       m_time;
    bool              m_paused = false;
    std::vector<char> m_moof_buf; ///< "moof" atom modification buffer
};
