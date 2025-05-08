#pragma once
#include <windows.h>
#include <comdef.h> // for _com_error


/** Converts unicode string to ASCII */
static inline std::string ToAscii(const std::wstring& w_str) {
#pragma warning(push)
#pragma warning(disable: 4996) // function or variable may be unsafe
    size_t N = w_str.size();
    std::string s_str;
    s_str.resize(N);
    wcstombs(const_cast<char*>(s_str.data()), w_str.c_str(), N);

    return s_str;
#pragma warning(pop)
}

static void COM_CHECK(HRESULT hr) {
    if (FAILED(hr)) {
        _com_error err(hr);
        const wchar_t* msg = err.ErrorMessage(); // weak ptr.
        throw std::runtime_error(ToAscii(msg));
    }
}
