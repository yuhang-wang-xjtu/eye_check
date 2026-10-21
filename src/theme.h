#pragma once
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include "dpi.h"
#include "typography.h"
#include "resource.h"
#include "app_icon.h"
namespace Theme {
inline bool Light = false;
inline COLORREF Background = RGB(18, 27, 36), Surface = RGB(35, 57, 65);
inline COLORREF Text = RGB(235, 243, 249), Muted = RGB(152, 174, 187), Accent = RGB(119, 215, 192);
inline void setLight(bool light) {
    Light=light;
    Background=light?RGB(246,248,249):RGB(18,27,36);
    Surface=light?RGB(223,237,233):RGB(35,57,65);
    Text=light?RGB(27,46,49):RGB(235,243,249);
    Muted=light?RGB(81,103,109):RGB(152,174,187);
    Accent=light?RGB(16,111,91):RGB(119,215,192);
}
inline HBRUSH colorBrush(COLORREF color, HBRUSH& brush, COLORREF& previous) {
    if(!brush || color!=previous) { if(brush) DeleteObject(brush); brush=CreateSolidBrush(color); previous=color; }
    return brush;
}
inline HBRUSH backgroundBrush() { static HBRUSH b=nullptr; static COLORREF c=0; return colorBrush(Background,b,c); }
inline HBRUSH surfaceBrush() { static HBRUSH b=nullptr; static COLORREF c=0; return colorBrush(Surface,b,c); }
inline void titlebar(HWND window) {
    BOOL dark = !Light; DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    COLORREF color = Background; DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &color, sizeof(color));
}
inline void button(const DRAWITEMSTRUCT* item, HFONT font) {
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    HBRUSH brush = CreateSolidBrush(disabled ? (Light?RGB(234,238,239):RGB(30,38,45)) :
        ((item->itemState & ODS_SELECTED) ? (Light?RGB(188,219,211):RGB(42,76,79)) : Surface));
    FillRect(item->hDC, &item->rcItem, brush); DeleteObject(brush);
    wchar_t text[128]{}; GetWindowTextW(item->hwndItem, text, 128);
    auto old = SelectObject(item->hDC, font);
    SetTextColor(item->hDC, disabled ? Muted : Text); SetBkMode(item->hDC, TRANSPARENT);
    RECT rect = item->rcItem;
    DrawTextW(item->hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(item->hDC, old);
    if (item->itemState & ODS_FOCUS) { const int inset = Dpi::scale(4,Dpi::of(item->hwndItem)); InflateRect(&rect, -inset, -inset); DrawFocusRect(item->hDC, &rect); }
}
inline LRESULT CALLBACK checkbox(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); RECT r{}; GetClientRect(hwnd, &r);
        FillRect(dc, &r, backgroundBrush());
        const UINT dpi = Dpi::of(hwnd); const auto px=[&](int v){return Dpi::scale(v,dpi);};
        const int top = (r.bottom - px(16)) / 2; RECT box{0, top, px(16), top + px(16)};
        FillRect(dc, &box, surfaceBrush());
        HBRUSH border = CreateSolidBrush(Accent); FrameRect(dc, &box, border); DeleteObject(border);
        if (SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            HPEN pen = CreatePen(PS_SOLID, px(2), Accent); auto old = SelectObject(dc, pen);
            MoveToEx(dc, px(3), top + px(8), nullptr); LineTo(dc, px(7), top + px(12)); LineTo(dc, px(13), top + px(4));
            SelectObject(dc, old); DeleteObject(pen);
        }
        wchar_t text[128]{}; GetWindowTextW(hwnd, text, 128);
        auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(hwnd,WM_GETFONT,0,0))); r.left = px(26);
        SetTextColor(dc, IsWindowEnabled(hwnd) ? Text : Muted); SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (GetFocus() == hwnd) DrawFocusRect(dc, &r);
        SelectObject(dc, old); EndPaint(hwnd, &ps); return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, checkbox, 1);
    return DefSubclassProc(hwnd, message, w, l);
}
inline BOOL CALLBACK child(HWND hwnd, LPARAM font) {
    SetWindowSubclass(hwnd,Dpi::focusSubclass,9,0);
    SendMessageW(hwnd, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
    wchar_t name[32]{}; GetClassNameW(hwnd, name, 32);
    if (_wcsicmp(name, L"Button") == 0) {
        const auto type = GetWindowLongPtrW(hwnd, GWL_STYLE) & BS_TYPEMASK;
        if (type == BS_AUTOCHECKBOX || type == BS_CHECKBOX) SetWindowSubclass(hwnd, checkbox, 1, static_cast<DWORD_PTR>(font));
        else SendMessageW(hwnd, BM_SETSTYLE, BS_OWNERDRAW, TRUE);
    } else if (_wcsicmp(name, L"Edit") == 0) SetWindowTheme(hwnd, Light?L"Explorer":L"DarkMode_Explorer", nullptr);
    return TRUE;
}
inline void apply(HWND hwnd) {
    titlebar(hwnd);
    AppIcon::apply(hwnd);
    // Dialog manager owns and scales this font together with dialog units.
    const auto font=reinterpret_cast<HFONT>(SendMessageW(hwnd,WM_GETFONT,0,0));
    EnumChildWindows(hwnd, child, reinterpret_cast<LPARAM>(font));
    HFONT caption=Dpi::font(Typography::Caption,FW_NORMAL,Typography::Face,Dpi::of(hwnd));
    HFONT old=static_cast<HFONT>(GetPropW(hwnd,L"EyeBreak.CaptionFont"));
    SetPropW(hwnd,L"EyeBreak.CaptionFont",caption);
    for(int id : {IDC_NIGHT_HELP,IDC_HOTKEY_HELP,IDC_RETURN_HELP,IDC_HEALTH_HELP,IDC_USAGE_HELP})
        if(HWND control=GetDlgItem(hwnd,id)) SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(caption),TRUE);
    if(old) DeleteObject(old);
}
inline INT_PTR dialogMessage(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_NCDESTROY:
        if(HANDLE font=RemovePropW(hwnd,L"EyeBreak.CaptionFont")) DeleteObject(font);
        return 0;
    case WM_CTLCOLORDLG: return reinterpret_cast<INT_PTR>(backgroundBrush());
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(w), Text); SetBkColor(reinterpret_cast<HDC>(w), Background);
        return reinterpret_cast<INT_PTR>(backgroundBrush());
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(w), Text); SetBkColor(reinterpret_cast<HDC>(w), Surface);
        return reinterpret_cast<INT_PTR>(surfaceBrush());
    case WM_DRAWITEM: {
        const auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);
        button(item,reinterpret_cast<HFONT>(SendMessageW(item->hwndItem,WM_GETFONT,0,0))); return TRUE;
    }
    }
    return 0;
}
}
