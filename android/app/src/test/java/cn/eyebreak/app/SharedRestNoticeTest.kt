package cn.eyebreak.app
import org.junit.Assert.*
import org.junit.Test

class SharedRestNoticeTest {
    private fun resting()=Session(0).apply { coordinated=true; phase=Phase.RESTING; deadline=25000; restCycle=1; roomKey="same-room"; devicePhase=Phase.AWAY }
    @Test fun lockedPhoneStillGetsEndOnceWithoutNetwork() {
        val s=resting()
        assertTrue(SharedRestNotice.due(s,24999).isEmpty())
        assertEquals(listOf("REST_END"),SharedRestNotice.due(s,25000))
        assertTrue(SharedRestNotice.due(s,30000).isEmpty())
        assertNull(ReminderDeadline.next(s,30000))
        assertEquals(Phase.AWAY,s.devicePhase)
    }
    @Test fun pcPollCannotConsumePhoneEndAndLateResponseDoesNotDuplicate() {
        val old=resting(); val next=Session(25000).apply {coordinated=true;restCycle=1;restFinished=1}
        assertEquals(listOf("REST_END"),SharedRestNotice.received(old,next,"same-room"))
        val later=Session(30000).apply {coordinated=true;restCycle=1;restFinished=1}
        assertTrue(SharedRestNotice.received(next,later,"same-room").isEmpty())
        val offline=resting(); SharedRestNotice.due(offline,25000)
        assertTrue(SharedRestNotice.received(offline,later,"same-room").isEmpty())
    }
    @Test fun switchingRoomDoesNotPlayHistoricalEnd() {
        val old=resting(); val next=Session(0).apply {coordinated=true;restCycle=8;restFinished=8}
        assertTrue(SharedRestNotice.received(old,next,"different-room").isEmpty())
    }
    @Test fun overdueSharedSnapshotDoesNotCreateRapidAlarmLoop() {
        val s=Session(0).apply {coordinated=true;deadline=1000;options.nightEnabled=false}
        assertNull(ReminderDeadline.next(s,1000))
        s.deadline=10000; s.options.sittingMinutes=1; s.sittingMs=60000
        assertEquals(10000L,ReminderDeadline.next(s,2000))
    }
}
