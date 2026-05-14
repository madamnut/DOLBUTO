#pragma once

#include <filesystem>

namespace dolbuto::config
{
    struct WorldConfig
    {
        int loadGridScale = 0;
        int terrainWorkerCount = 4;
        int maxTerrainUploadChunksPerFrame = 8;
        int maxTerrainUnloadChunksPerFrame = 16;
        int maxTerrainRetiredDestroyPerFrame = 4;
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
        int seaLevel = 256;
    };

    struct RenderConfig
    {
        float fluidWaterAlpha = 0.8f;
    };

    WorldConfig loadWorldConfig(const std::filesystem::path& path, const WorldConfig& defaults, int maxSeaLevel);
    RenderConfig loadRenderConfig(const std::filesystem::path& path, const RenderConfig& defaults);
}
