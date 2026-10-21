package cn.eyebreak.app

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.AtomicFile
import java.io.File
import java.io.ObjectInputStream
import java.io.ObjectOutputStream
import java.util.concurrent.Executors

class Repository private constructor(val context: Context) {
    val sync = SyncClient(context)
    private val localFile = AtomicFile(File(context.filesDir, "session.bin"))
    private val sharedFile = AtomicFile(File(context.filesDir, "shared-session.bin"))
    private fun activeFile() = if(sync.enabled) sharedFile else localFile
    private fun read(file: AtomicFile): Session = try { file.openRead().use { ObjectInputStream(it).readObject() as Session } } catch (_: Exception) { Session() }
    var session: Session = read(activeFile())
        private set
    var syncStatus = if (sync.enabled) "等待连接服务器" else "本地模式"
        private set
    var linked = false
        private set
    var busy = false
        private set
    var running = false
    var foreground = false
    var lastError = ""
    private var baseWall = System.currentTimeMillis()
    private var baseElapsed = SystemClock.elapsedRealtime()
    private var lastSave = 0L
    private val executor = Executors.newSingleThreadExecutor()
    private val main = Handler(Looper.getMainLooper())
    val listeners = mutableSetOf<() -> Unit>()
    val eventListeners = mutableSetOf<(List<String>) -> Unit>()
    private fun events(value: List<String>) {
        value.forEach { event ->
            try { Alerts.show(context, event, session) }
            catch (e: RuntimeException) { android.util.Log.e("EyeBreakService", "Reminder delivery failed", e) }
        }
        eventListeners.toList().forEach { listener ->
            try { listener(value) }
            catch (e: RuntimeException) { android.util.Log.w("EyeBreakService", "Reminder view unavailable", e) }
        }
    }
    private var lockPause = false
    private var localLocked = false
    @Volatile private var syncGeneration = 0
    fun now(atElapsed: Long = SystemClock.elapsedRealtime()) = baseWall + atElapsed - baseElapsed
    fun emit() {
        ReminderPopup.reconcile(session)
        listeners.toList().forEach { listener ->
            try { listener() }
            catch (e: RuntimeException) { android.util.Log.e("EyeBreakService", "State view refresh failed", e) }
        }
    }
    fun save() {
        val file = activeFile()
        var stream: java.io.FileOutputStream? = null
        try {
            stream = file.startWrite()
            val output = ObjectOutputStream(stream); output.writeObject(session); output.flush()
            file.finishWrite(stream); lastError = ""
        } catch (_: Exception) { if (stream != null) file.failWrite(stream); lastError = "本地记录尚未保存" }
    }
    fun tick() {
        val events = session.tick(now())
        if (events.isNotEmpty()) { save(); events(events) }
        if (SystemClock.elapsedRealtime() - lastSave > 15000) { save(); lastSave = SystemClock.elapsedRealtime() }
        emit()
    }
    fun command(action: String, completed: (() -> Unit)? = null) {
        if (sync.enabled) {
            if (!linked || busy) { syncStatus = "尚未连接；操作未提交，可重试连接或切换本地模式"; emit(); completed?.invoke(); return }
            contact(action, completed = completed); return
        }
        tick() // Deliver any deadline event before a button action settles the model.
        session.command(action, now()); save(); emit(); completed?.invoke()
    }
    fun activity() { if (session.phase == Phase.WAITING || session.phase == Phase.AWAY) command("continue") }
    fun testReminder() { events(listOf("TEST")) }
    fun changeOptions(options: Options) {
        if (sync.enabled) {
            if (!linked || busy) { syncStatus = "请先连接服务器再保存共享提醒设置"; emit(); return }
            session.options = session.options.copy(lightMode = options.lightMode, overlay = options.overlay, soundEnabled = options.soundEnabled, soundUri = options.soundUri)
            save()
            contact("settings", options)
        } else { tick(); session.options = options; save(); emit() }
    }
    fun screenOff() {
        // Locking one phone must not pause a shared session active on the PC.
        if (sync.enabled || localLocked) return
        localLocked = true
        lockPause = session.phase == Phase.PAUSED
        session.command("rest", now()); save(); emit()
    }
    fun unlocked() {
        if (sync.enabled) { contact(); return }
        if (!localLocked) return
        session.tick(now())
        if (lockPause) session.phase = Phase.PAUSED else session.command("continue", now())
        lockPause = false; localLocked = false; save(); emit()
    }
    fun connect() {
        if(!sync.enabled) { session.command("pause",now()); save() }
        syncGeneration++; busy=false
        sync.enable(true); linked=false; contact()
    }
    fun disconnect() {
        syncGeneration++; busy=false
        if(sync.enabled) { save(); sync.enable(false); baseWall=System.currentTimeMillis(); baseElapsed=SystemClock.elapsedRealtime(); session=read(localFile) }
        linked = false; syncStatus = "本地模式（本地历史独立保留，不覆盖服务器）"; save(); emit()
    }
    fun contact(action: String? = null, options: Options? = null, completed: (() -> Unit)? = null) {
        if (busy || !sync.enabled) { completed?.invoke(); return }
        busy = true
        val generation = syncGeneration
        val revision = session.revision
        executor.execute {
            if(generation != syncGeneration) { main.post { completed?.invoke() }; return@execute }
            try {
                val reply = if (action == null) sync.fetch() else sync.send(action, revision, options)
                main.post {
                    if(generation != syncGeneration || !sync.enabled) { completed?.invoke(); return@post }
                    try {
                        val previous = session
                        session = SessionJson.decode(reply.snapshot, options ?: session.options)
                        baseWall = reply.serverNow; baseElapsed = reply.receivedElapsed
                        val events = mutableListOf<String>()
                        if(previous.phase != session.phase && session.phase == Phase.RESTING) events += "REST_START"
                        if(previous.phase != session.phase && session.phase == Phase.WAITING) events += "REST_END"
                        if(!previous.sittingSent && session.sittingSent) events += "SITTING"
                        if(!previous.workSent && session.workSent) events += "LONG_WORK"
                        if(previous.lastNight != session.lastNight && session.lastNight.isNotEmpty()) events += "LATE_NIGHT"
                        if(events.isNotEmpty()) events(events)
                        linked = true; syncStatus = if (reply.conflict) "其他设备已更改状态，已刷新；请重新操作" else "已同步 · 共享会话 ${sync.room}"
                        save()
                    } catch (_: Exception) { linked = false; syncStatus = "服务器数据格式不匹配" }
                    busy = false; emit(); completed?.invoke()
                }
            } catch (e: Exception) {
                main.post {
                    if(generation == syncGeneration && sync.enabled) { linked = false; busy = false; syncStatus = e.message?.take(160) ?: "连接失败"; emit() }
                    completed?.invoke()
                }
            }
        }
    }
    companion object {
        // Only applicationContext is retained; no Activity/View is captured.
        @android.annotation.SuppressLint("StaticFieldLeak")
        @Volatile private var instance: Repository? = null
        fun get(context: Context): Repository = instance ?: synchronized(this) { instance ?: Repository(context.applicationContext).also { instance = it } }
    }
}
