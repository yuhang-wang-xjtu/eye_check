#include "../src/settings.h"
#include <cstdlib>
#include <iostream>
void check(bool ok, const char* label) { if (!ok) { std::cerr << label << '\n'; std::exit(1); } }
int main() {
    wchar_t directory[MAX_PATH]{}, temporary[MAX_PATH]{};
    check(GetTempPathW(MAX_PATH, directory) != 0, "temporary directory");
    check(GetTempFileNameW(directory, L"eyt", 0, temporary) != 0, "temporary file");
    DeleteFileW(temporary);
    AppSettings settings;
    settings.soundPath = L"C:\\测试目录\\自定义提示音.wav";
    settings.sittingMinutes = 45; settings.workMinutes = 90;
    settings.nightHour = 22; settings.nightMinute = 30;
    settings.workEnabled = false; settings.soundEnabled = false;
    settings.restSeconds = 45; settings.desktopIconEnabled = false;
    settings.lightMode = true;
    check(saveSettings(temporary, settings), "save settings");
    auto restored = loadSettings(temporary);
    check(restored.soundPath == settings.soundPath, "Unicode path roundtrip");
    check(restored.sittingMinutes == 45 && restored.workMinutes == 90, "threshold roundtrip");
    check(restored.nightHour == 22 && restored.nightMinute == 30, "night time roundtrip");
    check(!restored.workEnabled && !restored.soundEnabled, "toggles roundtrip");
    check(restored.restSeconds == 45 && !restored.desktopIconEnabled, "rest and desktop settings roundtrip");
    check(restored.lightMode, "light appearance persists across restart");
    WritePrivateProfileStringW(L"Reminders", L"SittingMinutes", L"999999", temporary);
    WritePrivateProfileStringW(L"Reminders", L"NightHour", L"999", temporary);
    WritePrivateProfileStringW(L"Reminders", L"RestSeconds", L"999", temporary);
    restored = loadSettings(temporary);
    check(restored.sittingMinutes == 480 && restored.nightHour == 23, "corrupt configuration bounded");
    check(restored.restSeconds == 600, "rest duration is bounded");
    DeleteFileW(temporary);
    restored = loadSettings(temporary);
    check(restored.sittingMinutes == 60 && restored.workMinutes == 120 && restored.soundPath.empty(), "missing file defaults");
    check(restored.restSeconds == 25 && restored.desktopIconEnabled, "new default rest and icon settings");
    check(!restored.lightMode, "existing configurations keep the dark appearance");
    std::cout << "All settings checks passed.\n";
}
