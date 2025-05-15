#include <atlbase.h>
#include <stdio.h>
#include <thread>
#include "Mpeg4Receiver.hpp"
#include "../AppWebStream/ComUtil.hpp"
#include "../AppWebStream/MP4Utils.hpp"
#include "DisplayWindow.hpp"


void ReceiveMovieThread(Mpeg4Receiver* receiver) {

    HRESULT hr = S_OK;
    while (SUCCEEDED(hr)) {
        hr = receiver->ReceiveFrame();
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        wprintf(L"Usage: StreamReceiver.exe URL (e.g. StreamReceiver.exe http://localhost:8080/movie.mp4)\n");
        return -1;
    }

    // create on-screen window for image display
    DisplayWindow wnd;

    // connect to MPEG4 H.264 stream
    _bstr_t url = argv[1];
    using namespace std::placeholders;
    Mpeg4Receiver receiver(url, std::bind(&DisplayWindow::OnNewFrame, wnd, _1, _2, _3, _4, _5));

    // start MPEG4 stream receive thread
    std::thread t(ReceiveMovieThread, &receiver);

    // message loop
    MSG msg{};
    while (BOOL ret = GetMessageW(&msg, wnd, 0, 0)) {
        if (ret == -1) // winodw close
            break;

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    receiver.Stop();
    t.join();
}


class StreamReceiverModule : public ATL::CAtlExeModuleT<StreamReceiverModule> {
public:
};

StreamReceiverModule _AtlModule;
