#include "renderer/RendererConfigBridge.h"

#include "config/ConfigLoaders.h"
#include "renderer/RendererAssetStore.h"
#include "game/ClientRuntimeState.h"
#include "renderer/RendererGpuResources.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace dolbuto
{
    namespace
    {
        constexpr int ChunkSizeY = 512;
        constexpr int DefaultSeaLevel = 256;
        constexpr int DefaultLoadGridScale = 1;
        constexpr int DefaultTerrainWorkerCount = 4;
        constexpr int DefaultMaxTerrainUploadChunksPerFrame = 8;
        constexpr int DefaultMaxTerrainUnloadChunksPerFrame = 16;
        constexpr int DefaultMaxTerrainRetiredDestroyPerFrame = 4;
        constexpr float DefaultGroundnessNoiseFeatureScale = 2000.0f;
        constexpr int DefaultGroundnessNoiseOctaveCount = 4;
        constexpr float DefaultGroundnessNoiseLacunarity = 3.0f;
        constexpr float DefaultGroundnessNoiseGain = 0.3f;
        constexpr bool DefaultGroundnessDomainWarpEnabled = true;
        constexpr float DefaultGroundnessDomainWarpAmplitude = 0.5f;
        constexpr float DefaultGroundnessDomainWarpFrequency = 1.0f;
        constexpr int DefaultGroundnessDomainWarpOctaveCount = 2;
        constexpr float DefaultGroundnessDomainWarpGain = 0.5f;
        constexpr float DefaultBaseNoiseFeatureScale = 1000.0f;
        constexpr int DefaultBaseNoiseOctaveCount = 2;
        constexpr float DefaultBaseNoiseLacunarity = 2.0f;
        constexpr float DefaultBaseNoiseGain = 0.5f;
        constexpr float DefaultSmoothnessNoiseFeatureScale = 4000.0f;
        constexpr int DefaultSmoothnessNoiseOctaveCount = 2;
        constexpr float DefaultSmoothnessNoiseLacunarity = 2.0f;
        constexpr float DefaultSmoothnessNoiseGain = 0.5f;
        constexpr float DefaultWeirdnessNoiseFeatureScale = 4000.0f;
        constexpr int DefaultWeirdnessNoiseOctaveCount = 1;
        constexpr float DefaultWeirdnessNoiseLacunarity = 2.0f;
        constexpr float DefaultWeirdnessNoiseGain = 0.5f;
        constexpr bool DefaultWeirdnessDomainWarpEnabled = true;
        constexpr float DefaultWeirdnessDomainWarpAmplitude = 0.3f;
        constexpr float DefaultWeirdnessDomainWarpFrequency = 1.0f;
        constexpr int DefaultWeirdnessDomainWarpOctaveCount = 2;
        constexpr float DefaultWeirdnessDomainWarpGain = 0.5f;
        constexpr float DefaultTemperatureNoiseStrength = 0.12f;
        constexpr float DefaultTemperatureNoiseFeatureScale = 8192.0f;
        constexpr int DefaultTemperatureNoiseOctaveCount = 2;
        constexpr float DefaultTemperatureNoiseLacunarity = 2.0f;
        constexpr float DefaultTemperatureNoiseGain = 0.5f;
        constexpr float DefaultTemperatureNoiseSimplexScale = 1.0f;
        constexpr float DefaultPrecipitationNoiseFeatureScale = 4096.0f;
        constexpr int DefaultPrecipitationNoiseOctaveCount = 3;
        constexpr float DefaultPrecipitationNoiseLacunarity = 2.0f;
        constexpr float DefaultPrecipitationNoiseGain = 0.5f;
        constexpr float DefaultPrecipitationNoiseSimplexScale = 1.0f;
        constexpr float DefaultFluidWaterAlpha = 0.8f;
        constexpr uint32_t SplineLutVersion = 1;
        constexpr uint32_t SplineLutCount = 1024;
        constexpr float SplineLutInputMin = -2.0f;
        constexpr float SplineLutInputMax = 2.0f;
        constexpr float PvLutInputMin = -1.0f;
        constexpr float PvLutInputMax = 1.0f;
        constexpr float HeightLutInputMin = 0.0f;
        constexpr float HeightLutInputMax = 2.0f;
    }

    RendererConfigBridge::RendererConfigBridge(
        game::ClientRuntimeState& client,
        RendererAssetStore& rendererAssets,
        VulkanResourceManager& gpuResources) :
        client_(client),
        rendererAssets_(rendererAssets),
        gpuResources_(gpuResources)
    {
    }

    void RendererConfigBridge::loadContentAndAssets(const std::filesystem::path& assetDirectory)
    {
        client_.content = game::ClientContent::load(assetDirectory);
        rendererAssets_ = RendererAssetStore::load(assetDirectory, client_.content, gpuResources_);
    }

    void RendererConfigBridge::loadWorldConfig(const std::filesystem::path& configDirectory)
    {
        config::WorldConfig defaults{};
        defaults.loadGridScale = DefaultLoadGridScale;
        defaults.terrainWorkerCount = DefaultTerrainWorkerCount;
        defaults.maxTerrainUploadChunksPerFrame = DefaultMaxTerrainUploadChunksPerFrame;
        defaults.maxTerrainUnloadChunksPerFrame = DefaultMaxTerrainUnloadChunksPerFrame;
        defaults.maxTerrainRetiredDestroyPerFrame = DefaultMaxTerrainRetiredDestroyPerFrame;
        defaults.groundnessNoiseFeatureScale = DefaultGroundnessNoiseFeatureScale;
        defaults.groundnessNoiseOctaveCount = DefaultGroundnessNoiseOctaveCount;
        defaults.groundnessNoiseLacunarity = DefaultGroundnessNoiseLacunarity;
        defaults.groundnessNoiseGain = DefaultGroundnessNoiseGain;
        defaults.groundnessDomainWarpEnabled = DefaultGroundnessDomainWarpEnabled;
        defaults.groundnessDomainWarpAmplitude = DefaultGroundnessDomainWarpAmplitude;
        defaults.groundnessDomainWarpFrequency = DefaultGroundnessDomainWarpFrequency;
        defaults.groundnessDomainWarpOctaveCount = DefaultGroundnessDomainWarpOctaveCount;
        defaults.groundnessDomainWarpGain = DefaultGroundnessDomainWarpGain;
        defaults.baseNoiseFeatureScale = DefaultBaseNoiseFeatureScale;
        defaults.baseNoiseOctaveCount = DefaultBaseNoiseOctaveCount;
        defaults.baseNoiseLacunarity = DefaultBaseNoiseLacunarity;
        defaults.baseNoiseGain = DefaultBaseNoiseGain;
        defaults.smoothnessNoiseFeatureScale = DefaultSmoothnessNoiseFeatureScale;
        defaults.smoothnessNoiseOctaveCount = DefaultSmoothnessNoiseOctaveCount;
        defaults.smoothnessNoiseLacunarity = DefaultSmoothnessNoiseLacunarity;
        defaults.smoothnessNoiseGain = DefaultSmoothnessNoiseGain;
        defaults.weirdnessNoiseFeatureScale = DefaultWeirdnessNoiseFeatureScale;
        defaults.weirdnessNoiseOctaveCount = DefaultWeirdnessNoiseOctaveCount;
        defaults.weirdnessNoiseLacunarity = DefaultWeirdnessNoiseLacunarity;
        defaults.weirdnessNoiseGain = DefaultWeirdnessNoiseGain;
        defaults.weirdnessDomainWarpEnabled = DefaultWeirdnessDomainWarpEnabled;
        defaults.weirdnessDomainWarpAmplitude = DefaultWeirdnessDomainWarpAmplitude;
        defaults.weirdnessDomainWarpFrequency = DefaultWeirdnessDomainWarpFrequency;
        defaults.weirdnessDomainWarpOctaveCount = DefaultWeirdnessDomainWarpOctaveCount;
        defaults.weirdnessDomainWarpGain = DefaultWeirdnessDomainWarpGain;
        defaults.temperatureNoiseStrength = DefaultTemperatureNoiseStrength;
        defaults.temperatureNoiseFeatureScale = DefaultTemperatureNoiseFeatureScale;
        defaults.temperatureNoiseOctaveCount = DefaultTemperatureNoiseOctaveCount;
        defaults.temperatureNoiseLacunarity = DefaultTemperatureNoiseLacunarity;
        defaults.temperatureNoiseGain = DefaultTemperatureNoiseGain;
        defaults.temperatureNoiseSimplexScale = DefaultTemperatureNoiseSimplexScale;
        defaults.precipitationNoiseFeatureScale = DefaultPrecipitationNoiseFeatureScale;
        defaults.precipitationNoiseOctaveCount = DefaultPrecipitationNoiseOctaveCount;
        defaults.precipitationNoiseLacunarity = DefaultPrecipitationNoiseLacunarity;
        defaults.precipitationNoiseGain = DefaultPrecipitationNoiseGain;
        defaults.precipitationNoiseSimplexScale = DefaultPrecipitationNoiseSimplexScale;
        defaults.seaLevel = DefaultSeaLevel;

        const config::WorldConfig worldConfig = config::loadWorldConfig(configDirectory / "world.json", defaults, ChunkSizeY - 1);
        client_.worldConfig.loadGridScale = worldConfig.loadGridScale;
        client_.worldConfig.terrainWorkerCount = worldConfig.terrainWorkerCount;
        client_.worldConfig.maxTerrainUploadChunksPerFrame = worldConfig.maxTerrainUploadChunksPerFrame;
        client_.worldConfig.maxTerrainUnloadChunksPerFrame = worldConfig.maxTerrainUnloadChunksPerFrame;
        client_.worldConfig.maxTerrainRetiredDestroyPerFrame = worldConfig.maxTerrainRetiredDestroyPerFrame;
        client_.worldConfig.groundnessNoiseFeatureScale = worldConfig.groundnessNoiseFeatureScale;
        client_.worldConfig.groundnessNoiseOctaveCount = worldConfig.groundnessNoiseOctaveCount;
        client_.worldConfig.groundnessNoiseLacunarity = worldConfig.groundnessNoiseLacunarity;
        client_.worldConfig.groundnessNoiseGain = worldConfig.groundnessNoiseGain;
        client_.worldConfig.groundnessDomainWarpEnabled = worldConfig.groundnessDomainWarpEnabled;
        client_.worldConfig.groundnessDomainWarpAmplitude = worldConfig.groundnessDomainWarpAmplitude;
        client_.worldConfig.groundnessDomainWarpFrequency = worldConfig.groundnessDomainWarpFrequency;
        client_.worldConfig.groundnessDomainWarpOctaveCount = worldConfig.groundnessDomainWarpOctaveCount;
        client_.worldConfig.groundnessDomainWarpGain = worldConfig.groundnessDomainWarpGain;
        client_.worldConfig.baseNoiseFeatureScale = worldConfig.baseNoiseFeatureScale;
        client_.worldConfig.baseNoiseOctaveCount = worldConfig.baseNoiseOctaveCount;
        client_.worldConfig.baseNoiseLacunarity = worldConfig.baseNoiseLacunarity;
        client_.worldConfig.baseNoiseGain = worldConfig.baseNoiseGain;
        client_.worldConfig.smoothnessNoiseFeatureScale = worldConfig.smoothnessNoiseFeatureScale;
        client_.worldConfig.smoothnessNoiseOctaveCount = worldConfig.smoothnessNoiseOctaveCount;
        client_.worldConfig.smoothnessNoiseLacunarity = worldConfig.smoothnessNoiseLacunarity;
        client_.worldConfig.smoothnessNoiseGain = worldConfig.smoothnessNoiseGain;
        client_.worldConfig.weirdnessNoiseFeatureScale = worldConfig.weirdnessNoiseFeatureScale;
        client_.worldConfig.weirdnessNoiseOctaveCount = worldConfig.weirdnessNoiseOctaveCount;
        client_.worldConfig.weirdnessNoiseLacunarity = worldConfig.weirdnessNoiseLacunarity;
        client_.worldConfig.weirdnessNoiseGain = worldConfig.weirdnessNoiseGain;
        client_.worldConfig.weirdnessDomainWarpEnabled = worldConfig.weirdnessDomainWarpEnabled;
        client_.worldConfig.weirdnessDomainWarpAmplitude = worldConfig.weirdnessDomainWarpAmplitude;
        client_.worldConfig.weirdnessDomainWarpFrequency = worldConfig.weirdnessDomainWarpFrequency;
        client_.worldConfig.weirdnessDomainWarpOctaveCount = worldConfig.weirdnessDomainWarpOctaveCount;
        client_.worldConfig.weirdnessDomainWarpGain = worldConfig.weirdnessDomainWarpGain;
        client_.worldConfig.temperatureNoiseStrength = worldConfig.temperatureNoiseStrength;
        client_.worldConfig.temperatureNoiseFeatureScale = worldConfig.temperatureNoiseFeatureScale;
        client_.worldConfig.temperatureNoiseOctaveCount = worldConfig.temperatureNoiseOctaveCount;
        client_.worldConfig.temperatureNoiseLacunarity = worldConfig.temperatureNoiseLacunarity;
        client_.worldConfig.temperatureNoiseGain = worldConfig.temperatureNoiseGain;
        client_.worldConfig.temperatureNoiseSimplexScale = worldConfig.temperatureNoiseSimplexScale;
        client_.worldConfig.precipitationNoiseFeatureScale = worldConfig.precipitationNoiseFeatureScale;
        client_.worldConfig.precipitationNoiseOctaveCount = worldConfig.precipitationNoiseOctaveCount;
        client_.worldConfig.precipitationNoiseLacunarity = worldConfig.precipitationNoiseLacunarity;
        client_.worldConfig.precipitationNoiseGain = worldConfig.precipitationNoiseGain;
        client_.worldConfig.precipitationNoiseSimplexScale = worldConfig.precipitationNoiseSimplexScale;
        client_.worldConfig.seaLevel = worldConfig.seaLevel;
    }

    void RendererConfigBridge::loadRenderConfig(const std::filesystem::path& configDirectory)
    {
        config::RenderConfig defaults{};
        defaults.fluidWaterAlpha = DefaultFluidWaterAlpha;

        const config::RenderConfig renderConfig = config::loadRenderConfig(configDirectory / "render.json", defaults);
        client_.renderConfig.fluidWaterAlpha = renderConfig.fluidWaterAlpha;
    }

    namespace
    {
        bool loadSplineLut(
            const std::filesystem::path& path,
            std::array<float, SplineLutCount>& target,
            float expectedXMin,
            float expectedXMax)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                return false;
            }

            char magic[4]{};
            uint32_t version = 0;
            uint32_t count = 0;
            float xMin = 0.0f;
            float xMax = 0.0f;
            float yMin = 0.0f;
            float yMax = 0.0f;

            file.read(magic, sizeof(magic));
            file.read(reinterpret_cast<char*>(&version), sizeof(version));
            file.read(reinterpret_cast<char*>(&count), sizeof(count));
            file.read(reinterpret_cast<char*>(&xMin), sizeof(xMin));
            file.read(reinterpret_cast<char*>(&xMax), sizeof(xMax));
            file.read(reinterpret_cast<char*>(&yMin), sizeof(yMin));
            file.read(reinterpret_cast<char*>(&yMax), sizeof(yMax));
            if (!file ||
                std::memcmp(magic, "DLSF", 4) != 0 ||
                version != SplineLutVersion ||
                count != SplineLutCount ||
                xMin != expectedXMin ||
                xMax != expectedXMax ||
                yMin >= yMax)
            {
                return false;
            }

            std::array<float, SplineLutCount> loaded{};
            file.read(reinterpret_cast<char*>(loaded.data()), static_cast<std::streamsize>(loaded.size() * sizeof(float)));
            if (!file)
            {
                return false;
            }

            target = loaded;
            return true;
        }
    }

    void RendererConfigBridge::loadTerrainLuts(const std::filesystem::path& assetDirectory)
    {
        for (uint32_t i = 0; i < SplineLutCount; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(SplineLutCount - 1u);
            client_.diagnostics.heightLut[i] = 120.0f + t * 20.0f;
            client_.diagnostics.groundnessBaselineLut[i] = SplineLutInputMin + t * (SplineLutInputMax - SplineLutInputMin);
            client_.diagnostics.groundnessInfluenceLut[i] = 0.25f;
            client_.diagnostics.smoothnessInfluenceLut[i] = 1.0f;
            client_.diagnostics.pvWeightLut[i] = 0.0f;
            client_.diagnostics.groundnessPvWeightLut[i] = 0.0f;
            client_.diagnostics.smoothnessPvWeightLut[i] = 0.0f;
        }

        const std::filesystem::path worldDataDirectory = assetDirectory / "data" / "world";
        loadSplineLut(worldDataDirectory / "height_lut.bin", client_.diagnostics.heightLut, HeightLutInputMin, HeightLutInputMax);
        loadSplineLut(worldDataDirectory / "groundness_baseline_lut.bin", client_.diagnostics.groundnessBaselineLut, SplineLutInputMin, SplineLutInputMax);
        loadSplineLut(worldDataDirectory / "groundness_influence_lut.bin", client_.diagnostics.groundnessInfluenceLut, SplineLutInputMin, SplineLutInputMax);
        loadSplineLut(worldDataDirectory / "smoothness_influence_lut.bin", client_.diagnostics.smoothnessInfluenceLut, SplineLutInputMin, SplineLutInputMax);
        loadSplineLut(worldDataDirectory / "pv_weight_lut.bin", client_.diagnostics.pvWeightLut, PvLutInputMin, PvLutInputMax);
        loadSplineLut(worldDataDirectory / "groundness_pv_weight_lut.bin", client_.diagnostics.groundnessPvWeightLut, SplineLutInputMin, SplineLutInputMax);
        loadSplineLut(worldDataDirectory / "smoothness_pv_weight_lut.bin", client_.diagnostics.smoothnessPvWeightLut, SplineLutInputMin, SplineLutInputMax);
    }
}
