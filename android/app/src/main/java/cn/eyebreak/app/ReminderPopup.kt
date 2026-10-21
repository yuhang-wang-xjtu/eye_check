package cn.eyebreak.app

import android.app.AlertDialog
import android.app.KeyguardManager
import android.content.Context
import android.provider.Settings
import android.view.ContextThemeWrapper
import android.view.WindowManager

object BackgroundReminder {
    private fun prefs(c: Context) = c.getSharedPreferences("reminder-presentation", Context.MODE_PRIVATE)
    fun enabled(c: Context) = prefs(c).getBoolean("background", true)
    fun enable(c: Context, value: Boolean) { prefs(c).edit().putBoolean("background", value).apply() }
    fun popup(c: Context) = prefs(c).getBoolean("popup", true)
    fun popup(c: Context, value: Boolean) { prefs(c).edit().putBoolean("popup", value).apply() }
}

object ReminderDialogs {
    fun build(c: Context, event: String, s: Session, command: (String) -> Unit): AlertDialog {
        val title = when(event) {
            "REST_START" -> "该让眼睛休息了"; "REST_END" -> "休息完成"
            "SITTING" -> "久坐提醒"; "LONG_WORK" -> "连续工作过长"
            "TEST" -> "护眼提醒测试"; else -> "深夜提醒"
        }
        val builder = AlertDialog.Builder(c).setTitle(title)
        when(event) {
            "REST_START" -> builder.setMessage("请看向约 6 米外，让眼睛放松 ${s.options.restSeconds} 秒。结束后需要新的操作或点击继续。")
                .setPositiveButton("知道了", null).setNeutralButton("立即休息") { _, _ -> command("rest") }
                .setNegativeButton("跳过休息") { _, _ -> command("skip") }
            "REST_END" -> builder.setMessage("本轮休息已完成，计时保持停止，直到你选择继续或产生新的操作。")
                .setPositiveButton("继续工作") { _, _ -> command("continue") }
                .setNeutralButton("立即休息") { _, _ -> command("rest") }.setNegativeButton("稍后", null)
            "TEST" -> builder.setMessage("这是提醒效果测试，不改变计时。后台弹窗需要允许显示在其他应用上层；声音和通知横幅还受系统通知设置控制。")
                .setPositiveButton("知道了", null)
            else -> builder.setMessage("请安排休息或结束本轮工作。手动休息按 5:1 回补，每日使用统计不会扣减。")
                .setPositiveButton("立即休息") { _, _ -> command("rest") }
                .setNeutralButton("结束本轮工作") { _, _ -> command("end_work") }.setNegativeButton("知道了", null)
        }
        return builder.create()
    }
}

/** Uses only the application context; never launches an Activity from the background. */
object ReminderPopup {
    @android.annotation.SuppressLint("StaticFieldLeak")
    private var dialog: AlertDialog? = null
    private var event = ""
    fun dismiss() { dialog?.dismiss(); dialog = null; event = "" }
    fun reconcile(s: Session) {
        if ((event == "REST_START" && s.phase != Phase.RESTING) || (event == "REST_END" && s.phase != Phase.WAITING)) dismiss()
    }
    fun show(c: Context, value: String, s: Session) {
        val app = c.applicationContext
        val repo = Repository.get(app)
        if (repo.foreground || !BackgroundReminder.popup(app) || !Settings.canDrawOverlays(app) ||
            app.getSystemService(KeyguardManager::class.java).isKeyguardLocked) return
        try {
            dismiss()
            val themed = ContextThemeWrapper(app, if(s.options.lightMode) android.R.style.Theme_Material_Light_Dialog_Alert else android.R.style.Theme_Material_Dialog_Alert)
            val next = ReminderDialogs.build(themed, value, s) { repo.command(it) }
            next.window?.setType(WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY)
            next.setOnDismissListener { if (dialog === next) { dialog = null; event = "" } }
            dialog = next; event = value; next.show()
        } catch (e: RuntimeException) {
            dialog = null; event = ""
            android.util.Log.w("EyeBreakService", "Reminder overlay unavailable; notification retained", e)
        }
    }
}
