#pragma once
#include "sync_client.h"
#include <bcrypt.h>
#include <array>
#include <vector>
#include <functional>
#include "app_version.h"

namespace Updates {
inline constexpr char Version[]=EYEBREAK_VERSION_TEXT;
inline constexpr wchar_t Api[]=L"https://api.github.com/repos/yuhang-wang-xjtu/eye_check/releases/latest";
inline constexpr wchar_t Page[]=L"https://github.com/yuhang-wang-xjtu/eye_check/releases/latest";
using Json=SharedSync::Json;
inline std::array<unsigned,3> version(std::string text) {
    if(!std::regex_match(text,std::regex("v?[0-9]{1,6}\\.[0-9]{1,6}\\.[0-9]{1,6}"))) throw std::runtime_error("version");
    if(text.front()=='v') text.erase(0,1);
    std::array<unsigned,3> result{};
    for(int i=0;i<3;++i) { auto stop=text.find('.'); result[i]=static_cast<unsigned>(std::stoul(text.substr(0,stop))); if(stop!=std::string::npos) text.erase(0,stop+1); }
    return result;
}
struct Release { std::string tag,url,digest; std::uint64_t bytes=0; bool newer=false; std::wstring path; };
inline std::string assetName(const std::string& tag) { return "EyeBreak-Windows-x64-"+tag+".zip"; }
inline Release parse(const std::string& body,const std::string& current=Version) {
    auto j=Json::parse(body); Release r;
    if(!j.at("draft").is_boolean() || !j.at("prerelease").is_boolean() || j.at("draft").get<bool>() || j.at("prerelease").get<bool>()) throw std::runtime_error("unstable release");
    r.tag=j.at("tag_name").get<std::string>(); if(r.tag.empty() || r.tag.front()!='v') throw std::runtime_error("tag");
    r.newer=version(r.tag)>version(current);
    const auto& assets=j.at("assets"); if(!assets.is_array()) throw std::runtime_error("assets");
    for(const auto& a:assets) if(a.at("name").get<std::string>()==assetName(r.tag)) {
        if(!r.url.empty()) throw std::runtime_error("duplicate asset");
        r.url=a.at("browser_download_url").get<std::string>();
        if(r.url!="https://github.com/yuhang-wang-xjtu/eye_check/releases/download/"+r.tag+"/"+assetName(r.tag)) throw std::runtime_error("asset url");
        r.digest=a.at("digest").get<std::string>();
        if(!std::regex_match(r.digest,std::regex("sha256:[0-9a-fA-F]{64}"))) throw std::runtime_error("digest");
        r.digest.erase(0,7); std::transform(r.digest.begin(),r.digest.end(),r.digest.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        r.bytes=SharedSync::integer(a,"size",100ULL*1024*1024); if(!r.bytes) throw std::runtime_error("size");
    }
    if(r.url.empty()) throw std::runtime_error("Windows asset unavailable"); return r;
}
inline void transfer(std::wstring url,std::uint64_t maximum,const std::function<void(const char*,DWORD)>& chunk,bool redirects) {
    using SharedSync::Handle;
    const auto start=GetTickCount64();
    for(int redirect=0;redirect<6;++redirect) {
        URL_COMPONENTS u{}; u.dwStructSize=sizeof(u); u.dwHostNameLength=u.dwUrlPathLength=u.dwExtraInfoLength=u.dwUserNameLength=u.dwPasswordLength=static_cast<DWORD>(-1);
        if(!WinHttpCrackUrl(url.c_str(),0,0,&u) || u.nScheme!=INTERNET_SCHEME_HTTPS || u.dwUserNameLength || u.dwPasswordLength || u.nPort!=443) throw std::runtime_error("HTTPS required");
        std::wstring host(u.lpszHostName,u.dwHostNameLength),path(u.lpszUrlPath,u.dwUrlPathLength);
        if(u.dwExtraInfoLength) path.append(u.lpszExtraInfo,u.dwExtraInfoLength);
        if(host!=L"api.github.com" && host!=L"github.com" && host!=L"release-assets.githubusercontent.com" && host!=L"objects.githubusercontent.com") throw std::runtime_error("untrusted redirect");
        Handle session(WinHttpOpen(L"EyeBreak/0.4.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
        WinHttpSetTimeouts(session,2000,3000,4000,5000);
        Handle connection(WinHttpConnect(session,host.c_str(),u.nPort,0));
        Handle request(WinHttpOpenRequest(connection,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
        DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if(!WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy)) ||
           !WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0) || !WinHttpReceiveResponse(request,nullptr)) throw std::runtime_error("network");
        DWORD code=0,size=sizeof(code);
        if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&size,WINHTTP_NO_HEADER_INDEX)) throw std::runtime_error("HTTP status");
        if(redirects && (code==301 || code==302 || code==303 || code==307 || code==308)) {
            DWORD needed=0; WinHttpQueryHeaders(request,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&needed,WINHTTP_NO_HEADER_INDEX);
            if(!needed || needed>32768) throw std::runtime_error("redirect size");
            std::wstring next(needed/sizeof(wchar_t),0);
            if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,next.data(),&needed,WINHTTP_NO_HEADER_INDEX)) throw std::runtime_error("redirect");
            next.resize(wcslen(next.c_str())); url=std::move(next); continue;
        }
        if(code!=200) throw std::runtime_error("GitHub HTTP "+std::to_string(code));
        std::uint64_t count=0; char buffer[16384]; DWORD n=0;
        do {
            if(!WinHttpReadData(request,buffer,sizeof(buffer),&n) || GetTickCount64()-start>60000) throw std::runtime_error("download timeout");
            count+=n; if(count>maximum) throw std::runtime_error("response size"); if(n) chunk(buffer,n);
        } while(n);
        return;
    }
    throw std::runtime_error("too many redirects");
}
struct Sha256 {
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr; std::vector<unsigned char> object;
    Sha256() {
        DWORD bytes=0,needed=0;
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("SHA256 provider");
        if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&bytes),sizeof(bytes),&needed,0)<0) { BCryptCloseAlgorithmProvider(algorithm,0); algorithm=nullptr; throw std::runtime_error("SHA256 property"); }
        object.resize(bytes);
        if(BCryptCreateHash(algorithm,&hash,object.data(),bytes,nullptr,0,0)<0) { BCryptCloseAlgorithmProvider(algorithm,0); algorithm=nullptr; throw std::runtime_error("SHA256 hash"); }
    }
    ~Sha256(){ if(hash) BCryptDestroyHash(hash); if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0); }
    void add(const char* data,DWORD bytes) { if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(data)),bytes,0)<0) throw std::runtime_error("SHA256 data"); }
    std::string finish() {
        unsigned char digest[32]{}; if(BCryptFinishHash(hash,digest,32,0)<0) throw std::runtime_error("SHA256 finish");
        const char* hex="0123456789abcdef"; std::string s; for(auto c:digest) { s+=hex[c>>4]; s+=hex[c&15]; } return s;
    }
};
inline Release check() {
    std::string body; transfer(Api,1024*1024,[&](const char* data,DWORD n){body.append(data,n);},false); return parse(body);
}
inline Release download(Release r,const std::wstring& base) {
    // Revalidate all externally supplied metadata before deriving any file path.
    version(r.tag); if(r.tag.empty() || r.tag.front()!='v' || r.url!="https://github.com/yuhang-wang-xjtu/eye_check/releases/download/"+r.tag+"/"+assetName(r.tag) ||
        !r.bytes || r.bytes>100ULL*1024*1024 || !std::regex_match(r.digest,std::regex("[0-9a-f]{64}"))) throw std::runtime_error("metadata");
    const auto directory=std::filesystem::path(base)/L"releases"/SharedSync::wide(r.tag);
    std::filesystem::create_directories(directory);
    const auto file=directory/SharedSync::wide(assetName(r.tag)),temporary=std::filesystem::path(file.wstring()+L".part-"+SharedSync::newDeviceId());
    try {
        Sha256 hash; std::uint64_t total=0;
        std::ofstream stream(temporary,std::ios::binary|std::ios::trunc); if(!stream) throw std::runtime_error("write");
        transfer(SharedSync::wide(r.url),r.bytes,[&](const char* data,DWORD n){ stream.write(data,n); if(!stream) throw std::runtime_error("disk"); hash.add(data,n); total+=n; },true);
        stream.close(); if(!stream || total!=r.bytes || hash.finish()!=r.digest) throw std::runtime_error("download verification");
        if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("save");
        r.path=file.wstring(); return r;
    } catch(...) { DeleteFileW(temporary.c_str()); throw; }
}
}
