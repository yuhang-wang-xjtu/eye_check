#include "../src/update_installer.h"
#include <iostream>
#include <cstdlib>
void check(bool ok,const char* why){if(!ok){std::cerr<<"FAILED: "<<why<<'\n';std::exit(1);}}
bool waitFile(const Updates::Path& file){for(int i=0;i<500;++i){if(std::filesystem::exists(file))return true;Sleep(10);}return false;}
int wmain(int argc,wchar_t** argv){
    check(argc==4,"production helper and two versioned probes supplied");
    const Updates::Path helper=std::filesystem::absolute(argv[1]),oldImage=argv[2],newImage=argv[3];
    const auto root=helper.parent_path()/(L"installer-test-"+SharedSync::newDeviceId());
    std::filesystem::create_directories(root);
    for(bool reject:{false,true}){
        const auto folder=root/(reject?L"rollback 目录":L"success 目录");std::filesystem::create_directories(folder);
        const auto target=folder/L"EyeBreak.exe";check(CopyFileW(oldImage.c_str(),target.c_str(),TRUE)!=FALSE,"copy old executable");
        const auto stopName=L"Local\\EyeBreakInstallTest-"+SharedSync::newDeviceId();Updates::WinHandle stop(CreateEventW(nullptr,TRUE,FALSE,stopName.c_str()));
        auto parent=Updates::launch(target,{L"--hold",stopName});check(waitFile(folder/L"old-ready"),"old executable really running");
        Updates::Release r;r.tag="v2.0.0";r.path=newImage.wstring();r.digest=Updates::fileHash(newImage);r.executable=true;r.newer=true;
        DWORD parentId=GetProcessId(parent);
        auto install=Updates::prepareInstall(r,folder,helper,parentId,target);
        check(WaitForSingleObject(parent,0)==WAIT_TIMEOUT && Updates::fileHash(target)==Updates::fileHash(oldImage),"preparing update cannot overwrite or stop the running old executable");
        const Updates::Json state={{"remaining",777000},{"usage",12345}};Updates::writeJson(install->state,state);
        std::ofstream(folder/L"settings.ini")<<"keep-settings";
        if(reject)std::ofstream(folder/L"reject-new")<<"simulate new startup failure";
        install->commitInstall();SetEvent(stop);
        check(WaitForSingleObject(install->helper,20000)==WAIT_OBJECT_0,"installer finishes");DWORD code=99;GetExitCodeProcess(install->helper,&code);
        check(code==(reject?1U:0U),"installer reports success or rollback");
        check(waitFile(folder/(reject?L"rolled-back":L"started")),"correct version is automatically restarted");
        check(Updates::fileHash(target)==Updates::fileHash(reject?oldImage:newImage),"target contains installed version or recovered old version");
        check(Updates::readJson(folder/L"seen-state.json")==state,"restart and rollback retain saved state");
        std::ifstream settings(folder/L"settings.ini");std::string value;settings>>value;settings.close();check(value=="keep-settings","local settings are not replaced");
    }
    check(root.parent_path()==helper.parent_path() && root.filename().wstring().find(L"installer-test-")==0,"cleanup belongs to the isolated build fixture");
    std::filesystem::remove_all(root);
    std::cout<<"Real running-file update, automatic restart and startup-failure rollback passed.\n";
}
