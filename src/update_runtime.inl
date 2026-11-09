struct UpdateJob { bool ok=false; Updates::Release release; std::wstring message; };
HWND updateWindow=nullptr;
std::future<UpdateJob> updateFuture;
Updates::Release updateRelease;
std::wstring updateStatus=L"当前版本 "+SharedSync::wide(Updates::Version)+L"；点击检查 GitHub 正式发布版。";
void updateView() {
    if(!updateWindow) return;
    SetDlgItemTextW(updateWindow,IDC_UPDATE_STATUS,updateStatus.c_str());
    SetDlgItemTextW(updateWindow,IDC_UPDATE_ACTION,!updateRelease.path.empty()?L"打开版本文件夹":updateRelease.newer?L"下载新版":L"检查更新");
    EnableWindow(GetDlgItem(updateWindow,IDC_UPDATE_ACTION),!updateFuture.valid());
}
void runUpdateJob(bool download) {
    if(updateFuture.valid()) return;
    updateStatus=download?L"正在下载并校验 SHA-256…":L"正在检查 GitHub 正式发布版…";
    const auto release=updateRelease; const auto directory=settingsDirectory;
    updateFuture=std::async(std::launch::async,[download,release,directory] {
        UpdateJob result;
        try {
            result.release=download?Updates::download(release,directory):Updates::check(); result.ok=true;
            result.message=download?L"下载及校验完成。打开版本文件夹，解压后退出旧版再运行新版。":
                L"当前 "+SharedSync::wide(Updates::Version)+L"；GitHub 最新 "+SharedSync::wide(result.release.tag)+(result.release.newer?L"，可下载更新。":L"，没有更新的正式版。");
        } catch(...) { result.message=L"更新检查或下载失败，请检查网络；可打开 GitHub 发布页手动下载。"; }
        return result;
    });
    updateView();
}
INT_PTR CALLBACK updateProc(HWND d,UINT msg,WPARAM w,LPARAM l) {
    if(Dpi::dialogScrollMessage(d,msg,w,l)) return TRUE;
    if(msg==WM_INITDIALOG || msg==RefreshDialogDpi) {
        updateWindow=d; Theme::apply(d); Dpi::fitDialog(d); updateView(); if(msg==WM_INITDIALOG) SetTimer(d,1,150,nullptr); return TRUE;
    }
    if(msg==WM_DPICHANGED) { PostMessageW(d,RefreshDialogDpi,0,0); return FALSE; }
    if(const auto result=Theme::dialogMessage(d,msg,w,l)) return result;
    if(msg==WM_TIMER && updateFuture.valid() && updateFuture.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
        auto result=updateFuture.get(); if(result.ok) updateRelease=std::move(result.release); updateStatus=std::move(result.message); updateView(); return TRUE;
    }
    if(msg==WM_COMMAND) {
        if(LOWORD(w)==IDC_UPDATE_ACTION) {
            if(!updateRelease.path.empty()) ShellExecuteW(d,L"open",std::filesystem::path(updateRelease.path).parent_path().c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            else runUpdateJob(updateRelease.newer); return TRUE;
        }
        if(LOWORD(w)==IDC_UPDATE_PAGE) { ShellExecuteW(d,L"open",Updates::Page,nullptr,nullptr,SW_SHOWNORMAL); return TRUE; }
        if(LOWORD(w)==IDCANCEL) { EndDialog(d,0); return TRUE; }
    }
    if(msg==WM_CLOSE) { EndDialog(d,0); return TRUE; }
    if(msg==WM_DESTROY) { KillTimer(d,1); updateWindow=nullptr; }
    return FALSE;
}
void showUpdates() {
    if(updateWindow) { SetForegroundWindow(updateWindow); return; }
    DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_UPDATE),window,updateProc,0);
}
