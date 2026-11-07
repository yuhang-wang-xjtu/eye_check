// Included in the main window namespace. Only the request function runs off-thread.
SharedSync::Config syncConfig;
SharedSync::Snapshot syncSnapshot;
std::future<SharedSync::Result> syncFuture;
std::uint64_t syncGeneration=0,requestGeneration=0,syncReceived=0,syncNextPoll=0;
bool syncActive=false,syncOnline=false,syncHasSnapshot=false;
bool pendingUnavailable=false,requestAvailable=false,requestObservation=false;
std::uint64_t availabilityEpoch=0,requestAvailabilityEpoch=0,syncInactiveAt=0;
std::wstring syncStatus=L"本地模式";
HWND syncWindow=nullptr;
BreakTimer localTimer(0);
HealthReminders localHealth(0);
AppSettings localSettings;
std::uint64_t localRemaining=0;
bool syncBusy() { return syncFuture.valid(); }
bool syncCanCommand() { return syncActive && syncOnline && syncHasSnapshot && syncSnapshot.available && !pendingUnavailable && !sessionTiming.blocked() && !syncBusy(); }
void syncAvailabilityChanged() {
    if(!syncActive) return;
    if(sessionTiming.blocked()) {
        if(!pendingUnavailable) syncInactiveAt=GetTickCount64();
        pendingUnavailable=true; ++availabilityEpoch;
    }
    syncNextPoll=0;
}
void updateSyncStatus() {
    if(syncWindow) SetDlgItemTextW(syncWindow,IDC_SYNC_STATUS,syncStatus.c_str());
}
void projectSync(std::uint64_t now) {
    if(!syncHasSnapshot) return;
    auto elapsed=now>=syncReceived?now-syncReceived:0;
    timer.state=syncSnapshot.state;
    timer.remaining=syncSnapshot.remaining;
    timer.restDurationMs=static_cast<std::uint64_t>(settings.restSeconds)*1000;
    const auto left=syncSnapshot.deadline>syncSnapshot.serverNow?syncSnapshot.deadline-syncSnapshot.serverNow:0;
    timer.deadline=now+(left>elapsed?left-elapsed:0);
    timer.awayStarted=now;
    health.sittingMs=syncSnapshot.sitting; health.workMs=syncSnapshot.work;
    // Disconnected snapshots stop projecting recovery/health after a short grace.
    const auto projected=syncOnline?elapsed:std::min<std::uint64_t>(elapsed,5000);
    if(timer.state==BreakTimer::State::Away) {
        const auto recovery=std::min<std::uint64_t>(projected,BreakTimer::WorkMs)*5;
        timer.remaining=std::min(BreakTimer::WorkMs,timer.remaining+recovery);
        health.recover(recovery,settings);
    } else if(timer.state==BreakTimer::State::Working) {
        const auto active=std::min(projected,left);
        health.sittingMs+=active; health.workMs+=active;
        if(sessionTiming.blocked() || pendingUnavailable) {
            const auto before=syncInactiveAt>syncReceived?std::min(syncInactiveAt-syncReceived,left):0;
            const auto recovery=now>syncInactiveAt?std::min<std::uint64_t>(now-syncInactiveAt,BreakTimer::WorkMs)*5:0;
            timer.state=BreakTimer::State::Away; timer.remaining=std::min(BreakTimer::WorkMs,left-before+recovery);
            health.sittingMs=syncSnapshot.sitting+before; health.workMs=syncSnapshot.work+before; health.recover(recovery,settings);
        }
    }
}
const DailyUsage& visibleUsage() { return syncActive?syncSnapshot.usage:dailyUsage; }
std::uint64_t visibleDay() {
    if(!syncActive || !syncHasSnapshot) return localCalendarMs()/DailyUsage::DayMs;
    // FILETIME epoch offset; server contract uses Asia/Shanghai calendar dates.
    return (11644473600000ULL+syncSnapshot.serverNow+(GetTickCount64()-syncReceived)+8*3600000ULL)/DailyUsage::DayMs;
}
void enterShared() {
    if(syncActive) return;
    const auto now=GetTickCount64(); sessionTiming.advance(now); persistUsage();
    localTimer=timer; localHealth=health; localSettings=settings; localRemaining=timer.left(now);
    syncActive=true; sessionTiming.externalAuthority=true; pendingUnavailable=false;
    if(sessionTiming.blocked()) { pendingUnavailable=true; syncInactiveAt=now; ++availabilityEpoch; }
    timer.state=BreakTimer::State::Paused; syncHasSnapshot=false;
    if(healthWindow) DestroyWindow(healthWindow);
    releaseReminderWindow();
    if(window) refresh();
}
void leaveShared() {
    if(syncActive) {
        const auto now=GetTickCount64(); sessionTiming.advance(now);
        timer=localTimer; health=localHealth; settings=localSettings;
        timer.remaining=localRemaining; timer.deadline=now+localRemaining; timer.awayStarted=now;
        sessionTiming.resumeLocal(now);
    }
    syncActive=false; syncOnline=false; syncHasSnapshot=false; ++syncGeneration;
    syncConfig.enabled=false; SharedSync::saveConfig(settingsDirectory,syncConfig);
    inputBoundary=GetTickCount(); awayInputArmed=false;
    syncStatus=L"本地模式；本地计时已恢复"; updateSyncStatus(); refresh();
}
bool launchSync(const SharedSync::Json& command=SharedSync::Json()) {
    if(syncBusy() || !syncActive) return false;
    requestGeneration=syncGeneration;
    const auto config=syncConfig;
    auto body=command;
    requestObservation=body.is_null(); requestAvailable=!pendingUnavailable && !sessionTiming.blocked(); requestAvailabilityEpoch=availabilityEpoch;
    if(requestObservation) { body=SharedSync::makeCommand("observe",syncHasSnapshot?syncSnapshot.revision:0); body["available"]=requestAvailable; body["platform"]="windows"; }
    try { syncFuture=std::async(std::launch::async,[config,body]{return SharedSync::request(config,body);}); }
    catch(...) { syncOnline=false; syncStatus=L"无法启动同步任务"; updateSyncStatus(); return false; }
    if(window) refresh();
    return true;
}
bool sendSync(const std::string& action,const AppSettings* draft=nullptr) {
    if(!syncCanCommand()) return false;
    return launchSync(SharedSync::makeCommand(action,syncSnapshot.revision,draft));
}
void pumpSync(std::uint64_t now) {
    if(syncBusy() && syncFuture.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
        auto result=syncFuture.get();
        if(requestGeneration==syncGeneration && syncActive) {
            syncOnline=result.ok;
            if(result.ok) {
                if(requestObservation && !requestAvailable && requestAvailabilityEpoch==availabilityEpoch) pendingUnavailable=false;
                const bool had=syncHasSnapshot;
                const auto old=syncSnapshot;
                syncSnapshot=std::move(result.snapshot); syncReceived=result.received; syncHasSnapshot=true;
                SharedSync::sharedFields(settings,syncSnapshot.settings);
                projectSync(now);
                const bool stateChanged=!had || old.state!=timer.state;
                if(stateChanged && (timer.state==BreakTimer::State::Waiting || timer.state==BreakTimer::State::Away)) {
                    inputBoundary=GetTickCount(); awayInputArmed=false; deviceInput.resetMotion();
                }
                health.sittingSent=syncSnapshot.sittingSent; health.workSent=syncSnapshot.workSent;
                if(!sessionTiming.blocked()) {
                    if(stateChanged && timer.state==BreakTimer::State::Resting) beginRest(true);
                    const bool finished=had && ((old.state==BreakTimer::State::Resting && old.deadline<=syncSnapshot.serverNow && timer.state!=BreakTimer::State::Resting && timer.state!=BreakTimer::State::Away && timer.state!=BreakTimer::State::Paused) ||
                        (old.state==BreakTimer::State::Working && timer.state==BreakTimer::State::Waiting));
                    if(finished) restCompleted();
                    if(had) {
                        int events=0;
                        if(syncSnapshot.sittingSent && !old.sittingSent) events|=HealthReminders::Sitting;
                        if(syncSnapshot.workSent && !old.workSent) events|=HealthReminders::LongWork;
                        if(!syncSnapshot.night.empty() && syncSnapshot.night!=old.night) events|=HealthReminders::LateNight;
                        if(events) showHealthReminder(events,true);
                    }
                }
                if(stateChanged && timer.state==BreakTimer::State::Working) releaseReminderWindow();
                syncStatus=result.status==409?L"本机状态已更新，请重试操作":L"已连接 · 本机独立计时，设置与统计同步";
            } else syncStatus=result.error+L"；稍后自动重连";
            syncNextPoll=result.ok && (pendingUnavailable || syncSnapshot.available!=!sessionTiming.blocked())?0:now+5000; updateSyncStatus(); refresh();
        }
    }
    if(!syncActive) return;
    projectSync(now);
    if(!syncBusy() && (now>=syncNextPoll || (syncHasSnapshot && syncOnline &&
        (timer.state==BreakTimer::State::Working || timer.state==BreakTimer::State::Resting) && timer.left(now)==0))) {
        syncNextPoll=now+5000; launchSync();
    }
}
std::wstring syncText(HWND d,int id) {
    const auto length=GetWindowTextLengthW(GetDlgItem(d,id)); std::wstring s(length+1,0);
    GetDlgItemTextW(d,id,s.data(),length+1); s.resize(length); return s;
}
INT_PTR CALLBACK syncProc(HWND d,UINT msg,WPARAM w,LPARAM l) {
    if(Dpi::dialogScrollMessage(d,msg,w,l)) return TRUE;
    if(msg==WM_INITDIALOG || msg==RefreshDialogDpi) {
        syncWindow=d; Theme::apply(d); Dpi::fitDialog(d);
        if(msg==WM_INITDIALOG) {
            SetDlgItemTextW(d,IDC_SYNC_URL,syncConfig.url.c_str()); SetDlgItemTextW(d,IDC_SYNC_ROOM,syncConfig.room.c_str());
            SendDlgItemMessageW(d,IDC_SYNC_URL,EM_SETLIMITTEXT,2048,0); SendDlgItemMessageW(d,IDC_SYNC_ROOM,EM_SETLIMITTEXT,80,0);
            SendDlgItemMessageW(d,IDC_SYNC_TOKEN,EM_SETLIMITTEXT,4096,0); updateSyncStatus();
        }
        return TRUE;
    }
    if(msg==WM_DPICHANGED) { PostMessageW(d,RefreshDialogDpi,0,0); return FALSE; }
    if(const auto r=Theme::dialogMessage(d,msg,w,l)) return r;
    if(msg==WM_COMMAND) {
        switch(LOWORD(w)) {
        case IDC_SYNC_CONNECT: {
            if(syncBusy()) { SetDlgItemTextW(d,IDC_SYNC_STATUS,L"正在连接，请稍后重试"); return TRUE; }
            SharedSync::Config draft{syncText(d,IDC_SYNC_URL),syncText(d,IDC_SYNC_ROOM),syncText(d,IDC_SYNC_TOKEN),true};
            draft.device=syncConfig.device.empty()?SharedSync::newDeviceId():syncConfig.device;
            if(draft.token.empty()) draft.token=syncConfig.token;
            if(!SharedSync::valid(draft)) { SetDlgItemTextW(d,IDC_SYNC_STATUS,L"请检查 HTTPS 地址、8–80 位会话 ID 和令牌"); return TRUE; }
            if(!SharedSync::saveConfig(settingsDirectory,draft)) { SetDlgItemTextW(d,IDC_SYNC_STATUS,L"无法安全保存同步配置"); return TRUE; }
            enterShared(); syncConfig=std::move(draft); ++syncGeneration; syncHasSnapshot=false; syncOnline=false;
            SetDlgItemTextW(d,IDC_SYNC_TOKEN,L""); syncStatus=L"连接中…"; updateSyncStatus(); launchSync(); refresh(); return TRUE;
        }
        case IDC_SYNC_LOCAL: leaveShared(); return TRUE;
        case IDC_SYNC_IMPORT: {
            wchar_t path[MAX_PATH]{}; OPENFILENAMEW pick{sizeof(pick)}; pick.hwndOwner=d; pick.lpstrFile=path; pick.nMaxFile=MAX_PATH;
            pick.lpstrFilter=L"连接配置 (*.txt)\0*.txt\0所有文件\0*.*\0"; pick.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
            if(GetOpenFileNameW(&pick)) {
                std::ifstream f(std::filesystem::path(path),std::ios::binary); std::string line; std::size_t size=0;
                while(std::getline(f,line) && (size+=line.size())<16384) {
                    if(!line.empty() && line.back()=='\r') line.pop_back();
                    for(const auto pair : {std::pair<const char*,int>{"Server: ",IDC_SYNC_URL},{"Session: ",IDC_SYNC_ROOM},{"Token: ",IDC_SYNC_TOKEN}})
                        if(line.rfind(pair.first,0)==0) SetDlgItemTextW(d,pair.second,SharedSync::wide(line.substr(strlen(pair.first))).c_str());
                }
                SetDlgItemTextW(d,IDC_SYNC_STATUS,L"已填入配置；点击连接并保存后生效");
            }
            return TRUE;
        }
        case IDCANCEL: EndDialog(d,0); return TRUE;
        }
    }
    if(msg==WM_CLOSE) { EndDialog(d,0); return TRUE; }
    if(msg==WM_DESTROY) syncWindow=nullptr;
    return FALSE;
}
void showSync() { DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_SYNC),window,syncProc,0); }
