package cn.eyebreak.app

/** Local phone policy: interactive screen = usage, non-interactive = recovery. */
class ScreenUsagePolicy {
    private var interactive: Boolean? = null
    fun observe(s: Session, screenOn: Boolean, now: Long): List<String> {
        if (interactive == screenOn) return emptyList()
        interactive = screenOn
        if (screenOn) {
            // Migrate legacy automatic-rest / waiting snapshots without asking.
            if (s.phase == Phase.RESTING || s.phase == Phase.WAITING) {
                s.remaining = Session.WORK_MS; s.phase = Phase.PAUSED; s.updatedAt = now
            }
            if (s.phase == Phase.AWAY || s.phase == Phase.PAUSED) s.command("continue", now, notificationOnly = true)
            return s.tick(now, notificationOnly = true)
        }
        val events = s.tick(now, notificationOnly = true)
        if (s.phase != Phase.AWAY) s.command("rest", now, notificationOnly = true)
        return events
    }
    fun tick(s: Session, now: Long) = s.tick(now, notificationOnly = true)
}
