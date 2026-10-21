#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <shlobj.h>
#include <wtsapi32.h>
#include <string>
#include <algorithm>
#include "timer.h"
#include "settings.h"
#include "resource.h"
#include "device_input.h"
#include "session_timing.h"
#include "theme.h"
#include "usage_store.h"
#include "diagnostics.h"

namespace {
constexpr UINT TrayMessage = WM_APP + 1;
constexpr int Toggle = 101, Rest = 102, Hide = 103, Quit = 104, Open = 105, Settings = 106, StoodUp = 107, EndWork = 108, Away = 109, Usage = 110;
const wchar_t* ClassName = L"EyeBreak202020Window";
HWND window = nullptr, toggleButton = nullptr, restButton = nullptr, hideButton = nullptr, settingsButton = nullptr;
HWND usageButton = nullptr;
HWND healthWindow = nullptr, settingsWindow = nullptr, usageWindow = nullptr;
DailyUsage dailyUsage;
std::wstring usageFile;
bool usageStorageOk = true;
bool usageReadable = true;
HWND desktopIcon = nullptr;
bool awayHotkeyAvailable = false;
bool awayInputArmed = false;
DWORD inputBoundary=0;
AppSettings settings;
std::wstring settingsDirectory, settingsFile, pendingSound;
HealthReminders health(GetTickCount64());
int displayedEvents = 0;
HFONT titleFont = nullptr, bodyFont = nullptr, clockFont = nullptr, captionFont = nullptr;
HFONT iconBodyFont = nullptr;
UINT mainDpi = 96, iconDpi = 96;
constexpr UINT RefreshDialogDpi = WM_APP + 40;
HBRUSH background = nullptr;
UINT taskbarCreated = 0;
bool trayPresent = false, hideAfterRest = false;
BreakTimer timer(GetTickCount64());
SessionTiming sessionTiming(timer, health, settings, GetTickCount64());
bool sessionNotificationsAvailable = false;
DeviceInput deviceInput;
unsigned completionAlerts = 0;
COLORREF bg = Theme::Background, foreground = Theme::Text;

void refresh();
void command(int id);
void showSettings();
void syncDesktopIcon();
void trayMenu();
std::wstring usageDuration(std::uint64_t milliseconds) {
    const auto seconds=milliseconds/1000;
    return std::to_wstring(seconds/3600)+L" 小时 "+std::to_wstring(seconds/60%60)+L" 分 "+std::to_wstring(seconds%60)+L" 秒";
}
void initializeUsage() {
    usageReadable=usageStorageOk=usageFile.empty() || loadUsage(usageFile,dailyUsage);
    sessionTiming.onActive=[](std::uint64_t end,std::uint64_t elapsed) {
        const auto now=GetTickCount64(), local=localCalendarMs();
        const auto lag=now>end?now-end:0;
        dailyUsage.record(elapsed,local>lag?local-lag:local);
    };
}
void persistUsage() {
    // A malformed existing file is preserved for recovery, never overwritten.
    if(usageReadable) usageStorageOk=saveUsage(usageFile,dailyUsage);
}
INT_PTR CALLBACK usageProc(HWND dialog,UINT message,WPARAM w,LPARAM l) {
    if(Dpi::dialogScrollMessage(dialog,message,w,l)) return TRUE;
    if(message==WM_INITDIALOG || message==RefreshDialogDpi) {
        usageWindow=dialog; Theme::apply(dialog); Dpi::fitDialog(dialog);
        if(message==WM_INITDIALOG) {
            auto days=dailyUsage.days;
            days.emplace(localCalendarMs()/DailyUsage::DayMs,0);
            for(auto item=days.rbegin();item!=days.rend();++item) {
                const auto text=usageDate(item->first)+L"    "+usageDuration(item->second);
                SendDlgItemMessageW(dialog,IDC_USAGE_LIST,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
            }
        }
        return TRUE;
    }
    if(message==WM_DPICHANGED) { PostMessageW(dialog,RefreshDialogDpi,0,0); return FALSE; }
    if(const auto result=Theme::dialogMessage(dialog,message,w,l)) return result;
    if(message==WM_COMMAND && (LOWORD(w)==IDOK || LOWORD(w)==IDCANCEL)) { EndDialog(dialog,IDOK); return TRUE; }
    if(message==WM_CLOSE) { EndDialog(dialog,IDOK); return TRUE; }
    if(message==WM_DESTROY) usageWindow=nullptr;
    return FALSE;
}
void applyAppearance() {
    Theme::setLight(settings.lightMode); bg=Theme::Background; foreground=Theme::Text;
    HBRUSH old=background; background=CreateSolidBrush(bg);
    if(window) { SetClassLongPtrW(window,GCLP_HBRBACKGROUND,reinterpret_cast<LONG_PTR>(background)); Theme::titlebar(window); }
    if(old) DeleteObject(old);
    for(HWND dialog : {settingsWindow,healthWindow,usageWindow}) if(dialog) Theme::apply(dialog);
    for(HWND target : {window,settingsWindow,healthWindow,usageWindow,desktopIcon})
        if(target) RedrawWindow(target,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);
}
void updateMainFonts(UINT dpi) {
    mainDpi=dpi;
    HFONT oldTitle=titleFont, oldBody=bodyFont, oldClock=clockFont, oldCaption=captionFont;
    titleFont=Dpi::font(Typography::Title,FW_SEMIBOLD,Typography::Face,dpi);
    bodyFont=Dpi::font(Typography::Body,FW_NORMAL,Typography::Face,dpi);
    captionFont=Dpi::font(Typography::Caption,FW_NORMAL,Typography::Face,dpi);
    clockFont=Dpi::font(Typography::Clock,FW_LIGHT,L"Segoe UI",dpi);
    for(HWND button : {toggleButton,restButton,hideButton,settingsButton,usageButton})
        if(button) SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(bodyFont),TRUE);
    if(oldTitle) DeleteObject(oldTitle); if(oldBody) DeleteObject(oldBody); if(oldClock) DeleteObject(oldClock);
    if(oldCaption) DeleteObject(oldCaption);
}
bool isWorking() { return !sessionTiming.blocked() && timer.state == BreakTimer::State::Working; }
const wchar_t* toggleLabel() { return timer.state==BreakTimer::State::Resting ? L"跳过休息" : isWorking() ? L"暂停计时" : L"继续工作"; }
std::wstring restLabel() { return L"立即休息"; }
void playReminderSound(const std::wstring& path, bool enabled) {
    if (!enabled) return;
    if (!path.empty() && PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT)) return;
    PlaySoundW(MAKEINTRESOURCEW(201), GetModuleHandleW(nullptr), SND_RESOURCE | SND_ASYNC | SND_NODEFAULT);
}
void bringReminderForward(HWND target) {
    ShowWindow(target, SW_RESTORE);
    SetWindowPos(target, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    if (!SetForegroundWindow(target)) {
        FLASHWINFO flash{sizeof(FLASHWINFO), target, FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0};
        FlashWindowEx(&flash);
    }
}
INT_PTR CALLBACK healthProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    if(Dpi::dialogScrollMessage(dialog,message,wparam,lparam)) return TRUE;
    if (message == WM_INITDIALOG || message == RefreshDialogDpi) { Theme::apply(dialog); Dpi::fitDialog(dialog); return TRUE; }
    if (message == WM_DPICHANGED) { PostMessageW(dialog,RefreshDialogDpi,0,0); return FALSE; }
    if (const auto result = Theme::dialogMessage(dialog, message, wparam, lparam)) return result;
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == IDC_IMMEDIATE_REST) {
            DestroyWindow(dialog); command(Rest); return TRUE;
        }
        if (LOWORD(wparam) == IDC_STOOD_UP) command(StoodUp);
        else if (LOWORD(wparam) == IDC_END_WORK) command(EndWork);
        else if (LOWORD(wparam) != IDOK && LOWORD(wparam) != IDCANCEL) return FALSE;
        DestroyWindow(dialog); return TRUE;
    }
    if (message == WM_CLOSE) { DestroyWindow(dialog); return TRUE; }
    if (message == WM_DESTROY) { healthWindow = nullptr; displayedEvents = 0; return TRUE; }
    return FALSE;
}
void showHealthReminder(int events, bool withSound) {
    displayedEvents |= events;
    if (!healthWindow) healthWindow = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_HEALTH), nullptr, healthProc, 0);
    if (!healthWindow) { MessageBoxW(window, L"健康提醒窗口创建失败。请重启软件。", L"20·20·20", MB_OK | MB_ICONWARNING); return; }
    std::wstring text;
    if (displayedEvents & HealthReminders::Sitting) text += L"久坐提醒：已累计 " + std::to_wstring(health.sittingMs / 60000) + L" 分钟，请起身活动。\r\n\r\n";
    if (displayedEvents & HealthReminders::LongWork) text += L"工作过长：本轮已累计 " + std::to_wstring(health.workMs / 60000) + L" 分钟，请安排一段较长休息。\r\n\r\n";
    if (displayedEvents & HealthReminders::LateNight) text += L"深夜提醒：已经进入设定的深夜时段，请考虑结束工作。";
    SetDlgItemTextW(healthWindow, IDC_HEALTH_TEXT, text.c_str());
    if (withSound) playReminderSound(settings.soundPath, settings.soundEnabled);
    bringReminderForward(healthWindow);
}
int dueHealthReminders(bool active) {
    SYSTEMTIME local{}; GetLocalTime(&local);
    SYSTEMTIME date = local; date.wHour = date.wMinute = date.wSecond = date.wMilliseconds = 0;
    FILETIME file{}; SystemTimeToFileTime(&date, &file);
    ULARGE_INTEGER value{}; value.LowPart = file.dwLowDateTime; value.HighPart = file.dwHighDateTime;
    return health.due(settings, static_cast<std::int64_t>(value.QuadPart / 864000000000ULL), local.wHour * 60 + local.wMinute, active);
}

