#pragma once
#include <cassert>
#include <windows.h>
#include <atltypes.h>


/** Window for displaying received RGBA images. */
class DisplayWindow {
public:
    DisplayWindow() {
        HINSTANCE instance = GetModuleHandleW(NULL);

        // register window class
        const wchar_t CLASS_NAME[] = L"StreamReceiver class";
        WNDCLASS wc = {};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = CLASS_NAME;
        RegisterClassW(&wc);

        // create offscreen window 
        m_wnd = CreateWindowExW(0, // optional window styles
            CLASS_NAME,               // window class
            L"StreamReceiver",        // window text
            WS_OVERLAPPEDWINDOW,      // window style
            CW_USEDEFAULT, CW_USEDEFAULT, // top-left position
            256, 256, // size
            NULL,      // parent
            NULL,      // menu
            instance,  // instance handle
            NULL       // additional data
        );
        assert(m_wnd);

        // show window
        ShowWindow(m_wnd, SW_SHOW);
    }

    ~DisplayWindow() {
    }

    void OnNewFrame(Mpeg4Receiver& receiver, int64_t frameTime, int64_t frameDuration, std::string_view buffer, bool metadataChanged) {
        wprintf(L"Frame received:\n");

        uint64_t startTime = receiver.GetStartTime(); // SECONDS since midnight, Jan. 1, 1904
        double dpi = receiver.GetDpi();
        auto resolution = receiver.GetResolution();
        if (metadataChanged) {
#if 0
            // resize window
            CRect rect; // outer rectangle
            GetWindowRect(m_wnd, &rect);
            CRect crect; // inner client rectangle
            GetClientRect(m_wnd, &crect);
            MoveWindow(m_wnd, rect.left, rect.top, resolution[0] + (rect.Width() - crect.Width()), resolution[1] + (rect.Height() - crect.Height()), /*repaint*/false);
#endif
            wprintf(L"  Start time: %hs (UTC)\n", TimeString1904(startTime).c_str());
            wprintf(L"  Frame DPI:  %f\n", dpi);
            wprintf(L"  Frame resolution: %u x %u\n", resolution[0], resolution[1]);
        }

        wprintf(L"  Frame time:     %f ms\n", frameTime * 0.1f / 1000); // convert to milliseconds
        wprintf(L"  Frame duration: %f ms\n", frameDuration * 0.1f / 1000); // convert to milliseconds

        // TODO: Display RGBA pixel data from "buffer" in window
        buffer;
    }

private:
    /** Window procedure for processing messages. */
    static LRESULT WindowProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProc(wnd, msg, wParam, lParam);
    }

    HWND m_wnd = 0;
};
