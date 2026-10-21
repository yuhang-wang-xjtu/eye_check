#pragma once
#include <cstdint>
#include <map>
#include <algorithm>

// Calendar-day buckets are independent of reminder recovery/reset counters.
class DailyUsage {
public:
    static constexpr std::uint64_t DayMs=86400000;
    std::map<std::uint64_t,std::uint64_t> days;
    bool dirty=false;
    void record(std::uint64_t elapsed, std::uint64_t localEndMs) {
        elapsed=std::min(elapsed,localEndMs);
        auto begin=localEndMs-elapsed;
        while(begin<localEndMs) {
            const auto day=begin/DayMs;
            const auto end=std::min(localEndMs,(day+1)*DayMs);
            days[day]+=end-begin;
            begin=end; dirty=true;
        }
    }
    std::uint64_t total(std::uint64_t day) const {
        const auto found=days.find(day); return found==days.end()?0:found->second;
    }
};
