#pragma once
#include <Windows.h>


class window_dc {
public:
    window_dc(HWND h) : wnd(h) {
        dc = GetDC(wnd);
        if (wnd) {
            if (!GetClientRect(h, &rect))
                throw std::runtime_error("GetClientRect failed");
        } else {
            // primary monitor resolution
            rect.right = GetSystemMetrics(SM_CXSCREEN);
            rect.bottom = GetSystemMetrics(SM_CYSCREEN);
        }
    }
    ~window_dc() {
        ReleaseDC(wnd, dc);
    }

    LONG width() const {
        return rect.right - rect.left;
    }
    LONG height() const {
        return rect.bottom - rect.top;
    }

    HWND wnd = nullptr;
    HDC  dc = nullptr;
private:
    RECT rect = {};
};


class offscreen_bmp {
public:
    offscreen_bmp(HDC ref_dc, LONG width, LONG height) {
        dc = CreateCompatibleDC(ref_dc);
        if (!dc)
            throw std::runtime_error("CreateCompatibleDC has failed");

        bmp = CreateCompatibleBitmap(ref_dc, width, height);
        if (!bmp)
            throw std::runtime_error("CreateCompatibleBitmap Failed");

        // make bitmap current for dc
        prev = SelectObject(dc, bmp);
    }
    ~offscreen_bmp() {
        SelectObject(dc, prev);
        DeleteObject(bmp);
        DeleteDC(dc);
    }

    int CopyToRGBABuffer(window_dc& src, /*out*/uint32_t* dst_ptr) {
        if (!BitBlt(/*dst*/dc, 0, 0, src.width(), src.height(), /*src*/src.dc, 0, 0, SRCCOPY))
            return -1;

        BITMAPINFO bmp_info = {};
        bmp_info.bmiHeader.biSize = sizeof(bmp_info.bmiHeader);
        {
            // call GetDIBits to fill "bmp_info" struct 
            int ok = GetDIBits(dc, bmp, 0, src.height(), nullptr, &bmp_info, DIB_RGB_COLORS);
            if (!ok)
                return E_FAIL;
            bmp_info.bmiHeader.biBitCount = 32;     // request 32bit RGBA image 
            bmp_info.bmiHeader.biCompression = BI_RGB; // disable compression
#ifdef ENABLE_FFMPEG
            bmp_info.bmiHeader.biHeight = -abs(bmp_info.bmiHeader.biHeight); // request bitmap with origin in top-left corner
#else
            bmp_info.bmiHeader.biHeight = abs(bmp_info.bmiHeader.biHeight); // request bottom-up bitmap, with origin in lower-left corner
#endif
        }

        // call GetDIBits to get image data
        int scan_lines = GetDIBits(dc, bmp, 0, src.height(), dst_ptr, &bmp_info, DIB_RGB_COLORS);
        return scan_lines;
    }

    HDC     dc = nullptr;
    HBITMAP bmp = nullptr;
private:
    HGDIOBJ prev = nullptr;
};
