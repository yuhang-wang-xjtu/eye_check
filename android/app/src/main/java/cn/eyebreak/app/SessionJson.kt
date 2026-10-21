package cn.eyebreak.app

import org.json.JSONObject

object SessionJson {
    fun sharedOptions(o: Options) = JSONObject().put("restSeconds", o.restSeconds)
        .put("sittingMinutes", o.sittingMinutes).put("workMinutes", o.workMinutes)
        .put("nightHour", o.nightHour).put("nightMinute", o.nightMinute)
        .put("sittingEnabled", o.sittingEnabled).put("workEnabled", o.workEnabled).put("nightEnabled", o.nightEnabled)
    fun decode(j: JSONObject, local: Options): Session {
        val s = Session(j.getLong("updatedAtMs"))
        s.phase = Phase.valueOf(j.getString("state")); s.deadline = j.getLong("deadlineMs")
        s.remaining = j.getLong("remainingMs"); s.sittingMs = j.getLong("sittingMs"); s.workMs = j.getLong("workMs")
        s.sittingSent = j.getBoolean("sittingSent"); s.workSent = j.getBoolean("workSent")
        s.lastNight = j.optString("lastNight"); s.zone = j.getString("zone"); s.revision = j.getLong("revision")
        val o = j.getJSONObject("settings")
        s.options = local.copy(restSeconds = o.getInt("restSeconds"), sittingMinutes = o.getInt("sittingMinutes"),
            workMinutes = o.getInt("workMinutes"), nightHour = o.getInt("nightHour"), nightMinute = o.getInt("nightMinute"),
            sittingEnabled = o.getBoolean("sittingEnabled"), workEnabled = o.getBoolean("workEnabled"), nightEnabled = o.getBoolean("nightEnabled"))
        val days = j.getJSONObject("usage")
        days.keys().forEach { date -> s.usage[date] = days.getLong(date) }
        require(s.remaining in 0..Session.WORK_MS && s.sittingMs >= 0 && s.workMs >= 0)
        require(s.options.restSeconds in 1..600 && s.options.sittingMinutes in 1..480 && s.options.workMinutes in 1..720)
        require(s.options.nightHour in 0..23 && s.options.nightMinute in 0..59)
        return s
    }
}
