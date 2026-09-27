#pragma once
#include <cstdint>
namespace sandbox {
enum class SeedDomain : uint32_t {
    climate_shift = 0x513ab921U,
    temperature = 0x731dd23bU,
    precipitation = 0x19ba347dU,
    land_seeds = 0x4c534545U,
    land_growth = 0x4c47524fU,
    growth_large = 0x474c4152U,
    growth_small = 0x47534d41U,
    terrain_tendency = 0x54454e44U,
    sea_tendency = 0x53454154U,
    sea_local = 0x5345414cU,
    terrain_compression = 0x54434f4dU,
    terrain_local = 0x544c4f43U,
    region_sites = 0x13571357U,
    boundary_x = 0x57415258U,
    boundary_z = 0x5741525aU
};
constexpr uint32_t mix_seed(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}
constexpr uint32_t derive_seed(uint32_t master, SeedDomain domain) {
    return mix_seed(master ^ static_cast<uint32_t>(domain));
}
} // namespace sandbox
