#pragma once
#include <windows.h>
#include "resource.h"
#include "dpi.h"
namespace AppIcon {
inline HICON load(int pixels) {
    return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP),
        IMAGE_ICON, pixels, pixels, LR_SHARED));
}
inline void apply(HWND hwnd, UINT dpi = 0) {
    if (!dpi) dpi = Dpi::of(hwnd);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
        reinterpret_cast<LPARAM>(load(GetSystemMetricsForDpi(SM_CXSMICON, dpi))));
    SendMessageW(hwnd, WM_SETICON, ICON_BIG,
        reinterpret_cast<LPARAM>(load(GetSystemMetricsForDpi(SM_CXICON, dpi))));
}
}