INT_PTR CALLBACK settingsProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    if(Dpi::dialogScrollMessage(dialog,message,wparam,lparam)) return TRUE;
    if (message == WM_DPICHANGED) { PostMessageW(dialog,RefreshDialogDpi,0,0); return FALSE; }
    if (message == RefreshDialogDpi) { Theme::apply(dialog); Dpi::fitDialog(dialog); return TRUE; }
    if (const auto result = Theme::dialogMessage(dialog, message, wparam, lparam)) return result;
    switch (message) {
    case WM_INITDIALOG:
        settingsWindow = dialog; pendingSound = settings.soundPath;
        Theme::apply(dialog);
        CheckDlgButton(dialog, IDC_SOUND_ENABLED, settings.soundEnabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_SITTING_ENABLED, settings.sittingEnabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_WORK_ENABLED, settings.workEnabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_NIGHT_ENABLED, settings.nightEnabled ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(dialog, IDC_SITTING_MINUTES, settings.sittingMinutes, FALSE);
        SetDlgItemInt(dialog, IDC_WORK_MINUTES, settings.workMinutes, FALSE);
        SetDlgItemInt(dialog, IDC_NIGHT_HOUR, settings.nightHour, FALSE);
        SetDlgItemInt(dialog, IDC_NIGHT_MINUTE, settings.nightMinute, FALSE);
        SetDlgItemInt(dialog, IDC_REST_SECONDS, settings.restSeconds, FALSE);
        CheckDlgButton(dialog, IDC_DESKTOP_ICON, settings.desktopIconEnabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_LIGHT_MODE, settings.lightMode ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(dialog, IDC_SOUND_PATH, pendingSound.empty() ? L"内置提示音" : pendingSound.c_str());
        for (int id : {IDC_SITTING_MINUTES, IDC_WORK_MINUTES, IDC_NIGHT_HOUR, IDC_NIGHT_MINUTE, IDC_REST_SECONDS})
            SendDlgItemMessageW(dialog, id, EM_SETLIMITTEXT, 3, 0);
        Dpi::fitDialog(dialog);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_BROWSE_SOUND: {
            wchar_t path[256]{};
            OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = dialog;
            ofn.lpstrFilter = L"WAV 声音文件 (*.wav)\0*.wav\0\0"; ofn.lpstrFile = path; ofn.nMaxFile = 256;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn)) {
                if (!PlaySoundW(path, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT)) {
                    MessageBoxW(dialog, L"无法播放此声音。请选择有效的 PCM WAV 文件。", L"声音格式", MB_OK | MB_ICONWARNING);
                } else { pendingSound = path; SetDlgItemTextW(dialog, IDC_SOUND_PATH, path); }
            }
            return TRUE;
        }
        case IDC_DEFAULT_SOUND: pendingSound.clear(); SetDlgItemTextW(dialog, IDC_SOUND_PATH, L"内置提示音"); return TRUE;
        case IDC_PREVIEW_SOUND: playReminderSound(pendingSound, true); return TRUE;
        case IDOK: {
            AppSettings draft = settings;
            auto get = [&](int id, int low, int high, int& destination) {
                BOOL ok = FALSE; UINT value = GetDlgItemInt(dialog, id, &ok, FALSE);
                if (!ok || value < static_cast<UINT>(low) || value > static_cast<UINT>(high)) { SetFocus(GetDlgItem(dialog, id)); return false; }
                destination = static_cast<int>(value); return true;
            };
            if (!get(IDC_SITTING_MINUTES, 1, 480, draft.sittingMinutes) || !get(IDC_WORK_MINUTES, 1, 720, draft.workMinutes) ||
                !get(IDC_NIGHT_HOUR, 0, 23, draft.nightHour) || !get(IDC_NIGHT_MINUTE, 0, 59, draft.nightMinute) ||
                !get(IDC_REST_SECONDS, 1, 600, draft.restSeconds)) {
                MessageBoxW(dialog, L"请填写有效数值：久坐 1–480 分钟，工作 1–720 分钟，时间 00:00–23:59，休息 1–600 秒。", L"检查设置", MB_OK | MB_ICONWARNING); return TRUE;
            }
            draft.soundEnabled = IsDlgButtonChecked(dialog, IDC_SOUND_ENABLED) == BST_CHECKED;
            draft.sittingEnabled = IsDlgButtonChecked(dialog, IDC_SITTING_ENABLED) == BST_CHECKED;
            draft.workEnabled = IsDlgButtonChecked(dialog, IDC_WORK_ENABLED) == BST_CHECKED;
            draft.nightEnabled = IsDlgButtonChecked(dialog, IDC_NIGHT_ENABLED) == BST_CHECKED;
            draft.desktopIconEnabled = IsDlgButtonChecked(dialog, IDC_DESKTOP_ICON) == BST_CHECKED;
            draft.lightMode = IsDlgButtonChecked(dialog, IDC_LIGHT_MODE) == BST_CHECKED;
            draft.soundPath.clear();
            if (!pendingSound.empty()) {
                const auto copy = settingsDirectory + L"\\custom.wav";
                if (!PlaySoundW(pendingSound.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT)) {
                    MessageBoxW(dialog, L"声音文件已经失效，请重新选择。", L"无法保存", MB_OK | MB_ICONWARNING); return TRUE;
                }
                if (_wcsicmp(pendingSound.c_str(), copy.c_str()) != 0) {
                    PlaySoundW(nullptr, nullptr, 0);
                    if (!CopyFileW(pendingSound.c_str(), copy.c_str(), FALSE)) {
                        MessageBoxW(dialog, L"无法复制声音文件到本机设置目录。", L"无法保存", MB_OK | MB_ICONWARNING); return TRUE;
                    }
                }
                draft.soundPath = copy;
            }
            if (!saveSettings(settingsFile, draft)) { MessageBoxW(dialog, L"无法写入设置，请检查目录权限或磁盘空间。", L"保存失败", MB_OK | MB_ICONWARNING); return TRUE; }
            settings = draft; timer.restDurationMs = static_cast<std::uint64_t>(settings.restSeconds) * 1000;
            applyAppearance(); syncDesktopIcon(); refresh(); EndDialog(dialog, IDOK); return TRUE;
        }
        case IDCANCEL: EndDialog(dialog, IDCANCEL); return TRUE;
        }
        break;
    case WM_CLOSE: EndDialog(dialog, IDCANCEL); return TRUE;
    case WM_DESTROY: settingsWindow = nullptr; return TRUE;
    }
    return FALSE;
}
void showSettings() {
    if (settingsWindow) { SetForegroundWindow(settingsWindow); return; }
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), window, settingsProc, 0);
}

