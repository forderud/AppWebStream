#include <atlbase.h>
#include <stdio.h>
#include "Mpeg4Receiver.hpp"
#include "../AppWebStream/ComUtil.hpp"
#include "../AppWebStream/MP4Utils.hpp"


static unsigned int Align16(unsigned int size) {
    if ((size % 16) == 0)
        return size;
    else
        return size + 16 - (size % 16);
}


static void OnProcessFrame(Mpeg4Receiver& receiver, IMFSample& frame, bool metadataChanged) {
    wprintf(L"Frame received:\n");

    uint64_t startTime = receiver.GetStartTime(); // SECONDS since midnight, Jan. 1, 1904
    double dpi = receiver.GetDpi();
    auto resolution = receiver.GetResolution();
    if (metadataChanged) {
        wprintf(L"  Start time: %hs (UTC)\n", TimeString1904(startTime).c_str());
        wprintf(L"  Frame DPI:  %f\n", dpi);
        wprintf(L"  Frame resolution: %u x %u\n", resolution[0], resolution[1]);
    }

    int64_t frameTime = 0; // in 100-nanosecond units since startTime
    COM_CHECK(frame.GetSampleTime(&frameTime));
    wprintf(L"  Frame time:     %f ms\n", frameTime * 0.1f / 1000); // convert to milliseconds

    int64_t frameDuration = 0; // in 100-nanosecond units
    COM_CHECK(frame.GetSampleDuration(&frameDuration));
    wprintf(L"  Frame duration: %f ms\n", frameDuration * 0.1f / 1000); // convert to milliseconds

    {
        DWORD bufferCount = 0;
        COM_CHECK(frame.GetBufferCount(&bufferCount));
        assert(bufferCount == 1); // one buffer per frame for video
    }

    {
        IMFMediaBufferPtr buffer;
        COM_CHECK(frame.GetBufferByIndex(0, &buffer)); // only one buffer per frame for video

        BYTE* bufferPtr = nullptr;
        DWORD bufferSize = 0;
        COM_CHECK(buffer->Lock(&bufferPtr, nullptr, &bufferSize));
        assert(bufferSize == 4 * Align16(resolution[0]) * Align16(resolution[1])); // buffer size is a multiple of MPEG4 16x16 macroblocks

        // TODO: Access RGBA pixel data in bufferPtr

        COM_CHECK(buffer->Unlock());
    }
}


int main(int argc, char* argv[]) {
    if (argc < 2) {
        wprintf(L"Usage: StreamReceiver.exe URL (e.g. StreamReceiver.exe http://localhost:8080/movie.mp4)\n");
        return -1;
    }

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
