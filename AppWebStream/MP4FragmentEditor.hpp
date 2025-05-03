#include <cassert>
#include <tuple>
#include <vector>
#include <Windows.h>


/** Deserialize & conververt from big-endian. */
template <typename T>
static T DeSerialize(const char* buf) {
    T val = {};
    for (size_t i = 0; i < sizeof(T); ++i)
        reinterpret_cast<BYTE*>(&val)[i] = buf[sizeof(T) - 1 - i];

    return val;
}

/** Serialize & conververt to big-endian. */
template <typename T>
static char* Serialize(char* buf, T val) {
    for (size_t i = 0; i < sizeof(T); ++i)
        buf[i] = reinterpret_cast<BYTE*>(&val)[sizeof(T) - 1 - i];

    return buf + sizeof(T);
}

/** Read big-endian fixed-point 16+16 float.
    REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/tools/IsoTypeReader.java */
inline double ReadFixed1616(const char* buf) {
    int32_t val = 0;
    val |= buf[0] << 24;
    val |= buf[1] << 16;
    val |= buf[2] << 8;
    val |= buf[3] << 0;

    return ((double)val) / (1 << 16);
}
/** Read big-endian fixed-point 2+30 float.
    REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/tools/IsoTypeReader.java */
inline double ReadFixed0230(const char* buf) {
    int32_t val = 0;
    val |= buf[0] << 24;
    val |= buf[1] << 16;
    val |= buf[2] << 8;
    val |= buf[3] << 0;

    return ((double)val) / (1 << 30);
}

/** Write big-endian fixed-point 16+16 float.
    REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/tools/IsoTypeWriter.java */
inline char* WriteFixed1616(char* buf, double in) {
    int32_t val = (int32_t)(in * (1 << 16));

    buf[0] = (val & 0xFF000000) >> 24;
    buf[1] = (char)((val & 0x00FF0000) >> 16);
    buf[2] = (val & 0x0000FF00) >> 8;
    buf[3] = (val & 0x000000FF);
    return buf + 4;
}
/** Write big-endian fixed-point 2+30 float.
    REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/tools/IsoTypeWriter.java */
inline char* WriteFixed0230(char* buf, double in) {
    int32_t val = (int32_t)(in * (1 << 30));

    buf[0] = (val & 0xFF000000) >> 24;
    buf[1] = (char)((val & 0x00FF0000) >> 16);
    buf[2] = (val & 0x0000FF00) >> 8;
    buf[3] = (val & 0x000000FF);
    return buf + 4;
}

/**  MP4 file uses time counting in SECONDS since midnight, Jan. 1, 1904. */
inline uint64_t CurrentTime1904() {
#if 0
    FILETIME curTime{};
    {
        SYSTEMTIME st{};
        GetSystemTime(&st);
        SystemTimeToFileTime(&st, &curTime);
    }

    FILETIME epochTime{};
    {
        // unix 1970 epoch
        SYSTEMTIME st{};
        st.wYear = 1904;
        st.wMonth = 1;
        st.wDay = 1;
        // for some reason needed to adjust epoch by 7min 10sec to match time(null)
        st.wHour = 0;
        st.wMinute = 7;
        st.wSecond = 10;
        SystemTimeToFileTime(&st, &epochTime);
    }

    ULARGE_INTEGER diff{}; // 100ns resolution
    diff.HighPart = curTime.dwHighDateTime - epochTime.dwHighDateTime;
    diff.LowPart = curTime.dwLowDateTime - epochTime.dwLowDateTime;
    return diff.QuadPart/10000000;
#else
    time_t now = time(NULL); // unix epoch since 1970-01-01
    // Convert from unix epoch to MPEG-4 epoch since midnight, Jan. 1, 1904.
    // Seconds between 1904-01-01 and Unix 1970 Epoch: (66 * 365 + 17) * (24 * 60 * 60) = 2082844800 (from https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mov.c)
    uint64_t mpeg4Time = now + (66 * 365 + 17) * (24 * 60 * 60);
    return mpeg4Time;
#endif
}

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
  - [trun] track run (will be modified) */
class MP4FragmentEditor {
    static constexpr uint32_t HEADER_SIZE = 8; // atom header size (4bytes size + 4byte name)

public:
    MP4FragmentEditor(double dpi) {
        m_dpi = dpi;
    }

