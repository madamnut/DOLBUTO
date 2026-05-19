#pragma once

#include "config/ViewmodelConfig.h"

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
        int seaLevel = 256;
    };

    struct RenderConfig
    {
        float fluidWaterAlpha = 0.8f;
    };

    WorldConfig loadWorldConfig(const std::filesystem::path& path, const WorldConfig& defaults, int maxSeaLevel);
    RenderConfig loadRenderConfig(const std::filesystem::path& path, const RenderConfig& defaults);
    ViewmodelConfig loadViewmodelConfig(const std::filesystem::path& path, const ViewmodelConfig& defaults);
}
