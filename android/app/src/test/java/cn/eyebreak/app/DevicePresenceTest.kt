package cn.eyebreak.app

import org.junit.Assert.*
import org.junit.Test

class DevicePresenceTest {
    @Test fun offDuringFlightIsNeverLost() {
        val p = DevicePresence(); p.observe(true)
        val sentEpoch = p.epoch
        p.observe(false); p.acknowledge(true, sentEpoch)
        assertTrue(p.needsReport()); assertFalse(p.nextAvailable()); assertFalse(p.mayResume())
        p.acknowledge(false, p.epoch)
        assertFalse(p.needsReport()); assertFalse(p.mayResume())
    }
    @Test fun rapidOffOnStillReportsOffBeforeOn() {
        val p = DevicePresence(); p.observe(true); p.acknowledge(true, p.epoch)
        p.observe(false); val offEpoch = p.epoch; p.observe(true)
        assertFalse(p.nextAvailable()); assertFalse(p.mayResume())
        p.acknowledge(false, offEpoch)
        assertTrue(p.nextAvailable()); assertTrue(p.needsReport()); assertFalse(p.mayResume())
        p.acknowledge(true, p.epoch)
        assertTrue(p.mayResume()); assertFalse(p.needsReport())
    }
    @Test fun secondOffCannotBeClearedByEarlierReply() {
        val p = DevicePresence(); p.observe(false); val first = p.epoch
        p.observe(true); p.observe(false); p.acknowledge(false, first)
        assertTrue(p.pendingOff); assertTrue(p.needsReport())
    }
}
