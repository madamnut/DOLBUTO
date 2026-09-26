#pragma once
#include "world/edit.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace sandbox {
inline constexpr int spawn_band_width = 8192;
inline constexpr std::array<int, 2> spawn_band_begin{28672, 94208};
inline constexpr unsigned spawn_attempt_limit = 512;
// Returned Y is the feet height. X/Z are block coordinates; place the player at their centres.
// Explicit random seed permits reproducible CPU diagnostics without changing the terrain seed.
std::optional<BlockPos> find_land_spawn(const TerrainGenerator& generator, uint64_t random_seed);
} // namespace sandbox
