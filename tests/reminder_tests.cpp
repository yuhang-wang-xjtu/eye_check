#include "../src/reminders.h"
#include <cstdlib>
#include <iostream>
void check(bool ok, const char* name) { if (!ok) { std::cerr << name << '\n'; std::exit(1); } }
int main() {
    ReminderSettings settings;
    HealthReminders r(0);
    r.update(3600000, true);
    check(r.due(settings, 100, 800, true) == HealthReminders::Sitting, "sitting threshold");
    check(r.due(settings, 100, 800, true) == 0, "no repeat without reset");
    r.stoodUp();
    check(r.workMs == 3600000 && r.sittingMs == 0, "standing preserves work cycle");
    r.update(7200000, true);
    check(r.due(settings, 100, 800, true) == (HealthReminders::Sitting | HealthReminders::LongWork), "combined thresholds");
    r.endWork();
    r.update(7200100, false);
    r.update(9900100, false);
    r.update(9900200, true);
    check(r.workMs == 0, "pause does not accumulate");
    r.update(9900300, true);
    check(r.workMs == 100, "resume counts new active time");
    check(r.due(settings, 100, 1380, true) == HealthReminders::LateNight, "night start");
    check(r.due(settings, 101, 120, true) == 0, "no duplicate across midnight");
    check(r.due(settings, 101, 360, true) == 0, "night ends at six");
    check(r.due(settings, 101, 1380, false) == 0, "night suppressed while paused");
    check(r.due(settings, 101, 1380, true) == HealthReminders::LateNight, "next night");
    r.wake(20000000, true);
    check(r.workMs == 0 && r.sittingMs == 0, "sleep resets work cycles");
    check(r.due(settings, 102, 120, true) == 0, "wake preserves nightly deduplication");
    settings.nightHour = 2;
    check(r.due(settings, 103, 60, true) == 0, "early morning custom start");
    check(r.due(settings, 103, 120, true) == HealthReminders::LateNight, "early morning trigger");
    settings.sittingEnabled = settings.workEnabled = settings.nightEnabled = false;
    r.update(99999999, true);
    check(r.due(settings, 104, 1380, true) == 0, "disabled reminders");
    std::cout << "All health reminder checks passed.\n";
}
