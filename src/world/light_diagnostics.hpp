#pragma once
#include <cstdint>

namespace sandbox {
// Compiled out of the game. The opt-in comparison executable enables these counters.
struct LightWorkStats {
    uint64_t pushes{}, pops{}, stale{}, updates{}, boundary_cells{}, halo_cells{};
};
#ifdef SANDBOX_LIGHT_BENCHMARK
inline thread_local LightWorkStats* light_work_stats{};
inline void light_count(uint64_t LightWorkStats::* field, uint64_t amount = 1) {
    if (light_work_stats)
        light_work_stats->*field += amount;
}
#else
inline void light_count(uint64_t LightWorkStats::*, uint64_t = 1) {}
#endif
} // namespace sandbox
