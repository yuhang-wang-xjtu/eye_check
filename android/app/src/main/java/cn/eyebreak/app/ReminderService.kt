package cn.eyebreak.app

import android.app.*
import android.content.*
import android.graphics.Color
import android.graphics.PixelFormat
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.*
import android.provider.Settings
import android.view.*
import android.widget.TextView

object Alerts {
    private const val STATUS = "status"
    private fun manager(c: Context) = c.getSystemService(NotificationManager::class.java)
    fun open(c: Context, event: String? = null) = PendingIntent.getActivity(c, event?.hashCode() ?: 1,
        Intent(c, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP).putExtra("event",event), PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
    fun action(c: Context, action: String): PendingIntent = PendingIntent.getBroadcast(c, action.hashCode(), Intent(c, AlarmReceiver::class.java).setAction(action), PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
    fun ongoing(c: Context, s: Session, now: Long): Notification {
        val timer = NotificationClock.from(s, now, SystemClock.elapsedRealtime(), System.currentTimeMillis())
        manager(c).createNotificationChannel(NotificationChannel(STATUS, "计时状态", NotificationManager.IMPORTANCE_LOW).apply { setSound(null, null) })
        val text = if (timer.countdown) {
            if (s.phase == Phase.WORKING) "剩余 ${clock(timer.remainingMs)} · 距离护眼休息" else "剩余 ${clock(timer.remainingMs)} · 请看向远处"
        } else if(s.phase == Phase.WAITING) "休息完成 · 亮屏后自动计时" else clock(timer.remainingMs)
        val notification = Notification.Builder(c, STATUS).setSmallIcon(R.drawable.ic_notification).setContentTitle("20·20·20 · ${phaseText(s.phase)}")
            .setContentText(text).setContentIntent(open(c)).setOnlyAlertOnce(true).setOngoing(true)
            .setShowWhen(false).setUsesChronometer(false)
        if(Build.VERSION.SDK_INT >= 31) notification.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE)
        return notification.build()
    }
    fun refresh(c: Context, s: Session, now: Long) {
        try { manager(c).notify(20,ongoing(c,s,now)) }
        catch (e: SecurityException) { android.util.Log.w("EyeBreakService","Notification permission unavailable",e) }
    }
    fun channelId(s: Session) = "reminder_${s.options.soundEnabled}_${s.options.soundUri.hashCode()}"
    fun show(c: Context, event: String, s: Session) {
        try { showNotification(c, event, s) }
        catch (e: RuntimeException) { android.util.Log.e("EyeBreakService", "Reminder notification failed: $event", e) }
    }
    private fun showNotification(c: Context, event: String, s: Session) {
        val channelId = channelId(s)
        prepareChannel(c, s)
        if (event == "REST_START") manager(c).cancel("REST_END".hashCode())
        if (event == "REST_END") manager(c).cancel("REST_START".hashCode())
        val title = when (event) { "TEST" -> "护眼提醒测试"; "REST_START" -> "该让眼睛休息了"; "REST_END" -> "休息完成"; "SITTING" -> "久坐提醒"; "LONG_WORK" -> "连续工作过长"; else -> "深夜提醒" }
        val text = when (event) {
            "TEST" -> "这是一条测试提醒，不会改变倒计时"
            "REST_START" -> if (s.phase == Phase.WORKING) "请远眺约 6 米，让眼睛放松；亮屏时继续累计使用" else "请远眺约 6 米，让眼睛放松"
            "REST_END" -> "休息结束，亮屏时自动继续计时"
            "SITTING" -> "请起身活动，也可以立即休息"
            "LONG_WORK" -> "请安排一段较长休息"
            else -> "已经进入深夜时段，请考虑结束工作"
        }
        val notification = Notification.Builder(c, channelId).setSmallIcon(R.drawable.ic_notification).setContentTitle(title)
            .setContentText(text).setStyle(Notification.BigTextStyle().bigText(text)).setContentIntent(open(c,event)).setAutoCancel(true)
            .setCategory(Notification.CATEGORY_REMINDER).setVisibility(Notification.VISIBILITY_PUBLIC)
        try { manager(c).notify(event.hashCode(), notification.build()) } catch (_: SecurityException) { /* User may decline notifications. */ }
    }
    fun prepareChannel(c: Context, s: Session) {
        val channel = NotificationChannel(channelId(s), "护眼与健康提醒", NotificationManager.IMPORTANCE_HIGH)
        val sound = if (!s.options.soundEnabled) null else if (s.options.soundUri.isEmpty()) Settings.System.DEFAULT_NOTIFICATION_URI else Uri.parse(s.options.soundUri)
        channel.setSound(sound, android.media.AudioAttributes.Builder().setUsage(android.media.AudioAttributes.USAGE_NOTIFICATION).build())
        channel.enableVibration(true); manager(c).createNotificationChannel(channel)
    }
    fun clock(ms: Long): String { val seconds = (ms.coerceAtLeast(0) + 999) / 1000; return "%02d:%02d".format(seconds / 60, seconds % 60) }
    fun phaseText(p: Phase) = when (p) { Phase.WORKING -> "工作中"; Phase.PAUSED -> "已暂停"; Phase.RESTING -> "远眺休息中"; Phase.WAITING -> "等待继续"; Phase.AWAY -> "休息回补中" }
    fun duration(ms: Long) = "${ms / 3600000} 小时 ${ms / 60000 % 60} 分 ${ms / 1000 % 60} 秒"
}

object AlarmSchedule {
    private val ticket = AlarmTicket()
    private var canceled = false
    fun fired() { ticket.clear(); canceled = false }
    fun set(c: Context, s: Session, now: Long) {
        val alarm = c.getSystemService(AlarmManager::class.java)
        val at = ReminderDeadline.next(s, now)
        val pending = Alerts.action(c, "alarm")
        if (at == null) { cancel(c); return }
        val exact = Build.VERSION.SDK_INT < 31 || alarm.canScheduleExactAlarms()
        if (ticket.matches(at, exact)) return
        ticket.clear(); canceled = false
        alarm.cancel(pending)
        val delay = (at - now).coerceAtLeast(100)
        try {
            if (exact) {
                // Visible user-requested timer deadlines; no minute alarms consuming
                // the idle quota before the short rest-completion alarm.
                alarm.setAlarmClock(AlarmManager.AlarmClockInfo(System.currentTimeMillis() + delay, Alerts.open(c)), pending)
            } else {
                alarm.setAndAllowWhileIdle(AlarmManager.ELAPSED_REALTIME_WAKEUP, SystemClock.elapsedRealtime() + delay, pending)
            }
            ticket.armed(at, exact)
        } catch (e: SecurityException) {
            android.util.Log.w("EyeBreakService", "Exact alarm permission changed", e)
            alarm.setAndAllowWhileIdle(AlarmManager.ELAPSED_REALTIME_WAKEUP, SystemClock.elapsedRealtime() + delay, pending)
            ticket.armed(at, false)
        }
    }
    fun cancel(c: Context) {
        if (!canceled) c.getSystemService(AlarmManager::class.java).cancel(Alerts.action(c, "alarm"))
        ticket.clear(); canceled = true
    }
}

class AlarmReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val repo = Repository.get(context)
        if (intent.action == "alarm") AlarmSchedule.fired()
        if(intent.action != "alarm" && intent.action != null && repo.sync.enabled) {
            val pending = goAsync()
            repo.command(intent.action!!) {
                AlarmSchedule.set(context,repo.session,repo.now())
                Alerts.refresh(context,repo.session,repo.now()); pending.finish()
            }
            return
        }
        if (intent.action == "alarm") repo.tick() else intent.action?.let { repo.command(it) }
        AlarmSchedule.set(context, repo.session, repo.now())
        Alerts.refresh(context,repo.session,repo.now())
        if (intent.action == "alarm" && !repo.running && BackgroundReminder.enabled(context)) {
            try { context.startForegroundService(Intent(context, ReminderService::class.java)) }
            catch (e: RuntimeException) { android.util.Log.w("EyeBreakService", "Timer delivered without service restart", e) }
        }
    }
}

