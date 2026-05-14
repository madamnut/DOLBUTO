#pragma once

#include "world/WorldTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace dolbuto::world
{
    inline constexpr std::size_t TerrainHeightLutCount = 1024u;

    struct TerrainBuilderConfig
    {
        std::array<uint16_t, TerrainHeightLutCount> heightLut{};
        int activeWorldSeedSalt = 0;
        int seaLevel = 256;

        float terrainNoiseFeatureScale = 220.0f;
        int terrainNoiseOctaveCount = 4;
        float terrainNoiseLacunarity = 2.0f;
        float terrainNoiseGain = 0.5f;
        float terrainNoiseSimplexScale = 1.0f;
        bool terrainDomainWarpEnabled = false;
        float terrainDomainWarpAmplitude = 0.0f;
        float terrainDomainWarpFrequency = 1.0f;
        int terrainDomainWarpOctaveCount = 2;
        float terrainDomainWarpGain = 0.5f;

        float temperatureNoiseStrength = 0.12f;
        float temperatureNoiseFeatureScale = 8192.0f;
        int temperatureNoiseOctaveCount = 2;
        float temperatureNoiseLacunarity = 2.0f;
        float temperatureNoiseGain = 0.5f;
        float temperatureNoiseSimplexScale = 1.0f;

        float precipitationNoiseFeatureScale = 4096.0f;
        int precipitationNoiseOctaveCount = 3;
        float precipitationNoiseLacunarity = 2.0f;
        float precipitationNoiseGain = 0.5f;
        float precipitationNoiseSimplexScale = 1.0f;
    };

    class TerrainBuilder
    {
    public:
        explicit TerrainBuilder(TerrainBuilderConfig config);

        std::shared_ptr<ChunkData> buildChunkData(int chunkX, int chunkZ) const;
        std::array<int, ChunkColumnCount> buildChunkHeightmap(int chunkX, int chunkZ) const;
        std::array<FeatureWriteListPtr, FeatureNeighborCount> buildTreeFeatures(
            const std::shared_ptr<ChunkData>& chunk,
            const std::array<int, ChunkColumnCount>& heights) const;
        bool applyFeatureWrites(
            const std::shared_ptr<ChunkData>& chunk,
            const std::array<FeatureWriteListPtr, FeatureNeighborCount>& incomingFeatureSlots) const;
        std::array<float, ChunkColumnCount> buildChunkTileableClimateNoise(
            int chunkX,
            int chunkZ,
            float featureScale,
            float simplexScale,
            int octaveCount,
            float lacunarity,
            float gain,
            int seed) const;
        void populateChunkClimate(ChunkData& chunk) const;
        float temperatureAtWrapped(int wrappedZ, float noise) const;
        float precipitationAtNoise(float noise) const;

    private:
        int terrainSeed(int offset = 0) const;
        int temperatureSeed() const;
        int precipitationSeed() const;
        float baseTemperatureAtWrappedZ(int wrappedZ) const;

        TerrainBuilderConfig config_;
    };
}
