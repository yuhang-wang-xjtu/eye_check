package cn.eyebreak.app

import org.junit.Assert.*
import org.junit.Test
import java.time.Instant

class ScreenUsagePolicyTest {
    private fun model(now: Long = 0) = Session(now).apply {
        options.nightEnabled = false; options.sittingEnabled = false; options.workEnabled = false
    }
    @Test fun automaticRestHasStartAndEndNotificationsAndExcludesAllUsageCounters() {
        val s=model(); val p=ScreenUsagePolicy(); p.observe(s,true,0)
        assertEquals(listOf("REST_START"),p.tick(s,Session.WORK_MS))
        assertEquals(Phase.RESTING,s.phase)
        p.tick(s,Session.WORK_MS+10000)
        assertEquals(Session.WORK_MS,s.usage.values.sum())
        assertEquals(Session.WORK_MS,s.sittingMs); assertEquals(Session.WORK_MS,s.workMs)
        assertEquals(listOf("REST_END"),p.tick(s,Session.WORK_MS+25000))
        assertEquals(Phase.WORKING,s.phase)
        assertEquals(Session.WORK_MS,s.usage.values.sum())
        p.tick(s,Session.WORK_MS+26000)
        assertEquals(Session.WORK_MS+1000,s.usage.values.sum())
        assertEquals(Session.WORK_MS+1000,s.workMs)
    }
    @Test fun completionNotifiesOnceAndStartsAFreshTwentyMinuteCycle() {
        val s=model(); val p=ScreenUsagePolicy(); p.observe(s,true,0)
        p.tick(s,Session.WORK_MS)
        val end=s.deadline
        assertEquals(listOf("REST_END"),p.tick(s,end))
        assertTrue(p.tick(s,end).isEmpty())
        assertEquals(end+Session.WORK_MS,s.deadline)
        assertEquals(listOf("REST_START"),p.tick(s,s.deadline))
    }
    @Test fun lateWorkCallbackStartsFullRestAndDoesNotChargePastTheWorkDeadline() {
        val s=model(); val p=ScreenUsagePolicy(); p.observe(s,true,0)
        val late=65*60000L
        assertEquals(listOf("REST_START"),p.tick(s,late))
        assertEquals(Phase.RESTING,s.phase)
        assertEquals(Session.WORK_MS,s.usage.values.sum())
        assertEquals(25000L,s.remainingAt(late))
        assertTrue(p.tick(s,late).isEmpty())
        assertEquals(listOf("REST_END"),p.tick(s,late+25000))
        assertEquals(Phase.WORKING,s.phase)
    }
    @Test fun screenOffStopsUsageAndRecoversAllThreeTimersAtFiveToOne() {
        val s=model(); val p=ScreenUsagePolicy(); p.observe(s,true,0)
        p.observe(s,false,300000); assertEquals(Phase.AWAY,s.phase)
        p.tick(s,310000)
        assertEquals(300000L,s.usage.values.sum()); assertEquals(950000L,s.remainingAt(310000))
        assertEquals(250000L,s.sittingMs); assertEquals(250000L,s.workMs)
        p.observe(s,true,310000); assertEquals(Phase.WORKING,s.phase)
        p.tick(s,320000)
        assertEquals(310000L,s.usage.values.sum()); assertEquals(940000L,s.remainingAt(320000))
    }
    @Test fun screenOnResumesImmediatelyWithoutUnlockOrInputEvents() {
        val s=model(); val p=ScreenUsagePolicy(); p.observe(s,false,0)
        p.tick(s,60000); assertEquals(Phase.AWAY,s.phase)
        p.observe(s,true,60000); assertEquals(Phase.WORKING,s.phase)
        p.tick(s,61000); assertEquals(1000L,s.usage.values.sum())
    }
    @Test fun persistedRestingSnapshotKeepsItsOriginalDeadlineOnScreenOn() {
        val s=model().apply { phase=Phase.RESTING; deadline=25000 }
        val p=ScreenUsagePolicy()
        assertTrue(p.observe(s,true,5000).isEmpty())
        assertEquals(Phase.RESTING,s.phase); assertEquals(20000L,s.remainingAt(5000))
        assertEquals(listOf("REST_END"),p.tick(s,25000))
        assertEquals(Phase.WORKING,s.phase); assertTrue(s.usage.isEmpty())
    }
    @Test fun persistedWaitingSnapshotResumesWithoutAQuestion() {
        val s=model().apply { phase=Phase.WAITING }
        val p=ScreenUsagePolicy(); p.observe(s,true,5000)
        assertEquals(Phase.WORKING,s.phase); assertEquals(Session.WORK_MS,s.remainingAt(5000))
    }
    @Test fun repeatedScreenSignalsDoNotCountTwiceOrUndoAnExplicitPause() {
        val s=model(); val p=ScreenUsagePolicy(); p.observe(s,true,0)
        p.tick(s,10000); p.observe(s,true,10000); p.tick(s,10000)
        assertEquals(10000L,s.usage.values.sum())
        s.command("pause",10000); p.observe(s,true,20000); p.tick(s,20000)
        assertEquals(Phase.PAUSED,s.phase); assertEquals(10000L,s.usage.values.sum())
        p.observe(s,false,20000); p.observe(s,true,21000)
        assertEquals(Phase.WORKING,s.phase)
    }
    @Test fun screenOffAtDeadlineDoesNotCancelAutomaticRestCompletion() {
        val s=model(); val p=ScreenUsagePolicy(); p.observe(s,true,0)
        assertEquals(listOf("REST_START"),p.observe(s,false,Session.WORK_MS))
        assertEquals(Phase.RESTING,s.phase)
        assertEquals(listOf("REST_END"),p.tick(s,Session.WORK_MS+25000))
        assertEquals(Phase.AWAY,s.phase); assertEquals(Session.WORK_MS,s.usage.values.sum())
    }
    @Test fun restEndsWhileScreenOffNotifiesAndStaysInRecoveryWithoutDoubleCredit() {
        val s=model(); val p=ScreenUsagePolicy(); p.observe(s,true,0)
        p.tick(s,Session.WORK_MS); p.observe(s,false,Session.WORK_MS+5000)
        assertEquals(Phase.RESTING,s.phase)
        assertEquals(listOf("REST_END"),p.tick(s,Session.WORK_MS+25000))
        assertEquals(Phase.AWAY,s.phase); assertEquals(Session.WORK_MS-100000,s.workMs)
        p.tick(s,Session.WORK_MS+26000)
        assertEquals(Session.WORK_MS-105000,s.workMs)
        p.observe(s,true,Session.WORK_MS+27000)
        assertEquals(Phase.WORKING,s.phase); assertEquals(Session.WORK_MS-110000,s.workMs)
        assertEquals(Session.WORK_MS,s.usage.values.sum())
    }
    @Test fun screenOnDuringRestDoesNotSkipRemainingRestSeconds() {
        val s=model(); val p=ScreenUsagePolicy(); p.observe(s,true,0)
        p.tick(s,Session.WORK_MS); p.observe(s,false,Session.WORK_MS+5000)
        p.observe(s,true,Session.WORK_MS+10000)
        assertEquals(Phase.RESTING,s.phase); assertEquals(15000L,s.remainingAt(s.updatedAt))
        assertEquals(Session.WORK_MS-25000,s.workMs)
        assertEquals(listOf("REST_END"),p.tick(s,Session.WORK_MS+25000))
        assertEquals(Phase.WORKING,s.phase); assertEquals(Session.WORK_MS,s.usage.values.sum())
    }
    @Test fun configurableRestDurationIsRespected() {
        val s=model().apply { options.restSeconds=10 }
        val p=ScreenUsagePolicy(); p.observe(s,true,0); p.tick(s,Session.WORK_MS)
        assertTrue(p.tick(s,Session.WORK_MS+9999).isEmpty())
        assertEquals(Phase.RESTING,s.phase)
        assertEquals(listOf("REST_END"),p.tick(s,Session.WORK_MS+10000))
        assertEquals(Phase.WORKING,s.phase)
    }
    @Test fun restCrossingMidnightIsNotChargedToEitherDay() {
        val start=Instant.parse("2025-01-01T23:40:00Z").toEpochMilli()
        val s=model(start).apply { zone="UTC" }; val p=ScreenUsagePolicy()
        p.observe(s,true,start); p.tick(s,start+Session.WORK_MS)
        p.tick(s,start+Session.WORK_MS+25000); p.tick(s,start+Session.WORK_MS+30000)
        assertEquals(Session.WORK_MS,s.usage["2025-01-01"])
        assertEquals(5000L,s.usage["2025-01-02"])
    }
}
