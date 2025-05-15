#include <iostream>
#include  <comutil.h>

#pragma comment(lib, "comsuppw.lib")


int main(int argc, char* argv[]) {
    if (argc < 2) {
        wprintf(L"Usage: StreamReceiver.exe URL (e.g. StreamReceiver.exe http://localhost:8080/movie.mp4)\n");
        return -1;
    }

    _bstr_t url = argv[1];
    printf("TODO: Write tool for dumping network stream sessions to file.\n");
}
