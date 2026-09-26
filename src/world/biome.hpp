#pragma once
#include <cmath>

namespace sandbox {
enum class Biome { ocean, river, land };

// Surface classification from the same shifted terrain signals as the density splines.
// Inputs must be finite. No climate, Y, water-block, or neighbouring-column dependency.
// Adapted from OverworldBiomeBuilder's ocean and valley regions, not its full biome lookup.
inline Biome surface_biome(float groundness, float smoothness, float weirdness) {
    if (groundness < -0.19f)
        return Biome::ocean;

    // The original central valley slice corresponds to PV <= about -0.85 near W=0.
    // Test W directly: folded PV also decreases for outlying raw noise values beyond |W|=1.
    if (std::abs(weirdness) > 0.05f)
        return Biome::land;

    // Rough terrain allows rivers only along the coast and near inland, not deep inland.
    if (smoothness < -0.375f)
        return groundness < 0.03f ? Biome::river : Biome::land;
    if (smoothness < 0.55f)
        return Biome::river;
    // The very smooth inland swamp/frozen-river variants are left as land until climate
    // subdivision is introduced; only their coastal river region is retained here.
    return groundness < -0.11f ? Biome::river : Biome::land;
}
} // namespace sandbox
