#include <atlbase.h>
#include <stdio.h>
#include "Mpeg4Receiver.hpp"


int main(int argc, char* argv[]) {
    if (argc < 2) {
        wprintf(L"Usage: StreamReceiver.exe URL (e.g. StreamReceiver.exe http://localhost:8080/movie.mp4)\n");
        return -1;
    }

    _bstr_t url = argv[1];
    // connect to MPEG4 H.264 stream
    Mpeg4Receiver receiver(url);

    HRESULT hr = S_OK;
    while (SUCCEEDED(hr)) {
        hr = receiver.ReceiveFrame();
    }
}


class StreamReceiverModule : public ATL::CAtlExeModuleT<StreamReceiverModule> {
public:
};

StreamReceiverModule _AtlModule;
