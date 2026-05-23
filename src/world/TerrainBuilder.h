#pragma once

#include "world/BlockData.h"
#include "world/WorldTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace dolbuto::world
{
    inline constexpr std::size_t TerrainSplineLutCount = 1024u;

    struct TerrainBuilderConfig
    {
        std::array<float, TerrainSplineLutCount> heightLut{};
        std::array<float, TerrainSplineLutCount> groundnessBaselineLut{};
        std::array<float, TerrainSplineLutCount> groundnessInfluenceLut{};
        std::array<float, TerrainSplineLutCount> smoothnessInfluenceLut{};
        std::array<float, TerrainSplineLutCount> pvWeightLut{};
        std::array<float, TerrainSplineLutCount> groundnessPvWeightLut{};
        std::array<float, TerrainSplineLutCount> smoothnessPvWeightLut{};
        LightAttenuationTablesPtr lightAttenuationTables;
        int activeWorldSeedSalt = 0;
        int seaLevel = 256;

        float groundnessNoiseFeatureScale = 2000.0f;
        int groundnessNoiseOctaveCount = 4;
        float groundnessNoiseLacunarity = 3.0f;
        float groundnessNoiseGain = 0.3f;
        bool groundnessDomainWarpEnabled = true;
        float groundnessDomainWarpAmplitude = 0.5f;
        float groundnessDomainWarpFrequency = 1.0f;
        int groundnessDomainWarpOctaveCount = 2;
        float groundnessDomainWarpGain = 0.5f;

        float baseNoiseFeatureScale = 1000.0f;
        int baseNoiseOctaveCount = 2;
        float baseNoiseLacunarity = 2.0f;
        float baseNoiseGain = 0.5f;

        float smoothnessNoiseFeatureScale = 4000.0f;
        int smoothnessNoiseOctaveCount = 2;
        float smoothnessNoiseLacunarity = 2.0f;
        float smoothnessNoiseGain = 0.5f;

        float weirdnessNoiseFeatureScale = 4000.0f;
        int weirdnessNoiseOctaveCount = 1;
        float weirdnessNoiseLacunarity = 2.0f;
        float weirdnessNoiseGain = 0.5f;
        bool weirdnessDomainWarpEnabled = true;
        float weirdnessDomainWarpAmplitude = 0.3f;
        float weirdnessDomainWarpFrequency = 1.0f;
        int weirdnessDomainWarpOctaveCount = 2;
        float weirdnessDomainWarpGain = 0.5f;

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

    struct TerrainDebugSample
    {
        float groundness = 0.0f;
        float smoothness = 0.0f;
        float weirdness = 0.0f;
        float pv = 0.0f;
        float baseline = 0.0f;
        float influence = 0.0f;
        float rawTerrainValue = 0.0f;
        float normalizedTerrainValue = 0.0f;
        float pvWeight = 0.0f;
        float pvMultiplier = 1.0f;
        float terrainValue = 0.0f;
        int height = 0;
    };

    enum class TerrainDebugNoise
    {
        Groundness,
        Smoothness,
        Weirdness,
        Pv
    };

    class TerrainBuilder
    {
    public:
        explicit TerrainBuilder(TerrainBuilderConfig config);

        std::shared_ptr<ChunkData> buildChunkData(int chunkX, int chunkZ) const;
        std::array<int, ChunkColumnCount> buildChunkHeightmap(int chunkX, int chunkZ) const;
        std::array<TerrainDebugSample, ChunkColumnCount> buildChunkTerrainDebugSamples(int chunkX, int chunkZ) const;
        std::vector<TerrainDebugSample> buildTerrainDebugSamples(int sampleSize, int worldExtentBlocks) const;
        std::vector<float> buildTerrainDebugNoise(TerrainDebugNoise noise, int sampleSize, int worldExtentBlocks) const;
        float groundnessAtWorld(int worldX, int worldZ) const;
        TerrainDebugSample sampleTerrainAtWorld(int worldX, int worldZ) const;
        uint16_t surfaceBlockAtWorld(int worldX, int worldZ, int* surfaceY = nullptr) const;
        std::array<FeatureWriteListPtr, FeatureNeighborCount> buildTreeFeatures(
            const std::shared_ptr<ChunkData>& chunk,
            const std::array<int, ChunkColumnCount>& heights) const;
        std::shared_ptr<ChunkData> resolveFeaturesForCenter(
            const std::array<std::shared_ptr<ChunkData>, 9>& sourceChunks) const;
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
        int groundnessSeed(int offset = 0) const;
        int smoothnessSeed() const;
        int weirdnessSeed(int offset = 0) const;
        int baseNoiseSeed() const;
        int temperatureSeed() const;
        int precipitationSeed() const;
        float baseTemperatureAtWrappedZ(int wrappedZ) const;

        TerrainBuilderConfig config_;
    };
}
