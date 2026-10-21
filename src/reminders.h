#pragma once
#include <cstdint>
#include <limits>

struct ReminderSettings {
    bool sittingEnabled = true;
    bool workEnabled = true;
    bool nightEnabled = true;
    bool soundEnabled = true;
    int sittingMinutes = 60;
    int workMinutes = 120;
    int nightHour = 23;
    int nightMinute = 0;
};

class HealthReminders {
public:
    enum Event { None = 0, Sitting = 1, LongWork = 2, LateNight = 4 };
    std::uint64_t sittingMs = 0, workMs = 0;
    bool sittingSent = false, workSent = false;
    std::int64_t lastNight = std::numeric_limits<std::int64_t>::min();
    std::uint64_t previous;
    bool wasActive = true;
    explicit HealthReminders(std::uint64_t now) : previous(now) {}
    void addActive(std::uint64_t elapsed) { sittingMs += elapsed; workMs += elapsed; }
    void recover(std::uint64_t elapsed, const ReminderSettings& settings) {
        sittingMs = elapsed >= sittingMs ? 0 : sittingMs - elapsed;
        workMs = elapsed >= workMs ? 0 : workMs - elapsed;
        if (sittingMs < static_cast<std::uint64_t>(settings.sittingMinutes) * 60000) sittingSent = false;
        if (workMs < static_cast<std::uint64_t>(settings.workMinutes) * 60000) workSent = false;
    }

    void update(std::uint64_t now, bool active) {
        if (active && wasActive && now >= previous) {
            const auto elapsed = now - previous;
            sittingMs += elapsed;
            workMs += elapsed;
        }
        previous = now;
        wasActive = active;
    }
    int due(const ReminderSettings& settings, std::int64_t day, int minute, bool active) {
        if (!active) return None;
        int events = None;
        if (settings.sittingEnabled && !sittingSent && sittingMs >= static_cast<std::uint64_t>(settings.sittingMinutes) * 60000) {
            events |= Sitting; sittingSent = true;
        }
        if (settings.workEnabled && !workSent && workMs >= static_cast<std::uint64_t>(settings.workMinutes) * 60000) {
            events |= LongWork; workSent = true;
        }
        const int start = settings.nightHour * 60 + settings.nightMinute;
        const bool night = start >= 360 ? (minute >= start || minute < 360) : (minute >= start && minute < 360);
        const auto nightDay = start >= 360 && minute < 360 ? day - 1 : day;
        if (settings.nightEnabled && night && lastNight != nightDay) {
            events |= LateNight; lastNight = nightDay;
        }
        return events;
    }
    void stoodUp() { sittingMs = 0; sittingSent = false; }
    void endWork() { stoodUp(); workMs = 0; workSent = false; }
    void wake(std::uint64_t now, bool active) { endWork(); previous = now; wasActive = active; }
};
