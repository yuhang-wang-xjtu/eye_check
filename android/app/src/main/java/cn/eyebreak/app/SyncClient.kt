package cn.eyebreak.app

import android.content.Context
import android.os.SystemClock
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URI
import java.net.URL
import java.security.KeyStore
import java.util.UUID
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

data class SyncReply(val snapshot: JSONObject, val serverNow: Long, val conflict: Boolean, val receivedElapsed: Long)
class SyncClient(context: Context) {
    private val prefs = context.getSharedPreferences("sync", Context.MODE_PRIVATE)
    var baseUrl: String
        get() = prefs.getString("url", "") ?: ""
        set(value) { prefs.edit().putString("url", value.trimEnd('/')).apply() }
    var room: String
        get() = prefs.getString("room", "") ?: ""
        set(value) { require(value.matches(Regex("[a-zA-Z0-9_-]{8,80}"))); prefs.edit().putString("room", value).apply() }
    val enabled get() = prefs.getBoolean("enabled", false)
    fun enable(value: Boolean) { prefs.edit().putBoolean("enabled", value).apply() }
    private fun key(): SecretKey {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        return (store.getKey("eyebreak-sync", null) as? SecretKey) ?: KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").run {
            init(KeyGenParameterSpec.Builder("eyebreak-sync", KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
            generateKey()
        }
    }
    fun setToken(token: String) {
        require(token.length in 16..4096)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding").apply { init(Cipher.ENCRYPT_MODE, key()) }
        prefs.edit().putString("token", Base64.encodeToString(cipher.doFinal(token.toByteArray()), Base64.NO_WRAP))
            .putString("iv", Base64.encodeToString(cipher.iv, Base64.NO_WRAP)).apply()
    }
    fun hasToken() = prefs.contains("token")
    private fun token(): String {
        val cipher = Cipher.getInstance("AES/GCM/NoPadding").apply {
            init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, Base64.decode(prefs.getString("iv", ""), Base64.NO_WRAP)))
        }
        return String(cipher.doFinal(Base64.decode(prefs.getString("token", ""), Base64.NO_WRAP)))
    }
    fun validateUrl(value: String) {
        val uri = URI(value)
        require(uri.userInfo == null && uri.rawQuery == null && uri.rawFragment == null && !uri.host.isNullOrEmpty()) { "请输入服务器根地址" }
        require(uri.scheme == "https" || (BuildConfig.DEBUG && uri.scheme == "http" && uri.host in listOf("10.0.2.2", "127.0.0.1", "localhost"))) {
            "远程同步需要 HTTPS；HTTP 仅允许本地开发服务器"
        }
    }
    fun fetch(): SyncReply = request(null)
    fun send(action: String, revision: Long, options: Options? = null): SyncReply {
        val body = JSONObject().put("action", action).put("expectedRevision", revision).put("operationId", UUID.randomUUID().toString())
        if (options != null) body.put("settings", SessionJson.sharedOptions(options))
        return request(body)
    }
    private fun request(body: JSONObject?): SyncReply {
        validateUrl(baseUrl)
        require(room.matches(Regex("[a-zA-Z0-9_-]{8,80}"))) { "会话标识需为 8–80 位字母、数字或横线" }
        val suffix = if (body == null) "" else "/commands"
        val connection = URL("$baseUrl/v1/sessions/$room$suffix").openConnection() as HttpURLConnection
        try {
            connection.connectTimeout = 2500; connection.readTimeout = 3500; connection.instanceFollowRedirects = false
            connection.setRequestProperty("Authorization", "Bearer ${token()}")
            connection.setRequestProperty("Accept", "application/json")
            if (body != null) {
                connection.requestMethod = "POST"; connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json")
                connection.outputStream.use { it.write(body.toString().toByteArray(Charsets.UTF_8)) }
            }
            val status = connection.responseCode
            require(status == 200 || status == 409) { "服务器返回 $status；请检查部署和访问令牌" }
            val stream = if (status == 409) connection.errorStream else connection.inputStream
            val bytes = stream.use { input ->
                val output = java.io.ByteArrayOutputStream(); val buffer = ByteArray(8192)
                while (output.size() <= 1024 * 1024) {
                    val count = input.read(buffer); if (count < 0) break
                    output.write(buffer, 0, count)
                }; output.toByteArray()
            }
            require(bytes.size <= 1024 * 1024) { "服务器响应过大" }
            val result = JSONObject(String(bytes, Charsets.UTF_8))
            return SyncReply(result.getJSONObject("snapshot"), result.getLong("serverNowMs"), status == 409, SystemClock.elapsedRealtime())
        } finally { connection.disconnect() }
    }
}
