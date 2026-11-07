package cn.eyebreak.app

/** Never drop a screen-off event just because a request was already in flight. */
class DevicePresence {
    var screenOn: Boolean? = null; private set
    var pendingOff = false; private set
    var epoch = 0L; private set
    var confirmed: Boolean? = null; private set
    fun observe(on: Boolean): Boolean {
        if (screenOn == on) return false
        screenOn = on
        if (!on) { pendingOff = true; epoch++ }
        return true
    }
    fun nextAvailable() = !pendingOff && screenOn == true
    fun acknowledge(available: Boolean, sentEpoch: Long) {
        confirmed = available
        if (!available && sentEpoch == epoch) pendingOff = false
    }
    fun needsReport() = pendingOff || confirmed != (screenOn == true)
    fun mayResume() = screenOn == true && confirmed == true && !pendingOff
}
