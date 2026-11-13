constexpr UINT UpdateExitMessage=WM_APP+52;
std::unique_ptr<Updates::PreparedInstall> pendingInstall;
struct UpdateStartup { Updates::Path state; std::wstring startedEvent; bool restored=false,visible=true; int x=0,y=0; } updateStartup;
void saveUpdateState(const Updates::Path& file) {
    const auto now=GetTickCount64();sessionTiming.advance(now);persistUsage();
    const auto& eye=syncActive?localTimer:timer;const auto& counters=syncActive?localHealth:health;
    RECT position{};if(window)GetWindowRect(window,&position);
    Updates::Json j={{"format",1},{"state",static_cast<int>(eye.state)},{"remaining",syncActive?localRemaining:eye.left(now)},
        {"sitting",counters.sittingMs},{"work",counters.workMs},{"sittingSent",counters.sittingSent},{"workSent",counters.workSent},{"lastNight",counters.lastNight},
        {"locked",sessionTiming.locked},{"sleeping",sessionTiming.sleeping},{"visible",window && IsWindowVisible(window)!=FALSE},{"x",position.left},{"y",position.top}};
    Updates::writeJson(file,j);
}
void restoreUpdateState() {
    if(updateStartup.state.empty())return;
    const auto j=Updates::readJson(updateStartup.state);if(j.at("format").get<int>()!=1)throw std::runtime_error("update state format");
    const auto value=SharedSync::integer(j,"state",4);const auto remaining=SharedSync::integer(j,"remaining",BreakTimer::WorkMs);const auto now=GetTickCount64();
    timer.state=static_cast<BreakTimer::State>(value);timer.remaining=remaining;timer.deadline=now+remaining;timer.awayStarted=now;
    health.sittingMs=SharedSync::integer(j,"sitting",100ULL*365*DailyUsage::DayMs);health.workMs=SharedSync::integer(j,"work",100ULL*365*DailyUsage::DayMs);
    health.sittingSent=j.at("sittingSent").get<bool>();health.workSent=j.at("workSent").get<bool>();health.lastNight=j.at("lastNight").get<std::int64_t>();
    sessionTiming.locked=j.at("locked").get<bool>();sessionTiming.sleeping=j.at("sleeping").get<bool>();sessionTiming.resumeLocal(now);
    inputBoundary=GetTickCount();awayInputArmed=false;deviceInput.reset();
    updateStartup.visible=j.at("visible").get<bool>();updateStartup.x=j.at("x").get<int>();updateStartup.y=j.at("y").get<int>();updateStartup.restored=true;
}
