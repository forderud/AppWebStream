#include <cstdio>
#include <vector>
#include "ClientSocket.hpp"

#pragma comment(lib, "comsuppw.lib")


int main(int argc, char* argv[]) {
    if (argc < 2) {
        wprintf(L"Usage: StreamReceiver.exe URL (e.g. StreamReceiver.exe http://localhost:8080/movie.mp4)\n");
        return -1;
    }

    std::string servername, port, resource;
    std::tie(servername, port, resource) = ParseURL(argv[1]);

    ClientSocket sock(servername.c_str(), port.c_str());

    sock.WriteHttpGet(resource);

    for (;;) {
        std::vector<BYTE> buffer(1024*1024, (BYTE)0); // 1MB

        uint32_t res = sock.Read(buffer.data(), (ULONG)buffer.size());
        printf("Read %u bytes.\n", res);
        if (res == 0)
            break;

        // sleep for 5 seconds to aid debugging
        Sleep(5000);
    }
}