    /** Intended to be called from IMFByteStream::BeginWrite and IMFByteStream::Write before forwarding the data to a socket.
        Will modify the "moof" atom if present.
        returns a (ptr, size) tuple pointing to a potentially modified buffer. */
    std::string_view EditStream (std::string_view buffer, bool update_moov) {
        if (buffer.size() < 5*HEADER_SIZE)
            return buffer; // too small to contain a moof (skip processing)

        uint32_t atom_size = GetAtomSize(buffer.data());
        assert(atom_size <= buffer.size());

        if (IsAtomType(buffer.data(), "moov")) {
            // Movie box (moov)
            assert(atom_size == buffer.size());
            ModifyMovieBox(buffer);
        } else if (IsAtomType(buffer.data(), "moof")) {
            // Movie Fragment (moof)
            assert(atom_size == buffer.size());
            if (update_moov)
                return ModifyMovieFragment(buffer.data(), atom_size);
        }

        return buffer;
    }

private:
    /* QuickTime transformation matrix.
    a,b,c,d,x,y: divided as 16.16 bits.
    u,v,w;       divided as 2.30 bits
    // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/support/Matrix.java */
    struct matrix {
        static constexpr uint32_t SIZE = 9 * sizeof(int32_t); // serialization size
        double a, b, u;
        double c, d, v;
        double tx, ty, w;

        matrix(const char* buf) {
            // TOOD: Implement fixed-point parsing
            a = ReadFixed1616(buf); buf += sizeof(int32_t);
            b = ReadFixed1616(buf); buf += sizeof(int32_t);
            u = ReadFixed0230(buf); buf += sizeof(int32_t);
            c = ReadFixed1616(buf); buf += sizeof(int32_t);
            d = ReadFixed1616(buf); buf += sizeof(int32_t);
            v = ReadFixed0230(buf); buf += sizeof(int32_t);
            tx = ReadFixed1616(buf); buf += sizeof(int32_t);
            ty= ReadFixed1616(buf); buf += sizeof(int32_t);
            w = ReadFixed0230(buf); buf += sizeof(int32_t);
        }

        char* Write(char* buf) const {
            buf = WriteFixed1616(buf, a);
            buf = WriteFixed1616(buf, b);
            buf = WriteFixed0230(buf, u);
            buf = WriteFixed1616(buf, c);
            buf = WriteFixed1616(buf, d);
            buf = WriteFixed0230(buf, v);
            buf = WriteFixed1616(buf, tx);
            buf = WriteFixed1616(buf, ty);
            buf = WriteFixed0230(buf, w);
            return buf;
        }
    };

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
#ifdef ANALYZE_MVHD
            ptr += HEADER_SIZE; // skip size & type

            auto version = DeSerialize<uint8_t>(ptr);
            ptr += 1;

            ptr += 3; // skip over "flags" field

            uint64_t creationTime = 0;
            uint64_t modificationTime = 0;
            if (version == 1) {
                assert(mvhd_len == 120);

                // seconds since Fri Jan 1 00:00:00 1904
                creationTime = DeSerialize<uint64_t>(ptr);
                ptr += 8;

                modificationTime = DeSerialize<uint64_t>(ptr);
                ptr += 8;
            } else {
                assert(mvhd_len == 108);

                // seconds since Fri Jan 1 00:00:00 1904
                creationTime = DeSerialize<uint32_t>(ptr);
                ptr += 4;

                modificationTime = DeSerialize<uint32_t>(ptr);
                ptr += 4;
            }

            auto timeScale = DeSerialize<uint32_t>(ptr); // 50000 = 50ms
            ptr += 4;

            uint64_t duration = 0;
            if (version == 1) {
                duration = DeSerialize<uint64_t>(ptr);
                ptr += 8;
            } else {
                duration = DeSerialize<uint32_t>(ptr);
                ptr += 4;
            }

            auto rate = DeSerialize<uint32_t>(ptr); // preferred playback rate (16+16 fraction)
            ptr += 4;

            auto volume = DeSerialize<int16_t>(ptr); // master volume of file (8+8 fraction)
            ptr += 2;

            ptr += sizeof(uint16_t); // reserved
            ptr += sizeof(uint32_t) * 2; // reserved

