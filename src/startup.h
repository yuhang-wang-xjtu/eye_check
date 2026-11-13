#pragma once
#include <windows.h>
#include <string>

namespace Startup {
inline constexpr auto Key=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr auto Name=L"EyeBreak202020";
inline bool enabled() {
    DWORD bytes=0;
    return RegGetValueW(HKEY_CURRENT_USER,Key,Name,RRF_RT_REG_SZ,nullptr,nullptr,&bytes)==ERROR_SUCCESS && bytes>sizeof(wchar_t);
}
inline bool setEnabled(bool enabled) {
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,Key,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    LSTATUS result;
    if(enabled) {
        wchar_t path[32768]{};const auto length=GetModuleFileNameW(nullptr,path,32768);
        if(!length || length>=32768){RegCloseKey(key);return false;}
        const auto command=L"\""+std::wstring(path,length)+L"\" --startup";
        result=RegSetValueExW(key,Name,0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
    } else {
        result=RegDeleteValueW(key,Name);
        if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS;
    }
    RegCloseKey(key);return result==ERROR_SUCCESS;
}
}
