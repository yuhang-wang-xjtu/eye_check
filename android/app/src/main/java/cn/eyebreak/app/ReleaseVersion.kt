package cn.eyebreak.app

import java.net.URI

/** Only stable, numeric releases from our own repository are install candidates. */
object ReleaseVersion {
    const val REPOSITORY = "https://github.com/yuhang-wang-xjtu/eye_check"
    const val API = "https://api.github.com/repos/yuhang-wang-xjtu/eye_check/releases/latest"
    fun parts(value: String): List<Int>? {
        if (!Regex("v?[0-9]{1,6}\\.[0-9]{1,6}\\.[0-9]{1,6}").matches(value)) return null
        return value.removePrefix("v").split('.').map { it.toInt() }
    }
    fun newer(candidate: String, current: String): Boolean {
        val a = parts(candidate) ?: return false
        val b = parts(current.substringBefore('-')) ?: return false
        return a.zip(b).firstOrNull { it.first != it.second }?.let { it.first > it.second } ?: false
    }
    fun assetName(tag: String) = "EyeBreak-Android-$tag.apk"
    fun validAsset(url: String, tag: String): Boolean = try {
        parts(tag) != null && tag.startsWith("v") && URI(url).let {
            it.scheme == "https" && it.host == "github.com" && it.port == -1 && it.rawUserInfo == null &&
                it.rawQuery == null && it.rawFragment == null &&
                it.rawPath == "/yuhang-wang-xjtu/eye_check/releases/download/$tag/${assetName(tag)}"
        }
    } catch (_: Exception) { false }
    fun digest(value: String): String? = value.takeIf { Regex("sha256:[0-9a-fA-F]{64}").matches(it) }?.substring(7)?.lowercase()
}
