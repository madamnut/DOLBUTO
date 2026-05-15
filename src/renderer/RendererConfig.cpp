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
        constexpr int TerrainMinHeight = 120;
        constexpr int TerrainMaxHeight = 140;
        constexpr float DefaultTerrainNoiseFeatureScale = 220.0f;
        constexpr int DefaultTerrainNoiseOctaveCount = 4;
        constexpr float DefaultTerrainNoiseLacunarity = 2.0f;
        constexpr float DefaultTerrainNoiseGain = 0.5f;
        constexpr float DefaultTerrainNoiseSimplexScale = 1.0f;
        constexpr bool DefaultTerrainDomainWarpEnabled = false;
        constexpr float DefaultTerrainDomainWarpAmplitude = 0.0f;
        constexpr float DefaultTerrainDomainWarpFrequency = 1.0f;
        constexpr int DefaultTerrainDomainWarpOctaveCount = 2;
        constexpr float DefaultTerrainDomainWarpGain = 0.5f;
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
        constexpr uint32_t HeightLutVersion = 1;
        constexpr uint32_t HeightLutCount = 1024;
        constexpr float HeightLutNoiseMin = -2.0f;
        constexpr float HeightLutNoiseMax = 2.0f;
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
        defaults.terrainNoiseFeatureScale = DefaultTerrainNoiseFeatureScale;
        defaults.terrainNoiseOctaveCount = DefaultTerrainNoiseOctaveCount;
        defaults.terrainNoiseLacunarity = DefaultTerrainNoiseLacunarity;
        defaults.terrainNoiseGain = DefaultTerrainNoiseGain;
        defaults.terrainNoiseSimplexScale = DefaultTerrainNoiseSimplexScale;
        defaults.terrainDomainWarpEnabled = DefaultTerrainDomainWarpEnabled;
        defaults.terrainDomainWarpAmplitude = DefaultTerrainDomainWarpAmplitude;
        defaults.terrainDomainWarpFrequency = DefaultTerrainDomainWarpFrequency;
        defaults.terrainDomainWarpOctaveCount = DefaultTerrainDomainWarpOctaveCount;
        defaults.terrainDomainWarpGain = DefaultTerrainDomainWarpGain;
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
        client_.worldConfig.terrainNoiseFeatureScale = worldConfig.terrainNoiseFeatureScale;
        client_.worldConfig.terrainNoiseOctaveCount = worldConfig.terrainNoiseOctaveCount;
        client_.worldConfig.terrainNoiseLacunarity = worldConfig.terrainNoiseLacunarity;
        client_.worldConfig.terrainNoiseGain = worldConfig.terrainNoiseGain;
        client_.worldConfig.terrainNoiseSimplexScale = worldConfig.terrainNoiseSimplexScale;
        client_.worldConfig.terrainDomainWarpEnabled = worldConfig.terrainDomainWarpEnabled;
        client_.worldConfig.terrainDomainWarpAmplitude = worldConfig.terrainDomainWarpAmplitude;
        client_.worldConfig.terrainDomainWarpFrequency = worldConfig.terrainDomainWarpFrequency;
        client_.worldConfig.terrainDomainWarpOctaveCount = worldConfig.terrainDomainWarpOctaveCount;
        client_.worldConfig.terrainDomainWarpGain = worldConfig.terrainDomainWarpGain;
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

    void RendererConfigBridge::loadHeightLut(const std::filesystem::path& assetDirectory)
    {
        for (uint32_t i = 0; i < HeightLutCount; ++i)
        {
            const double t = static_cast<double>(i) / static_cast<double>(HeightLutCount - 1u);
            client_.diagnostics.heightLut[i] = static_cast<uint16_t>(std::lround(static_cast<double>(TerrainMinHeight) + t * static_cast<double>(TerrainMaxHeight - TerrainMinHeight)));
        }

        const std::filesystem::path path = assetDirectory / "data" / "world" / "height_lut.bin";
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return;
        }

        char magic[4]{};
        uint32_t version = 0;
        uint32_t count = 0;
        float noiseMin = 0.0f;
        float noiseMax = 0.0f;

        file.read(magic, sizeof(magic));
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        file.read(reinterpret_cast<char*>(&noiseMin), sizeof(noiseMin));
        file.read(reinterpret_cast<char*>(&noiseMax), sizeof(noiseMax));
        if (!file || std::memcmp(magic, "DLHT", 4) != 0 || version != HeightLutVersion || count != HeightLutCount || noiseMin != HeightLutNoiseMin || noiseMax != HeightLutNoiseMax)
        {
            return;
        }

        std::array<uint16_t, HeightLutCount> loaded{};
        file.read(reinterpret_cast<char*>(loaded.data()), static_cast<std::streamsize>(loaded.size() * sizeof(uint16_t)));
        if (!file)
        {
            return;
        }

        client_.diagnostics.heightLut = loaded;
    }
}
