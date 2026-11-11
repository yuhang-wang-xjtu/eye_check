#pragma once
#include <set>

// The whole closing chord belongs to acknowledgement, not "continue work".
// Legacy key messages and Raw Input may arrive in either order.
class ReminderKeys {
    std::set<unsigned> held;
public:
    void reset() { held.clear(); }
    bool active() const { return !held.empty(); }
    void dismiss(unsigned key) { if(key && key<256) held.insert(key); }
    bool down(unsigned key) {
        if(!active()) return false;
        dismiss(key); return true;
    }
    bool up(unsigned key) {
        const bool consumed=active(); held.erase(key); return consumed;
    }
};
