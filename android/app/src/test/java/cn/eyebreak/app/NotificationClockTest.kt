package cn.eyebreak.app

import org.junit.Assert.*
import org.junit.Test

class NotificationClockTest {
    private fun model()=Session(1000).apply { options.nightEnabled=false }
    @Test fun workingCountdownRefreshesTextEverySecondWithoutMovingDeadline() {
        val s=model()
        val first=NotificationClock.from(s,1000,5000,100000)
        assertTrue(first.countdown)
        for(second in 1..15) {
            val delta=second*1000L
            val later=NotificationClock.from(s,1000+delta,5000+delta,100000+delta)
            assertEquals(Session.WORK_MS-delta,later.remainingMs)
            assertEquals(first.endElapsedMs,later.endElapsedMs)
            assertEquals(first.whenWallMs,later.whenWallMs)
            assertNotEquals(first.refreshKey,later.refreshKey)
        }
    }
    @Test fun restHasItsOwnDeadlineAndWaitingStopsTheSystemClock() {
        val s=model(); s.tick(s.deadline)
        val rest=NotificationClock.from(s,s.updatedAt,9000,200000)
        assertTrue(rest.countdown); assertEquals(25000,rest.remainingMs)
        s.tick(s.deadline)
        val waiting=NotificationClock.from(s,s.updatedAt,34000,225000)
        assertFalse(waiting.countdown); assertNotEquals(rest.refreshKey,waiting.refreshKey)
    }
    @Test fun pauseFreezesAndResumeRebasesDeadline() {
        val s=model(); s.command("pause",11000)
        val paused=NotificationClock.from(s,11000,15000,110000)
        assertFalse(paused.countdown)
        assertEquals(paused.refreshKey,NotificationClock.from(s,91000,95000,190000).refreshKey)
        s.command("continue",91000)
        val resumed=NotificationClock.from(s,91000,95000,190000)
        assertTrue(resumed.countdown); assertEquals(Session.WORK_MS-10000,resumed.remainingMs)
    }
    @Test fun recoveryRefreshesEverySecondAtFiveTimesRate() {
        val s=model(); s.command("rest",101000)
        val first=NotificationClock.from(s,101000,5000,100000)
        val next=NotificationClock.from(s,102000,6000,101000)
        assertFalse(first.countdown); assertEquals(first.remainingMs+5000,next.remainingMs)
        assertNotEquals(first.refreshKey,next.refreshKey)
    }
    @Test fun sharedServerClockOffsetIsConvertedToDeviceClock() {
        val s=model()
        val result=NotificationClock.from(s,2000,10000,5000000)
        assertEquals(5000000+s.remainingAt(2000),result.whenWallMs)
        assertEquals(10000+s.remainingAt(2000),result.endElapsedMs)
    }
    @Test fun expiredTimerCannotKeepAPlatformChronometerRunningBelowZero() {
        val s=model()
        for(delay in listOf(0L,1000L,3600000L)) {
            val model=NotificationClock.from(s,s.deadline+delay,10000+delay,5000000+delay)
            assertFalse(model.countdown); assertEquals(0L,model.remainingMs)
        }
    }
}
