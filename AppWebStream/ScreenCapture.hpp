#pragma once
#include <Windows.h>


class window_dc {
public:
    window_dc(HWND h) : m_wnd(h) {
        m_dc = GetDC(m_wnd);
        if (m_wnd) {
            if (!GetClientRect(h, &m_rect))
                throw std::runtime_error("GetClientRect failed");
        } else {
            // primary monitor resolution
            m_rect.right = GetSystemMetrics(SM_CXSCREEN);
            m_rect.bottom = GetSystemMetrics(SM_CYSCREEN);
        }
    }
    ~window_dc() {
        ReleaseDC(m_wnd, m_dc);
    }

    unsigned int width() const {
        return m_rect.right - m_rect.left;
    }
    unsigned int height() const {
        return m_rect.bottom - m_rect.top;
    }

    HWND m_wnd = nullptr;
    HDC  m_dc = nullptr;
private:
    RECT m_rect = {};
};


class offscreen_bmp {
public:
    offscreen_bmp(HDC ref_dc, LONG width, LONG height) : m_width(width), m_height(height) {
        m_dc = CreateCompatibleDC(ref_dc);
        if (!m_dc)
            throw std::runtime_error("CreateCompatibleDC has failed");

        m_bmp = CreateCompatibleBitmap(ref_dc, width, height);
        if (!m_bmp)
            throw std::runtime_error("CreateCompatibleBitmap Failed");

        // make bitmap current for dc
        m_prev = SelectObject(m_dc, m_bmp);
    }
    ~offscreen_bmp() {
        SelectObject(m_dc, m_prev);
        DeleteObject(m_bmp);
        DeleteDC(m_dc);
    }

    int CopyToRGBABuffer(window_dc& src, /*out*/uint32_t* dst_ptr) {
        // copy window content to bitmap
        if (!BitBlt(/*dst*/m_dc, 0, 0, m_width, m_height, /*src*/src.m_dc, 0, 0, SRCCOPY))
            return -1;

        BITMAPINFO bmp_info{};
        bmp_info.bmiHeader.biSize = sizeof(bmp_info.bmiHeader);
        {
            // call GetDIBits to fill "bmp_info" struct 
            int ok = GetDIBits(m_dc, m_bmp, 0, m_height, nullptr, &bmp_info, DIB_RGB_COLORS);
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

        // copy bitmap content to destination buffer
        int scan_lines = GetDIBits(m_dc, m_bmp, 0, m_height, dst_ptr, &bmp_info, DIB_RGB_COLORS);
        return scan_lines;
    }

    HDC          m_dc = nullptr;
    HBITMAP      m_bmp = nullptr;
private:
    unsigned int m_width = 0;
    unsigned int m_height = 0;
    HGDIOBJ      m_prev = nullptr;
};
