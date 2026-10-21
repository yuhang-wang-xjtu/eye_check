#pragma once
#include <windows.h>
#include <string>
#include <algorithm>
#include "reminders.h"

struct AppSettings : ReminderSettings {
    std::wstring soundPath;
    int restSeconds = 25;
    bool desktopIconEnabled = true;
    bool lightMode = false;
};
inline AppSettings loadSettings(const std::wstring& file) {
    AppSettings s;
    auto read = [&](const wchar_t* key, int fallback, int low, int high) {
        return std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Reminders", key, fallback, file.c_str())), low, high);
    };
    s.soundEnabled = read(L"SoundEnabled", 1, 0, 1) != 0;
    s.restSeconds = read(L"RestSeconds", 25, 1, 600);
    s.desktopIconEnabled = read(L"DesktopIconEnabled", 1, 0, 1) != 0;
    s.lightMode = read(L"LightMode", 0, 0, 1) != 0;
    s.sittingEnabled = read(L"SittingEnabled", 1, 0, 1) != 0;
    s.workEnabled = read(L"WorkEnabled", 1, 0, 1) != 0;
    s.nightEnabled = read(L"NightEnabled", 1, 0, 1) != 0;
    s.sittingMinutes = read(L"SittingMinutes", 60, 1, 480);
    s.workMinutes = read(L"WorkMinutes", 120, 1, 720);
    s.nightHour = read(L"NightHour", 23, 0, 23);
    s.nightMinute = read(L"NightMinute", 0, 0, 59);
    wchar_t path[256]{};
    GetPrivateProfileStringW(L"Reminders", L"SoundPath", L"", path, 256, file.c_str());
    s.soundPath = path;
    return s;
}
inline bool saveSettings(const std::wstring& file, const AppSettings& s) {
    // Write one complete section rather than leaving a partially updated configuration.
    std::wstring section;
    auto add = [&](const wchar_t* key, const std::wstring& value) {
        section += key; section += L"="; section += value; section.push_back(L'\0');
    };
    add(L"SoundEnabled", std::to_wstring(s.soundEnabled));
    add(L"RestSeconds", std::to_wstring(s.restSeconds));
    add(L"DesktopIconEnabled", std::to_wstring(s.desktopIconEnabled));
    add(L"LightMode", std::to_wstring(s.lightMode));
    add(L"SittingEnabled", std::to_wstring(s.sittingEnabled));
    add(L"WorkEnabled", std::to_wstring(s.workEnabled));
    add(L"NightEnabled", std::to_wstring(s.nightEnabled));
    add(L"SittingMinutes", std::to_wstring(s.sittingMinutes));
    add(L"WorkMinutes", std::to_wstring(s.workMinutes));
    add(L"NightHour", std::to_wstring(s.nightHour));
    add(L"NightMinute", std::to_wstring(s.nightMinute));
    add(L"SoundPath", s.soundPath);
    section.push_back(L'\0');
    // A BOM ensures Chinese filenames survive INI writes.
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
        HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        const WORD bom = 0xFEFF; DWORD written = 0;
        const bool ok = WriteFile(handle, &bom, sizeof(bom), &written, nullptr) && written == sizeof(bom);
        CloseHandle(handle);
        if (!ok) return false;
    }
    return WritePrivateProfileSectionW(L"Reminders", section.c_str(), file.c_str()) != FALSE;
}
