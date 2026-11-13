#include "../src/update_checker.h"
#include <iostream>
#include <cstdlib>
void check(bool ok,const char* reason){ if(!ok){std::cerr<<"FAILED: "<<reason<<'\n';std::exit(1);} }
bool rejects(const Updates::Json& j){try{Updates::parse(j.dump(),"0.3.0");return false;}catch(...){return true;}}
int main(int argc,char** argv) {
    if(argc>1 && (std::string(argv[1])=="--online" || std::string(argv[1])=="--download")) {
        try { auto r=Updates::check(); check(!r.tag.empty() && r.bytes>0,"real GitHub metadata");
            if(std::string(argv[1])=="--download") { check(argc==3,"download destination provided"); r=Updates::download(r,SharedSync::wide(argv[2])); check(!r.path.empty(),"download saved only after size and SHA256 verification"); }
            std::cout<<"GitHub update metadata and requested download verified: "<<r.tag<<'\n'; return 0; }
        catch(...) { check(false,"GitHub update request"); }
    }
    Updates::Json j={{"draft",false},{"prerelease",false},{"tag_name","v0.4.0"},{"assets",Updates::Json::array({{
        {"name","EyeBreak-Windows-x64-v0.4.0.zip"},
        {"browser_download_url","https://github.com/yuhang-wang-xjtu/eye_check/releases/download/v0.4.0/EyeBreak-Windows-x64-v0.4.0.zip"},
        {"digest","sha256:"+std::string(64,'a')},{"size",1234}}})}};
    check(Updates::parse(j.dump(),"0.3.0").newer,"newer version offered");
    check(!Updates::parse(j.dump(),"0.4.0").newer,"same version ignored");
    check(!Updates::parse(j.dump(),"0.5.0").newer,"downgrade ignored");
    check(Updates::version("v0.10.0")>Updates::version("v0.9.9"),"numeric comparison");
    auto direct=j;auto exe=j["assets"][0];exe["name"]="EyeBreak-Windows-x64-v0.4.0.exe";exe["browser_download_url"]="https://github.com/yuhang-wang-xjtu/eye_check/releases/download/v0.4.0/EyeBreak-Windows-x64-v0.4.0.exe";direct["assets"].push_back(exe);
    check(Updates::parse(direct.dump(),"0.3.0").executable,"automatic updater prefers verified standalone executable over ZIP");
    auto bad=j; bad["tag_name"]="v../../bad"; check(rejects(bad),"path traversal tag");
    bad=j; bad["draft"]=true; check(rejects(bad),"draft ignored");
    bad=j; bad["prerelease"]=true; check(rejects(bad),"prerelease ignored");
    bad=j; bad["assets"][0]["browser_download_url"]="https://evil.example/test.zip"; check(rejects(bad),"foreign download rejected");
    bad=j; bad["assets"][0]["digest"]="none"; check(rejects(bad),"missing digest rejected");
    bad=j; bad["assets"][0]["size"]=0; check(rejects(bad),"empty download rejected");
    bad=j; bad["assets"][0]["size"]=104857601; check(rejects(bad),"large download rejected");
    bad=j; bad["assets"].push_back(bad["assets"][0]); check(rejects(bad),"ambiguous asset rejected");
    Updates::Sha256 sha; sha.add("abc",3); check(sha.finish()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","native SHA256 known vector");
    std::cout<<"Update version, asset and checksum validation passed.\n";
}
