#pragma once
#include <cassert>
#include <windows.h>


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
        HWND wnd = CreateWindowExW(0, // optional window styles
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
        assert(wnd);

        // show window
        ShowWindow(wnd, SW_SHOW);
    }

    ~DisplayWindow() {
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
};
