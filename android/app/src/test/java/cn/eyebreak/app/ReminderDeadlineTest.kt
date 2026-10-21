package cn.eyebreak.app

import org.junit.Assert.*
import org.junit.Test
import java.time.ZonedDateTime

class ReminderDeadlineTest {
    private fun model() = Session(0).apply { options.nightEnabled=false }
    @Test fun eyeAlarmTargetsTheDeadlineRatherThanEveryMinute() {
        val s=model()
        assertEquals(Session.WORK_MS,ReminderDeadline.next(s,0))
        s.tick(61000)
        assertEquals(Session.WORK_MS,ReminderDeadline.next(s,61000))
    }
    @Test fun restCompletionGetsAnIndependentShortDeadline() {
        val s=model(); s.tick(Session.WORK_MS)
        assertEquals(Session.WORK_MS+25000,ReminderDeadline.next(s,s.updatedAt))
        s.tick(s.deadline); assertEquals(Phase.WAITING,s.phase)
        assertNull(ReminderDeadline.next(s,s.updatedAt))
        s.tick(s.updatedAt+3600000); assertEquals(Phase.WAITING,s.phase)
    }
    @Test fun earliestUnsentHealthReminderWins() {
        val s=model(); s.options.sittingMinutes=2; s.options.workMinutes=3
        assertEquals(120000L,ReminderDeadline.next(s,0))
        s.tick(120000); assertEquals(180000L,ReminderDeadline.next(s,120000))
        s.tick(180000); assertEquals(Session.WORK_MS,ReminderDeadline.next(s,180000))
    }
    @Test fun pausedAndRecoveryStatesDoNotScheduleWorkAlarms() {
        val s=model(); s.command("pause",1000); assertNull(ReminderDeadline.next(s,1000))
        s.command("rest",2000); assertNull(ReminderDeadline.next(s,2000))
    }
    @Test fun dueAlarmCacheMustBeClearedAfterDeliveryAndPermissionChanges() {
        val ticket=AlarmTicket()
        ticket.armed(1200000,true); assertTrue(ticket.matches(1200000,true))
        assertFalse(ticket.matches(1200000,false))
        ticket.clear(); assertFalse(ticket.matches(1200000,true))
        ticket.armed(1200000,true); assertFalse(ticket.matches(1225000,true))
    }
    @Test fun nightIsScheduledAtActualLocalThresholdAndDoesNotRepeat() {
        val start=ZonedDateTime.parse("2025-01-01T22:55:00+08:00[Asia/Shanghai]").toInstant().toEpochMilli()
        val s=Session(start).apply { zone="Asia/Shanghai" }
        assertEquals(start+5*60000,ReminderDeadline.next(s,start))
        s.tick(start+5*60000)
        assertEquals(s.deadline,ReminderDeadline.next(s,s.updatedAt))
    }
    @Test fun multiHourCallbackDelayStillStartsFullVisibleRestAndThenWaits() {
        val s=model(); val late=12*3600000L
        assertEquals(listOf("REST_START"),s.tick(late))
        assertEquals(late+25000,s.deadline)
        assertEquals(listOf("REST_END"),s.tick(late+25000))
        assertEquals(Phase.WAITING,s.phase)
        assertEquals(Session.WORK_MS,s.usage.values.sum())
    }
}
