package cn.eyebreak.app

/** Project only this phone's timer. Screen-on continuation awaits its server ack. */
object DeviceSyncPolicy {
    fun tick(s: Session, screenOn: Boolean, now: Long): List<String> {
        if (!screenOn && s.phase == Phase.RESTING) s.recoverHealth(now - s.updatedAt)
        val events = s.tick(now)
        if (!screenOn && s.phase in listOf(Phase.WORKING, Phase.PAUSED, Phase.WAITING)) s.command("rest", now)
        return events
    }
}