class ReminderService : Service() {
    private lateinit var repo: Repository
    private val handler = Handler(Looper.getMainLooper())
    private var overlay: TextView? = null
    private var nextSync = 0L
    private var notificationKey = ""
    private var lastTickElapsed = 0L
    private var explicitStop = false
    private val screen = object : BroadcastReceiver() {
        override fun onReceive(c: Context, intent: Intent) {
            if (intent.action == Intent.ACTION_SCREEN_OFF) repo.observeScreen(false)
            if (intent.action == Intent.ACTION_SCREEN_ON) repo.observeScreen(true)
            if (intent.action == Intent.ACTION_SCREEN_ON || intent.action == Intent.ACTION_TIME_CHANGED) {
                if (intent.action == Intent.ACTION_TIME_CHANGED) AlarmSchedule.fired()
                repo.tick(); notificationKey = ""; update()
            }
        }
    }
    private val ticker = object : Runnable {
        override fun run() {
            val elapsed=SystemClock.elapsedRealtime()
            if(lastTickElapsed!=0L && elapsed-lastTickElapsed>5000) android.util.Log.w("EyeBreakService","Ticker delayed by ${elapsed-lastTickElapsed} ms")
            lastTickElapsed=elapsed
            try {
                repo.tick()
                if (repo.sync.enabled && elapsed >= nextSync) { repo.contact(); nextSync = elapsed + 5000 }
            } catch (e: RuntimeException) {
                repo.lastError="后台刷新异常，请重新打开应用查看状态"
                android.util.Log.e("EyeBreakService","Background refresh failed",e)
            } finally {
                // repo.tick emits the listener once. A late/failed callback must
                // not terminate the refresh loop or accumulate timing drift.
                handler.postDelayed(this, 1000 - SystemClock.elapsedRealtime() % 1000)
            }
        }
    }
    private val changed: () -> Unit = { if (::repo.isInitialized) update() }
    override fun onCreate() {
        super.onCreate(); repo = Repository.get(this); repo.running = true
        android.util.Log.i("EyeBreakService","Service started; clamped notification countdown enabled")
        repo.observeScreen(getSystemService(PowerManager::class.java).isInteractive)
        repo.tick()
        startForeground(20, Alerts.ongoing(this, repo.session, repo.now()))
        repo.listeners += changed
        update()
        val filter = IntentFilter().apply { addAction(Intent.ACTION_SCREEN_OFF); addAction(Intent.ACTION_SCREEN_ON); addAction(Intent.ACTION_TIME_CHANGED) }
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(screen, filter, RECEIVER_NOT_EXPORTED) else registerReceiver(screen, filter)
        handler.post(ticker)
    }
    private fun update() {
        val s = repo.session
        val elapsedNow = SystemClock.elapsedRealtime()
        val now = repo.now(elapsedNow)
        AlarmSchedule.set(this, s, now)
        val timer = NotificationClock.from(s,now,elapsedNow,System.currentTimeMillis())
        val key = timer.refreshKey
        if (notificationKey != key) {
            notificationKey = key
            Alerts.refresh(this,s,now)
        }
        val allowed = s.options.overlay && Settings.canDrawOverlays(this)
        try {
            if (allowed && overlay == null) createOverlay()
            if (!allowed && overlay != null) removeOverlay()
        } catch (e: RuntimeException) {
            overlay=null; repo.lastError="悬浮窗暂不可用，通知计时继续运行"
            android.util.Log.w("EyeBreakService","Overlay unavailable",e)
        }
        overlay?.apply {
            text = if (s.phase == Phase.WAITING) "待继续" else if (s.phase == Phase.PAUSED) "已暂停" else (if (s.phase == Phase.AWAY) "↑" else "") + Alerts.clock(s.remainingAt(repo.now()))
            setTextColor(if (s.options.lightMode) Color.rgb(27, 46, 49) else Color.rgb(235, 243, 249))
            background = GradientDrawable().apply { setColor(if (s.options.lightMode) 0xffdfede9.toInt() else 0xff233941.toInt()); cornerRadius = 24f }
        }
    }
    private fun createOverlay() {
        val density = resources.displayMetrics.density
        val params = WindowManager.LayoutParams((94 * density).toInt(), (42 * density).toInt(), WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE, PixelFormat.TRANSLUCENT).apply { gravity = Gravity.TOP or Gravity.START; x = 20; y = 160 }
        val view = object : TextView(this) {
            override fun performClick(): Boolean { super.performClick(); repo.activity(); try { Alerts.open(this@ReminderService).send() } catch (_: PendingIntent.CanceledException) {}; return true }
        }.apply { gravity = Gravity.CENTER; textSize = 15f; contentDescription = "护眼倒计时，点击打开应用，拖动移动" }
        var downX = 0f; var downY = 0f; var startX = 0; var startY = 0; var moved = false
        view.setOnTouchListener { _, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> { downX = event.rawX; downY = event.rawY; startX = params.x; startY = params.y; moved = false }
                MotionEvent.ACTION_MOVE -> {
                    if (kotlin.math.abs(event.rawX - downX) + kotlin.math.abs(event.rawY - downY) > 12 * density) moved = true
                    if (moved) {
                        params.x = (startX + event.rawX - downX).toInt().coerceAtLeast(0); params.y = (startY + event.rawY - downY).toInt().coerceAtLeast(0)
                        try { getSystemService(WindowManager::class.java).updateViewLayout(view, params) }
                        catch (e: RuntimeException) { removeOverlay(); repo.lastError="悬浮窗暂不可用，通知计时继续运行"; android.util.Log.w("EyeBreakService","Overlay move failed",e) }
                    }
                }
                MotionEvent.ACTION_UP -> if (!moved) view.performClick()
            }; true
        }
        try { getSystemService(WindowManager::class.java).addView(view, params); overlay = view } catch (_: Exception) { repo.lastError = "悬浮窗暂不可用" }
    }
    private fun removeOverlay() {
        val old=overlay; overlay=null
        try { old?.let { getSystemService(WindowManager::class.java).removeView(it) } }
        catch (e: RuntimeException) { android.util.Log.w("EyeBreakService","Overlay already removed",e) }
    }
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == "TEST") handler.postDelayed({ repo.testReminder() }, 5000)
        if (intent?.action == "STOP") {
            explicitStop = true
            if (!repo.sync.enabled) repo.session.command("pause", repo.now(), notificationOnly = true)
            repo.save(); AlarmSchedule.cancel(this); stopSelf(); return START_NOT_STICKY
        }
        notificationKey=""; update(); return START_STICKY
    }
    override fun onDestroy() {
        android.util.Log.i("EyeBreakService","Service stopped")
        handler.removeCallbacksAndMessages(null); repo.listeners -= changed; repo.running = false
        unregisterReceiver(screen); removeOverlay()
        if (explicitStop) AlarmSchedule.cancel(this)
        // A system service teardown is not a user pause. Keep the deadline alarm
        // armed so the receiver can deliver and settle the next state independently.
        repo.save(); repo.emit(); super.onDestroy()
    }
    override fun onBind(intent: Intent?) = null
}
