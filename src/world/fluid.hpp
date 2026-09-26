#pragma once
#include "world/edit.hpp"
#include <unordered_set>

namespace sandbox {
struct FluidCell {
    Block block{Block::air};
    Fluid fluid{};
};
struct FluidChange {
    BlockPos position;
    Fluid before, after;
};
// Main-thread, fixed-tick solver. Callers commit the returned transaction only after tick returns.
class FluidSimulation {
  public:
    using Sample = std::function<std::optional<FluidCell>(BlockPos)>;
    void wake(BlockPos position);
    void seed(const Column& column, const Sample& sample);
    std::vector<FluidChange> tick(const Sample& sample);
    size_t active_count() const { return active_.size(); }

  private:
    std::unordered_set<uint64_t> active_;
    uint64_t tick_{};
    static uint64_t key(BlockPos p);
    static BlockPos position(uint64_t key);
};
} // namespace sandbox