            // matrix to map points from one coordinate space into another
            matrix mat(ptr);
            ptr += matrix::SIZE;

            ptr += sizeof(uint32_t) * 6; // reserved

            auto nextTrackId = DeSerialize<uint32_t>(ptr);
            ptr += 4;

            // end of "mvhd" atom
            assert(ptr == buffer.data() + HEADER_SIZE + mvhd_len);
#else
            ptr += mvhd_len;
#endif
        }
        {
            // entering "trak" atom
            assert(IsAtomType(ptr, "trak"));
            //uint32_t trak_len = GetAtomSize(ptr);
            ptr += HEADER_SIZE; // skip size & type

            {
                // skip over "tkhd" atom
                assert(IsAtomType(ptr, "tkhd"));
                uint32_t tkhd_len = GetAtomSize(ptr);
                ptr += tkhd_len;

                // TODO: Parse create- & modify-time (encoded same as "mvhd")
            }

            // entring "mdia" atom
            assert(IsAtomType(ptr, "mdia"));
            //uint32_t mdia_len = GetAtomSize(ptr);
            ptr += HEADER_SIZE; // skip size & type

            {
                // skip over "mdhd" atom
                assert(IsAtomType(ptr, "mdhd"));
                uint32_t mdhd_len = GetAtomSize(ptr);
                ptr += mdhd_len;

                // TODO: Parse create- & modify-time (encoded same as "mvhd")
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

            uint32_t versionFlags = DeSerialize<uint32_t>(ptr); // 8bit version followed by 24bit flags
            assert(versionFlags == 0);
            ptr += sizeof(uint32_t);
            
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

                //auto width = DeSerialize<uint16_t>(ptr);
                ptr += 2;
                //auto height = DeSerialize<uint16_t>(ptr);
                ptr += 2;

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
    std::string_view ModifyMovieFragment (const char* buf, const ULONG size) {
        assert(GetAtomSize(buf) == size);

        // copy to temporary buffer before modifying & extending atoms
        m_moof_buf.resize(size-8+20);
        memcpy(m_moof_buf.data()/*dst*/, buf, size);

        char* moof_ptr = m_moof_buf.data(); // switch to internal buffer

        // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/MovieFragmentHeaderBox.java
        char* mfhd_ptr = moof_ptr + HEADER_SIZE;
        uint32_t mfhd_size = GetAtomSize(mfhd_ptr);
        if (!IsAtomType(mfhd_ptr, "mfhd")) // movie fragment header
            throw std::runtime_error("not a \"mfhd\" atom");
        auto seq_nr = DeSerialize<uint32_t>(mfhd_ptr+HEADER_SIZE+4); // increases by one per fragment
        seq_nr;

        // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackFragmentBox.java
        char* traf_ptr = mfhd_ptr + mfhd_size; // jump to next atom (don't inspect mfhd fields)
        uint32_t traf_size = GetAtomSize(traf_ptr);
        if (!IsAtomType(traf_ptr, "traf")) // track fragment
            throw std::runtime_error("not a \"traf\" atom");
        char* tfhd_ptr = traf_ptr + HEADER_SIZE;

        unsigned long pos_idx = static_cast<unsigned long>(tfhd_ptr - moof_ptr);
        int rel_size = ProcessTrackFrameChildren(m_moof_buf.data()+pos_idx, size, size-pos_idx);
        if (rel_size) {
            // size have changed - update size of parent atoms
            Serialize<uint32_t>(moof_ptr, size+rel_size);
            Serialize<uint32_t>(traf_ptr, traf_size+rel_size);
        }
        return std::string_view(moof_ptr, size + rel_size);
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
    int ProcessTrackFrameChildren (char* tfhd_ptr, ULONG moof_size, ULONG buf_size) {
        const unsigned long FLAGS_SIZE  = 4; // atom flags size (1byte version + 3bytes with flags)
        const unsigned int BASE_DATA_OFFSET_SIZE = 8; // size of tfhd flag to remove
        const unsigned int TFDT_SIZE = 20; // size of new tfdt atom that is added

        if (buf_size < 2*HEADER_SIZE+8)
            return 0; // too small to contain tfhd & trun atoms

        // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackFragmentHeaderBox.java
        uint32_t tfhd_size = GetAtomSize(tfhd_ptr);
        {
            if (!IsAtomType(tfhd_ptr, "tfhd")) // track fragment header
                return 0; // not a "tfhd" atom

            // process tfhd content
            const BYTE DEFAULT_BASE_IS_MOOF_FLAG = 0x02; // stored at offset 1
            const BYTE BASE_DATA_OFFSET_FLAG     = 0x01; // stored at offset 3
            char* payload = tfhd_ptr + HEADER_SIZE;
            // 1: set default-base-is-moof flag
            payload[1] |=  DEFAULT_BASE_IS_MOOF_FLAG;

            if (!(payload[3] & BASE_DATA_OFFSET_FLAG))
                return 0; // base-data-offset not set

            // 2: remove base-data-offset flag
            payload[3] &= ~BASE_DATA_OFFSET_FLAG;
            Serialize<uint32_t>(tfhd_ptr, tfhd_size-BASE_DATA_OFFSET_SIZE); // shrink atom size

            payload += FLAGS_SIZE; // skip "flags" field
            payload += 4;          // skip track-ID field (4bytes)

            // move remaining tfhd fields over data_offset
            size_t remaining_size = tfhd_size-HEADER_SIZE-FLAGS_SIZE-4 - BASE_DATA_OFFSET_SIZE;
            MemMove(payload/*dst*/, payload+BASE_DATA_OFFSET_SIZE/*src*/, remaining_size/*size*/);
        }
        // pointer to right after shrunken tfhd atom
        char* ptr = tfhd_ptr + tfhd_size-BASE_DATA_OFFSET_SIZE;

        // move "trun" atom to make room for a new "tfhd"
        MemMove(ptr+TFDT_SIZE-BASE_DATA_OFFSET_SIZE/*dst*/, ptr/*src*/, buf_size-tfhd_size/*size*/);

        {
            char* tfdt_ptr = ptr;
            // insert an empty "tfdt" atom (20bytes)
            // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackFragmentBaseMediaDecodeTimeBox.java
            Serialize<uint32_t>(tfdt_ptr, TFDT_SIZE);
            memcpy(tfdt_ptr+4/*dst*/, "tfdt", 4); // track fragment base media decode timebox
            tfdt_ptr += HEADER_SIZE;
            *tfdt_ptr = 1; // version 1 (no other flags)
            tfdt_ptr += FLAGS_SIZE; // skip flags
            // write tfdt/baseMediaDecodeTime
            tfdt_ptr = Serialize<uint64_t>(tfdt_ptr, m_cur_time);
        }

        {
            // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackRunBox.java
            char* trun_ptr = ptr + TFDT_SIZE;
            uint32_t trun_size = GetAtomSize(trun_ptr);
            trun_size;
            if (!IsAtomType(trun_ptr, "trun")) // track run box
                throw std::runtime_error("not a \"trun\" atom");
            char* payload = trun_ptr + HEADER_SIZE;
            assert(payload[0] == 1);   // check version
            const BYTE SAMPLE_DURATION_PRESENT_FLAG = 0x01; // stored at offset 2
            const BYTE DATA_OFFSET_PRESENT_FLAG = 0x01;     // stored at offset 3
            assert(payload[2] & SAMPLE_DURATION_PRESENT_FLAG); // verify that sampleDurationPresent is set
            assert(payload[3] & DATA_OFFSET_PRESENT_FLAG); // verify that dataOffsetPresent is set
            payload += FLAGS_SIZE; // skip flags

            auto sample_count = DeSerialize<uint32_t>(payload); // frame count (typ 1)
            assert(sample_count > 0);
            payload += sizeof(sample_count);

            // overwrite data_offset field
            payload = Serialize<uint32_t>(payload, moof_size-BASE_DATA_OFFSET_SIZE+TFDT_SIZE+8); // +8 experiementally derived

            auto sample_dur = DeSerialize<uint32_t>(payload); // duration of first sample

            // update baseMediaDecodeTime for next fragment
            m_cur_time += sample_count*sample_dur;
        }

        return TFDT_SIZE - BASE_DATA_OFFSET_SIZE; // tfdt added, tfhd shrunk
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
    uint64_t          m_cur_time = 0;
    std::vector<char> m_moof_buf; ///< "moof" atom modification buffer
};
