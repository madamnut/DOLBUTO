#pragma once
#include "core/world_rules.hpp"
#include <algorithm>
#include <array>

namespace sandbox {
struct WorldDate {
    uint64_t year{}, day_number{1};
    unsigned month{1}, day{1}, hour{}, minute{};
};
inline WorldDate world_date(uint64_t ticks) {
    constexpr std::array<unsigned, 12> month_days{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const uint64_t days = ticks / ticks_per_day;
    WorldDate result;
    result.year = days / 365;
    result.day_number = days + 1;
    unsigned remaining = static_cast<unsigned>(days % 365);
    while (remaining >= month_days[result.month - 1]) {
        remaining -= month_days[result.month - 1];
        ++result.month;
    }
    result.day = remaining + 1;
    const auto time = static_cast<unsigned>(ticks % ticks_per_day);
    result.hour = time / ticks_per_hour;
    result.minute = (time % ticks_per_hour) / (ticks_per_hour / 60);
    return result;
}
class WorldClock {
  public:
    uint64_t ticks() const { return static_cast<uint64_t>(ticks_); }
    double day_ticks() const { return std::fmod(ticks_, double(ticks_per_day)); }
    WorldDate date() const { return world_date(ticks()); }
    void advance(double delta) { set(ticks_ + delta); }
    void set(double ticks) {
        if (std::isfinite(ticks))
            // No dates before the epoch; keep integer ticks exactly representable in double.
            ticks_ = std::clamp(ticks, 0.0, 9007199254740991.0);
    }

  private:
    double ticks_{start_day_tick}; // Retain fractional ticks for smooth manual time adjustment.
};
} // namespace sandbox