std::wstring clockText() {
    if (timer.state == BreakTimer::State::Waiting) return L"00:00";
    auto seconds = (timer.left(GetTickCount64()) + 999) / 1000;
    wchar_t text[32];
    swprintf_s(text, L"%02llu:%02llu", seconds / 60, seconds % 60);
    return text;
}
void removeTray() {
    NOTIFYICONDATAW n{}; n.cbSize = sizeof(n); n.hWnd = window; n.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &n); trayPresent = false;
}
bool addTray() {
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n); n.hWnd = window; n.uID = 1;
    n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    n.uCallbackMessage = TrayMessage;
    n.hIcon = AppIcon::load(GetSystemMetricsForDpi(SM_CXSMICON, mainDpi));
    wcscpy_s(n.szTip, L"20·20·20 护眼提醒");
    const bool modified = trayPresent && Shell_NotifyIconW(NIM_MODIFY, &n) != FALSE;
    trayPresent = modified || Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
    if (trayPresent) { n.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &n); }
    return trayPresent;
}
void refresh() {
    SetWindowTextW(toggleButton, toggleLabel());
    EnableWindow(toggleButton, !sessionTiming.blocked());
    SetWindowTextW(restButton, restLabel().c_str());
    ShowWindow(restButton,timer.state==BreakTimer::State::Away?SW_HIDE:SW_SHOW);
    EnableWindow(restButton, !sessionTiming.blocked());
    InvalidateRect(window, nullptr, FALSE);
    if (desktopIcon) InvalidateRect(desktopIcon, nullptr, FALSE);
    if (trayPresent) {
        NOTIFYICONDATAW n{}; n.cbSize = sizeof(n); n.hWnd = window; n.uID = 1; n.uFlags = NIF_TIP;
        std::wstring tip = L"20·20·20 — ";
        tip += timer.state == BreakTimer::State::Away ? L"休息回补中 " + clockText() :
            timer.state == BreakTimer::State::Waiting ? L"休息完成，等待继续工作" :
            timer.state == BreakTimer::State::Paused ? L"已暂停" :
            (timer.state == BreakTimer::State::Resting ? L"休息中 " : L"距离休息 ") + clockText();
        wcsncpy_s(n.szTip, tip.c_str(), _TRUNCATE); Shell_NotifyIconW(NIM_MODIFY, &n);
    }
}
void showWindow() { hideAfterRest = false; ShowWindow(window, SW_RESTORE); SetForegroundWindow(window); }
void releaseReminderWindow() {
    SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    FLASHWINFO flash{sizeof(FLASHWINFO), window, FLASHW_STOP, 0, 0};
    FlashWindowEx(&flash);
}
void hideWindow() {
    if (trayPresent) { releaseReminderWindow(); ShowWindow(window, SW_HIDE); }
    else MessageBoxW(window, L"托盘图标暂时不可用，请保留窗口或使用退出按钮。", L"20·20·20", MB_OK);
}
void onInputResumed(std::uint64_t now) {
    sessionTiming.advance(now);
    releaseReminderWindow();
    if (hideAfterRest) hideWindow();
    hideAfterRest = false; refresh();
}
bool resumeFromInput(const char* source,DWORD eventTime) {
    const auto state=timer.state;
    if(sessionTiming.blocked() || (state!=BreakTimer::State::Waiting && state!=BreakTimer::State::Away)) return false;
    const DWORD age=eventTime-inputBoundary;
    if((state==BreakTimer::State::Away && !awayInputArmed) || !age || age>=0x80000000U) return false;
    const auto now=GetTickCount64(); sessionTiming.advance(now);
    if(!timer.deviceActivity(now)) return false;
    Diagnostics::write(source,state,timer.state); onInputResumed(now); return true;
}
void restCompleted() {
    inputBoundary=GetTickCount();
    ++completionAlerts;
    // Rebase devices after the notification; held controls and queued reports
    // from the rest interval must not count as a fresh operation.
    deviceInput.resetMotion();
    // Completion is a notification only; it must never resume the work timer.
    refresh();
    playReminderSound(settings.soundPath, settings.soundEnabled);
    if (trayPresent) {
        NOTIFYICONDATAW n{}; n.cbSize = sizeof(n); n.hWnd = window; n.uID = 1; n.uFlags = NIF_INFO;
        wcscpy_s(n.szInfoTitle, L"休息完成");
        wcscpy_s(n.szInfo, L"点击继续工作，或操作鼠标、键盘、手柄后开始下一轮计时。");
        n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND; Shell_NotifyIconW(NIM_MODIFY, &n);
    }
    bringReminderForward(window);
    if (GetForegroundWindow() == window) SetFocus(toggleButton);
}
void beginRest(bool notify) {
    hideAfterRest = !IsWindowVisible(window);
    if (notify && trayPresent) {
        NOTIFYICONDATAW n{}; n.cbSize = sizeof(n); n.hWnd = window; n.uID = 1; n.uFlags = NIF_INFO;
        wcscpy_s(n.szInfoTitle, L"该让眼睛休息了");
        std::wstring text = L"看向约 6 米远的地方，持续 " + std::to_wstring(settings.restSeconds) + L" 秒。";
        wcsncpy_s(n.szInfo, text.c_str(), _TRUNCATE);
        n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND; Shell_NotifyIconW(NIM_MODIFY, &n);
    }
    refresh();
    // Embedded audio is independent of the Windows notification sound setting.
    playReminderSound(settings.soundPath, settings.soundEnabled);
    bringReminderForward(window);
    if (GetForegroundWindow() == window) SetFocus(restButton);
}
void command(int id) {
    auto now = GetTickCount64();
    if (sessionTiming.blocked() && id != Quit) return;
    sessionTiming.advance(now);
    const auto beforeCommand=timer.state;
    switch (id) {
    case Toggle:
        if(timer.state==BreakTimer::State::Resting) timer.skip(now);
        else timer.toggle(now);
        hideAfterRest=false; releaseReminderWindow(); break;
    case Rest:
    case Away:
        if (timer.state == BreakTimer::State::Away) {
            if(id==Rest) break;
            timer.resume(now);
        }
        else {
            LASTINPUTINFO input{sizeof(LASTINPUTINFO), 0};
            const bool valid = GetLastInputInfo(&input) != FALSE;
            timer.away(now, input.dwTime, valid); awayInputArmed = false; inputBoundary=GetTickCount(); deviceInput.resetMotion();
            PlaySoundW(nullptr, nullptr, 0);
            if (healthWindow) DestroyWindow(healthWindow);
            releaseReminderWindow(); hideAfterRest = false; hideWindow();
        }
        break;
    case Usage:
        persistUsage();
        DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_USAGE),window,usageProc,0); return;
    case Settings: showSettings(); return;
    case StoodUp: health.stoodUp(); break;
    case EndWork:
        health.endWork(); timer.resetAfterSleep(now);
        timer.state = BreakTimer::State::Paused;
        releaseReminderWindow(); hideAfterRest = false; break;
    case Hide: hideAfterRest = false; hideWindow(); break;
    case Open: showWindow(); break;
    case Quit: DestroyWindow(window); return;
    }
    const auto reason="command-"+std::to_string(id);
    Diagnostics::write(reason.c_str(),beforeCommand,timer.state);
    persistUsage(); refresh();
}
HMENU buildTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, Open, L"打开主窗口");
    if(!sessionTiming.blocked()) {
        using State=BreakTimer::State;
        AppendMenuW(menu,MF_STRING,Toggle,toggleLabel());
        if(timer.state!=State::Away) AppendMenuW(menu,MF_STRING,Rest,L"立即休息    Ctrl+Alt+L");
        if(settings.sittingEnabled && health.sittingMs>0)
            AppendMenuW(menu,MF_STRING,StoodUp,L"我已起身活动（重置久坐）");
        if(health.workMs>0 || health.sittingMs>0 || timer.state==State::Working || timer.state==State::Resting)
            AppendMenuW(menu,MF_STRING,EndWork,L"结束本轮工作（清零并暂停）");
    }
    AppendMenuW(menu, MF_STRING, Usage, L"每日使用统计…");
    AppendMenuW(menu, MF_STRING, Settings, L"提醒设置…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Quit, L"退出");
    return menu;
}
void trayMenu() {
    HMENU menu = buildTrayMenu();
    POINT point{}; GetCursorPos(&point); SetForegroundWindow(window);
    int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu); PostMessageW(window, WM_NULL, 0, 0);
    if (selected) command(selected);
}
void drawText(HDC dc, HFONT font, COLORREF color, const std::wstring& text, RECT rect, UINT flags = DT_LEFT | DT_SINGLELINE) {
    auto old = SelectObject(dc, font); SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, &rect, flags); SelectObject(dc, old);
}
std::wstring durationText(std::uint64_t milliseconds) {
    const auto seconds = (milliseconds + 999) / 1000;
    wchar_t value[40]{}; swprintf_s(value, L"%02llu:%02llu", seconds / 60, seconds % 60);
    return value;
}
constexpr int IconWidth = 76, IconHeight = 36;
void updateIconDpi(HWND hwnd, UINT dpi) {
    iconDpi=dpi;
    if(iconBodyFont) DeleteObject(iconBodyFont);
    iconBodyFont=Dpi::font(Typography::Body,FW_NORMAL,Typography::Face,dpi);
    const int width=Dpi::scale(IconWidth,dpi), height=Dpi::scale(IconHeight,dpi);
    SetWindowPos(hwnd,nullptr,0,0,width,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    HRGN region=CreateRoundRectRgn(0,0,width+1,height+1,Dpi::scale(22,dpi),Dpi::scale(22,dpi));
    if(!SetWindowRgn(hwnd,region,TRUE)) DeleteObject(region);
    InvalidateRect(hwnd,nullptr,FALSE);
}
void constrainIcon(HWND hwnd, int x, int y) {
    POINT point{x, y}; MONITORINFO monitor{sizeof(MONITORINFO)};
    GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &monitor);
    RECT size{}; GetWindowRect(hwnd,&size);
    x = std::clamp<LONG>(x, monitor.rcWork.left, std::max(monitor.rcWork.left, monitor.rcWork.right - (size.right-size.left)));
    y = std::clamp<LONG>(y, monitor.rcWork.top, std::max(monitor.rcWork.top, monitor.rcWork.bottom - (size.bottom-size.top)));
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
LRESULT CALLBACK desktopProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    static POINT mouseStart{}, windowStart{};
    static bool moved = false;
    switch (message) {
    case WM_CREATE: updateIconDpi(hwnd,Dpi::of(hwnd)); return 0;
    case WM_DPICHANGED: {
        const auto suggested=reinterpret_cast<const RECT*>(lparam);
        updateIconDpi(hwnd,HIWORD(wparam)); constrainIcon(hwnd,suggested->left,suggested->top); return 0;
    }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONDOWN: {
        GetCursorPos(&mouseStart); RECT r{}; GetWindowRect(hwnd, &r); windowStart = {r.left, r.top};
        moved = false; SetCapture(hwnd); return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == hwnd && (wparam & MK_LBUTTON)) {
            POINT mouse{}; GetCursorPos(&mouse);
            if (abs(mouse.x - mouseStart.x) >= GetSystemMetricsForDpi(SM_CXDRAG,iconDpi) || abs(mouse.y - mouseStart.y) >= GetSystemMetricsForDpi(SM_CYDRAG,iconDpi)) moved = true;
            if (moved) constrainIcon(hwnd, windowStart.x + mouse.x - mouseStart.x, windowStart.y + mouse.y - mouseStart.y);
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) {
            ReleaseCapture();
            if (moved) {
                RECT r{}; GetWindowRect(hwnd, &r);
                WritePrivateProfileStringW(L"Desktop", L"X", std::to_wstring(r.left).c_str(), settingsFile.c_str());
                WritePrivateProfileStringW(L"Desktop", L"Y", std::to_wstring(r.top).c_str(), settingsFile.c_str());
            } else command(Open);
        }
        return 0;
    case WM_CONTEXTMENU: trayMenu(); return 0;
    case WM_DISPLAYCHANGE: {
        RECT r{}; GetWindowRect(hwnd, &r); constrainIcon(hwnd, r.left, r.top); return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); RECT r{}; GetClientRect(hwnd, &r);
        HBRUSH brush = CreateSolidBrush(isWorking() ? Theme::Surface : Theme::Background);
        FillRect(dc, &r, brush); DeleteObject(brush);
        const auto label = timer.state == BreakTimer::State::Away ? L"↑" + clockText() :
            timer.state == BreakTimer::State::Waiting ? std::wstring(L"待继续") :
            timer.state == BreakTimer::State::Paused ? std::wstring(L"已暂停") : clockText();
        drawText(dc, iconBodyFont, foreground, label, Dpi::rect(0,0,IconWidth,IconHeight,iconDpi), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_DESTROY:
        DeleteObject(iconBodyFont); iconBodyFont=nullptr;
        desktopIcon = nullptr; return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
void syncDesktopIcon() {
    if (!desktopIcon && settings.desktopIconEnabled) {
        WNDCLASSW wc{}; wc.lpfnWndProc = desktopProc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EyeBreak202020Desktop"; wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
        RegisterClassW(&wc);
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        int x = static_cast<int>(GetPrivateProfileIntW(L"Desktop", L"X", work.right - IconWidth - 24, settingsFile.c_str()));
        int y = static_cast<int>(GetPrivateProfileIntW(L"Desktop", L"Y", work.top + 80, settingsFile.c_str()));
        desktopIcon = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"20·20·20 桌面悬浮图标", WS_POPUP, x, y, IconWidth, IconHeight, nullptr, nullptr, wc.hInstance, nullptr);
        if (desktopIcon) {
            constrainIcon(desktopIcon, x, y);
        } else MessageBoxW(window, L"桌面悬浮图标创建失败，仍可使用托盘菜单。", L"20·20·20", MB_OK | MB_ICONWARNING);
    }
    if (desktopIcon) ShowWindow(desktopIcon, settings.desktopIconEnabled ? SW_SHOWNOACTIVATE : SW_HIDE);
}
void layout(int width, int height) {
    const auto px=[](int v){return Dpi::scale(MulDiv(v,4,5),mainDpi);};
    int margin = px(28), gap = px(12), buttonWidth = (width - 2 * margin - 2 * gap) / 3;
    MoveWindow(toggleButton, margin, height - px(78), buttonWidth, px(42), TRUE);
    MoveWindow(restButton, margin + buttonWidth + gap, height - px(78), buttonWidth, px(42), TRUE);
    MoveWindow(hideButton, margin + 2 * (buttonWidth + gap), height - px(78), buttonWidth, px(42), TRUE);
    MoveWindow(settingsButton, width - px(118), px(24), px(90), px(36), TRUE);
    MoveWindow(usageButton, width - px(218), px(24), px(90), px(36), TRUE);
}
LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == taskbarCreated && taskbarCreated != 0) {
        if (!addTray()) ShowWindow(hwnd, SW_SHOW);
        return 0;
    }
    switch (message) {
    case WM_CREATE: {
        window = hwnd;
        SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) | WS_CLIPCHILDREN);
        Theme::titlebar(hwnd);
        AppIcon::apply(hwnd);
        updateMainFonts(Dpi::of(hwnd));
        auto button = [&](int id, const wchar_t* text) {
            HWND b = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE); return b;
        };
        toggleButton = button(Toggle, L"暂停计时"); restButton = button(Rest, L"立即休息"); hideButton = button(Hide, L"收起到托盘");
        settingsButton = button(Settings, L"设置");
        usageButton = button(Usage,L"统计");
        RECT client{}; GetClientRect(hwnd, &client); layout(client.right, client.bottom);
        awayHotkeyAvailable = RegisterHotKey(hwnd, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'L') != FALSE;
        deviceInput.attach(hwnd);
        sessionNotificationsAvailable = WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION) != FALSE;
        addTray(); SetTimer(hwnd, 1, 250, nullptr); return 0;
    }
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED) hideWindow();
        else layout(LOWORD(lparam), HIWORD(lparam));
        return 0;
    case WM_DPICHANGED: {
        updateMainFonts(HIWORD(wparam));
        AppIcon::apply(hwnd,HIWORD(wparam));
        if(trayPresent) addTray();
        const auto suggested=reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,
            suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);
        RECT client{}; GetClientRect(hwnd,&client); layout(client.right,client.bottom);
        InvalidateRect(hwnd,nullptr,FALSE); return 0;
    }
    case WM_GETMINMAXINFO: {
        const auto size=Dpi::outerSize(hwnd,496,384,mainDpi);
        auto info = reinterpret_cast<MINMAXINFO*>(lparam); info->ptMinTrackSize = {size.cx,size.cy}; return 0;
    }
    case WM_COMMAND: command(LOWORD(wparam)); return 0;
    case WM_HOTKEY: if (wparam == 1) command(Away); return 0;
    case WM_INPUT: {
        if (!sessionTiming.blocked() && deviceInput.raw(reinterpret_cast<HRAWINPUT>(lparam))) {
            resumeFromInput(deviceInput.source,static_cast<DWORD>(GetMessageTime()));
        }
        // DefWindowProc performs required cleanup for foreground raw input.
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    case WM_INPUT_DEVICE_CHANGE:
        deviceInput.forget(static_cast<std::uintptr_t>(lparam)); return 0;
    case WM_TIMER: {
        static ULONGLONG previousSecond = 0, lastUsageSave = 0;
        auto now = GetTickCount64(); auto oldState = timer.state;
        sessionTiming.advance(now);
        if(now-lastUsageSave>=30000) { persistUsage(); lastUsageSave=now; }
        if (sessionTiming.blocked()) { if (previousSecond != now / 1000) { refresh(); previousSecond = now / 1000; } return 0; }
        LASTINPUTINFO input{sizeof(LASTINPUTINFO), 0};
        const bool validInput = GetLastInputInfo(&input) != FALSE;
        const bool controllerChanged = deviceInput.pollControllers();
        const bool wasAwayArmed = awayInputArmed;
        if (oldState == BreakTimer::State::Away && !awayInputArmed) {
            // Ignore release events belonging to the click/hotkey that entered away mode.
            const bool held = (GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000) ||
                (GetAsyncKeyState('L') & 0x8000) || (GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
            if (!held && validInput) { timer.waitingInput = input.dwTime; timer.inputBaselineValid = true; awayInputArmed = true; inputBoundary=static_cast<DWORD>(now); deviceInput.resetMotion(); }
        }
        // GetLastInputInfo includes injected/system input and is not proof of activity.
        const bool eyeStarted = timer.tick(now);
        if ((oldState == BreakTimer::State::Waiting || (oldState == BreakTimer::State::Away && wasAwayArmed)) && controllerChanged)
            resumeFromInput("xinput",static_cast<DWORD>(now));
        if (eyeStarted) beginRest(true);
        if(oldState!=timer.state) Diagnostics::write("timer",oldState,timer.state);
        if (oldState == BreakTimer::State::Resting && timer.state == BreakTimer::State::Waiting) restCompleted();
        if ((oldState == BreakTimer::State::Waiting || oldState == BreakTimer::State::Away) && isWorking()) {
            onInputResumed(now);
        }
        if (previousSecond != now / 1000 || oldState != timer.state) { refresh(); previousSecond = now / 1000; }
        const int events = dueHealthReminders(isWorking());
        if (events) showHealthReminder(events, !eyeStarted);
        return 0;
    }
    case WM_WTSSESSION_CHANGE: {
        const auto beforeSession=timer.state;
        if (wparam == WTS_SESSION_LOCK) {
            sessionTiming.lock(GetTickCount64());
            PlaySoundW(nullptr, nullptr, 0); releaseReminderWindow();
            if (healthWindow) DestroyWindow(healthWindow);
        } else if (wparam == WTS_SESSION_UNLOCK) {
            sessionTiming.unlock(GetTickCount64()); deviceInput.reset(); awayInputArmed = false;
            LASTINPUTINFO input{sizeof(input), 0};
            if (GetLastInputInfo(&input)) { timer.waitingInput = input.dwTime; timer.inputBaselineValid = true; }
        }
        inputBoundary=GetTickCount(); awayInputArmed=false;
        Diagnostics::write(wparam==WTS_SESSION_LOCK?"session-lock":"session-unlock",beforeSession,timer.state);
        persistUsage(); refresh(); return 0;
    }
    case WM_POWERBROADCAST: {
        const auto beforePower=timer.state;
        if (wparam == PBT_APMSUSPEND) sessionTiming.suspend(GetTickCount64());
        if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND) {
            sessionTiming.wake(GetTickCount64()); deviceInput.reset();
        }
        inputBoundary=GetTickCount(); awayInputArmed=false;
        Diagnostics::write(wparam==PBT_APMSUSPEND?"suspend":"wake",beforePower,timer.state);
        persistUsage(); refresh();
        return TRUE;
    }
    case TrayMessage:
        if (LOWORD(lparam) == WM_CONTEXTMENU || LOWORD(lparam) == WM_RBUTTONUP) trayMenu();
        else if (LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == NIN_KEYSELECT || LOWORD(lparam) == NIN_BALLOONUSERCLICK) showWindow();
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DRAWITEM: {
        Theme::button(reinterpret_cast<DRAWITEMSTRUCT*>(lparam), bodyFont);
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); RECT rect{}; GetClientRect(hwnd, &rect);
        if(rect.right<=0 || rect.bottom<=0) { EndPaint(hwnd,&ps); return 0; }
        const auto px=[](int value){return Dpi::scale(MulDiv(value,4,5),mainDpi);};
        const auto row=[&](int top,int bottom){return RECT{px(28),px(top),rect.right-px(28),px(bottom)};};
        HDC memory = CreateCompatibleDC(dc); HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom); auto old = SelectObject(memory, bitmap);
        FillRect(memory, &rect, background);
        drawText(memory, titleFont, foreground, L"20 · 20 · 20", row(24,66));
        std::wstring subtitle = L"每 20 分钟，远眺约 6 米，休息 " + std::to_wstring(settings.restSeconds) + L" 秒";
        drawText(memory, captionFont, Theme::Muted, subtitle, row(73,103));
        const wchar_t* status = sessionTiming.blocked() ? L"锁屏 / 睡眠中 · 三项按 5:1 回补" :
            timer.state == BreakTimer::State::Resting ? L"现在看向远处，让眼睛放松" :
            timer.state == BreakTimer::State::Waiting ? L"休息完成 · 点击继续，或操作鼠标 / 键盘 / 手柄" :
            timer.state == BreakTimer::State::Away ? L"休息回补中（5 倍）· 新操作后自动继续" :
            timer.state == BreakTimer::State::Paused ? L"计时已暂停" : L"距离下一次休息";
        drawText(memory, bodyFont, Theme::Accent, status, row(128,158), DT_CENTER | DT_SINGLELINE);
        drawText(memory, clockFont, foreground, clockText(), row(163,264), DT_CENTER | DT_SINGLELINE);
        drawText(memory, captionFont, Theme::Muted, L"今日使用 "+usageDuration(dailyUsage.total(localCalendarMs()/DailyUsage::DayMs))+
            (usageStorageOk?L"":L"（尚未保存）"), row(274,302), DT_CENTER | DT_SINGLELINE);
        const auto sittingLimit = static_cast<std::uint64_t>(settings.sittingMinutes) * 60000;
        const auto workLimit = static_cast<std::uint64_t>(settings.workMinutes) * 60000;
        const std::wstring sitting = settings.sittingEnabled ? durationText(health.sittingMs >= sittingLimit ? 0 : sittingLimit - health.sittingMs) : L"已关闭";
        const std::wstring working = settings.workEnabled ? durationText(health.workMs >= workLimit ? 0 : workLimit - health.workMs) : L"已关闭";
        HBRUSH card = CreateSolidBrush(Theme::Surface); RECT cardRect=row(302,340);
        FillRect(memory, &cardRect, card); DeleteObject(card);
        drawText(memory, bodyFont, Theme::Text, L"久坐还剩 " + sitting + L"    连续工作还剩 " + working,
            row(307,337), DT_CENTER | DT_SINGLELINE);
        drawText(memory, captionFont, Theme::Muted, !sessionNotificationsAvailable ? L"锁屏监听暂不可用 · 请重新启动程序" :
            awayHotkeyAvailable ? L"立即休息：Ctrl+Alt+L · 休息 / 锁屏按 5:1 回补" : L"Ctrl+Alt+L 被占用 · 请点击立即休息", row(345,373), DT_CENTER | DT_SINGLELINE);
        BitBlt(dc, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);
        SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); EndPaint(hwnd, &ps); return 0;
    }
    case WM_CLOSE: hideWindow(); return 0;
    case WM_DESTROY:
        Diagnostics::write("shutdown",timer.state,timer.state);
        sessionTiming.advance(GetTickCount64()); persistUsage();
        if (sessionNotificationsAvailable) WTSUnRegisterSessionNotification(hwnd);
        deviceInput.detach();
        UnregisterHotKey(hwnd, 1);
        if (desktopIcon) DestroyWindow(desktopIcon);
        if (healthWindow) DestroyWindow(healthWindow);
        PlaySoundW(nullptr, nullptr, 0);
        KillTimer(hwnd, 1); removeTray(); DeleteObject(titleFont); DeleteObject(bodyFont); DeleteObject(clockFont); DeleteObject(captionFont); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    HANDLE singleton = CreateMutexW(nullptr, FALSE, L"Local\\EyeBreak202020.Singleton");
    if (!singleton) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(ClassName, nullptr);
        if (existing) PostMessageW(existing, WM_COMMAND, Open, 0);
        CloseHandle(singleton); return 0;
    }
    Dpi::enable(); background = CreateSolidBrush(bg);
    wchar_t appData[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData))) {
        settingsDirectory = std::wstring(appData) + L"\\EyeBreak202020";
        CreateDirectoryW(settingsDirectory.c_str(), nullptr);
        settingsFile = settingsDirectory + L"\\settings.ini";
        settings = loadSettings(settingsFile);
        if (GetFileAttributesW(settingsFile.c_str()) == INVALID_FILE_ATTRIBUTES) saveSettings(settingsFile, settings);
    } else {
        MessageBoxW(nullptr, L"无法读取本机设置目录，软件将使用默认设置。", L"20·20·20", MB_OK | MB_ICONWARNING);
    }
    if(!settingsDirectory.empty()) usageFile=settingsDirectory+L"\\usage.tsv";
    if(!settingsDirectory.empty()) Diagnostics::file=settingsDirectory+L"\\diagnostics.log";
    Diagnostics::write("startup-raw-input-v2",timer.state,timer.state);
    initializeUsage();
    if(!usageStorageOk) MessageBoxW(nullptr,L"每日使用统计文件读取失败，原文件将保留。",L"使用统计",MB_OK|MB_ICONWARNING);
    applyAppearance();
    timer.restDurationMs = static_cast<std::uint64_t>(settings.restSeconds) * 1000;
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc{}; wc.lpfnWndProc = proc; wc.hInstance = instance; wc.lpszClassName = ClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = AppIcon::load(GetSystemMetricsForDpi(SM_CXICON,GetDpiForSystem())); wc.hbrBackground = background;
    if (!RegisterClassW(&wc)) { DeleteObject(background); CloseHandle(singleton); return 1; }
    window = CreateWindowExW(0, ClassName, L"20·20·20 护眼提醒", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, Dpi::scale(640,GetDpiForSystem()), Dpi::scale(520,GetDpiForSystem()), nullptr, nullptr, instance, nullptr);
    if (!window) { DeleteObject(background); CloseHandle(singleton); return 1; }
    const auto initialSize=Dpi::outerSize(window,496,384,mainDpi);
    SetWindowPos(window,nullptr,0,0,initialSize.cx,initialSize.cy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    syncDesktopIcon(); refresh();
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (healthWindow && IsDialogMessageW(healthWindow, &message)) continue;
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    DeleteObject(background); CloseHandle(singleton); return 0;
}
