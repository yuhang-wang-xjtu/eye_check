#pragma once
#include <windows.h>
#include <string>
#include <cstdio>
#include "timer.h"
namespace Diagnostics {
inline std::wstring file;
inline const char* state(BreakTimer::State s) {
    switch(s) { case BreakTimer::State::Working:return "working"; case BreakTimer::State::Paused:return "paused";
        case BreakTimer::State::Resting:return "resting"; case BreakTimer::State::Waiting:return "waiting"; case BreakTimer::State::Away:return "away"; }
    return "unknown";
}
inline void write(const char* reason,BreakTimer::State before,BreakTimer::State after) {
    if(file.empty()) return;
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if(GetFileAttributesExW(file.c_str(),GetFileExInfoStandard,&info) && (info.nFileSizeHigh || info.nFileSizeLow>2*1024*1024))
        MoveFileExW(file.c_str(),(file+L".1").c_str(),MOVEFILE_REPLACE_EXISTING);
    HANDLE handle=CreateFileW(file.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t{}; GetLocalTime(&t); char line[256]{};
    const int size=sprintf_s(line,"%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu %s %s->%s\r\n",
        t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetCurrentProcessId(),reason,state(before),state(after));
    DWORD written=0; if(size>0) WriteFile(handle,line,static_cast<DWORD>(size),&written,nullptr); CloseHandle(handle);
}
}
