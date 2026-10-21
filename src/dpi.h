#pragma once
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
namespace Dpi {
inline int scale(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }
inline UINT of(HWND window) { const UINT value = GetDpiForWindow(window); return value ? value : 96; }
inline RECT rect(int left, int top, int right, int bottom, UINT dpi) {
    return {scale(left,dpi),scale(top,dpi),scale(right,dpi),scale(bottom,dpi)};
}
inline HFONT font(int size, int weight, const wchar_t* face, UINT dpi) {
    return CreateFontW(-scale(size,dpi),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,face);
}
inline void enable() {
    // The manifest is authoritative; this also covers native test hosts.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}
inline SIZE outerSize(HWND window, int width, int height, UINT dpi) {
    RECT r{0,0,scale(width,dpi),scale(height,dpi)};
    AdjustWindowRectExForDpi(&r,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,
        static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi);
    return {r.right-r.left,r.bottom-r.top};
}
inline constexpr UINT FocusVisible = WM_APP + 41;
struct DialogScroll { int contentHeight=0, offset=0, wheel=0; UINT dpi=96; };
inline DialogScroll* scrollState(HWND hwnd) { return static_cast<DialogScroll*>(GetPropW(hwnd,L"EyeBreak.DpiScroll")); }
inline void scroll(HWND hwnd, int requested) {
    auto state=scrollState(hwnd); if(!state) return;
    RECT client{}; GetClientRect(hwnd,&client);
    const int next=std::clamp(requested,0,std::max<int>(0,state->contentHeight-client.bottom));
    const int delta=state->offset-next;
    if(delta) for(HWND child=GetWindow(hwnd,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
        RECT r{}; GetWindowRect(child,&r); MapWindowPoints(nullptr,hwnd,reinterpret_cast<POINT*>(&r),2);
        SetWindowPos(child,nullptr,r.left,r.top+delta,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    state->offset=next;
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,state->contentHeight-1),static_cast<UINT>(client.bottom),next,0};
    SetScrollInfo(hwnd,SB_VERT,&info,TRUE); InvalidateRect(hwnd,nullptr,TRUE);
}
inline void fitDialog(HWND hwnd) {
    RECT outer{},client{}; GetWindowRect(hwnd,&outer); GetClientRect(hwnd,&client);
    auto state=scrollState(hwnd);
    const UINT dpi=of(hwnd);
    if(!state) { state=new DialogScroll{client.bottom,0,0,dpi}; SetPropW(hwnd,L"EyeBreak.DpiScroll",state); }
    else if(state->dpi!=dpi) {
        state->contentHeight=MulDiv(state->contentHeight,dpi,state->dpi);
        state->offset=MulDiv(state->offset,dpi,state->dpi); state->dpi=dpi;
    }
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
    const int border=outer.bottom-outer.top-client.bottom;
    const int fullHeight=state->contentHeight+border;
    const int height=std::min<int>(fullHeight,monitor.rcWork.bottom-monitor.rcWork.top-scale(16,dpi));
    const int top=std::clamp<LONG>(outer.top,monitor.rcWork.top,std::max<LONG>(monitor.rcWork.top,monitor.rcWork.bottom-height));
    auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);
    SetWindowLongPtrW(hwnd,GWL_STYLE,height<fullHeight ? style|WS_VSCROLL : style&~WS_VSCROLL);
    SetWindowPos(hwnd,nullptr,outer.left,top,outer.right-outer.left,height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    scroll(hwnd,state->offset);
}
inline LRESULT CALLBACK focusSubclass(HWND hwnd,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
    if(msg==WM_SETFOCUS) PostMessageW(GetParent(hwnd),FocusVisible,0,reinterpret_cast<LPARAM>(hwnd));
    if(msg==WM_NCDESTROY) RemoveWindowSubclass(hwnd,focusSubclass,9);
    return DefSubclassProc(hwnd,msg,w,l);
}
inline bool dialogScrollMessage(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    auto state=scrollState(hwnd);
    if(msg==WM_NCDESTROY) { delete state; RemovePropW(hwnd,L"EyeBreak.DpiScroll"); return false; }
    if(!state) return false;
    if(msg==WM_MOUSEWHEEL) {
        state->wheel+=static_cast<short>(HIWORD(w)); const int steps=state->wheel/WHEEL_DELTA; state->wheel%=WHEEL_DELTA;
        scroll(hwnd,state->offset-steps*scale(48,state->dpi)); return true;
    }
    if(msg==WM_VSCROLL) {
        int next=state->offset; RECT client{}; GetClientRect(hwnd,&client);
        switch(LOWORD(w)) {
        case SB_LINEUP: next-=scale(24,state->dpi); break;
        case SB_LINEDOWN: next+=scale(24,state->dpi); break;
        case SB_PAGEUP: next-=client.bottom; break;
        case SB_PAGEDOWN: next+=client.bottom; break;
        case SB_TOP: next=0; break;
        case SB_BOTTOM: next=state->contentHeight; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: {
            SCROLLINFO info{sizeof(info),SIF_TRACKPOS}; GetScrollInfo(hwnd,SB_VERT,&info); next=info.nTrackPos; break;
        }
        }
        scroll(hwnd,next); return true;
    }
    if(msg==FocusVisible && IsWindow(reinterpret_cast<HWND>(l))) {
        RECT r{},client{}; GetWindowRect(reinterpret_cast<HWND>(l),&r);
        MapWindowPoints(nullptr,hwnd,reinterpret_cast<POINT*>(&r),2); GetClientRect(hwnd,&client);
        const int margin=scale(8,state->dpi);
        if(r.top<margin) scroll(hwnd,state->offset+r.top-margin);
        else if(r.bottom>client.bottom-margin) scroll(hwnd,state->offset+r.bottom-client.bottom+margin);
        return true;
    }
    return false;
}
}
