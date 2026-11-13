#pragma once
#include "update_checker.h"
#include <shellapi.h>
#include <winver.h>
#include <memory>

namespace Updates {
using Path=std::filesystem::path;
struct WinHandle {
    HANDLE value=nullptr;
    explicit WinHandle(HANDLE h=nullptr):value(h){}
    ~WinHandle(){if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value);}
    WinHandle(const WinHandle&)=delete;
    WinHandle& operator=(const WinHandle&)=delete;
    WinHandle(WinHandle&& h) noexcept:value(h.value){h.value=nullptr;}
    WinHandle& operator=(WinHandle&& h) noexcept {if(this!=&h){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=h.value;h.value=nullptr;}return *this;}
    operator HANDLE() const{return value;}
};
inline Path modulePath(){wchar_t p[32768]{};DWORD n=GetModuleFileNameW(nullptr,p,32768);if(!n || n>=32768)throw std::runtime_error("module path");return Path(std::wstring(p,n));}
inline std::vector<std::wstring> arguments(){int n=0;auto values=CommandLineToArgvW(GetCommandLineW(),&n);if(!values)throw std::runtime_error("command line");std::vector<std::wstring> args(values,values+n);LocalFree(values);return args;}
inline std::wstring quote(const std::wstring& value){
    std::wstring r=L"\""; unsigned slashes=0;
    for(auto c:value){if(c==L'\\')++slashes;else{r.append(c==L'"'?slashes*2+1:slashes,L'\\');r+=c;slashes=0;}}
    r.append(slashes*2,L'\\');return r+L"\"";
}
inline WinHandle launch(const Path& image,const std::vector<std::wstring>& args){
    std::wstring command=quote(image.wstring());for(const auto& a:args)command+=L" "+quote(a);
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION info{};
    if(!CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,image.parent_path().c_str(),&startup,&info))throw std::runtime_error("launch");
    CloseHandle(info.hThread);return WinHandle(info.hProcess);
}
inline std::array<unsigned,3> imageVersion(const Path& path){
    DWORD ignored=0,n=GetFileVersionInfoSizeW(path.c_str(),&ignored);if(!n)throw std::runtime_error("version resource");
    std::vector<BYTE> bytes(n);VS_FIXEDFILEINFO* info=nullptr;UINT size=0;
    if(!GetFileVersionInfoW(path.c_str(),0,n,bytes.data()) || !VerQueryValueW(bytes.data(),L"\\",reinterpret_cast<void**>(&info),&size) || size<sizeof(*info) || info->dwSignature!=0xfeef04bd)throw std::runtime_error("version resource");
    return {HIWORD(info->dwFileVersionMS),LOWORD(info->dwFileVersionMS),HIWORD(info->dwFileVersionLS)};
}
inline std::string fileHash(const Path& path){
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("read executable");Sha256 hash;char data[16384];
    while(input){input.read(data,sizeof(data));auto n=input.gcount();if(n)hash.add(data,static_cast<DWORD>(n));}
    if(input.bad())throw std::runtime_error("read executable");return hash.finish();
}
inline std::uint64_t processBirth(HANDLE process){
    FILETIME created{},exit{},kernel{},user{};if(!GetProcessTimes(process,&created,&exit,&kernel,&user))throw std::runtime_error("process identity");
    return (static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
}
inline Path processImage(HANDLE process){wchar_t path[32768]{};DWORD n=32768;if(!QueryFullProcessImageNameW(process,0,path,&n))throw std::runtime_error("process image");return Path(std::wstring(path,n));}
inline bool samePath(const Path& a,const Path& b){return _wcsicmp(std::filesystem::absolute(a).lexically_normal().c_str(),std::filesystem::absolute(b).lexically_normal().c_str())==0;}
inline void writeJson(const Path& file,const Json& json){
    const auto temp=Path(file.wstring()+L".tmp");std::ofstream output(temp,std::ios::binary|std::ios::trunc);output<<json.dump();output.close();
    if(!output || !MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("save update state");
}
inline Json readJson(const Path& file){
    if(std::filesystem::file_size(file)>65536)throw std::runtime_error("update state size");
    std::ifstream input(file,std::ios::binary);return Json::parse(input);
}
struct PreparedInstall {
    Path manifest,state;
    WinHandle ready,cancel,commit,helper;
    bool committed=false;
    ~PreparedInstall(){if(!committed)cancelInstall();}
    void cancelInstall(){if(cancel.value)SetEvent(cancel);}
    void commitInstall(){if(!SetEvent(commit))throw std::runtime_error("commit update");committed=true;}
    bool waitReady(){HANDLE waits[]={ready.value,helper.value};return WaitForMultipleObjects(2,waits,FALSE,10000)==WAIT_OBJECT_0;}
};
inline std::unique_ptr<PreparedInstall> prepareInstall(const Release& release,const Path& base,const Path& helperImage,DWORD parentPid,const Path& target){
    if(!release.executable || fileHash(release.path)!=release.digest || imageVersion(release.path)!=version(release.tag))throw std::runtime_error("update payload verification");
    WinHandle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,parentPid));
    if(!parent.value || !samePath(processImage(parent),target) || imageVersion(target)>=version(release.tag))throw std::runtime_error("update target");
    auto result=std::make_unique<PreparedInstall>();const auto id=SharedSync::newDeviceId();const auto root=base/L"updates"/id;
    std::filesystem::create_directories(root);result->manifest=root/L"install.json";result->state=root/L"state.json";
    const auto helperPath=root/L"EyeBreakUpdateHelper.exe";
    if(!CopyFileW(helperImage.c_str(),helperPath.c_str(),TRUE))throw std::runtime_error("copy update helper");
    const auto event=L"Local\\EyeBreakUpdate-"+id;
    result->ready=WinHandle(CreateEventW(nullptr,TRUE,FALSE,(event+L"-ready").c_str()));
    result->cancel=WinHandle(CreateEventW(nullptr,TRUE,FALSE,(event+L"-cancel").c_str()));
    result->commit=WinHandle(CreateEventW(nullptr,TRUE,FALSE,(event+L"-commit").c_str()));
    if(!result->ready.value || !result->cancel.value || !result->commit.value)throw std::runtime_error("update events");
    Json job={{"parent",parentPid},{"birth",processBirth(parent)},{"target",SharedSync::narrow(std::filesystem::absolute(target).wstring())},
        {"payload",SharedSync::narrow(std::filesystem::absolute(release.path).wstring())},{"hash",release.digest},{"tag",release.tag},
        {"event",SharedSync::narrow(event)},{"state",SharedSync::narrow(result->state.wstring())},{"id",SharedSync::narrow(id)}};
    writeJson(result->manifest,job);
    result->helper=launch(helperPath,{L"--apply-update",result->manifest.wstring()});
    if(!result->waitReady()){result->cancelInstall();throw std::runtime_error("update helper not ready or directory is not writable");}
    return result;
}
inline int runInstaller(const Path& manifest){
    Path target,stage,backup,state;bool replaced=false,parentExited=false;std::wstring ackName;
    try {
        auto j=readJson(manifest);target=SharedSync::wide(j.at("target").get<std::string>());state=SharedSync::wide(j.at("state").get<std::string>());
        const auto payload=Path(SharedSync::wide(j.at("payload").get<std::string>()));const auto id=SharedSync::wide(j.at("id").get<std::string>());
        if(!std::regex_match(SharedSync::narrow(id),std::regex("[A-Za-z0-9_-]{8,80}")))throw std::runtime_error("job id");
        stage=Path(target.wstring()+L".pending-"+id+L".exe");backup=Path(target.wstring()+L".rollback-"+id+L".exe");
        const auto event=SharedSync::wide(j.at("event").get<std::string>());ackName=event+L"-started";
        WinHandle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,j.at("parent").get<DWORD>()));
        if(!parent.value || processBirth(parent)!=j.at("birth").get<std::uint64_t>() || !samePath(processImage(parent),target))throw std::runtime_error("old process identity changed");
        WinHandle ready(OpenEventW(EVENT_MODIFY_STATE,FALSE,(event+L"-ready").c_str())),cancel(OpenEventW(SYNCHRONIZE,FALSE,(event+L"-cancel").c_str())),commit(OpenEventW(SYNCHRONIZE,FALSE,(event+L"-commit").c_str()));
        if(!ready.value || !cancel.value || !commit.value || !CopyFileW(payload.c_str(),stage.c_str(),TRUE))throw std::runtime_error("target directory is not writable");
        if(fileHash(stage)!=j.at("hash").get<std::string>() || imageVersion(stage)!=version(j.at("tag").get<std::string>()) || imageVersion(target)>=imageVersion(stage))throw std::runtime_error("staged executable verification");
        SetEvent(ready);HANDLE wait[]={parent.value,cancel.value};
        if(WaitForMultipleObjects(2,wait,FALSE,120000)!=WAIT_OBJECT_0){DeleteFileW(stage.c_str());return 2;}
        if(WaitForSingleObject(cancel,0)==WAIT_OBJECT_0 || WaitForSingleObject(commit,0)!=WAIT_OBJECT_0){DeleteFileW(stage.c_str());return 2;}
        parentExited=true;
        // Same-directory replacement preserves the original executable path and shortcuts.
        bool moved=false;
        for(int attempt=0;attempt<40 && !moved;++attempt){moved=MoveFileExW(target.c_str(),backup.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;if(!moved)Sleep(100);}
        if(!moved)throw std::runtime_error("old executable is still locked");
        replaced=true;
        if(!MoveFileExW(stage.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("install new executable");
        WinHandle started(CreateEventW(nullptr,TRUE,FALSE,ackName.c_str()));if(!started.value)throw std::runtime_error("startup event");
        auto child=launch(target,{L"--updated",ackName,state.wstring()});HANDLE startup[]={started.value,child.value};
        if(WaitForMultipleObjects(2,startup,FALSE,15000)!=WAIT_OBJECT_0){TerminateProcess(child,1);WaitForSingleObject(child,5000);throw std::runtime_error("new version failed to start");}
        DeleteFileW(backup.c_str());DeleteFileW(state.c_str());return 0;
    } catch(...) {
        DeleteFileW(stage.c_str());
        if(replaced && !backup.empty()) {
            // Preserve the backup if rollback itself fails; never delete the only working copy.
            if(MoveFileExW(backup.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
                try{auto old=launch(target,{L"--update-rollback",state.wstring()});}catch(...){}
            }
        } else if(parentExited) {
            try{auto old=launch(target,{L"--update-rollback",state.wstring()});}catch(...){}
        }
        return 1;
    }
}
}
