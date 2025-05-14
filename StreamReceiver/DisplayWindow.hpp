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
#if 0
        ShowWindow(m_wnd, SW_SHOW);
#endif
    }

    ~DisplayWindow() {
    }

    void OnNewFrame(Mpeg4Receiver& receiver, int64_t frameTime, int64_t frameDuration, std::string_view buffer, bool metadataChanged) {
        wprintf(L"Frame received:\n");

        uint64_t startTime = receiver.GetStartTime(); // SECONDS since midnight, Jan. 1, 1904
        double dpi = receiver.GetDpi();
        auto resolution = receiver.GetResolution();
        if (metadataChanged) {
            // resize window to new resolution
            CRect rect; // outer rectangle
            GetWindowRect(m_wnd, &rect);
            CRect crect; // inner client rectangle
            GetClientRect(m_wnd, &crect);
            MoveWindow(m_wnd, rect.left, rect.top, resolution[0] + (rect.Width() - crect.Width()), resolution[1] + (rect.Height() - crect.Height()), /*repaint*/false);

            // log new frame metadata
            wprintf(L"  Start time: %hs (UTC)\n", TimeString1904(startTime).c_str());
            wprintf(L"  Frame DPI:  %f\n", dpi);
            wprintf(L"  Frame resolution: %u x %u\n", resolution[0], resolution[1]);
        }

        wprintf(L"  Frame time:     %f ms\n", frameTime * 0.1f / 1000); // convert to milliseconds
        wprintf(L"  Frame duration: %f ms\n", frameDuration * 0.1f / 1000); // convert to milliseconds

        DrawBitmap(resolution, buffer);
    }

private:
    void DrawBitmap(std::array<uint32_t, 2> resolution, std::string_view buffer) {
        RECT rc{};
        GetClientRect(m_wnd, &rc);

        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(m_wnd, &ps);
        {
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = resolution[0];
            bmi.bmiHeader.biHeight = -(int)resolution[1]; // negative to indicate origin in upper left corner
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            bmi.bmiHeader.biSizeImage = 0; // zero for uncompressed RGB bitmaps
            bmi.bmiHeader.biClrUsed = 0;
            bmi.bmiHeader.biClrImportant = 0;

            int lines = StretchDIBits(dc, 0, 0, rc.right, rc.bottom, 0, 0, resolution[0], resolution[1], buffer.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
            assert(lines == (int)resolution[1]);
        }
        EndPaint(m_wnd, &ps);
    }

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
