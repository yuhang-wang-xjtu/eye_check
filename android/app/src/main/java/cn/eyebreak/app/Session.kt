package cn.eyebreak.app

import java.io.Serializable
import java.time.Instant
import java.time.ZoneId
import kotlin.math.min

enum class Phase { WORKING, PAUSED, RESTING, WAITING, AWAY }
data class Options(
    var restSeconds: Int = 25, var sittingMinutes: Int = 60, var workMinutes: Int = 120,
    var nightHour: Int = 23, var nightMinute: Int = 0,
    var sittingEnabled: Boolean = true, var workEnabled: Boolean = true,
    var nightEnabled: Boolean = true, var soundEnabled: Boolean = true,
    var lightMode: Boolean = false, var overlay: Boolean = false, var soundUri: String = ""
) : Serializable { companion object { private const val serialVersionUID: Long = 1L } }

class Session(now: Long = System.currentTimeMillis()) : Serializable {
    var phase = Phase.WORKING
    var updatedAt = now
    var deadline = now + WORK_MS
    var remaining = WORK_MS
    var sittingMs = 0L
    var workMs = 0L
    var sittingSent = false
    var workSent = false
    var lastNight = ""
    var options = Options()
    var usage = sortedMapOf<String, Long>()
    var revision = 0L
    var zone = ZoneId.systemDefault().id

    fun remainingAt(now: Long): Long = when (phase) {
        Phase.WORKING, Phase.RESTING -> (deadline - now).coerceAtLeast(0)
        Phase.AWAY -> (remaining + (now - updatedAt).coerceAtLeast(0).coerceAtMost(WORK_MS) * 5).coerceAtMost(WORK_MS)
        else -> remaining
    }
    private fun countUsage(start: Long, end: Long) {
        var cursor = start
        val tz = ZoneId.of(zone)
        while (cursor < end) {
            val date = Instant.ofEpochMilli(cursor).atZone(tz).toLocalDate()
            val next = min(end, date.plusDays(1).atStartOfDay(tz).toInstant().toEpochMilli())
            usage[date.toString()] = (usage[date.toString()] ?: 0) + next - cursor
            cursor = next
        }
    }
    fun tick(now: Long): List<String> {
        val events = mutableListOf<String>()
        if (now < updatedAt) { // Changing the wall clock cannot invent usage.
            if (phase == Phase.WORKING || phase == Phase.RESTING) deadline = now + (deadline - updatedAt).coerceAtLeast(0)
            updatedAt = now
            return events
        }
        when (phase) {
            Phase.WORKING -> {
                val end = min(now, deadline)
                val elapsed = (end - updatedAt).coerceAtLeast(0)
                sittingMs += elapsed; workMs += elapsed
                countUsage(updatedAt, end)
                if (now >= deadline) {
                    phase = Phase.RESTING; deadline += options.restSeconds * 1000L; events += "REST_START"
                }
            }
            Phase.AWAY -> {
                remaining = remainingAt(now)
                val recovery = min(now - updatedAt, 365L * 86400000) * 5
                sittingMs = (sittingMs - recovery).coerceAtLeast(0)
                workMs = (workMs - recovery).coerceAtLeast(0)
                if (sittingMs < options.sittingMinutes * 60000L) sittingSent = false
                if (workMs < options.workMinutes * 60000L) workSent = false
            }
            else -> Unit
        }
        if (phase == Phase.RESTING && now >= deadline) {
            phase = Phase.WAITING; remaining = WORK_MS; events.clear(); events += "REST_END"
        }
        updatedAt = now
        if (phase == Phase.WORKING) {
            if (options.sittingEnabled && !sittingSent && sittingMs >= options.sittingMinutes * 60000L) { sittingSent = true; events += "SITTING" }
            if (options.workEnabled && !workSent && workMs >= options.workMinutes * 60000L) { workSent = true; events += "LONG_WORK" }
            val local = Instant.ofEpochMilli(now).atZone(ZoneId.of(zone))
            val minute = local.hour * 60 + local.minute
            val start = options.nightHour * 60 + options.nightMinute
            val night = if (start >= 360) minute >= start || minute < 360 else minute >= start && minute < 360
            val key = (if (start >= 360 && minute < 360) local.toLocalDate().minusDays(1) else local.toLocalDate()).toString()
            if (options.nightEnabled && night && lastNight != key) { lastNight = key; events += "LATE_NIGHT" }
        }
        return events
    }
    fun command(action: String, now: Long) {
        tick(now)
        when (action) {
            "pause" -> if (phase == Phase.WORKING) { remaining = remainingAt(now); phase = Phase.PAUSED }
            "continue" -> if (phase in listOf(Phase.PAUSED, Phase.WAITING, Phase.AWAY)) { deadline = now + remainingAt(now); phase = Phase.WORKING }
            "rest" -> if (phase != Phase.AWAY) {
                remaining = if (phase == Phase.WORKING || phase == Phase.PAUSED) remainingAt(now) else WORK_MS
                phase = Phase.AWAY
            }
            "skip" -> if (phase == Phase.RESTING) { remaining = WORK_MS; deadline = now + WORK_MS; phase = Phase.WORKING }
            "stood_up" -> { sittingMs = 0; sittingSent = false }
            "end_work" -> { sittingMs = 0; workMs = 0; sittingSent = false; workSent = false; remaining = WORK_MS; phase = Phase.PAUSED }
            else -> error("Unknown action")
        }
        updatedAt = now
    }
    companion object { const val WORK_MS = 20 * 60 * 1000L; private const val serialVersionUID: Long = 1L }
}
