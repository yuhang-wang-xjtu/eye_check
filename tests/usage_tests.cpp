#include "../src/usage_store.h"
#include "../src/session_timing.h"
#include <iostream>
#include <cstdlib>
void check(bool ok,const char* label) { if(!ok) { std::cerr<<label<<'\n'; std::exit(1); } }
int main() {
    const auto day=150000ULL, midnight=(day+1)*DailyUsage::DayMs;
    DailyUsage usage;
    usage.record(10000,midnight+3000);
    check(usage.total(day)==7000 && usage.total(day+1)==3000,"work interval split at local midnight");
    ReminderSettings settings; BreakTimer eye(0); HealthReminders health(0);
    SessionTiming timing(eye,health,settings,0);
    usage=DailyUsage{};
    timing.onActive=[&](std::uint64_t end,std::uint64_t elapsed){ usage.record(elapsed,day*DailyUsage::DayMs+end); };
    timing.advance(60000); eye.toggle(60000); timing.advance(120000);
    check(usage.total(day)==60000,"only working time counts, manual pause excluded");
    eye.away(120000); timing.advance(180000); eye.resume(180000);
    check(health.workMs==0 && usage.total(day)==60000,"5 to 1 recovery never subtracts daily usage");
    timing.advance(181000); eye.rest(181000); timing.advance(206000); eye.finish(); timing.advance(210000);
    check(usage.total(day)==61000,"fixed eye rest and waiting do not count");
    eye.resume(210000); timing.lock(211000); timing.advance(214000); timing.suspend(214000);
    timing.wake(220000); timing.unlock(221000); eye.resume(221000); timing.advance(222000);
    check(usage.total(day)==63000,"lock and overlapping sleep do not count");
    health.endWork();
    check(usage.total(day)==63000,"resetting reminder counters preserves daily total");
    timing.advance(eye.deadline+15000);
    check(usage.total(day)==63000+BreakTimer::WorkMs-1000,"delayed callback counts only to work deadline");
    wchar_t directory[MAX_PATH]{},path[MAX_PATH]{};
    GetTempPathW(MAX_PATH,directory); GetTempFileNameW(directory,L"eus",0,path); DeleteFileW(path);
    check(saveUsage(path,usage),"usage file saves atomically");
    DailyUsage restored; check(loadUsage(path,restored)&&restored.days==usage.days,"daily history survives restart");
    { std::ofstream file{std::filesystem::path(path)}; file<<"damaged file"; }
    const auto saved=restored.days;
    check(!loadUsage(path,restored) && restored.days==saved,"malformed file does not erase loaded history");
    DeleteFileW(path);
    check(usageDate(day).size()==10,"history has readable calendar dates");
    std::cout<<"All daily usage checks passed.\n";
}
