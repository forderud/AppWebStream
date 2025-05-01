#include <cassert>
#include <tuple>
#include <vector>
#include <Windows.h>

/** Process atoms within a MPEG4 MovieFragment (moof) to make the stream comply with ISO base media file format (https://www.iso.org/standard/68960.html).
    Work-around for shortcommings in the Media Foundation MPEG4 file sink (https://learn.microsoft.com/en-us/windows/win32/medfound/mpeg-4-file-sink).
    Please delete this class if a better alternative becomes available.
Expected atom hiearchy:
[moof] movie fragment
* [mfhd] movie fragment header
* [traf] track fragment
  - [tfhd] track fragment header (will be modified)
  - [tfdt] track fragment decode timebox (will be added)
  - [trun] track run (will be modified) */
class MP4FragmentEditor {
    static constexpr unsigned long S_HEADER_SIZE = 8;

public:
    MP4FragmentEditor() {
    }

    /** Intended to be called from IMFByteStream::BeginWrite and IMFByteStream::Write before forwarding the data to a socket.
        Will modify the "moof" atom if present.
        returns a (ptr, size) tuple pointing to a potentially modified buffer. */
    std::string_view EditStream (std::string_view buf) {
        if (buf.size() < 5*S_HEADER_SIZE)
            return buf; // too small to contain a moof (skip processing)

        uint32_t atom_size = GetAtomSize(buf.data());
        assert(atom_size <= buf.size());

        if (IsAtomType(buf.data(), "moov")) {
            // Movie container (moov)
            assert(atom_size == buf.size());
            return PrependXmpPacket(buf.data(), buf.size());
        } else if (IsAtomType(buf.data(), "moof")) {
            // Movie Fragment (moof)
            assert(atom_size == buf.size());
            return ModifyMovieFragment(buf.data(), atom_size);
        }

        return buf;
    }

private:
    /** REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/MovieFragmentBox.java */
    std::string_view ModifyMovieFragment (const char* buf, const ULONG size) {
        assert(GetAtomSize(buf) == size);

        // copy to temporary buffer before modifying & extending atoms
        m_moof_buf.resize(size-8+20);
        memcpy(m_moof_buf.data()/*dst*/, buf, size);

        char* moof_ptr = m_moof_buf.data(); // switch to internal buffer

        // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/MovieFragmentHeaderBox.java
        char* mfhd_ptr = moof_ptr + S_HEADER_SIZE;
        uint32_t mfhd_size = GetAtomSize(mfhd_ptr);
        if (!IsAtomType(mfhd_ptr, "mfhd")) // movie fragment header
            throw std::runtime_error("not a \"mfhd\" atom");
        auto seq_nr = DeSerialize<uint32_t>(mfhd_ptr+S_HEADER_SIZE+4); // increases by one per fragment
        seq_nr;

        // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackFragmentBox.java
        char* traf_ptr = mfhd_ptr + mfhd_size; // jump to next atom (don't inspect mfhd fields)
        uint32_t traf_size = GetAtomSize(traf_ptr);
        if (!IsAtomType(traf_ptr, "traf")) // track fragment
            throw std::runtime_error("not a \"traf\" atom");
        char* tfhd_ptr = traf_ptr + S_HEADER_SIZE;

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
        const unsigned long HEADER_SIZE = 8; // atom header size (4bytes size + 4byte name)
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
            Serialize<uint64_t>(tfdt_ptr, m_cur_time);
            tfdt_ptr += sizeof(uint64_t);
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

            auto sample_count = DeSerialize<uint32_t>(payload);
            assert(sample_count > 0);
            payload += sizeof(sample_count);

            // overwrite data_offset field
            Serialize<uint32_t>(payload, moof_size-BASE_DATA_OFFSET_SIZE+TFDT_SIZE+8); // +8 experiementally derived
            payload += sizeof(uint32_t);

            auto sample_dur = DeSerialize<uint32_t>(payload); // duration of first sample

            // update baseMediaDecodeTime for next fragment
            m_cur_time += sample_count*sample_dur;
        }

