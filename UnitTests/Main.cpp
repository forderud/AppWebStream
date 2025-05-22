#include <Windows.h>
#include <iostream>
#include "../AppWebStream/MP4Utils.hpp"


void TimeConvTests() {
    printf("* Time conversion tests.\n");
    {
        // test time conversion to & from MPE4 epoch
        uint64_t unixTime = time(NULL); // current time in seconds since 1970-01-01
        uint64_t mpegTime = UnixTimeToMpeg4Time(unixTime);
        uint64_t unixTime2 = Mpeg4TimeToUnixTime(mpegTime);
        if (unixTime2 != unixTime)
            throw std::runtime_error("Time conversion error");
    }

    {
        // Compare Windows FILETIME-based time encoding against Unix time_t-based time encoding.
        // Use midnight of 18th of May 2025 as example date.

        FILETIME winTime{}; // 100-nanosecond intervals since January 1, 1601 (UTC)
        {
            SYSTEMTIME st{};
            st.wYear = 2025;
            st.wMonth = 5; // [1-12]
            //st.wDayOfWeek = 0; // [0-6]
            st.wDay = 18;  // [1-31]
            st.wHour = 0; // [0-23]
            st.wMinute = 0; // [0-59]
            st.wSecond = 0; // [0-59]
            st.wMilliseconds = 0; // [0-999]
            SystemTimeToFileTime(&st, &winTime);
        }

        time_t unixTime = 0; // seconds since midnight, Jan. 1, 1970
        {
            tm tmp{};
            tmp.tm_sec;   // [0, 60] including leap second
            tmp.tm_min;   // [0, 59]
            tmp.tm_hour;  // [0, 23]
            tmp.tm_mday = 18;  // day of the month - [1, 31]
            tmp.tm_mon = 5 - 1;   // months since January - [0, 11]
            tmp.tm_year = 2025 - 1900;  // years since 1900
            //tmp.tm_wday;  // days since Sunday - [0, 6]
            //tmp.tm_yday;  // days since January 1 - [0, 365]
            //tmp.tm_isdst; // daylight savings time flag
            unixTime = _mkgmtime(&tmp);
        }

        uint64_t winMpeg = WindowsTimeToMpeg4Time(winTime);
        uint64_t unixMpeg = UnixTimeToMpeg4Time(unixTime);
        if (winMpeg != unixMpeg)
            throw std::runtime_error("Time conversion error");
    }

    {
        // convert between FILETIME and MPEG4 time
        FILETIME winTime{};
        GetSystemTimeAsFileTime(&winTime);

        uint64_t mpegTime = WindowsTimeToMpeg4Time(winTime); // rounds down to the nearest second
        FILETIME winTime2 = Mpeg4TimeToWindowsTime(mpegTime);

        uint64_t diff = FileTimeToU64(winTime) - FileTimeToU64(winTime2);
        if (std::llabs(diff) >= FILETIME_PER_SECONDS) // difference should never exceed 1sec
            throw std::runtime_error("Time conversion error");
    }
}

void SerializationTests() {
    printf("* Serialization tests.\n");

    {
        // 2 byte data
        uint16_t val1 = 0x1c28;
        
        char buffer[2] = {};
        char* end = Serialize(buffer, val1);
        if (end != buffer+2)
            throw std::runtime_error("serialization error");

        auto val2 = DeSerialize<uint16_t>(buffer);
        if (val2 != val1)
            throw std::runtime_error("serialization error");
    }
    {
        // 4 byte data
        uint32_t val1 = 0x11223344;

        char buffer[4] = {};
        char* end = Serialize(buffer, val1);
        if (end != buffer + 4)
            throw std::runtime_error("serialization error");

        auto val2 = DeSerialize<uint32_t>(buffer);
        if (val2 != val1)
            throw std::runtime_error("serialization error");
    }
}

void FixedPointTests() {
    const double TEST_VALUES_1616[] = {-32768, -10000, -1000, -100, -10.0, -5.0, -1.0, -0.5, 0.0, 0.5, 1.0, 5.0, 10.0, 100, 1000, 10000, 32767.75}; // range [-32768, +32768)
    for (double val1 : TEST_VALUES_1616) {
        // 16+16 bits
        char buffer[2 * sizeof(uint16_t)] = {};
        WriteFixed1616(buffer, val1);
        double val2 = ReadFixed1616(buffer);

        if (std::fabs(val2 - val1) > 1e-9)
            throw std::runtime_error("fixed point error");
    }

    const double TEST_VALUES_88[] = {-128, -100, -10.0, -5.0, -1.0, -0.5, 0.0, 0.5, 1.0, 5.0, 10.0, 100, 127.75}; // range [-128, +128)
    for (double val1 : TEST_VALUES_88) {
        // 8+8 bits
        char buffer[2 * sizeof(uint8_t)] = {};
        WriteFixed88(buffer, val1);
        double val2 = ReadFixed88(buffer);

        if (std::fabs(val2 - val1) > 1e-9)
            throw std::runtime_error("fixed point error");
    }

    const double TEST_VALUES_0230[] = {-2.0, -1.0, -0.5, 0.0, 0.5, 1.0, 1.9}; // range [-2, +2)
    for (double val1 : TEST_VALUES_0230) {
        // 2+30 bits
        char buffer[sizeof(uint32_t)] = {};
        WriteFixed0230(buffer, val1);
        double val2 = ReadFixed0230(buffer);

        if (std::fabs(val2 - val1) > 1e-9)
            throw std::runtime_error("fixed point error");
    }

}

int main() {
    printf("Running unit tests:\n");

    SerializationTests();
    TimeConvTests();
    FixedPointTests();

    printf("[success]\n");
}
