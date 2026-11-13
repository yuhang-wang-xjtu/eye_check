#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
int wmain(int argc,wchar_t** argv){
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);auto root=std::filesystem::path(path).parent_path();
    if(argc==3 && std::wstring(argv[1])==L"--hold"){
        HANDLE stop=OpenEventW(SYNCHRONIZE,FALSE,argv[2]);if(!stop)return 2;
        std::ofstream(root/L"old-ready")<<"ready";WaitForSingleObject(stop,30000);CloseHandle(stop);return 0;
    }
    if(argc==4 && std::wstring(argv[1])==L"--updated"){
        if(std::filesystem::exists(root/L"reject-new"))return 3;
        std::filesystem::copy_file(argv[3],root/L"seen-state.json",std::filesystem::copy_options::overwrite_existing);
        std::ofstream(root/L"started")<<PROBE_VERSION;
        HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,argv[2]);if(!event)return 4;SetEvent(event);CloseHandle(event);return 0;
    }
    if(argc==3 && std::wstring(argv[1])==L"--update-rollback"){
        std::filesystem::copy_file(argv[2],root/L"seen-state.json",std::filesystem::copy_options::overwrite_existing);
        std::ofstream(root/L"rolled-back")<<PROBE_VERSION;return 0;
    }
    return 1;
}
