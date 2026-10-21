package cn.eyebreak.app

import android.app.*
import android.content.*
import android.content.pm.PackageManager
import android.net.Uri
import android.os.*
import android.provider.Settings
import android.widget.Toast
import org.json.JSONObject
import java.io.File
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/** Explicit checks only. DownloadManager owns the transfer; Android owns installation. */
class GitHubUpdater(private val activity: Activity) {
    private val prefs = activity.getSharedPreferences("github-update", Context.MODE_PRIVATE)
    private val downloads = activity.getSystemService(DownloadManager::class.java)
    private val handler = Handler(Looper.getMainLooper())
    private var checking = false
    private var verifying = false
    private var shown = -1L
    private var permissionPending = false
    private var started = false
    private val poll = object : Runnable {
        override fun run() { inspect(); if (started) handler.postDelayed(this, 1500) }
    }
    fun start() {
        val tag = prefs.getString("tag", "") ?: ""
        if (tag.isNotEmpty() && !ReleaseVersion.newer(tag, BuildConfig.VERSION_NAME)) {
            val old = prefs.getLong("id", -1)
            if (old != -1L) downloads.remove(old)
            prefs.edit().clear().apply()
        }
        started = true
        handler.post(poll)
    }
    fun stop() { started = false; handler.removeCallbacks(poll) }
    fun resume() {
        if (permissionPending) {
            permissionPending = false
            if (activity.packageManager.canRequestPackageInstalls()) install()
            else message("未允许安装更新；可再次点击检查更新重试")
        }
    }
    private fun alive() = !activity.isFinishing && !activity.isDestroyed
    private fun message(text: String) { if (alive()) Toast.makeText(activity, text, Toast.LENGTH_LONG).show() }
    private fun background(work: () -> Unit) = Thread(work, "github-update").start()
    fun check() {
        if (checking || verifying) { message("正在检查，请稍候"); return }
        checking = true
        message("正在检查 GitHub 最新版本…")
        background {
            try {
                val connection = URL(ReleaseVersion.API).openConnection() as HttpURLConnection
                val release = try {
                    connection.connectTimeout = 10000; connection.readTimeout = 15000
                    connection.instanceFollowRedirects = false
                    connection.setRequestProperty("Accept", "application/vnd.github+json")
                    connection.setRequestProperty("User-Agent", "EyeBreak/${BuildConfig.VERSION_NAME}")
                    connection.setRequestProperty("X-GitHub-Api-Version", "2022-11-28")
                    val code = connection.responseCode
                    check(code == 200) { when(code) {
                        404 -> "仓库尚未发布正式版本"
                        403, 429 -> "GitHub 请求受限，请稍后再试"
                        else -> "GitHub 检查失败（HTTP $code）"
                    } }
                    val bytes = connection.inputStream.use { stream ->
                        val output = ByteArrayOutputStream()
                        val buffer = ByteArray(8192)
                        while (true) {
                            val count = stream.read(buffer); if (count < 0) break
                            check(output.size() + count <= 1024 * 1024) { "版本信息过大" }
                            output.write(buffer, 0, count)
                        }
                        output.toByteArray()
                    }
                    JSONObject(String(bytes, Charsets.UTF_8))
                } finally { connection.disconnect() }
                check(!release.optBoolean("draft") && !release.optBoolean("prerelease")) { "未找到正式版本" }
                val tag = release.getString("tag_name")
                check(ReleaseVersion.parts(tag) != null) { "无法识别版本号：$tag" }
                if (!ReleaseVersion.newer(tag, BuildConfig.VERSION_NAME)) {
                    handler.post { checking = false; message("当前已是最新版本（${BuildConfig.VERSION_NAME}）") }
                } else {
                    val assets = release.getJSONArray("assets")
                    val asset = (0 until assets.length()).map { assets.getJSONObject(it) }
                        .firstOrNull { it.optString("name") == ReleaseVersion.assetName(tag) }
                        ?: error("该版本尚未提供 Android 安装包")
                    val url = asset.getString("browser_download_url")
                    check(ReleaseVersion.validAsset(url, tag)) { "安装包地址无效" }
                    val sha = ReleaseVersion.digest(asset.optString("digest")) ?: error("安装包缺少 SHA-256 校验信息")
                    val size = asset.getLong("size")
                    check(size in 1..100L * 1024 * 1024) { "安装包大小无效" }
                    val notes = release.optString("body").take(4000)
                    handler.post {
                        checking = false
                        if (alive()) AlertDialog.Builder(activity).setTitle("发现新版本 $tag")
                            .setMessage("当前版本 ${BuildConfig.VERSION_NAME}\n下载约 ${size / 1024} KB；安装保留本机设置和记录。\n\n$notes")
                            .setPositiveButton("下载更新") { _, _ -> download(tag, url, sha, size) }
                            .setNegativeButton("稍后", null).show()
                    }
                }
            } catch (e: Exception) {
                handler.post { checking = false; message(e.message ?: "网络不可用，请稍后再试") }
            }
        }
    }
    private fun download(tag: String, url: String, sha: String, size: Long) {
        try {
            val old = prefs.getLong("id", -1)
            if (old != -1L && prefs.getString("sha", "") == sha) { shown = -1; inspect(); message("已在下载或等待安装，请查看系统下载通知"); return }
            if (old != -1L) downloads.remove(old)
            val file = File(activity.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS) ?: error("下载目录不可用"), "eyebreak-update.apk")
            check(!file.exists() || file.delete()) { "无法清理旧安装包" }
            val request = DownloadManager.Request(Uri.parse(url)).setTitle("20·20·20 $tag")
                .setDescription("下载完成后返回护眼 App 确认安装")
                .setMimeType("application/vnd.android.package-archive")
                .setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
                .setDestinationUri(Uri.fromFile(file))
            val id = downloads.enqueue(request)
            prefs.edit().putLong("id", id).putString("sha", sha).putString("tag", tag).putLong("size", size).apply()
            shown = -1; message("开始下载；下载完成后返回此应用安装")
        } catch (e: Exception) { message(e.message ?: "无法启动下载") }
    }
    private fun inspect() {
        val id = prefs.getLong("id", -1)
        if (id == -1L || shown == id || verifying || !started) return
        try {
            downloads.query(DownloadManager.Query().setFilterById(id)).use { cursor ->
                if (!cursor.moveToFirst()) { prefs.edit().clear().apply(); return }
                when (cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS))) {
                    DownloadManager.STATUS_FAILED -> {
                        downloads.remove(id); prefs.edit().clear().apply(); message("更新下载失败，请检查网络后重试")
                    }
                    DownloadManager.STATUS_SUCCESSFUL -> verify(id)
                }
            }
        } catch (e: Exception) { shown = id; message(e.message ?: "无法读取下载状态") }
    }
    @Suppress("DEPRECATION")
    private fun verify(id: Long) {
        verifying = true
        val expectedSha = prefs.getString("sha", "")
        val expectedTag = prefs.getString("tag", "")
        val expectedSize = prefs.getLong("size", -1)
        background {
            try {
                val file = File(activity.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS), "eyebreak-update.apk")
                check(file.length() == expectedSize) { "安装包大小不符，请重新下载" }
                val digest = MessageDigest.getInstance("SHA-256")
                file.inputStream().use { stream -> val buffer = ByteArray(16384); while (true) {
                    val count = stream.read(buffer); if (count < 0) break; digest.update(buffer, 0, count)
                } }
                val actual = digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
                check(actual == expectedSha) { "安装包 SHA-256 不符，请重新下载" }
                val pm = activity.packageManager
                val flags = if (Build.VERSION.SDK_INT >= 28) PackageManager.GET_SIGNING_CERTIFICATES else PackageManager.GET_SIGNATURES
                val apk = pm.getPackageArchiveInfo(file.absolutePath, flags) ?: error("无法解析安装包")
                val installed = pm.getPackageInfo(activity.packageName, flags)
                check(apk.packageName == activity.packageName && "v${apk.versionName}" == expectedTag) { "安装包身份或版本不符" }
                val newerCode = if (Build.VERSION.SDK_INT >= 28) apk.longVersionCode > installed.longVersionCode else apk.versionCode > installed.versionCode
                check(newerCode) { "安装包版本不高于当前版本" }
                fun signatures(info: android.content.pm.PackageInfo) =
                    if (Build.VERSION.SDK_INT >= 28) info.signingInfo?.apkContentsSigners?.toList() else info.signatures?.toList()
                val ours = signatures(installed)
                check(!ours.isNullOrEmpty() && ours.toSet() == signatures(apk)?.toSet()) { "安装包签名与当前应用不一致" }
                handler.post {
                    verifying = false
                    if (started && alive()) {
                        shown = id
                        AlertDialog.Builder(activity).setTitle("更新已下载并校验")
                            .setMessage("是否安装 $expectedTag？系统将请求安装确认，现有记录和设置会保留。")
                            .setPositiveButton("安装更新") { _, _ -> requestInstall() }.setNegativeButton("稍后", null).show()
                    }
                }
            } catch (e: Exception) {
                handler.post { verifying = false; shown = id; downloads.remove(id); prefs.edit().clear().apply(); message(e.message ?: "安装包校验失败") }
            }
        }
    }
    private fun requestInstall() {
        try {
            if (activity.packageManager.canRequestPackageInstalls()) install()
            else AlertDialog.Builder(activity).setTitle("允许安装应用更新")
                .setMessage("需要在系统设置中允许本应用安装更新，返回后打开安装确认界面。")
                .setPositiveButton("前往设置") { _, _ ->
                    try {
                        permissionPending = true
                        activity.startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${activity.packageName}")))
                    } catch (_: ActivityNotFoundException) { permissionPending = false; message("系统未提供安装权限设置") }
                }.setNegativeButton("取消", null).show()
        } catch (e: Exception) { message(e.message ?: "无法请求安装更新") }
    }
    private fun install() {
        try {
            val uri = downloads.getUriForDownloadedFile(prefs.getLong("id", -1)) ?: error("安装包已被移除，请重新下载")
            activity.startActivity(Intent(Intent.ACTION_VIEW).setDataAndType(uri, "application/vnd.android.package-archive")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION))
        } catch (e: Exception) { message(e.message ?: "无法打开系统安装器") }
    }
}
