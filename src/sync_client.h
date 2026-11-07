#pragma once
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <objbase.h>
#include <fstream>
#include <filesystem>
#include <future>
#include <regex>
#include "../third_party/nlohmann/json.hpp"
#include "settings.h"
#include "timer.h"
#include "usage_store.h"

namespace SharedSync {
using Json=nlohmann::json;
struct Config { std::wstring url,room,token; bool enabled=false; std::wstring device; };
inline std::wstring newDeviceId() {
    GUID id{}; if(FAILED(CoCreateGuid(&id))) throw std::runtime_error("uuid");
    wchar_t text[40]{}; StringFromGUID2(id,text,40); return std::wstring(text+1,36);
}
struct Handle {
    HINTERNET value;
    explicit Handle(HINTERNET v):value(v) { if(!v) throw std::runtime_error("network"); }
    ~Handle(){ WinHttpCloseHandle(value); }
    operator HINTERNET() const { return value; }
    Handle(const Handle&)=delete;
};
inline std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n) throw std::runtime_error("utf8");
    std::wstring r(n,0); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),r.data(),n); return r;
}
inline std::string narrow(const std::wstring& s) {
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    if(!n && !s.empty()) throw std::runtime_error("utf16");
    std::string r(n,0); WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr); return r;
}
inline bool valid(const Config& c) {
    if(c.url.size()>2048 || c.token.size()<16 || c.token.size()>4096) return false;
    if(c.room.size()<8 || c.room.size()>80 || !std::all_of(c.room.begin(),c.room.end(),[](wchar_t ch){return (ch>=L'a' && ch<=L'z') || (ch>=L'A' && ch<=L'Z') || (ch>=L'0' && ch<=L'9') || ch==L'_' || ch==L'-';})) return false;
    if(!std::all_of(c.token.begin(),c.token.end(),[](wchar_t ch){return ch>=33 && ch<=126;})) return false;
    if(c.url.find_first_of(L"\r\n?#")!=std::wstring::npos) return false;
    if(std::any_of(c.url.begin(),c.url.end(),[](wchar_t ch){return ch<=32 || ch==L'\\';})) return false;
    URL_COMPONENTS u{}; u.dwStructSize=sizeof(u); u.dwHostNameLength=u.dwUserNameLength=u.dwPasswordLength=static_cast<DWORD>(-1);
    return WinHttpCrackUrl(c.url.c_str(),0,0,&u) && u.nScheme==INTERNET_SCHEME_HTTPS && u.dwHostNameLength && !u.dwUserNameLength && !u.dwPasswordLength;
}
inline Json options(const AppSettings& s) {
    return {{"restSeconds",s.restSeconds},{"sittingMinutes",s.sittingMinutes},{"workMinutes",s.workMinutes},
        {"nightHour",s.nightHour},{"nightMinute",s.nightMinute},{"sittingEnabled",s.sittingEnabled},
        {"workEnabled",s.workEnabled},{"nightEnabled",s.nightEnabled}};
}
inline std::uint64_t integer(const Json& j,const char* key,std::uint64_t max) {
    const auto& v=j.at(key);
    if(!v.is_number_integer() || (v.is_number_integer() && !v.is_number_unsigned() && v.get<std::int64_t>()<0)) throw std::runtime_error("integer");
    auto n=v.get<std::uint64_t>(); if(n>max) throw std::runtime_error("range"); return n;
}
inline std::uint64_t dateDay(const std::string& date) {
    if(!std::regex_match(date,std::regex("[0-9]{4}-[0-9]{2}-[0-9]{2}"))) throw std::runtime_error("date");
    SYSTEMTIME s{}; s.wYear=static_cast<WORD>(std::stoi(date.substr(0,4))); s.wMonth=static_cast<WORD>(std::stoi(date.substr(5,2))); s.wDay=static_cast<WORD>(std::stoi(date.substr(8,2)));
    FILETIME f{}; if(!SystemTimeToFileTime(&s,&f)) throw std::runtime_error("date");
    ULARGE_INTEGER n{}; n.LowPart=f.dwLowDateTime; n.HighPart=f.dwHighDateTime;
    auto day=n.QuadPart/10000/DailyUsage::DayMs;
    if(narrow(usageDate(day))!=date) throw std::runtime_error("date"); return day;
}
struct Snapshot {
    Json raw; std::uint64_t serverNow=0,revision=0,deadline=0,remaining=0,sitting=0,work=0;
    BreakTimer::State state=BreakTimer::State::Working;
    AppSettings settings; DailyUsage usage; bool sittingSent=false,workSent=false,available=false; std::string night,zone;
};
inline Snapshot parse(const std::string& body) {
    if(body.size()>1024*1024) throw std::runtime_error("size");
    auto j=Json::parse(body); const auto& s=j.at("snapshot"); Snapshot r; r.raw=s;
    r.serverNow=integer(j,"serverNowMs",4102444800000ULL);
    r.revision=integer(s,"revision",9007199254740991ULL);
    auto updated=integer(s,"updatedAtMs",4102444800000ULL);
    if(updated>r.serverNow || r.serverNow-updated>60000) throw std::runtime_error("timestamp");
    r.deadline=integer(s,"deadlineMs",4102445400000ULL); r.remaining=integer(s,"remainingMs",BreakTimer::WorkMs);
    r.sitting=integer(s,"sittingMs",100ULL*365*DailyUsage::DayMs); r.work=integer(s,"workMs",100ULL*365*DailyUsage::DayMs);
    const auto state=s.at("state").get<std::string>();
    if(state=="WORKING") r.state=BreakTimer::State::Working;
    else if(state=="RESTING") r.state=BreakTimer::State::Resting;
    else if(state=="WAITING") r.state=BreakTimer::State::Waiting;
    else if(state=="AWAY") r.state=BreakTimer::State::Away;
    else if(state=="PAUSED") r.state=BreakTimer::State::Paused;
    else throw std::runtime_error("state");
    const auto& o=s.at("settings");
    auto bounded=[&](const char* key,int low,int high){ auto n=integer(o,key,high); if(n<static_cast<unsigned>(low)) throw std::runtime_error("setting"); return static_cast<int>(n); };
    r.settings.restSeconds=bounded("restSeconds",1,600); r.settings.sittingMinutes=bounded("sittingMinutes",1,480);
    r.settings.workMinutes=bounded("workMinutes",1,720); r.settings.nightHour=bounded("nightHour",0,23); r.settings.nightMinute=bounded("nightMinute",0,59);
    auto flag=[](const Json& a,const char* key){ if(!a.at(key).is_boolean()) throw std::runtime_error("boolean"); return a.at(key).get<bool>(); };
    r.settings.sittingEnabled=flag(o,"sittingEnabled"); r.settings.workEnabled=flag(o,"workEnabled"); r.settings.nightEnabled=flag(o,"nightEnabled");
    r.sittingSent=flag(s,"sittingSent"); r.workSent=flag(s,"workSent"); r.night=s.at("lastNight").get<std::string>();
    if(s.contains("available")) r.available=flag(s,"available");
    if(!r.night.empty()) dateDay(r.night);
    r.zone=s.at("zone").get<std::string>();
    // The deployed shared session and its calendar are explicitly China time.
    if(r.zone!="Asia/Shanghai") throw std::runtime_error("unsupported zone");
    const auto& days=s.at("usage"); if(!days.is_object() || days.size()>36600) throw std::runtime_error("usage");
    for(auto i=days.begin();i!=days.end();++i) r.usage.days[dateDay(i.key())]=integer(days,i.key().c_str(),DailyUsage::DayMs);
    if((r.state==BreakTimer::State::Working || r.state==BreakTimer::State::Resting) && r.deadline>r.serverNow+BreakTimer::WorkMs) throw std::runtime_error("deadline");
    return r;
}
inline void sharedFields(AppSettings& to,const AppSettings& from) {
    to.restSeconds=from.restSeconds; to.sittingMinutes=from.sittingMinutes; to.workMinutes=from.workMinutes;
    to.nightHour=from.nightHour; to.nightMinute=from.nightMinute;
    to.sittingEnabled=from.sittingEnabled; to.workEnabled=from.workEnabled; to.nightEnabled=from.nightEnabled;
}
struct Result { bool ok=false; DWORD status=0; Snapshot snapshot; std::wstring error; std::uint64_t received=0; };
inline Result request(const Config& c,const Json& command=Json()) {
    Result r;
    try {
        if(!valid(c)) throw std::runtime_error("config");
        URL_COMPONENTS u{}; u.dwStructSize=sizeof(u); u.dwHostNameLength=u.dwUrlPathLength=static_cast<DWORD>(-1);
        if(!WinHttpCrackUrl(c.url.c_str(),0,0,&u)) throw std::runtime_error("url");
        std::wstring host(u.lpszHostName,u.dwHostNameLength),path(u.lpszUrlPath,u.dwUrlPathLength);
        while(!path.empty() && path.back()==L'/') path.pop_back();
        if(c.device.empty() || c.device.size()>80 || c.device.find_first_not_of(L"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_")!=std::wstring::npos) throw std::runtime_error("device");
        path+=L"/v2/sessions/"+c.room+L"/devices/"+c.device; if(!command.is_null()) path+=L"/commands";
        Handle session(WinHttpOpen(L"EyeBreak/0.3",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
        WinHttpSetTimeouts(session,1500,1500,2000,2500);
        Handle connection(WinHttpConnect(session,host.c_str(),u.nPort,0));
        Handle req(WinHttpOpenRequest(connection,command.is_null()?L"GET":L"POST",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
        DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if(!WinHttpSetOption(req,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy))) throw std::runtime_error("redirect");
        const auto headers=L"Authorization: Bearer "+c.token+L"\r\nContent-Type: application/json\r\n";
        const auto body=command.is_null()?std::string():command.dump();
        if(!WinHttpSendRequest(req,headers.c_str(),static_cast<DWORD>(-1),body.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(body.data()),static_cast<DWORD>(body.size()),static_cast<DWORD>(body.size()),0) || !WinHttpReceiveResponse(req,nullptr)) throw std::runtime_error("network");
        DWORD size=sizeof(r.status); WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&r.status,&size,WINHTTP_NO_HEADER_INDEX);
        if(r.status!=200 && r.status!=409) {
            r.error=r.status==401?L"鉴权失败，请检查令牌":L"服务器拒绝请求（HTTP "+std::to_wstring(r.status)+L"）"; return r;
        }
        std::string response; char buffer[8192]; DWORD read=0; const auto start=GetTickCount64();
        do {
            if(!WinHttpReadData(req,buffer,sizeof(buffer),&read) || response.size()+read>1024*1024 || GetTickCount64()-start>10000) throw std::runtime_error("response");
            response.append(buffer,read);
        } while(read);
        if(Json::parse(response).value("protocol",0)!=2) throw std::runtime_error("protocol");
        r.snapshot=parse(response); r.received=GetTickCount64(); r.ok=true;
    } catch(...) { r.error=L"连接失败或服务器数据无效；已保留本地记录"; }
    return r;
}
inline Json makeCommand(const std::string& action,std::uint64_t revision,const AppSettings* settings=nullptr) {
    GUID id{}; if(FAILED(CoCreateGuid(&id))) throw std::runtime_error("uuid");
    wchar_t text[40]{}; StringFromGUID2(id,text,40); std::wstring uuid(text); uuid=uuid.substr(1,36);
    Json j={{"action",action},{"expectedRevision",revision},{"operationId",narrow(uuid)}};
    if(settings) j["settings"]=options(*settings); return j;
}
inline bool saveConfig(const std::wstring& dir,const Config& c) {
    if(dir.empty()) return false;
    if(c.enabled && !valid(c)) return false;
    const auto ini=dir+L"\\sync.ini";
    if(!c.enabled) return WritePrivateProfileStringW(L"Sync",L"Enabled",L"0",ini.c_str())!=FALSE;
    DATA_BLOB input{static_cast<DWORD>(c.token.size()*sizeof(wchar_t)),reinterpret_cast<BYTE*>(const_cast<wchar_t*>(c.token.data()))},out{};
    if(!CryptProtectData(&input,L"EyeBreak sync token",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)) return false;
    const auto file=dir+L"\\sync-token.bin",tmp=file+L".tmp";
    std::ofstream stream(std::filesystem::path(tmp),std::ios::binary|std::ios::trunc); stream.write(reinterpret_cast<char*>(out.pbData),out.cbData); stream.close(); LocalFree(out.pbData);
    if(!stream || !MoveFileExW(tmp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return false;
    return WritePrivateProfileStringW(L"Sync",L"Url",c.url.c_str(),ini.c_str()) && WritePrivateProfileStringW(L"Sync",L"Room",c.room.c_str(),ini.c_str()) && WritePrivateProfileStringW(L"Sync",L"Device",c.device.c_str(),ini.c_str()) && WritePrivateProfileStringW(L"Sync",L"Enabled",L"1",ini.c_str());
}
inline Config loadConfig(const std::wstring& dir) {
    if(dir.empty()) return {};
    Config c; const auto ini=dir+L"\\sync.ini"; wchar_t text[2049]{};
    GetPrivateProfileStringW(L"Sync",L"Device",L"",text,2049,ini.c_str()); c.device=text;
    if(c.device.empty()) { c.device=newDeviceId(); WritePrivateProfileStringW(L"Sync",L"Device",c.device.c_str(),ini.c_str()); }
    GetPrivateProfileStringW(L"Sync",L"Url",L"",text,2049,ini.c_str()); c.url=text;
    GetPrivateProfileStringW(L"Sync",L"Room",L"",text,2049,ini.c_str()); c.room=text;
    std::ifstream f(std::filesystem::path(dir+L"\\sync-token.bin"),std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(f)),{});
    if(!bytes.empty() && bytes.size()<16384) {
        DATA_BLOB in{static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE*>(bytes.data())},out{};
        if(CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)) {
            if(out.cbData%sizeof(wchar_t)==0) c.token.assign(reinterpret_cast<wchar_t*>(out.pbData),out.cbData/sizeof(wchar_t)); LocalFree(out.pbData);
        }
    }
    c.enabled=GetPrivateProfileIntW(L"Sync",L"Enabled",0,ini.c_str())!=0 && valid(c); return c;
}
}
