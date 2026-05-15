#pragma once

#include "world/TerrainBuilder.h"
#include "world/WorldTypes.h"

#include <cstdint>
#include <vector>

namespace dolbuto::world
{
    class ClimateSystem
    {
    public:
        explicit ClimateSystem(TerrainBuilderConfig config);

        static float decodeClimateValue(uint8_t value);

        int temperatureSeed() const;
        int precipitationSeed() const;
        std::vector<float> buildTileableNoise(
            int sampleSize,
            float featureScale,
            float simplexScale,
            int octaveCount,
            float lacunarity,
            float gain,
            int seed) const;
        float sampleTileableNoise(
            int wrappedX,
            int wrappedZ,
            float featureScale,
            float simplexScale,
            int octaveCount,
            float lacunarity,
            float gain,
            int seed) const;
        void populateChunkClimate(ChunkData& chunk) const;
        float temperatureAtWrapped(int wrappedZ, float noise) const;
        float precipitationAtNoise(float noise) const;

        const TerrainBuilderConfig& config() const;

    private:
        TerrainBuilderConfig config_;
    };
}
