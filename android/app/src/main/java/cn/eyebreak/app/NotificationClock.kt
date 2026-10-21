package cn.eyebreak.app

/** SystemUI advances the countdown; app callbacks only publish new deadlines/states. */
data class NotificationClock(
    val countdown: Boolean,
    val remainingMs: Long,
    val endElapsedMs: Long,
    val whenWallMs: Long,
    val refreshKey: String
) {
    companion object {
        fun from(s: Session, now: Long, elapsedNow: Long, wallNow: Long): NotificationClock {
            val countdown = s.phase == Phase.WORKING || s.phase == Phase.RESTING
            val remaining = s.remainingAt(now)
            val end = elapsedNow + remaining
            // Deadlines remain stable as time passes. Recovery is five times faster
            // than a normal clock and therefore needs a fresh value each second.
            val value = if (countdown) end else (remaining + 999) / 1000
            return NotificationClock(countdown, remaining, end, wallNow + remaining, "${s.phase}:$value")
        }
    }
}
