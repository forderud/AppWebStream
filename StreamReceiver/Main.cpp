#include <atlbase.h>
#include <stdio.h>
#include "Mpeg4Receiver.hpp"
#include "../AppWebStream/ComUtil.hpp"
#include "../AppWebStream/MP4Utils.hpp"
#include "DisplayWindow.hpp"


int main(int argc, char* argv[]) {
    if (argc < 2) {
        wprintf(L"Usage: StreamReceiver.exe URL (e.g. StreamReceiver.exe http://localhost:8080/movie.mp4)\n");
        return -1;
    }

    DisplayWindow wnd;

    _bstr_t url = argv[1];
    // connect to MPEG4 H.264 stream
    using namespace std::placeholders;
    Mpeg4Receiver receiver(url, std::bind(&DisplayWindow::OnNewFrame, &wnd, _1, _2, _3, _4, _5));

    HRESULT hr = S_OK;
    while (SUCCEEDED(hr)) {
        hr = receiver.ReceiveFrame();

        // non-blocking message loop
        MSG msg{};
        while (BOOL ret = PeekMessageW(&msg, wnd, 0, 0, PM_REMOVE) != 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}


class StreamReceiverModule : public ATL::CAtlExeModuleT<StreamReceiverModule> {
public:
};

StreamReceiverModule _AtlModule;
