#pragma once
#include <cstdint>

class BreakTimer {
public:
    enum class State { Working, Paused, Resting, Waiting, Away };
    static constexpr std::uint64_t WorkMs = 20 * 60 * 1000;
    static constexpr std::uint64_t RestMs = 25 * 1000;
    State state = State::Working;
    std::uint64_t deadline = 0;
    std::uint64_t remaining = WorkMs;
    std::uint64_t restDurationMs = RestMs;
    std::uint32_t waitingInput = 0;
    bool inputBaselineValid = false;
    std::uint32_t completionInputCutoff = 0;
    bool completionCutoffValid = false;
    std::uint64_t awayStarted = 0;
    static constexpr std::uint64_t AwayRecoveryRate = 5;

    explicit BreakTimer(std::uint64_t now) : deadline(now + WorkMs) {}
    std::uint64_t left(std::uint64_t now) const {
        if (state == State::Away) {
            const auto elapsed = now > awayStarted ? now - awayStarted : 0;
            const auto room = WorkMs - remaining;
            if (elapsed >= (room + AwayRecoveryRate - 1) / AwayRecoveryRate) return WorkMs;
            return remaining + elapsed * AwayRecoveryRate;
        }
        if (state == State::Paused || state == State::Waiting) return remaining;
        return deadline > now ? deadline - now : 0;
    }
    void resume(std::uint64_t now) {
        if (state == State::Away) remaining = left(now);
        deadline = now + remaining; state = State::Working; inputBaselineValid = false;
    }
    void toggle(std::uint64_t now) {
        if (state == State::Working) {
            remaining = left(now);
            state = State::Paused;
        } else if (state != State::Resting) resume(now);
    }
    void rest(std::uint64_t now) {
        if (state == State::Resting || state == State::Away) return;
        state = State::Resting;
        deadline = now + restDurationMs;
    }
    void finish(std::uint32_t input = 0, bool valid = false, std::uint64_t now = 0) {
        state = State::Waiting;
        remaining = WorkMs;
        waitingInput = input; inputBaselineValid = valid;
        completionInputCutoff = static_cast<std::uint32_t>(now);
        completionCutoffValid = now != 0;
    }
    void skip(std::uint64_t now) { remaining = WorkMs; resume(now); }
    bool deviceActivity(std::uint64_t now) {
        if (state != State::Waiting && state != State::Away) return false;
        resume(now); return true;
    }
    void away(std::uint64_t now, std::uint32_t input = 0, bool valid = false) {
        if (state == State::Away) return;
        remaining = state == State::Working || state == State::Paused ? left(now) : WorkMs;
        state = State::Away; awayStarted = now;
        waitingInput = input; inputBaselineValid = valid;
    }
    // Returns true only when a scheduled rest starts.
    bool tick(std::uint64_t now, std::uint32_t input = 0, bool valid = false) {
        if (state == State::Working && now >= deadline) { rest(now); return true; }
        if (state == State::Resting && now >= deadline) finish(input, valid, now);
        else if ((state == State::Waiting || state == State::Away) && valid) {
            if (!inputBaselineValid) { waitingInput = input; inputBaselineValid = true; }
            else if (waitingInput != input &&
                (state != State::Waiting || !completionCutoffValid ||
                 (static_cast<std::uint32_t>(input-completionInputCutoff) > 0 &&
                  static_cast<std::uint32_t>(input-completionInputCutoff) < 0x80000000U))) resume(now);
        }
        return false;
    }
    void resetAfterSleep(std::uint64_t now) {
        if (state == State::Resting) finish();
        remaining = WorkMs;
        awayStarted = now;
        deadline = now + WorkMs;
        inputBaselineValid = false;
    }
};
