// Exercise the production window procedures with an isolated configuration.
#include "../src/main.cpp"
#include <iostream>
#include <cstdlib>
void check(bool ok, const char* label) { if (!ok) { std::cerr << "FAILED: " << label << '\n'; std::exit(1); } }
int main(int argc, char** argv) {
    Dpi::enable();
    wchar_t directory[MAX_PATH]{}, file[MAX_PATH]{};
    GetTempPathW(MAX_PATH, directory); GetTempFileNameW(directory, L"ewt", 0, file);
    DeleteFileW(file); settingsFile = file;
    usageFile=std::wstring(file)+L".usage"; initializeUsage();
    settings.soundEnabled = settings.nightEnabled = settings.sittingEnabled = settings.workEnabled = false;
    background = CreateSolidBrush(bg);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    for(int size : {16,20,24,32,40,48,64,128,256}) {
        const HICON icon=AppIcon::load(size); ICONINFO info{}; BITMAP bitmap{};
        check(icon && GetIconInfo(icon,&info), "embedded icon available at Windows display sizes");
        GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap);
        check(bitmap.bmWidth==size && bitmap.bmHeight==size, "icon has requested pixel dimensions");
        DeleteObject(info.hbmColor); DeleteObject(info.hbmMask);
    }
    WNDCLASSW wc{}; wc.lpfnWndProc = proc; wc.hInstance = instance; wc.lpszClassName = L"EyeBreakWindowTest";
    check(RegisterClassW(&wc) != 0, "register test window");
    window = CreateWindowW(wc.lpszClassName, L"EyeBreak test", WS_OVERLAPPEDWINDOW, 0, 0, 640, 520, nullptr, nullptr, instance, nullptr);
    check(window != nullptr, "main window created");
    check(AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(window),DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2), "manifest selects per-monitor V2 awareness");
    const auto initial=Dpi::outerSize(window,496,384,mainDpi);
    SetWindowPos(window,nullptr,0,0,initial.cx,initial.cy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    check(IsWindow(toggleButton) && IsWindow(restButton) && IsWindow(hideButton) && IsWindow(settingsButton), "main action controls created");
    RECT buttonRect{}; GetWindowRect(toggleButton, &buttonRect);
    check(buttonRect.right > buttonRect.left && buttonRect.bottom > buttonRect.top, "buttons have an initial layout before first resize");
    check((GetWindowLongPtrW(window, GWL_STYLE) & WS_CLIPCHILDREN) != 0, "timer repaint preserves child buttons");
    const UINT realDpi=Dpi::of(window);
    for(UINT dpi : {96U,120U,144U,192U}) {
        const auto size=Dpi::outerSize(window,496,384,dpi);
        RECT suggested{80,80,80+size.cx,80+size.cy};
        SendMessageW(window,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));
        LOGFONTW font{}; GetObjectW(bodyFont,sizeof(font),&font);
        check(font.lfHeight==-Dpi::scale(Typography::Body,dpi), "body font scales for 100/125/150/200 percent");
        RECT button{},client{}; GetWindowRect(toggleButton,&button);
        MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&button),2); GetClientRect(window,&client);
        check(button.left==Dpi::scale(22,dpi) && button.bottom-button.top==Dpi::scale(34,dpi), "button size and margin scale with DPI");
        check(button.bottom<=client.bottom && button.top>=Dpi::scale(298,dpi), "buttons stay inside client and below information rows");
    }
    {
        const auto size=Dpi::outerSize(window,496,384,realDpi); RECT suggested{80,80,80+size.cx,80+size.cy};
        SendMessageW(window,WM_DPICHANGED,MAKEWPARAM(realDpi,realDpi),reinterpret_cast<LPARAM>(&suggested));
    }
    syncDesktopIcon();
    check(desktopIcon && IsWindowVisible(desktopIcon), "desktop icon visible");
    check((GetWindowLongPtrW(desktopIcon, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0, "icon does not steal focus");
    for(UINT dpi : {96U,120U,144U,192U}) {
        RECT suggested{100,100,100+Dpi::scale(IconWidth,dpi),100+Dpi::scale(IconHeight,dpi)};
        SendMessageW(desktopIcon,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));
        RECT icon{}; GetWindowRect(desktopIcon,&icon);
        check(icon.right-icon.left==Dpi::scale(IconWidth,dpi) && icon.bottom-icon.top==Dpi::scale(IconHeight,dpi), "floating icon scales independently of main window");
        check(mainDpi==realDpi, "floating icon DPI change leaves main font unchanged");
    }
    settings.desktopIconEnabled = false; syncDesktopIcon();
    check(!IsWindowVisible(desktopIcon), "desktop icon can be disabled");
    HWND dialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), window, settingsProc, 0);
    check(dialog != nullptr && GetDlgItem(dialog, IDC_REST_SECONDS) != nullptr, "settings controls created");
    check(GetDlgItem(dialog,IDC_LIGHT_MODE)!=nullptr, "appearance setting is available");
    for(bool light : {true,false}) {
        settings.lightMode=light; applyAppearance();
        LOGBRUSH brush{}; GetObjectW(background,sizeof(brush),&brush);
        check(Theme::Light==light && brush.lbColor==Theme::Background, "appearance refreshes main background");
        HDC dc=GetDC(dialog);
        SendMessageW(dialog,WM_CTLCOLORSTATIC,reinterpret_cast<WPARAM>(dc),reinterpret_cast<LPARAM>(GetDlgItem(dialog,IDC_HEALTH_HELP)));
        check(GetTextColor(dc)==Theme::Text && GetBkColor(dc)==Theme::Background, "dialog uses active appearance palette");
        ReleaseDC(dialog,dc);
    }
    check(GetDlgItemInt(dialog, IDC_REST_SECONDS, nullptr, FALSE) == 25, "settings default 25 seconds");
    const auto dialogFont=SendMessageW(dialog,WM_GETFONT,0,0);
    LOGFONTW dialogFace{},captionFace{};
    GetObjectW(reinterpret_cast<HFONT>(dialogFont),sizeof(dialogFace),&dialogFace);
    GetObjectW(reinterpret_cast<HFONT>(SendDlgItemMessageW(dialog,IDC_NIGHT_HELP,WM_GETFONT,0,0)),sizeof(captionFace),&captionFace);
    check(dialogFace.lfHeight==-Dpi::scale(Typography::Body,Dpi::of(dialog)), "dialog and main body text use the same physical size");
    check(captionFace.lfHeight==-Dpi::scale(Typography::Caption,Dpi::of(dialog)), "helper text uses shared caption scale");
    check(SendMessageW(dialog,WM_GETICON,ICON_SMALL,0)!=0 && SendMessageW(window,WM_GETICON,ICON_SMALL,0)!=0,
        "main window and settings display the application icon");
    check(SendDlgItemMessageW(dialog,IDC_REST_SECONDS,WM_GETFONT,0,0)==dialogFont, "dialog controls use dialog DPI font rather than main font");
    auto scroll=Dpi::scrollState(dialog); check(scroll!=nullptr, "dialog viewport initialized");
    const int originalHeight=scroll->contentHeight;
    scroll->contentHeight=originalHeight*3; Dpi::fitDialog(dialog);
    SendMessageW(dialog,WM_VSCROLL,SB_BOTTOM,0);
    check(scroll->offset>0, "oversized high-DPI dialog can scroll to bottom");
    SendMessageW(dialog,Dpi::FocusVisible,0,reinterpret_cast<LPARAM>(GetDlgItem(dialog,IDC_SOUND_ENABLED)));
    RECT focused{},viewport{}; GetWindowRect(GetDlgItem(dialog,IDC_SOUND_ENABLED),&focused);
    MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&focused),2); GetClientRect(dialog,&viewport);
    check(focused.top>=0 && focused.bottom<=viewport.bottom, "keyboard focus scrolls top control into view");
    scroll->contentHeight=originalHeight; Dpi::fitDialog(dialog);
    if (argc > 1 && std::string(argv[1]) == "--preview") {
        settings.desktopIconEnabled=true; syncDesktopIcon(); CheckDlgButton(dialog,IDC_DESKTOP_ICON,BST_CHECKED);
        if(argc > 2 && std::string(argv[2]) == "--light") { settings.lightMode=true; applyAppearance(); CheckDlgButton(dialog,IDC_LIGHT_MODE,BST_CHECKED); }
        SetWindowTextW(window, L"EyeBreak 界面预览");
        ShowWindow(window, SW_SHOW); ShowWindow(dialog, SW_SHOW);
        SetWindowPos(window, nullptr, 80, 80, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
        SetWindowPos(dialog, HWND_TOP, 740, 80, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
        showHealthReminder(HealthReminders::Sitting | HealthReminders::LongWork, false);
        SetTimer(window, 99, 120000, nullptr);
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_TIMER && msg.wParam == 99) break;
            if (IsWindow(dialog) && IsDialogMessageW(dialog, &msg)) continue;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (IsWindow(dialog)) DestroyWindow(dialog);
        if (IsWindow(window)) DestroyWindow(window);
        DeleteObject(background); DeleteFileW(file); DeleteFileW(usageFile.c_str()); return 0;
    }
    DestroyWindow(dialog);
    SendMessageW(window, WM_HOTKEY, 1, 0);
    check(timer.state == BreakTimer::State::Away && !IsWindowVisible(window), "hotkey enters away state");
    SendMessageW(window, WM_TIMER, 1, 0);
    check(timer.state == BreakTimer::State::Away, "away timer stays stopped until new input");
    SendMessageW(window, WM_HOTKEY, 1, 0);
    check(timer.state == BreakTimer::State::Working, "hotkey returns to work");
    command(Away);
    LASTINPUTINFO awayInput{sizeof(awayInput), 0}; GetLastInputInfo(&awayInput);
    awayInputArmed = true; timer.waitingInput = awayInput.dwTime - 1; timer.inputBaselineValid = true;
    SendMessageW(window, WM_TIMER, 1, 0);
    check(timer.state == BreakTimer::State::Away,"global last-input changes alone cannot resume away mode");
    inputBoundary=GetTickCount()-1;
    resumeFromInput("test-keyboard",GetTickCount());
    check(timer.state == BreakTimer::State::Working, "verified device input resumes explicit away mode");
    timer.restDurationMs = 1000;
    command(Rest);
    check(timer.state == BreakTimer::State::Away, "immediate rest uses recovery state");
    command(Toggle); command(Toggle);
    check(timer.state==BreakTimer::State::Paused,"manual pause active");
    command(Rest);
    check(timer.state==BreakTimer::State::Away,"paused timer can enter immediate recovery rest");
    command(Toggle); timer.rest(GetTickCount64()); beginRest(false);
    check(timer.state == BreakTimer::State::Resting && IsWindowVisible(window), "automatic eye rest uses fixed duration");
    timer.deadline = GetTickCount64() - 1;
    SendMessageW(window, WM_TIMER, 1, 0);
    check(timer.state == BreakTimer::State::Waiting && IsWindowVisible(window), "completion waits with continue window");
    check((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0, "completion reminder stays visible");
    check(completionAlerts == 1, "completion reminder issued once");
    LASTINPUTINFO input{sizeof(input), 0};
    check(GetLastInputInfo(&input) != FALSE, "input detection available");
    timer.waitingInput = input.dwTime; timer.inputBaselineValid = true;
    SendMessageW(window, WM_TIMER, 1, 0);
    check(timer.state == BreakTimer::State::Waiting, "unchanged input does not restart");
    check(completionAlerts == 1, "waiting does not repeat completion notification");
    check(!resumeFromInput("stale-device-report",inputBoundary-1),"queued input from before completion cannot resume work");
    timer.waitingInput = input.dwTime - 1;
    timer.completionInputCutoff = input.dwTime - 1;
    SendMessageW(window, WM_TIMER, 1, 0);
    check(timer.state==BreakTimer::State::Waiting,"changed global input timestamp cannot restart completed rest");
    inputBoundary=GetTickCount()-1;
    resumeFromInput("test-mouse",GetTickCount());
    check(timer.state == BreakTimer::State::Working && timer.left(GetTickCount64()) > BreakTimer::WorkMs - 1000, "verified input starts full cycle");
    check((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0, "resuming releases completion topmost");
    timer.finish();
    check(timer.deviceActivity(GetTickCount64()), "external device can resume waiting timer");
    onInputResumed(GetTickCount64());
    check(timer.state == BreakTimer::State::Working, "external input enters working state");
    timer.finish(); command(Toggle);
    check(timer.state == BreakTimer::State::Working, "continue command resumes");
    SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 0);
    check(sessionTiming.locked && timer.state == BreakTimer::State::Away, "session lock message starts recovery");
    const auto beforeLockAlerts = completionAlerts;
    SendMessageW(window, WM_TIMER, 1, 0);
    check(sessionTiming.locked && timer.state == BreakTimer::State::Away && completionAlerts == beforeLockAlerts, "locked timer cannot resume or notify");
    SendMessageW(window, WM_POWERBROADCAST, PBT_APMSUSPEND, 0);
    SendMessageW(window, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);
    check(sessionTiming.locked && timer.state == BreakTimer::State::Away, "sleep wake does not bypass lock");
    SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_SESSION_UNLOCK, 0);
    check(!sessionTiming.blocked() && timer.state == BreakTimer::State::Away, "unlock alone cannot resume recovered cycle");
    timer.finish(0,false,GetTickCount64());
    SendMessageW(window,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);
    SendMessageW(window,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);
    SendMessageW(window,WM_TIMER,1,0);
    check(timer.state==BreakTimer::State::Waiting,"completed rest stays waiting through lock and unlock");
    using State=BreakTimer::State;
    health.sittingMs=health.workMs=0;
    for(State state : {State::Working,State::Paused,State::Resting,State::Waiting,State::Away}) {
        timer.state=state;
        HMENU menu=buildTrayMenu();
        auto present=[&](int id){return GetMenuState(menu,id,MF_BYCOMMAND)!=static_cast<UINT>(-1);};
        check(present(Open)&&present(Settings)&&present(Quit),"common menu actions always available");
        check(present(Toggle),"one state-specific pause, skip or continue action");
        check(present(Rest)==(state!=State::Away),"immediate rest available except while already recovering");
        check(!present(Away),"no separate away menu entry");
        check(present(Usage),"daily usage history available");
        for(int i=0;i<GetMenuItemCount(menu);++i) check(GetSubMenu(menu,i)==nullptr,"menus remain flat");
        check(!present(StoodUp),"no reset sitting action for an empty sitting timer");
        DestroyMenu(menu);
    }
    timer.state=State::Working; timer.deadline=GetTickCount64()+BreakTimer::WorkMs;
    showHealthReminder(HealthReminders::LateNight,false);
    check(GetDlgItem(healthWindow,IDC_IMMEDIATE_REST)!=nullptr,"night reminder has the same immediate rest action");
    SendMessageW(healthWindow,WM_COMMAND,IDC_IMMEDIATE_REST,0);
    check(timer.state==State::Away && !healthWindow,"night reminder action starts recovery and closes reminder");
    HWND statistics=CreateDialogParamW(instance,MAKEINTRESOURCEW(IDD_USAGE),window,usageProc,0);
    check(statistics && SendDlgItemMessageW(statistics,IDC_USAGE_LIST,LB_GETCOUNT,0,0)>=1,"usage page includes today");
    DestroyWindow(statistics);
    DestroyWindow(window); DeleteObject(background); DeleteFileW(file); DeleteFileW(usageFile.c_str());
    std::cout << "All native window checks passed.\n";
}
