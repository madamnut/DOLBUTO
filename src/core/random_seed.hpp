#pragma once
#include <cstdint>
namespace sandbox {
enum class SeedDomain : uint32_t {
    climate_shift = 0x513ab921U,
    temperature = 0x731dd23bU,
    precipitation = 0x19ba347dU,
    region_sites = 0x13571357U,
    fine_sites = 0x24682468U,
    continent = 0x414c414eU,
    archipelago = 0x41524348U,
    island = 0x49534c45U,
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
