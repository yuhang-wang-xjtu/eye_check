package cn.eyebreak.app

import android.content.Context

object BackgroundReminder {
    private fun prefs(c: Context) = c.getSharedPreferences("reminder-presentation", Context.MODE_PRIVATE)
    fun enabled(c: Context) = prefs(c).getBoolean("background", true)
    fun enable(c: Context, value: Boolean) { prefs(c).edit().putBoolean("background", value).apply() }
}
