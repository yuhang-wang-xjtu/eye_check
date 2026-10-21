#include "../src/session_timing.h"
#include <iostream>
#include <cstdlib>
void check(bool ok, const char* label) { if (!ok) { std::cerr << label << '\n'; std::exit(1); } }
int main() {
    ReminderSettings settings;
    BreakTimer eye(0); HealthReminders health(0); SessionTiming clock(eye, health, settings, 0);
    clock.advance(148000);
    check(eye.left(148000) == 1052000 && health.sittingMs == 148000 && health.workMs == 148000, "all timers account the same 148 seconds");
    clock.lock(148000); clock.lock(150000);
    check(clock.blocked() && eye.state == BreakTimer::State::Away, "duplicate lock preserves first start");
    clock.advance(153000); clock.unlock(158000);
    check(eye.left(158000) == 1102000, "ten locked seconds recover fifty seconds of eye timer");
    check(health.sittingMs == 98000 && health.workMs == 98000, "health timers recover the same fifty seconds exactly once");
    check(!clock.blocked() && eye.state == BreakTimer::State::Away, "unlock requires fresh human activity");
    eye.resume(158000);
    clock.advance(159000);
    check(eye.left(159000) == 1101000 && health.sittingMs == 99000, "all timers run at normal speed after unlock");
    clock.unlock(160000); clock.advance(160000);
    check(health.sittingMs == 100000, "duplicate unlock cannot double credit");
    clock.lock(160000); clock.suspend(161000); clock.wake(171000);
    check(clock.blocked() && clock.locked && eye.state == BreakTimer::State::Away, "wake cannot resume a locked session");
    clock.unlock(172000);
    check(health.sittingMs == 40000 && eye.left(172000) == 1160000, "lock plus sleep shares one continuous recovery interval");
    eye.resume(172000);
    clock.advance(173000); eye.toggle(173000);
    clock.lock(173000); clock.unlock(183000);
    check(eye.state == BreakTimer::State::Paused && eye.left(183000) == BreakTimer::WorkMs, "manual pause preserved while recovery is capped");
    check(health.sittingMs == 0 && health.workMs == 0, "health recovery cannot become negative");
    clock.advance(190000); check(health.sittingMs == 0, "paused interval does not count");
    eye.toggle(190000); clock.advance(191000); eye.away(191000);
    clock.advance(192000); eye.resume(192000);
    check(health.sittingMs == 0 && eye.left(192000) == BreakTimer::WorkMs, "manual away shares recovery rules");
    clock.advance(193000); eye.rest(193000); clock.advance(200000);
    check(health.sittingMs == 1000, "eye rest does not accumulate working time");
    clock.lock(200000); clock.unlock(201000);
    check(eye.state == BreakTimer::State::Waiting, "lock during rest cannot restart a work cycle");
    clock.suspend(202000); clock.wake(203000);
    check(eye.state==BreakTimer::State::Waiting,"automatic wake preserves waiting state");
    BreakTimer delayedEye(0); HealthReminders delayedHealth(0); SessionTiming delayed(delayedEye, delayedHealth, settings, 0);
    delayed.advance(BreakTimer::WorkMs + 15000);
    check(delayedHealth.sittingMs == BreakTimer::WorkMs, "delayed callbacks cannot add work beyond eye deadline");
    std::cout << "All unified timing and lock checks passed.\n";
}
