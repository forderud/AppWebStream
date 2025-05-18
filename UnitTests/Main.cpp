#include <Windows.h>
#include <iostream>
#include "../AppWebStream/MP4Utils.hpp"


int main() {
    printf("Running unit tests...\n");

    {
        uint64_t unixTime = time(NULL); // unix epoch since 1970-01-01
        uint64_t mpegTime = UnixTimeToMpeg4Time(unixTime);
        uint64_t unixTime2 = Mpeg4TimeToUnixTime(mpegTime);
        if (unixTime2 != unixTime)
            throw std::runtime_error("Time conversion error");
    }

    printf("[success]\n");
}
