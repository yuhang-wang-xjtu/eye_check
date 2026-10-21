# Android 原型 · 20·20·20

Kotlin + 原生 Activity/View。最低 Android 8.0，编译/目标 API 35，AGP 8.13.0、Kotlin 2.2.20、Gradle 8.14.3、JDK 17+。

## 功能

- 每 20 分钟工作后自动休息，默认 25 秒，可调整；结束后等待新操作或手动继续。
- 工作、暂停、自动休息、等待继续及手动休息回补状态；三项提醒按 5:1 回补。
- 久坐、连续工作、深夜提醒，默认或自定义提示音。
- 每日使用记录仅累计工作时间，休息回补不扣减历史。
- 深浅色主题、可选悬浮倒计时、前台提醒服务和系统通知。
- 通知使用系统秒级倒计时；状态与截止时间变化时校准。回补状态每秒更新。
- 可配置共享会话服务，访问令牌用 Android Keystore 加密保存。

## 构建

在 Android Studio 中打开此目录，使用兼容的 Gradle JDK（可选 Studio 自带 JBR），配置 Android SDK。也可以配置 JAVA_HOME 和 ANDROID_HOME 后执行：

```powershell
.\gradlew.bat assembleDebug testDebugUnitTest lintDebug
```

Linux/macOS 使用 ./gradlew。APK 位于 app/build/outputs/apk/debug/app-debug.apk，使用开发者调试签名。发布覆盖更新必须保持签名一致并提高 versionCode；不得提交签名密钥。

安装后点击“开始后台提醒”，授予通知权限；可在设置中打开精确定时和悬浮窗权限。

## GitHub 更新

设置页“检查 GitHub 更新”访问固定仓库 yuhang-wang-xjtu/eye_check 的正式 Release。仅在用户主动检查时请求，按数字比较版本，跳过草稿、预发布和降级；网络失败和限流会显示提示。

确认后由 DownloadManager 下载并显示进度。完成后返回应用确认安装；安装前核对 SHA-256、大小、包名、版本号和签名，再请求系统安装权限并打开安装器。覆盖安装保留设置和记录，卸载会删除记录。更新过程不占用提醒服务线程。

发布 tag 格式为 v主版本.次版本.修订号，附件名为 EyeBreak-Android-v主版本.次版本.修订号.apk。版本比较、下载地址限制及哈希格式有单元测试；完整安装交互仍需真机验证。

参考：[GitHub Releases API](https://docs.github.com/en/rest/releases/releases#get-the-latest-release)、[DownloadManager](https://developer.android.com/reference/android/app/DownloadManager)。

## 同步

不预置服务器地址或令牌。参考协议和服务见 [sync/](../sync/README.md)，需自行部署 HTTPS 后填写服务地址、共享会话标识及令牌。仅本地调试地址 localhost、127.0.0.1、模拟器宿主 10.0.2.2 允许 HTTP。

服务端统一结算一份工作时间，客户端每 5 秒同步，操作带版本号防止覆盖冲突。Windows 客户端尚未接入；旧统计不自动上传。断网时仅预测显示，操作需在线确认，失败命令不自动重放。

本地记录与共享缓存分开保存，切换共享会话不会覆盖本地历史；离线记录暂不合并至服务器。共享模式下手机锁屏不会自动暂停其他设备的会话，共享休息通过“立即休息”发出。

## 平台限制

普通 Android 应用不能检测其他 App 的触控或外设操作，只检测自身界面和悬浮窗操作，也可点击通知继续工作。后台使用通知，不强抢其他应用前台。

强制停止、未启动服务和厂商省电策略可能影响提醒；不申请开机自启。真实设备上的后台、省电、外设及安装兼容性仍需验证。
