#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "usage.h"

inline bool loadUsage(const std::wstring& path, DailyUsage& usage) {
    std::ifstream file{std::filesystem::path(path)};
    if(!file) return GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES;
    std::string line; if(!std::getline(file,line) || line!="EyeBreak daily usage v1") return false;
    DailyUsage loaded;
    while(std::getline(file,line)) {
        std::istringstream row(line); std::uint64_t day=0,value=0; std::string extra;
        if(!(row>>day>>value) || row>>extra || day>4000000 || value>DailyUsage::DayMs*366) return false;
        loaded.days[day]=value;
    }
    if(file.bad()) return false;
    usage=std::move(loaded); return true;
}
inline bool saveUsage(const std::wstring& path, DailyUsage& usage) {
    if(!usage.dirty || path.empty()) return true;
    const auto temporary=path+L".tmp";
    {
        std::ofstream file{std::filesystem::path(temporary),std::ios::trunc};
        if(!file) return false;
        file<<"EyeBreak daily usage v1\n";
        for(const auto& item:usage.days) file<<item.first<<' '<<item.second<<'\n';
        file.flush(); if(!file) return false;
    }
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return false;
    usage.dirty=false; return true;
}
inline std::uint64_t localCalendarMs() {
    SYSTEMTIME time{}; GetLocalTime(&time); FILETIME file{}; SystemTimeToFileTime(&time,&file);
    ULARGE_INTEGER value{}; value.LowPart=file.dwLowDateTime; value.HighPart=file.dwHighDateTime;
    return value.QuadPart/10000;
}
inline std::wstring usageDate(std::uint64_t day) {
    ULARGE_INTEGER value{}; value.QuadPart=day*DailyUsage::DayMs*10000;
    FILETIME file{value.LowPart,value.HighPart}; SYSTEMTIME time{}; FileTimeToSystemTime(&file,&time);
    wchar_t text[32]{}; swprintf_s(text,L"%04u-%02u-%02u",time.wYear,time.wMonth,time.wDay); return text;
}
