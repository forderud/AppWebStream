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
    val |= (unsigned char)buf[0] << 8;
    val |= (unsigned char)buf[1] << 0;

    return ((double)val) / (1 << 8);
}
/** Write big-endian fixed-point 8+8 float. */
inline char* WriteFixed88(char* buf, double in) {
    auto val = (int16_t)(in * (1 << 8));

    buf[0] = (val & 0xFF00) >> 8;
    buf[1] = (val & 0x00FF);
    return buf + 2;
}

/** Read big-endian fixed-point 16+16 float.
    REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/tools/IsoTypeReader.java */
inline double ReadFixed1616(const char* buf) {
    int32_t val = 0;
    val |= (unsigned char)buf[0] << 24;
    val |= (unsigned char)buf[1] << 16;
    val |= (unsigned char)buf[2] << 8;
    val |= (unsigned char)buf[3] << 0;

    return ((double)val) / (1 << 16);
}
/** Write big-endian fixed-point 16+16 float.
    REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/tools/IsoTypeWriter.java */
inline char* WriteFixed1616(char* buf, double in) {
    auto val = (int32_t)(in * (1 << 16));

    buf[0] = (val & 0xFF000000) >> 24;
    buf[1] = (char)((val & 0x00FF0000) >> 16);
    buf[2] = (val & 0x0000FF00) >> 8;
    buf[3] = (val & 0x000000FF);
    return buf + 4;
}

/** Read big-endian fixed-point 2+30 float.
    REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/tools/IsoTypeReader.java */
inline double ReadFixed0230(const char* buf) {
    int32_t val = 0;
    val |= (unsigned char)buf[0] << 24;
    val |= (unsigned char)buf[1] << 16;
    val |= (unsigned char)buf[2] << 8;
    val |= (unsigned char)buf[3] << 0;

    return ((double)val) / (1 << 30);
}
/** Write big-endian fixed-point 2+30 float.
    REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/tools/IsoTypeWriter.java */
inline char* WriteFixed0230(char* buf, double in) {
    auto val = (int32_t)(in * (1 << 30));

    buf[0] = (val & 0xFF000000) >> 24;
    buf[1] = (char)((val & 0x00FF0000) >> 16);
    buf[2] = (val & 0x0000FF00) >> 8;
    buf[3] = (val & 0x000000FF);
    return buf + 4;
}

/* QuickTime transformation matrix.
a,b,c,d,x,y: divided as 16.16 bits.   x' = a*x + c*y + tx
u,v,w;       divided as 2.30 bits.    y' = b*x + d*y + ty
// REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/support/Matrix.java */
struct matrix {
    static constexpr uint32_t SIZE = 9 * sizeof(int32_t); // serialization size

    double a=1,  b=0,  u=0;
    double c=0,  d=1,  v=0;
    double tx=0, ty=0, w=1;

    matrix() = default;

    void Read(const char* buf) {
        a = ReadFixed1616(buf); buf += sizeof(int32_t);
        b = ReadFixed1616(buf); buf += sizeof(int32_t);
        u = ReadFixed0230(buf); buf += sizeof(int32_t);
        assert(u == 0.0);
        c = ReadFixed1616(buf); buf += sizeof(int32_t);
        d = ReadFixed1616(buf); buf += sizeof(int32_t);
        v = ReadFixed0230(buf); buf += sizeof(int32_t);
        assert(v == 0.0);
        tx = ReadFixed1616(buf); buf += sizeof(int32_t);
        ty = ReadFixed1616(buf); buf += sizeof(int32_t);
        w = ReadFixed0230(buf); buf += sizeof(int32_t);
        assert(w == 1.0);
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


/** Cast Windows FILETIME to a 64bit integer to ease computations. 
    Unit: 100-nanosecond intervals since January 1, 1601 (UTC). */
inline uint64_t FileTimeToU64(FILETIME winTime) {
    ULARGE_INTEGER res{};
    res.HighPart = winTime.dwHighDateTime;
    res.LowPart = winTime.dwLowDateTime;
    return res.QuadPart;
}
/** Cast a 64bit integet back to a Windows FILETIME.
    Unit: 100-nanosecond intervals since January 1, 1601 (UTC). */
inline FILETIME U64ToFileTime(uint64_t winTime) {
    ULARGE_INTEGER tmp{};
    tmp.QuadPart = winTime;

    FILETIME ft{};
    ft.dwHighDateTime = tmp.HighPart;
    ft.dwLowDateTime = tmp.LowPart;
    return ft;
}


/** Convert from Unix epoch to MPEG-4 epoch in seconds since midnight, Jan. 1, 1904.
    Typically called with time(NULL) as input. */
inline uint64_t UnixTimeToMpeg4Time(uint64_t unixTime) {
    // Seconds between 1904-01-01 and Unix 1970 Epoch: (66 * 365 + 17) * (24 * 60 * 60) = 2082844800 (from https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mov.c)
    uint64_t mpeg4Time = unixTime + (66 * 365 + 17) * (24 * 60 * 60);
    return mpeg4Time;
}

/** Convert from MPEG4 epoch to Unix epoch in since midnight, Jan. 1, 1970. */
inline uint64_t Mpeg4TimeToUnixTime(uint64_t mpeg4Time) {
    // Seconds between 1904-01-01 and Unix 1970 Epoch: (66 * 365 + 17) * (24 * 60 * 60) = 2082844800 (from https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mov.c)
    uint64_t unixTime = mpeg4Time - (66 * 365 + 17) * (24 * 60 * 60);
    return unixTime;
}

constexpr uint64_t FILETIME_PER_SECONDS = 10000000;

/** Convert from 100-nanosecond intervals since January 1, 1601 (UTC) to MPEG4 time.
    Typically called with GetSystemTimeAsFileTime() as input. */
inline uint64_t WindowsTimeToMpeg4Time(FILETIME winTime) {
    FILETIME epochTime{}; // MPEG4 1904 epoch
    {
        SYSTEMTIME st{};
        st.wYear = 1904;
        st.wMonth = 1;
        st.wDay = 1;
        SystemTimeToFileTime(&st, &epochTime);
    }

    uint64_t mpegTime = FileTimeToU64(winTime) - FileTimeToU64(epochTime);

    // convert frp, 100-nanosecond intervals to seconds
    return mpegTime/FILETIME_PER_SECONDS;
}

inline FILETIME Mpeg4TimeToWindowsTime(uint64_t mpeg4Time) {
    FILETIME epochTime{}; // MPEG4 1904 epoch
    {
        SYSTEMTIME st{};
        st.wYear = 1904;
        st.wMonth = 1;
        st.wDay = 1;
        SystemTimeToFileTime(&st, &epochTime);
    }

    uint64_t winTime = FileTimeToU64(epochTime) + mpeg4Time*FILETIME_PER_SECONDS;
    return U64ToFileTime(winTime);
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
