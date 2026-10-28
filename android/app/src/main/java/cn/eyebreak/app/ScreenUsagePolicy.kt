package cn.eyebreak.app

/** Screen-on resumes work, but never skips a running automatic rest. */
class ScreenUsagePolicy {
    private var interactive: Boolean? = null
    fun observe(s: Session, screenOn: Boolean, now: Long): List<String> {
        if (interactive == screenOn) return emptyList()
        val events = if (interactive != null) tick(s, now) else emptyList()
        interactive = screenOn
        if (screenOn) {
            if (s.phase in listOf(Phase.AWAY, Phase.PAUSED, Phase.WAITING)) s.command("continue", now)
        } else if (s.phase != Phase.RESTING && s.phase != Phase.AWAY) {
            s.command("rest", now)
        }
        return events + tick(s, now)
    }
    fun tick(s: Session, now: Long): List<String> {
        // While the screen is off, health timers recover even during timed rest.
        // This interval is settled once, before Session advances updatedAt.
        if (s.phase == Phase.RESTING && interactive == false) s.recoverHealth(now - s.updatedAt)
        val events = s.tick(now)
        if (s.phase == Phase.WAITING) {
            if (interactive == true) s.command("continue", now)
            else if (interactive == false) s.command("rest", now)
        }
        return events
    }
}
