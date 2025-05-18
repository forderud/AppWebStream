#pragma once
#include <cassert>
#include <ctime>
#include <string>
#include <tuple>
#include <Windows.h>


struct uint24_t {
    uint8_t raw[3]{};

    uint24_t() = default;

    uint24_t(uint32_t val) {
        raw[0] = (val >> 0) & 0xFF;
        raw[1] = (val >> 8) & 0xFF;
        raw[2] = (val >> 16) & 0xFF;
    }

    operator uint32_t () const {
        // no endianess conversion here, since it's already done in the (De)Serialize function
        uint32_t val = 0;
        val |= raw[0] << 0;
        val |= raw[1] << 8;
        val |= raw[2] << 16;
        return val;
    }
};
static_assert(sizeof(uint24_t) == 3);

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

/** Read big-endian fixed-point 8+8 float.
    REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/tools/IsoTypeReader.java */
inline double ReadFixed88(const char* buf) {
    int16_t val = 0;
    val |= buf[0] << 8;
    val |= buf[1] << 0;

    return ((double)val) / (1 << 8);
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
        ty = ReadFixed1616(buf); buf += sizeof(int32_t);
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

inline uint64_t UnixTimeToMpeg4Time(uint64_t unixTime) {
    // Convert from unix epoch to MPEG-4 epoch since midnight, Jan. 1, 1904.
    // Seconds between 1904-01-01 and Unix 1970 Epoch: (66 * 365 + 17) * (24 * 60 * 60) = 2082844800 (from https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mov.c)
    uint64_t mpeg4Time = unixTime + (66 * 365 + 17) * (24 * 60 * 60);
    return mpeg4Time;
}

inline uint64_t Mpeg4TimeToUnixTime(uint64_t mpeg4Time) {
    // Convert from MPEg-4 epoch to Unix epoch since midnight, Jan. 1, 1970.
    // Seconds between 1904-01-01 and Unix 1970 Epoch: (66 * 365 + 17) * (24 * 60 * 60) = 2082844800 (from https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mov.c)
    uint64_t unixTime = mpeg4Time - (66 * 365 + 17) * (24 * 60 * 60);
    return unixTime;
}

/** Convert from 100-nanosecond intervals since January 1, 1601 (UTC) to MPEG4 time. */
inline uint64_t WindowsTimeToMpeg4Time(FILETIME winTime) {
    FILETIME epochTime{}; // MPEG4 1904 epoch
    {
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
    diff.HighPart = winTime.dwHighDateTime - epochTime.dwHighDateTime;
    diff.LowPart = winTime.dwLowDateTime - epochTime.dwLowDateTime;
    return diff.QuadPart / 10000000; // convert to seconds
}

/**  MPEG4 file uses time counting in SECONDS since midnight, Jan. 1, 1904. */
inline uint64_t CurrentTime1904() {
    FILETIME curTime{};
    {
        SYSTEMTIME st{};
        GetSystemTime(&st);
        SystemTimeToFileTime(&st, &curTime);
    }

    return WindowsTimeToMpeg4Time(curTime);
}

inline std::string TimeString1904(uint64_t mpeg4Time) {
    time_t unixTime = Mpeg4TimeToUnixTime(mpeg4Time);

    tm timeStruct{}; // in UTC time
    errno_t res = gmtime_s(&timeStruct, &unixTime);
    assert(!res);

    char buffer[26]{}; // always same size
    res = asctime_s(buffer, &timeStruct);
    assert(!res);

    // remove newline at end of string
    buffer[24] = '\0';

    return buffer;
}


/** Mofified version of "memmove" that clears the abandoned bytes, as well as intermediate data.
WARNING: Only use for contiguous/overlapping moves, or else it will clear more than excpected. */
inline void MemMove(char* dest, const char* source, size_t num) {
    // move memory block
    memmove(dest, source, num);

    // clear abandoned byte range
    if (dest > source)
        memset(const_cast<char*>(source)/*dst*/, 0/*val*/, dest - source/*size*/);
    else
        memset(dest + num/*dst*/, 0/*val*/, source - dest/*size*/);
}

/** Get MPEG4 atom type (4 chars). */
static char* GetAtomType(const char* atom_ptr) {
    return (char*)atom_ptr + 4;
}

/** Check if an MPEG4 atom is of a given type. */
inline bool IsAtomType(const char* atom_ptr, const char type[4]) {
    return memcmp(GetAtomType(atom_ptr), type, 4) == 0; // atom type is stored at offset 4-7
}

/** Get the size of an MPEG4 atom. */
inline uint32_t GetAtomSize(const char* atom_ptr) {
    return DeSerialize<uint32_t>(atom_ptr);
}
