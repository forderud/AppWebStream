#include <atlbase.h>
#include <stdio.h>
#include "Mpeg4Receiver.hpp"
#include "../AppWebStream/ComUtil.hpp"
#include "../AppWebStream/MP4Utils.hpp"
#include "DisplayWindow.hpp"


static void OnProcessFrame(Mpeg4Receiver& receiver, int64_t frameTime, int64_t frameDuration, std::string_view buffer, bool metadataChanged) {
    wprintf(L"Frame received:\n");

    uint64_t startTime = receiver.GetStartTime(); // SECONDS since midnight, Jan. 1, 1904
    double dpi = receiver.GetDpi();
    auto resolution = receiver.GetResolution();
    if (metadataChanged) {
        wprintf(L"  Start time: %hs (UTC)\n", TimeString1904(startTime).c_str());
        wprintf(L"  Frame DPI:  %f\n", dpi);
        wprintf(L"  Frame resolution: %u x %u\n", resolution[0], resolution[1]);
    }

    wprintf(L"  Frame time:     %f ms\n", frameTime * 0.1f / 1000); // convert to milliseconds
    wprintf(L"  Frame duration: %f ms\n", frameDuration * 0.1f / 1000); // convert to milliseconds

    // TODO: Display RGBA pixel data from "buffer" in window
    buffer;
}


int main(int argc, char* argv[]) {
    if (argc < 2) {
        wprintf(L"Usage: StreamReceiver.exe URL (e.g. StreamReceiver.exe http://localhost:8080/movie.mp4)\n");
        return -1;
    }

#if 0
    DisplayWindow wnd;
#endif

    _bstr_t url = argv[1];
    // connect to MPEG4 H.264 stream
    Mpeg4Receiver receiver(url, OnProcessFrame);

    HRESULT hr = S_OK;
    while (SUCCEEDED(hr)) {
        hr = receiver.ReceiveFrame();
    }
}


class StreamReceiverModule : public ATL::CAtlExeModuleT<StreamReceiverModule> {
public:
};

StreamReceiverModule _AtlModule;
