package cn.eyebreak.app

import org.junit.Assert.*
import org.junit.Test
import java.time.ZoneId
import java.time.ZonedDateTime

class SessionTest {
    private fun session(now: Long = 0) = Session(now).apply { zone = "Asia/Shanghai"; options.nightEnabled = false }
    @Test fun automaticRestWaitsWithoutNewActivity() {
        val s = session(); assertEquals(listOf("REST_START"), s.tick(Session.WORK_MS))
        assertEquals(Phase.RESTING, s.phase)
        assertEquals(listOf("REST_END"), s.tick(Session.WORK_MS + 25000))
        assertEquals(Phase.WAITING, s.phase)
        s.tick(10 * 86400000); assertEquals(Phase.WAITING, s.phase)
        assertEquals(Session.WORK_MS, s.usage.values.sum())
        s.command("continue", 10 * 86400000); assertEquals(Phase.WORKING, s.phase)
    }
    @Test fun pauseCanEnterRecoveryAndUsageNeverDecreases() {
        val s = session(); s.command("pause", 148000); s.tick(150000)
        assertEquals(1052000, s.remaining)
        s.command("rest", 150000); s.tick(151000)
        assertEquals(1057000, s.remaining); assertEquals(143000, s.workMs)
        assertEquals(148000, s.usage.values.sum())
        s.command("end_work", 151000); assertEquals(148000, s.usage.values.sum())
    }
    @Test fun healthAndWorkUseOneClock() {
        val s = session(); s.tick(148000)
        assertEquals(148000, s.workMs); assertEquals(s.workMs, s.sittingMs)
        s.command("rest", 148000); s.tick(158000)
        assertEquals(1102000, s.remaining); assertEquals(98000, s.workMs)
        s.command("continue", 158000); s.tick(159000)
        assertEquals(99000, s.workMs); assertEquals(149000, s.usage.values.sum())
    }
    @Test fun splitsHistoryAtLocalMidnight() {
        val start = ZonedDateTime.of(2026, 10, 19, 23, 59, 55, 0, ZoneId.of("Asia/Shanghai")).toInstant().toEpochMilli()
        val s = session(start); s.tick(start + 10000)
        assertEquals(5000L, s.usage["2026-10-19"]); assertEquals(5000L, s.usage["2026-10-20"])
    }
    @Test fun lateCallbacksStopAtEyeDeadline() {
        val s = session(); assertEquals(listOf("REST_START"), s.tick(Session.WORK_MS + 100000))
        assertEquals(Phase.RESTING, s.phase); assertEquals(Session.WORK_MS, s.workMs)
        assertEquals(Session.WORK_MS, s.usage.values.sum())
        assertEquals(Session.WORK_MS+125000,s.deadline)
    }
    @Test fun delayedRestDoesNotRestartTimer() {
        val s = session(); s.options.restSeconds = 40
        s.tick(Session.WORK_MS + 25000); assertEquals(Phase.RESTING, s.phase)
        s.tick(Session.WORK_MS + 40000); assertEquals(Phase.RESTING, s.phase)
        s.tick(Session.WORK_MS + 65000); assertEquals(Phase.WAITING, s.phase)
    }
    @Test fun healthFlagsRearmAfterRecovery() {
        val s = session(); s.options.sittingMinutes = 1
        assertTrue(s.tick(60000).contains("SITTING")); assertFalse(s.tick(61000).contains("SITTING"))
        s.command("rest", 61000); s.tick(62000); s.command("continue", 62000)
        assertTrue(s.tick(66000).contains("SITTING"))
    }
    @Test fun backwardsClockDoesNotInventUsage() {
        val s = session(10000); s.tick(11000); s.tick(9000)
        assertEquals(1000, s.usage.values.sum()); assertEquals(Phase.WORKING, s.phase)
    }
    @Test fun resetAndSkipPreserveDailyTotal() {
        val s = session(); s.tick(Session.WORK_MS); s.command("skip", Session.WORK_MS)
        s.command("end_work", Session.WORK_MS + 1000)
        assertEquals(Phase.PAUSED, s.phase); assertEquals(0, s.workMs)
        assertEquals(Session.WORK_MS + 1000, s.usage.values.sum())
    }
}
