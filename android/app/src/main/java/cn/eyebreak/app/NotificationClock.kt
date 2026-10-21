package cn.eyebreak.app

/** Clamped notification text: the platform Chronometer otherwise runs below zero. */
data class NotificationClock(
    val countdown: Boolean,
    val remainingMs: Long,
    val endElapsedMs: Long,
    val whenWallMs: Long,
    val refreshKey: String
) {
    companion object {
        fun from(s: Session, now: Long, elapsedNow: Long, wallNow: Long): NotificationClock {
            val remaining = s.remainingAt(now)
            val countdown = remaining > 0 && (s.phase == Phase.WORKING || s.phase == Phase.RESTING)
            val end = elapsedNow + remaining
            val value = if (countdown) "$end:${(remaining + 999) / 1000}" else "${(remaining + 999) / 1000}"
            return NotificationClock(countdown, remaining, end, wallNow + remaining, "${s.phase}:$value")
        }
    }
}
