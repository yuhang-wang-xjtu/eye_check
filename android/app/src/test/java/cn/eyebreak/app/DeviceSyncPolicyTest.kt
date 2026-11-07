package cn.eyebreak.app

import org.junit.Assert.*
import org.junit.Test

class DeviceSyncPolicyTest {
    @Test fun offImmediatelyStopsOnlyThisPhone() {
        val phone = Session(0); val pc = Session(0)
        DeviceSyncPolicy.tick(phone, false, 10000)
        assertEquals(Phase.AWAY, phone.phase); assertEquals(Phase.WORKING, pc.phase)
        DeviceSyncPolicy.tick(phone, false, 20000)
        assertEquals(10000L, phone.usage.values.sum()); assertEquals(0L, phone.workMs)
        DeviceSyncPolicy.tick(phone, true, 21000)
        assertEquals(Phase.AWAY, phone.phase) // server must acknowledge this phone's own continue
    }
    @Test fun restEndOnlyNotifiesOnceUntilServerContinues() {
        val s = Session(0)
        assertTrue(DeviceSyncPolicy.tick(s, true, Session.WORK_MS).contains("REST_START"))
        assertTrue(DeviceSyncPolicy.tick(s, true, Session.WORK_MS+25000).contains("REST_END"))
        assertEquals(Phase.WAITING, s.phase)
        assertFalse(DeviceSyncPolicy.tick(s, true, Session.WORK_MS+26000).contains("REST_END"))
        assertEquals(Session.WORK_MS, s.usage.values.sum())
    }
    @Test fun restEndsOffWithoutCountingOrContinuing() {
        val s = Session(0); DeviceSyncPolicy.tick(s, true, Session.WORK_MS)
        DeviceSyncPolicy.tick(s, false, Session.WORK_MS+1000)
        assertTrue(DeviceSyncPolicy.tick(s, false, Session.WORK_MS+25000).contains("REST_END"))
        assertEquals(Phase.AWAY, s.phase)
        DeviceSyncPolicy.tick(s, false, Session.WORK_MS+30000)
        assertEquals(Session.WORK_MS, s.usage.values.sum())
    }
}
