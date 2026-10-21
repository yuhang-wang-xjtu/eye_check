#pragma once
#include "timer.h"
#include "reminders.h"
#include <algorithm>
#include <limits>
#include <functional>

// One monotonic timestamp settles all three timers before each state change.
class SessionTiming {
    BreakTimer& eye;
    HealthReminders& health;
    const ReminderSettings& settings;
    std::uint64_t previous;
    bool preservePause = false;
    bool preserveWaiting = false;
public:
    bool locked = false, sleeping = false;
    bool recoverHealth = true;
    std::function<void(std::uint64_t,std::uint64_t)> onActive;
    SessionTiming(BreakTimer& e, HealthReminders& h, const ReminderSettings& s, std::uint64_t now)
        : eye(e), health(h), settings(s), previous(now) {}
    bool blocked() const { return locked || sleeping; }
    void advance(std::uint64_t now) {
        if (now < previous) return;
        if (!blocked() && eye.state == BreakTimer::State::Working) {
            const auto end = std::min(now, eye.deadline);
            if (end > previous) {
                health.addActive(end - previous);
                if(onActive) onActive(end,end-previous);
            }
        } else if (eye.state == BreakTimer::State::Away && recoverHealth) {
            const auto elapsed = now - previous;
            const auto maximum = std::numeric_limits<std::uint64_t>::max() / BreakTimer::AwayRecoveryRate;
            health.recover(std::min(elapsed, maximum) * BreakTimer::AwayRecoveryRate, settings);
        }
        previous = now; health.previous = now;
        health.wasActive = !blocked() && eye.state == BreakTimer::State::Working;
    }
    void lock(std::uint64_t now) {
        if (locked) return;
        advance(now);
        if (!sleeping) {
            preservePause = eye.state == BreakTimer::State::Paused;
            preserveWaiting = eye.state == BreakTimer::State::Waiting || eye.state == BreakTimer::State::Resting;
            eye.away(now);
        }
        locked = true;
    }
    void unlock(std::uint64_t now) {
        if (!locked) return;
        advance(now); locked = false;
        if (!sleeping) restore(now);
    }
    void suspend(std::uint64_t now) {
        if (sleeping) return;
        advance(now);
        if (!locked) {
            preservePause = eye.state == BreakTimer::State::Paused;
            preserveWaiting = eye.state == BreakTimer::State::Waiting || eye.state == BreakTimer::State::Resting;
            eye.away(now);
        }
        sleeping = true;
    }
    void wake(std::uint64_t now) {
        if (!sleeping) return;
        advance(now); sleeping = false;
        if (!locked) restore(now);
    }
private:
    void restore(std::uint64_t now) {
        eye.resume(now);
        if (preservePause) eye.toggle(now);
        else if(preserveWaiting) eye.finish(0,false,now);
        else eye.away(now); // Waking a machine is not evidence that a person returned.
        previous = now;
    }
};