        return TFDT_SIZE - BASE_DATA_OFFSET_SIZE; // tfdt added, tfhd shrunk
    }

    std::string_view PrependXmpPacket(const char* buf, size_t size) {
        m_xmp_buf.clear();
        m_xmp_buf.reserve(512 + size);
        m_xmp_buf.resize(4 + 4 + 16); // 4byte size prefix, 4byte "uuid" type, 16byte UUID
        memcpy(m_xmp_buf.data() + 4, "uuid", 4); // atom type

        GUID guid{};
        CLSIDFromString(L"{be7acfcb-97a9-42e8-9c71-999491e3afac}", &guid);
        memcpy(m_xmp_buf.data() + 8, &guid, sizeof(guid)); // XMP UUID value

        {
            // XMP packet in UTF-8
            // based on https://archimedespalimpsest.net/Documents/External/XMP/XMPSpecificationPart3.pdf
            const char prefix[] = "<?xpacket begin=\"﻿\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>"; // "begin" value is UTF-8 BOM (0xEF 0xBB 0xBF)
            m_xmp_buf.insert(m_xmp_buf.end(), prefix, prefix + sizeof(prefix));

            const char header[] = "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description rdf:about='' xmlns:tiff='http://ns.adobe.com/tiff/1.0/'>";
            m_xmp_buf.insert(m_xmp_buf.end(), header, header + sizeof(header));

            const char resUnit[] = "<tiff:ResolutionUnit>3</tiff:ResolutionUnit>"; // 3 is cm
            m_xmp_buf.insert(m_xmp_buf.end(), resUnit, resUnit + sizeof(resUnit));

            const char xRes[] = "<tiff:XResolution>1000/1</tiff:XResolution>"; // horizontal pixels per cm
            m_xmp_buf.insert(m_xmp_buf.end(), xRes, xRes + sizeof(xRes));

            const char yRes[] = "<tiff:YResolution>1000/1</tiff:YResolution>"; // vertical pixels per cm
            m_xmp_buf.insert(m_xmp_buf.end(), yRes, yRes + sizeof(yRes));

            const char footer[] = "</rdf:Description></rdf:RDF></x:xmpmeta>";
            m_xmp_buf.insert(m_xmp_buf.end(), footer, footer + sizeof(footer));

            const char suffix[] = "<?xpacket end=\"r\"?>"; // "r" means read-only (not in-place editable)
            m_xmp_buf.insert(m_xmp_buf.end(), suffix, suffix + sizeof(suffix));
        }

        // set atom size prefix
        Serialize<uint32_t>(m_xmp_buf.data(), (uint32_t)m_xmp_buf.size());

        // add "moov" atom afterwards
        m_xmp_buf.insert(m_xmp_buf.end(), buf, buf + size);

        return std::string_view(m_xmp_buf.data(), m_xmp_buf.size());
    }


    /* QuickTime transformation matrix.
        a,b,c,d,x,y: divided as 16.16 bits.
        u,v,w;       divided as 2.30 bits */
    struct matrix {
        int32_t a, b, u;
        int32_t c, d, v;
        int32_t x, y, w;
    };
    static_assert(sizeof(matrix) == 36);

#if 0
    std::tuple<const char*, ULONG> ModifyMovieContainer(const char* buf, const ULONG size) {
        const char* ptr = buf;
        assert(IsAtomType(ptr, "moov"));
        assert(GetAtomSize(ptr) == size);
        ptr += 8; // skip size & type

        {
            // now entering the "mvhd" atom
            assert(IsAtomType(ptr, "mvhd"));
            uint32_t mvhd_len = GetAtomSize(ptr);
            ptr += 8; // skip size & type

            auto version = DeSerialize<uint8_t>(ptr);
            ptr += 1;

            ptr += 3; // skip over "flags" field

            if (version == 1) {
                assert(mvhd_len == 120);

                // seconds since Fri Jan 1 00:00:00 1904
                auto creationTime = DeSerialize<uint64_t>(ptr);
                ptr += 8;

                auto modificationTime = DeSerialize<uint64_t>(ptr);
                ptr += 8;
            } else {
                assert(mvhd_len == 108);

                // seconds since Fri Jan 1 00:00:00 1904
                auto creationTime = DeSerialize<uint32_t>(ptr);
                ptr += 4;

                auto modificationTime = DeSerialize<uint32_t>(ptr);
                ptr += 4;
            }

            auto timeScale = DeSerialize<uint32_t>(ptr); // 50000 = 50ms
            ptr += 4;

            if (version == 1) {
                auto duration = DeSerialize<uint64_t>(ptr);
                ptr += 8;
            } else {
                auto duration = DeSerialize<uint32_t>(ptr);
                ptr += 4;
            }

            auto rate = DeSerialize<uint32_t>(ptr); // preferred playback rate
            ptr += 4;

            auto volume = DeSerialize<int16_t>(ptr); // master volume of file
            ptr += 2;

            ptr += sizeof(uint16_t); // reserved
            ptr += sizeof(uint32_t) * 2; // reserved

            auto mat = DeSerialize<matrix>(ptr);
            ptr += sizeof(matrix);

            ptr += sizeof(uint32_t) * 6; // reserved

            auto nextTrackId = DeSerialize<uint32_t>(ptr);
            ptr += 4;

            // end of "mvhd" atom
            assert(ptr == buf + 8 + mvhd_len);
        }
        return std::tie(buf, size);
    }
#endif

    /** Deserialize & conververt from big-endian. */
    template <typename T>
    static T DeSerialize (const char* buf) {
        T val = {};
        for (size_t i = 0; i < sizeof(T); ++i)
            reinterpret_cast<BYTE*>(&val)[i] = buf[sizeof(T)-1-i];

        return val;
    }

    /** Serialize & conververt to big-endian. */
    template <typename T>
    static void Serialize (char* buf, T val) {
        for (size_t i = 0; i < sizeof(T); ++i)
            buf[i] = reinterpret_cast<BYTE*>(&val)[sizeof(T)-1-i];
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
    uint64_t          m_cur_time = 0;
    std::vector<char> m_moof_buf; ///< "moof" atom modification buffer
    std::vector<char> m_xmp_buf;  ///< top-level "uuid" atom with XMP resolution metadata
};
