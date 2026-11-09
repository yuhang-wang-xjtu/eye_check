package cn.eyebreak.app

/** A received timed rest keeps its completion alarm even when offline/off-screen. */
object SharedRestNotice {
    fun due(s: Session, now: Long): List<String> {
        if (!s.coordinated || s.phase != Phase.RESTING || now < s.deadline || s.restCycle <= s.restEndAlertedCycle) return emptyList()
        s.restEndAlertedCycle = s.restCycle
        return listOf("REST_END")
    }
    fun received(previous: Session, next: Session, room: String): List<String> {
        next.roomKey = room
        next.restEndAlertedCycle = if (previous.coordinated && previous.roomKey == room) previous.restEndAlertedCycle else next.restFinished
        if (next.restFinished <= next.restEndAlertedCycle) return emptyList()
        next.restEndAlertedCycle = next.restFinished
        return listOf("REST_END")
    }
}
