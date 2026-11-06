#include "../src/sync_client.h"
#include <iostream>
#include <cstdlib>
using namespace SharedSync;
void check(bool ok,const char* reason) { if(!ok) { std::cerr<<"FAILED: "<<reason<<'\n'; std::exit(1); } }
Json fixture() {
    AppSettings s;
    return {{"serverNowMs",1800000000000ULL},{"snapshot",{
        {"revision",3},{"updatedAtMs",1800000000000ULL},{"deadlineMs",1800001200000ULL},
        {"state","WORKING"},{"remainingMs",1200000},{"sittingMs",1000},{"workMs",2000},
        {"settings",options(s)},{"sittingSent",false},{"workSent",false},{"lastNight",""},
        {"zone","Asia/Shanghai"},{"usage",{{"2026-11-07",1000}}}}}};
}
bool rejects(const Json& j) { try { parse(j.dump()); return false; } catch(...) { return true; } }
int wmain(int argc,wchar_t** argv) {
    if(argc==3 && std::wstring(argv[1])==L"--online") {
        // Private connection file is read in memory only. Always use a fresh test room.
        Config c; std::ifstream f(std::filesystem::path(argv[2]),std::ios::binary); std::string line;
        while(std::getline(f,line)) {
            if(!line.empty() && line.back()=='\r') line.pop_back();
            if(line.rfind("Server: ",0)==0) c.url=wide(line.substr(8));
            if(line.rfind("Token: ",0)==0) c.token=wide(line.substr(7));
        }
        auto id=makeCommand("pause",0).at("operationId").get<std::string>(); c.room=L"validation_"+wide(id);
        check(valid(c),"private config valid");
        auto first=request(c); check(first.ok && first.status==200,"HTTPS GET");
        auto command=makeCommand("pause",first.snapshot.revision);
        auto paused=request(c,command); check(paused.ok && paused.snapshot.state==BreakTimer::State::Paused,"HTTPS pause");
        auto repeated=request(c,command); check(repeated.ok && repeated.snapshot.revision==paused.snapshot.revision,"idempotent retry");
        auto stale=request(c,makeCommand("continue",first.snapshot.revision)); check(stale.ok && stale.status==409 && stale.snapshot.state==BreakTimer::State::Paused,"stale command rejected");
        auto settings=AppSettings{}; settings.restSeconds=33;
        auto updated=request(c,makeCommand("settings",stale.snapshot.revision,&settings)); check(updated.ok && updated.status==200 && updated.snapshot.settings.restSeconds==33,"shared settings");
        auto away=request(c,makeCommand("rest",updated.snapshot.revision)); check(away.ok && away.snapshot.state==BreakTimer::State::Away,"shared away");
        auto working=request(c,makeCommand("continue",away.snapshot.revision)); check(working.ok && working.snapshot.state==BreakTimer::State::Working,"shared continue");
        auto stop=request(c,makeCommand("end_work",working.snapshot.revision)); check(stop.ok && stop.snapshot.sitting==0 && stop.snapshot.state==BreakTimer::State::Paused,"end work");
        std::cout<<"HTTPS integration passed; temporary room "<<narrow(c.room)<<'\n'; return 0;
    }
    auto j=fixture(); const auto s=parse(j.dump());
    check(s.revision==3 && s.usage.total(dateDay("2026-11-07"))==1000,"snapshot and calendar");
    for(const auto* state:{"PAUSED","RESTING","WAITING","AWAY"}) { auto v=j; v["snapshot"]["state"]=state; check(!rejects(v),"all states"); }
    auto bad=j; bad["snapshot"]["state"]="UNKNOWN"; check(rejects(bad),"unknown state");
    bad=j; bad["snapshot"]["revision"]=-1; check(rejects(bad),"negative revision");
    bad=j; bad["snapshot"]["sittingSent"]=1; check(rejects(bad),"boolean type");
    bad=j; bad["snapshot"]["remainingMs"]=1200001; check(rejects(bad),"remaining bound");
    bad=j; bad["snapshot"]["settings"]["restSeconds"]=0; check(rejects(bad),"setting bound");
    bad=j; bad["snapshot"]["settings"]["restSeconds"]=1.5; check(rejects(bad),"fractional setting");
    bad=j; bad["snapshot"]["usage"]={{"2026-02-30",1000}}; check(rejects(bad),"invalid date");
    bad=j; bad["snapshot"]["usage"]={{"2026-11-07",86400001}}; check(rejects(bad),"usage bound");
    bad=j; bad["snapshot"]["zone"]="wrong"; check(rejects(bad),"unsupported calendar");
    bad=j; bad["snapshot"].erase("settings"); check(rejects(bad),"missing fields");
    Config c{L"https://example.org/eyebreak",L"test_room",L"private-test-token-123",true}; check(valid(c),"valid HTTPS config");
    auto invalid=c; invalid.url=L"http://example.org"; check(!valid(invalid),"no cleartext token");
    invalid=c; invalid.url=L"https://user:password@example.org"; check(!valid(invalid),"no URL credentials");
    invalid=c; invalid.room=L"../escape"; check(!valid(invalid),"no path injection");
    invalid=c; invalid.token+=L"\r\nX: bad"; check(!valid(invalid),"no header injection");
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH,temp);
    const auto dir=std::wstring(temp)+L"eyebreak-sync-"+wide(makeCommand("pause",0)["operationId"].get<std::string>());
    check(CreateDirectoryW(dir.c_str(),nullptr)!=FALSE,"test directory");
    check(saveConfig(dir,c),"DPAPI save"); auto loaded=loadConfig(dir);
    check(loaded.enabled && loaded.token==c.token && loaded.url==c.url && loaded.room==c.room,"DPAPI current-user roundtrip");
    std::ifstream ini(std::filesystem::path(dir+L"\\sync.ini"),std::ios::binary); std::string plain((std::istreambuf_iterator<char>(ini)),{}); ini.close();
    check(plain.find(narrow(c.token))==std::string::npos,"token not in INI");
    loaded.enabled=false; check(saveConfig(dir,loaded) && !loadConfig(dir).enabled,"disconnect persists");
    DeleteFileW((dir+L"\\sync.ini").c_str()); DeleteFileW((dir+L"\\sync-token.bin").c_str()); RemoveDirectoryW(dir.c_str());
    std::cout<<"All sync parsing, validation and credential checks passed.\n";
}
