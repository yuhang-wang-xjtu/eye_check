package cn.eyebreak.app

import org.junit.Assert.*
import org.junit.Test

class ScreenUsagePolicyTest {
    private fun model() = Session(0).apply {
        options.nightEnabled = false; options.sittingEnabled = false; options.workEnabled = false
    }
    @Test fun litScreenKeepsCountingAcrossReminderWithoutWaitingForAChoice() {
        val s=model(); val policy=ScreenUsagePolicy(); policy.observe(s,true,0)
        assertEquals(listOf("REST_START"),policy.tick(s,Session.WORK_MS))
        assertEquals(Phase.WORKING,s.phase)
        assertEquals(2*Session.WORK_MS,s.deadline)
        policy.tick(s,Session.WORK_MS+25000)
        assertEquals(Phase.WORKING,s.phase)
        assertEquals(Session.WORK_MS+25000,s.usage.values.sum())
        assertEquals(Session.WORK_MS+25000,s.workMs)
    }
    @Test fun multipleIntervalsSendOnlyOneLateNotificationAndNeverCountBelowZero() {
        val s=model(); val policy=ScreenUsagePolicy(); policy.observe(s,true,0)
        val late=65*60000L
        assertEquals(listOf("REST_START"),policy.tick(s,late))
        assertEquals(late,s.usage.values.sum()); assertEquals(late,s.sittingMs)
        assertEquals(15*60000L,s.remainingAt(late))
        assertTrue(s.deadline>late)
        assertTrue(policy.tick(s,late).isEmpty())
    }
    @Test fun screenOffStopsUsageAndRecoversAllThreeTimersAtFiveToOne() {
        val s=model(); val policy=ScreenUsagePolicy(); policy.observe(s,true,0)
        policy.observe(s,false,300000); assertEquals(Phase.AWAY,s.phase)
        policy.tick(s,310000)
        assertEquals(300000L,s.usage.values.sum())
        assertEquals(950000L,s.remainingAt(310000))
        assertEquals(250000L,s.sittingMs); assertEquals(250000L,s.workMs)
        policy.observe(s,true,310000); assertEquals(Phase.WORKING,s.phase)
        policy.tick(s,320000)
        assertEquals(310000L,s.usage.values.sum())
        assertEquals(940000L,s.remainingAt(320000))
    }
    @Test fun screenOnResumesImmediatelyWithoutUnlockOrInputEvents() {
        val s=model(); val policy=ScreenUsagePolicy(); policy.observe(s,false,0)
        policy.tick(s,60000); assertEquals(Phase.AWAY,s.phase)
        policy.observe(s,true,60000)
        assertEquals(Phase.WORKING,s.phase)
        policy.tick(s,61000); assertEquals(1000L,s.usage.values.sum())
    }
    @Test fun upgradeMigratesLegacyRestingAndWaitingSnapshotsWithoutQuestion() {
        for (phase in listOf(Phase.RESTING,Phase.WAITING)) {
            val s=model().apply { this.phase=phase }
            val policy=ScreenUsagePolicy()
            assertTrue(policy.observe(s,true,5000).isEmpty())
            assertEquals(Phase.WORKING,s.phase); assertEquals(Session.WORK_MS,s.remainingAt(5000))
        }
    }
    @Test fun repeatedScreenSignalsDoNotCountTheSameTimeTwiceOrUndoManualPause() {
        val s=model(); val policy=ScreenUsagePolicy(); policy.observe(s,true,0)
        policy.tick(s,10000); policy.observe(s,true,10000); policy.tick(s,10000)
        assertEquals(10000L,s.usage.values.sum())
        s.command("pause",10000,notificationOnly=true)
        policy.observe(s,true,20000); policy.tick(s,20000)
        assertEquals(Phase.PAUSED,s.phase); assertEquals(10000L,s.usage.values.sum())
        policy.observe(s,false,20000); policy.observe(s,true,21000)
        assertEquals(Phase.WORKING,s.phase)
    }
    @Test fun screenOffAtDeadlineLeavesRecoveryInsteadOfStartingATimedRest() {
        val s=model(); val policy=ScreenUsagePolicy(); policy.observe(s,true,0)
        policy.observe(s,false,Session.WORK_MS)
        assertEquals(Phase.AWAY,s.phase)
        policy.tick(s,Session.WORK_MS+3600000)
        assertEquals(Session.WORK_MS,s.usage.values.sum())
        assertEquals(Session.WORK_MS,s.remainingAt(s.updatedAt))
    }
}
