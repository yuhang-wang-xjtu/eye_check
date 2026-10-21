package cn.eyebreak.app

import android.Manifest
import android.app.*
import android.content.*
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.*
import android.provider.Settings
import android.text.InputType
import android.view.*
import android.widget.*
import java.time.Instant
import java.time.ZoneId
import java.util.UUID

class MainActivity : Activity() {
    private lateinit var updater: GitHubUpdater
    private lateinit var repo: Repository
    private lateinit var countdown: TextView
    private lateinit var phase: TextView
    private lateinit var totals: TextView
    private lateinit var health: TextView
    private lateinit var syncText: TextView
    private lateinit var notice: TextView
    private lateinit var primary: Button
    private lateinit var rest: Button
    private var bg = 0xff121b24.toInt(); private var surface = 0xff233941.toInt()
    private var ink = 0xffebf3f9.toInt(); private var muted = 0xff98aebb.toInt(); private var accent = 0xff77d7c0.toInt()
    private var lastInteraction = 0L
    private val handler = Handler(Looper.getMainLooper())
    private val update: () -> Unit = { if (::countdown.isInitialized) render() }
    private val showEvents: (List<String>) -> Unit = { events -> events.firstOrNull { it !in listOf("REST_START","REST_END") }?.let { showReminder(it) } }
    private val ticker = object : Runnable {
        override fun run() { if (!repo.running) repo.tick(); render(); handler.postDelayed(this, 1000) }
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        repo = Repository.get(this)
        setTheme(if (repo.session.options.lightMode) android.R.style.Theme_Material_Light_NoActionBar else R.style.AppTheme)
        super.onCreate(savedInstanceState)
        updater = GitHubUpdater(this)
        if (repo.session.options.lightMode) {
            bg = 0xfff6f8f9.toInt(); surface = 0xffdfede9.toInt(); ink = 0xff1b2e31.toInt(); muted = 0xff51676d.toInt(); accent = 0xff106f5b.toInt()
        }
        build()
        intent.getStringExtra("event")?.let { window.decorView.post { showReminder(it) } }
    }
    override fun onStart() { super.onStart(); updater.start(); repo.listeners += update; repo.eventListeners += showEvents; handler.post(ticker) }
    override fun onResume() { super.onResume(); updater.resume() }
    override fun onStop() { updater.stop(); handler.removeCallbacks(ticker); repo.listeners -= update; repo.eventListeners -= showEvents; repo.save(); super.onStop() }
    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
    private fun shape(color: Int, radius: Int = 18) = GradientDrawable().apply { setColor(color); cornerRadius = dp(radius).toFloat() }
    private fun label(text: String, size: Float = 14f, color: Int = ink): TextView = TextView(this).apply { this.text = text; textSize = size; setTextColor(color); setPadding(0, dp(6), 0, dp(6)) }
    private fun column() = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
    private fun button(text: String, action: () -> Unit) = Button(this).apply {
        this.text = text; textSize = 14f; isAllCaps = false; setTextColor(ink); background = shape(surface, 12)
        setPadding(dp(10), dp(8), dp(10), dp(8)); setOnClickListener { action() }
        layoutParams = LinearLayout.LayoutParams(-1, dp(46)).apply { topMargin = dp(8) }
    }
    private fun build() {
        val root = column().apply { setBackgroundColor(bg); setPadding(dp(22), dp(18), dp(22), dp(24)) }
        // Fit both gesture/navigation bars, including Android 15 edge-to-edge.
        root.setOnApplyWindowInsetsListener { v, insets ->
            val top = if (Build.VERSION.SDK_INT >= 30) insets.getInsets(WindowInsets.Type.systemBars()).top else insets.systemWindowInsetTop
            val bottom = if (Build.VERSION.SDK_INT >= 30) insets.getInsets(WindowInsets.Type.systemBars()).bottom else insets.systemWindowInsetBottom
            v.setPadding(dp(22), top + dp(12), dp(22), bottom + dp(20)); insets
        }
        val title = label("20 · 20 · 20", 25f).apply { typeface = Typeface.DEFAULT_BOLD }
        root.addView(title); root.addView(label("每 20 分钟，让眼睛放松一下", 13f, muted))
        val toolbar = LinearLayout(this)
        listOf("统计" to { showStatistics() }, "设置" to { showSettings() }, "同步" to { showSync() }, "更多" to { showActions(toolbar) }).forEach { (name, action) ->
            toolbar.addView(button(name, action), LinearLayout.LayoutParams(0, dp(42), 1f).apply { setMargins(0, dp(10), dp(6), dp(8)) })
        }
        root.addView(toolbar)
        val card = column().apply { background = shape(surface); setPadding(dp(12), dp(18), dp(12), dp(18)) }
        phase = label("", 14f, accent).apply { gravity = Gravity.CENTER }
        countdown = label("20:00", 64f).apply { gravity = Gravity.CENTER; typeface = Typeface.create("sans-serif-light", Typeface.NORMAL) }
        card.addView(phase); card.addView(countdown)
        card.addView(label("远眺约 6 米 · 自动休息 ${repo.session.options.restSeconds} 秒", 12f, muted).apply { gravity = Gravity.CENTER })
        root.addView(card)
        totals = label("", 16f); root.addView(totals)
        health = label("", 13f, muted); root.addView(health)
        primary = button("暂停计时") {
            repo.command(when (repo.session.phase) { Phase.WORKING -> "pause"; Phase.RESTING -> "skip"; else -> "continue" })
        }; root.addView(primary)
        rest = button("立即休息") { repo.command("rest") }; root.addView(rest)
        root.addView(button("开始后台提醒") {
            startForegroundService(Intent(this, ReminderService::class.java))
            if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
                requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 11)
        })
        syncText = label("", 12f, muted); root.addView(syncText)
        notice = label("", 12f, muted); root.addView(notice)
        root.addView(label("手动休息按 5:1 回补，新的操作后继续。每日累计只增不减。后台无法检测其他 App 的触控；可点通知继续工作。", 12f, muted))
        val scroll = ScrollView(this).apply { isFillViewport = true; addView(root) }; setContentView(scroll)
        render()
    }
    private fun render() {
        val s = repo.session
        phase.text = Alerts.phaseText(s.phase)
        countdown.text = if (s.phase == Phase.WAITING) "00:00" else Alerts.clock(s.remainingAt(repo.now()))
        val today = Instant.ofEpochMilli(repo.now()).atZone(ZoneId.of(s.zone)).toLocalDate().toString()
        totals.text = "今日使用  ${Alerts.duration(s.usage[today] ?: 0)}"
        health.text = "久坐：${if (s.options.sittingEnabled) Alerts.clock((s.options.sittingMinutes * 60000L - s.sittingMs).coerceAtLeast(0)) else "已关闭"}   连续工作：${if (s.options.workEnabled) Alerts.clock((s.options.workMinutes * 60000L - s.workMs).coerceAtLeast(0)) else "已关闭"}"
        primary.text = when (s.phase) { Phase.WORKING -> "暂停计时"; Phase.RESTING -> "跳过休息"; else -> "继续工作" }
        rest.visibility = if (s.phase == Phase.AWAY) View.GONE else View.VISIBLE
        syncText.text = "${repo.syncStatus}${if (repo.busy) " · 连接中" else ""}"
        val exact = Build.VERSION.SDK_INT < 31 || getSystemService(AlarmManager::class.java).canScheduleExactAlarms()
        notice.text = listOfNotNull(if (!repo.running) "后台提醒未开启" else null,
            if (!getSystemService(NotificationManager::class.java).areNotificationsEnabled()) "通知权限未开启" else null,
            if (!exact) "未开启精确定时，后台提醒可能延迟" else null, repo.lastError.takeIf { it.isNotEmpty() }).joinToString(" · ")
    }
    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        if(event.action == MotionEvent.ACTION_DOWN) {
            fun buttonAt(view: View): Boolean {
                val bounds = android.graphics.Rect()
                if(!view.getGlobalVisibleRect(bounds) || !bounds.contains(event.rawX.toInt(), event.rawY.toInt())) return false
                if(view is Button || view is EditText) return true
                return view is android.view.ViewGroup && (0 until view.childCount).any { buttonAt(view.getChildAt(it)) }
            }
            if(!buttonAt(window.decorView)) repo.activity()
        }
        if (event.action == MotionEvent.ACTION_MOVE && SystemClock.elapsedRealtime() - lastInteraction > 1000) {
            repo.activity(); lastInteraction = SystemClock.elapsedRealtime()
        }
        return super.dispatchTouchEvent(event)
    }
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.action == KeyEvent.ACTION_DOWN && event.keyCode !in listOf(KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_VOLUME_UP, KeyEvent.KEYCODE_VOLUME_DOWN)) repo.activity()
        return super.dispatchKeyEvent(event)
    }
    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (kotlin.math.abs(event.getAxisValue(MotionEvent.AXIS_X)) > .2 || kotlin.math.abs(event.getAxisValue(MotionEvent.AXIS_Y)) > .2 || event.action == MotionEvent.ACTION_SCROLL) repo.activity()
        return super.dispatchGenericMotionEvent(event)
    }
    private fun form(): Pair<ScrollView, LinearLayout> {
        val content = column().apply { setPadding(dp(20), dp(8), dp(20), dp(8)) }
        return ScrollView(this).apply { addView(content) } to content
    }
    private fun number(parent: LinearLayout, name: String, value: Int): EditText {
        parent.addView(label(name, 13f, muted))
        return EditText(this).apply { inputType = InputType.TYPE_CLASS_NUMBER; setSingleLine(); setText(value.toString()); parent.addView(this) }
    }
    private fun check(parent: LinearLayout, text: String, selected: Boolean) = CheckBox(this).apply { this.text = text; isChecked = selected; setTextColor(ink); parent.addView(this) }
    private fun showSettings() {
        val (view, box) = form(); val o = repo.session.options
        val restSeconds = number(box, "自动护眼休息时长（1–600 秒）", o.restSeconds)
        val sitting = check(box, "久坐提醒", o.sittingEnabled); val sittingMins = number(box, "久坐周期（1–480 分钟）", o.sittingMinutes)
        val work = check(box, "连续工作提醒", o.workEnabled); val workMins = number(box, "连续工作周期（1–720 分钟）", o.workMinutes)
        val night = check(box, "深夜提醒", o.nightEnabled); val hour = number(box, "深夜开始小时（0–23）", o.nightHour); val minute = number(box, "分钟（0–59；深夜截至 06:00）", o.nightMinute)
        val sound = check(box, "提醒声音", o.soundEnabled); val light = check(box, "浅色模式", o.lightMode)
        box.addView(button(if (o.soundUri.isEmpty()) "选择自定义声音（当前：默认）" else "更换自定义声音") {
            startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply { type = "audio/*"; addCategory(Intent.CATEGORY_OPENABLE); addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) }, 12)
        })
        box.addView(button("恢复默认声音") { repo.changeOptions(repo.session.options.copy(soundUri = "")); toast("已恢复默认声音") })
        box.addView(button("试听当前声音") {
            try {
                val uri = if(repo.session.options.soundUri.isEmpty()) Settings.System.DEFAULT_NOTIFICATION_URI else Uri.parse(repo.session.options.soundUri)
                val ringtone = android.media.RingtoneManager.getRingtone(this,uri)
                ringtone.play(); handler.postDelayed({ ringtone.stop() },5000)
            } catch (_: Exception) { toast("声音暂不可用，请重新选择") }
        })
        val overlay = check(box, "桌面悬浮倒计时", o.overlay)
        box.addView(button("悬浮窗权限") { startActivity(Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName"))) })
        if (Build.VERSION.SDK_INT >= 31) box.addView(button("开启精确定时权限") { startActivity(Intent(Settings.ACTION_REQUEST_SCHEDULE_EXACT_ALARM, Uri.parse("package:$packageName"))) })
        box.addView(button("系统通知设置") { startActivity(Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_APP_PACKAGE, packageName)) })
        box.addView(button("检查 GitHub 更新（${BuildConfig.VERSION_NAME}）") { updater.check() })
        box.addView(button("停止后台提醒") { stopService(Intent(this, ReminderService::class.java)); toast("后台提醒已停止") })
        val dialog = AlertDialog.Builder(this).setTitle("提醒设置").setView(view).setPositiveButton("保存", null).setNegativeButton("取消", null).create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                try {
                    fun read(field: EditText, range: IntRange): Int = field.text.toString().toInt().also { require(it in range) }
                    val draft = repo.session.options.copy(restSeconds = read(restSeconds, 1..600), sittingMinutes = read(sittingMins, 1..480), workMinutes = read(workMins, 1..720),
                        nightHour = read(hour, 0..23), nightMinute = read(minute, 0..59), sittingEnabled = sitting.isChecked, workEnabled = work.isChecked,
                        nightEnabled = night.isChecked, soundEnabled = sound.isChecked, lightMode = light.isChecked, overlay = overlay.isChecked)
                    if (draft.overlay && !Settings.canDrawOverlays(this)) { toast("请先授权悬浮窗权限"); return@setOnClickListener }
                    repo.changeOptions(draft); dialog.dismiss(); if (repo.session.options.lightMode != o.lightMode) recreate()
                } catch (_: Exception) { toast("请输入范围内的有效数值") }
            }
        }; dialog.show()
    }
    private fun showStatistics() {
        repo.tick()
        val (view, box) = form()
        box.addView(label("仅累计工作状态；休息回补和重置提醒不会扣减记录。", 13f, muted))
        val days = repo.session.usage.toSortedMap(compareByDescending { it })
        val today = Instant.ofEpochMilli(repo.now()).atZone(ZoneId.of(repo.session.zone)).toLocalDate().toString(); days.putIfAbsent(today, 0)
        days.forEach { (day, ms) -> box.addView(label("$day\n${Alerts.duration(ms)}", 15f)) }
        AlertDialog.Builder(this).setTitle("每日使用统计").setView(view).setPositiveButton("关闭", null).show()
    }
    private fun showActions(anchor: View) {
        val s = repo.session
        val popup = PopupMenu(this, anchor)
        if(s.options.sittingEnabled && s.sittingMs > 0) popup.menu.add(0,1,0,"我已起身活动（重置久坐）")
        if(s.workMs > 0 || s.sittingMs > 0 || s.phase in listOf(Phase.WORKING,Phase.RESTING)) popup.menu.add(0,2,1,"结束本轮工作（清零并暂停）")
        if(popup.menu.size() == 0) { toast("当前没有需要重置的计时"); return }
        popup.setOnMenuItemClickListener { repo.command(if(it.itemId == 1) "stood_up" else "end_work"); true }; popup.show()
    }
    private fun showSync() {
        val (view, box) = form()
        box.addView(label("手机与电脑使用同一会话。服务器统一结算倒计时和使用时长，避免两台设备重复累计。", 13f, muted))
        box.addView(label("服务器地址（远程必须 HTTPS）", 13f, muted))
        val url = EditText(this).apply { setSingleLine(); inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI; hint = "https://sync.example.com"; setText(repo.sync.baseUrl); box.addView(this) }
        box.addView(label("共享会话标识（另一设备填写相同内容）", 13f, muted))
        val room = EditText(this).apply { setSingleLine(); setText(repo.sync.room.ifEmpty { UUID.randomUUID().toString() }); box.addView(this) }
        box.addView(label(if (repo.sync.hasToken()) "访问令牌（留空保留已保存令牌）" else "访问令牌（由服务端生成）", 13f, muted))
        val token = EditText(this).apply { setSingleLine(); inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD; box.addView(this) }
        box.addView(label("请填写自行部署的同步服务 HTTPS 地址。连接后以服务器会话为准；断网时不上传旧状态，共享操作需在线确认。", 12f, muted))
        box.addView(button("切换本地模式") { repo.disconnect(); toast("已切换本地模式") })
        val dialog = AlertDialog.Builder(this).setTitle("服务器同步原型").setView(view).setPositiveButton("连接", null).setNegativeButton("取消", null).create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                try {
                    repo.sync.validateUrl(url.text.toString().trim()); repo.sync.room = room.text.toString().trim(); repo.sync.baseUrl = url.text.toString().trim()
                    if (token.text.isNotEmpty()) repo.sync.setToken(token.text.toString())
                    require(repo.sync.hasToken()) { "请填写访问令牌" }
                    repo.connect(); dialog.dismiss()
                } catch (e: Exception) { toast(e.message ?: "请检查同步设置") }
            }
        }; dialog.show()
    }
    @Deprecated("Native prototype uses the platform document picker")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == 12 && resultCode == RESULT_OK) data?.data?.let { uri ->
            try { contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION); repo.changeOptions(repo.session.options.copy(soundUri = uri.toString())); toast("已选择声音") }
            catch (_: Exception) { toast("无法持久读取此音频，请重新选择") }
        }
    }
    private fun toast(text: String) { Toast.makeText(this, text, Toast.LENGTH_LONG).show() }
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent); setIntent(intent)
        intent.getStringExtra("event")?.let { showReminder(it) }
    }
    private fun showReminder(event: String) {
        if(event in listOf("REST_START", "REST_END")) return
        val title = when(event) { "SITTING" -> "久坐提醒"; "LONG_WORK" -> "连续工作过长"; else -> "深夜提醒" }
        AlertDialog.Builder(this).setTitle(title).setMessage("请安排休息或结束本轮工作。手动休息按 5:1 回补，每日使用统计不会扣减。")
            .setPositiveButton("立即休息") { _, _ -> repo.command("rest") }
            .setNeutralButton("结束本轮工作") { _, _ -> repo.command("end_work") }.setNegativeButton("知道了",null).show()
    }
}
