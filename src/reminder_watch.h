#pragma once
#include <array>
#include <vector>

// Physical key-state edges supplement messages when a game keeps foreground
// input. A key already held when the popup appears is never acknowledgement.
class ReminderWatch {
    std::array<bool,256> previous{};
public:
    void prime(const std::array<bool,256>& down) { previous=down; }
    bool press(unsigned key) {
        if(key>=previous.size()) return false;
        const bool fresh=!previous[key]; previous[key]=true; return fresh;
    }
    void release(unsigned key) { if(key<previous.size()) previous[key]=false; }
    std::vector<unsigned> sample(const std::array<bool,256>& down) {
        std::vector<unsigned> pressed;
        for(unsigned key=1;key<down.size();++key) if(down[key] && !previous[key]) pressed.push_back(key);
        previous=down; return pressed;
    }
};
