package cn.eyebreak.app

import java.time.Instant
import java.time.LocalTime
import java.time.ZoneId

/** One alarm for the next actual reminder, never a periodic minute wake-up. */
object ReminderDeadline {
    fun next(s: Session, now: Long): Long? {
        if (s.phase == Phase.RESTING) return s.deadline
        if (s.phase != Phase.WORKING) return null
        var next = s.deadline
        if (s.options.sittingEnabled && !s.sittingSent)
            next = minOf(next, s.updatedAt + (s.options.sittingMinutes * 60000L - s.sittingMs).coerceAtLeast(0))
        if (s.options.workEnabled && !s.workSent)
            next = minOf(next, s.updatedAt + (s.options.workMinutes * 60000L - s.workMs).coerceAtLeast(0))
        if (s.options.nightEnabled) {
            val zone = ZoneId.of(s.zone)
            val local = Instant.ofEpochMilli(now).atZone(zone)
            val minute = local.hour * 60 + local.minute
            val start = s.options.nightHour * 60 + s.options.nightMinute
            val inNight = if (start >= 360) minute >= start || minute < 360 else minute >= start && minute < 360
            val key = (if (start >= 360 && minute < 360) local.toLocalDate().minusDays(1) else local.toLocalDate()).toString()
            if (inNight && key != s.lastNight) next = minOf(next, now)
            else {
                var date = local.toLocalDate()
                var at = date.atTime(LocalTime.of(s.options.nightHour, s.options.nightMinute)).atZone(zone)
                if (at.toInstant().toEpochMilli() <= now || date.toString() == s.lastNight) {
                    date = date.plusDays(1); at = date.atTime(LocalTime.of(s.options.nightHour, s.options.nightMinute)).atZone(zone)
                }
                next = minOf(next, at.toInstant().toEpochMilli())
            }
        }
        return next.coerceAtLeast(now)
    }
}

/** An alarm is one-shot. A delivered/failed alarm must never remain cached as armed. */
class AlarmTicket {
    private var key: String? = null
    fun matches(deadline: Long, exact: Boolean) = key == "$deadline:$exact"
    fun armed(deadline: Long, exact: Boolean) { key = "$deadline:$exact" }
    fun clear() { key = null }
}
