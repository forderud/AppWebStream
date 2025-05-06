#include <cassert>
#include <tuple>
#include <vector>
#include "MP4Utils.hpp"


/** Process atoms within a MPEG4 MovieFragment (moof) to make the stream comply with ISO base media file format (https://www.iso.org/standard/68960.html).
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
class MP4FragmentEditor {
    static constexpr uint32_t HEADER_SIZE = 8; // atom header size (4bytes size + 4byte name)
    static constexpr uint32_t VERSION_FLAGS_SIZE = 4;  // version & flags size (1byte version + 3bytes flags)

    static constexpr uint32_t BASE_DATA_OFFSET_SIZE = 8; // size of tfhd flag to remove
    static constexpr uint32_t TFDT_SIZE = 20;    // size of new tfdt atom that is added

public:
    MP4FragmentEditor(double dpi, uint64_t startTime1904) {
        m_dpi = dpi;
        m_startTime = startTime1904;
    }

    /** Intended to be called from IMFByteStream::BeginWrite and IMFByteStream::Write before forwarding the data to a socket.
        Will modify the "moof" atom if present.
        returns a (ptr, size) tuple pointing to a potentially modified buffer. */
    std::string_view EditStream (std::string_view buffer) {
        if (buffer.size() < HEADER_SIZE)
            return buffer; // buffer too small for MPEG atom header parsing

        if (IsAtomType(buffer.data(), "moov")) {
            uint32_t atom_size = GetAtomSize(buffer.data());
            assert(atom_size <= buffer.size());

            // Movie box (moov)
            assert(atom_size == buffer.size());
            ModifyMovieBox(buffer);
            return buffer;
        } else if (IsAtomType(buffer.data(), "moof")) {
            uint32_t atom_size = GetAtomSize(buffer.data());
            assert(atom_size <= buffer.size());

            // Movie Fragment (moof)
#ifndef ENABLE_FFMPEG
            assert(atom_size == buffer.size());
            return ModifyMovieFragment(buffer.data(), (ULONG)buffer.size(), true);
#else
            return ModifyMovieFragment(buffer.data(), (ULONG)buffer.size(), false);
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

private:
    void ModifyMovieBox(std::string_view buffer) {
        char* ptr = (char*)buffer.data();
        assert(IsAtomType(ptr, "moov"));
        assert(GetAtomSize(ptr) == buffer.size());
        ptr += HEADER_SIZE; // skip size & type

        {
            // entering "mvhd" atom
            // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/MovieHeaderBox.java
            assert(IsAtomType(ptr, "mvhd"));
            uint32_t mvhd_len = GetAtomSize(ptr);
            ptr += HEADER_SIZE; // skip size & type

            uint8_t version = 0;
            std::tie(version, ptr) = ParseVersionCreateModifyTime(ptr, m_startTime);

            m_timeScale = DeSerialize<uint32_t>(ptr); // 1000*fps
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
            matrix mat(ptr);
            ptr += matrix::SIZE;

            ptr += sizeof(uint32_t) * 6; // reserved

            //auto nextTrackId = DeSerialize<uint32_t>(ptr); // (typ 2)
            ptr += 4;

            // end of "mvhd" atom
            assert(ptr == buffer.data() + HEADER_SIZE + mvhd_len);
        }
        {
            // entering "trak" atom
            assert(IsAtomType(ptr, "trak"));
            //uint32_t trak_len = GetAtomSize(ptr);
            ptr += HEADER_SIZE; // skip size & type

            {
                // partially parse "tkhd" atom
                // REF: https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mov.c#L5478
                assert(IsAtomType(ptr, "tkhd"));
                uint32_t tkhd_len = GetAtomSize(ptr);

                char* tkhd_ptr = ptr + HEADER_SIZE;

                uint8_t version = 0;
                std::tie(version, tkhd_ptr) = ParseVersionCreateModifyTime(tkhd_ptr, m_startTime);

                ptr += tkhd_len;
            }

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

                uint8_t version = 0;
                std::tie(version, mdhd_ptr) = ParseVersionCreateModifyTime(mdhd_ptr, m_startTime);

                ptr += mdhd_len;
            }
            {
                // skip over "hdlr" atom
                assert(IsAtomType(ptr, "hdlr"));
                uint32_t hdlr_len = GetAtomSize(ptr);
                ptr += hdlr_len;
            }

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
            {
                // skip over "dinf" atom
                assert(IsAtomType(ptr, "dinf"));
                uint32_t dinf_len = GetAtomSize(ptr);
                ptr += dinf_len;
            }

            // entering "stbl" atmom
            assert(IsAtomType(ptr, "stbl"));
            //uint32_t stbl_len = GetAtomSize(ptr);
            ptr += HEADER_SIZE; // skip size & type

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
            assert(entryCount == 1);
            ptr += sizeof(uint32_t);

            {
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
                double dpi = ReadFixed1616(ptr);
                assert(dpi == 72); // 72.00 horizontal DPI
                dpi = ReadFixed1616(ptr + 4);
                assert(dpi == 72); // 72.00 vertical DPI

                // update video DPI in fixed-point 16+16 format
                WriteFixed1616(ptr, m_dpi);    // horizontal DPI
                WriteFixed1616(ptr + 4, m_dpi);// vertical DPI

                // ignore the remaining parameters
            }
        }
    }

    /** REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/MovieFragmentBox.java */
    std::string_view ModifyMovieFragment (const char* buf, const ULONG buf_size, bool add_tfdt) {
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

        // "tfhd" atom immediately follows
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
            assert(IsAtomType(tfhd_ptr, "tfhd")); // track fragment header
            // process tfhd content
            char* payload = tfhd_ptr + HEADER_SIZE;
            auto version = DeSerialize<uint8_t>(payload);
            assert(version == 0);
            payload += sizeof(uint8_t);

            {
                // "tfhd" atom flags (from https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/isom.h)
                constexpr uint32_t MOV_TFHD_BASE_DATA_OFFSET = 0x01;
                //constexpr uint32_t MOV_TFHD_STSD_ID = 0x02;
                //constexpr uint32_t MOV_TFHD_DEFAULT_DURATION = 0x08;
                //constexpr uint32_t MOV_TFHD_DEFAULT_SIZE = 0x10;
                //constexpr uint32_t MOV_TFHD_DEFAULT_FLAGS = 0x20;
                //constexpr uint32_t MOV_TFHD_DURATION_IS_EMPTY = 0x010000;
                constexpr uint32_t MOV_TFHD_DEFAULT_BASE_IS_MOOF = 0x020000;

                uint32_t flags = DeSerialize<uint24_t>(payload);
#ifndef ENABLE_FFMPEG
                assert(flags & MOV_TFHD_BASE_DATA_OFFSET);
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

            // move "trun" atom to make room for a new "tfhd"
            MemMove(ptr+TFDT_SIZE-BASE_DATA_OFFSET_SIZE/*dst*/, ptr/*src*/, buf_size-tfhd_size/*size*/);
        }

        if (add_tfdt) {
            // insert new "tfdt" atom (20bytes)
            // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackFragmentBaseMediaDecodeTimeBox.java
            char* tfdt_ptr = ptr;

            Serialize<uint32_t>(tfdt_ptr, TFDT_SIZE);
            memcpy(tfdt_ptr+4/*dst*/, "tfdt", 4); // track fragment base media decode timebox
            tfdt_ptr += HEADER_SIZE;

            *tfdt_ptr = 1; // version 1 (no other flags)
            tfdt_ptr += VERSION_FLAGS_SIZE; // skip flags
            // write tfdt/baseMediaDecodeTime
            tfdt_ptr = Serialize<uint64_t>(tfdt_ptr, m_cur_time);

            assert(tfdt_ptr == ptr + TFDT_SIZE);
            ptr += TFDT_SIZE;
        } else {
            // inspect existing tfdt atom
            assert(IsAtomType(ptr, "tfdt"));
            uint32_t tfdt_size = GetAtomSize(ptr);

            // check baseMediaDecodeTime
            auto baseMediaDecodeTime = DeSerialize<uint64_t>(ptr + HEADER_SIZE + VERSION_FLAGS_SIZE);
            assert(baseMediaDecodeTime == m_cur_time);

            ptr += tfdt_size;
        }

        {
            // modify "trun" atom
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

            // "trun" atom flags (from https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/isom.h)
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
                    auto sample_dur = DeSerialize<uint32_t>(payload); // frame duration (typ 1000)
                    payload += sizeof(uint32_t);

                    // update baseMediaDecodeTime for next fragment
                    m_cur_time += sample_dur;
                } else {
                    m_cur_time += 256; // matches FFmpeg counting
                }

                if (flags & MOV_TRUN_SAMPLE_SIZE) {
                    //auto sample_size = DeSerialize<uint32_t>(payload);
                    payload += sizeof(uint32_t);
                }

                if (flags & MOV_TRUN_SAMPLE_FLAGS) {
                    //auto sample_flags = DeSerialize<uint32_t>(payload);
                    payload += sizeof(uint32_t);
                }

                if (flags & MOV_TRUN_SAMPLE_CTS) {
                    //auto sample_cto = DeSerialize<int32_t>(payload); // uint32_t for version==0, int32_t for version > 0
                    payload += sizeof(int32_t);
                }
            }

            assert(payload == trun_ptr + trun_size);
        }
    }

    static std::tuple<uint8_t, char*> ParseVersionCreateModifyTime(char* ptr, uint64_t newTime) {
        auto version = DeSerialize<uint8_t>(ptr);
        ptr += 1;

        //uint32_t flags = DeSerialize<uint24_t>(ptr);
        ptr += sizeof(uint24_t);

        // seconds since Fri Jan 1 00:00:00 1904
        uint64_t creationTime = 0;
        uint64_t modificationTime = 0;
        if (version == 1) {
            creationTime = DeSerialize<uint64_t>(ptr);
            Serialize<uint64_t>(ptr, newTime);
            ptr += 8;

            modificationTime = DeSerialize<uint64_t>(ptr);
            Serialize<uint64_t>(ptr, newTime);
            ptr += 8;
        } else {
            creationTime = DeSerialize<uint32_t>(ptr);
            Serialize<uint32_t>(ptr, (uint32_t)newTime);
            ptr += 4;

            modificationTime = DeSerialize<uint32_t>(ptr);
            Serialize<uint32_t>(ptr, (uint32_t)newTime);
            ptr += 4;
        }

        // return preexisting creation & modification times
        return std::tie(version, ptr);
    }

    /** Mofified version of "memmove" that clears the abandoned bytes, as well as intermediate data.
    WARNING: Only use for contiguous/overlapping moves, or else it will clear more than excpected. */
    static void MemMove (char* dest, const char* source, size_t num) {
        // move memory block
        memmove(dest, source, num);

        // clear abandoned byte range
        if (dest > source)
            memset(const_cast<char*>(source)/*dst*/, 0/*val*/, dest-source/*size*/);
        else
            memset(dest+num/*dst*/, 0/*val*/, source-dest/*size*/);
    }

    /** Get MPEG4 atom type (4 chars). */
    static char* GetAtomType(const char* atom_ptr) {
        return (char*)atom_ptr + 4;
    }

    /** Check if an MPEG4 atom is of a given type. */
    static bool IsAtomType (const char* atom_ptr, const char type[4]) {
        return memcmp(GetAtomType(atom_ptr), type, 4) == 0; // atom type is stored at offset 4-7
    }

    /** Get the size of an MPEG4 atom. */
    static uint32_t GetAtomSize (const char* atom_ptr) {
        return DeSerialize<uint32_t>(atom_ptr);
    }

private:
    double            m_dpi = 0;
    uint64_t          m_startTime = 0; // creation- & modification time
    uint32_t          m_timeScale = 0; // unit: 1000*fps (50000 = 50fps)
    uint64_t          m_cur_time = 0;
    std::vector<char> m_moof_buf; ///< "moof" atom modification buffer
};
