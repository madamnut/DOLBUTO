#include "world/ClimateSystem.h"

#include <FastNoise/FastNoise.h>

#include <cmath>
#include <cstddef>

namespace dolbuto::world
{
    namespace
    {
        constexpr int TerrainTilePeriod = 65536;
        constexpr int WorldSizeBlocks = TerrainTilePeriod;
        constexpr uint8_t ClimateMaxByte = 255;
        constexpr int TemperatureNoiseSeed = 2400;
        constexpr int PrecipitationNoiseSeed = 2401;

        int positiveModulo(int value, int divisor)
        {
            const int result = value % divisor;
            return result < 0 ? result + divisor : result;
        }

        FastNoise::SmartNode<> climateNoiseGenerator(float simplexScale, int octaveCount, float lacunarity, float gain)
        {
            struct CachedGenerator
            {
                float simplexScale = 0.0f;
                int octaveCount = 0;
                float lacunarity = 0.0f;
                float gain = 0.0f;
                FastNoise::SmartNode<> generator;
            };

            thread_local CachedGenerator cache{};
            if (cache.generator &&
                cache.simplexScale == simplexScale &&
                cache.octaveCount == octaveCount &&
                cache.lacunarity == lacunarity &&
                cache.gain == gain)
            {
                return cache.generator;
            }

            auto simplex = FastNoise::New<FastNoise::Simplex>();
            auto fbm = FastNoise::New<FastNoise::FractalFBm>();
            if (!simplex || !fbm)
            {
                return FastNoise::SmartNode<>{};
            }

            simplex->SetScale(simplexScale);
            fbm->SetSource(simplex);
            fbm->SetOctaveCount(octaveCount);
            fbm->SetLacunarity(lacunarity);
            fbm->SetGain(gain);

            cache.simplexScale = simplexScale;
            cache.octaveCount = octaveCount;
            cache.lacunarity = lacunarity;
            cache.gain = gain;
            cache.generator = FastNoise::SmartNode<>(fbm);
            return cache.generator;
        }
    }

    ClimateSystem::ClimateSystem(TerrainBuilderConfig config) :
        config_(config)
    {
    }

    float ClimateSystem::decodeClimateValue(uint8_t value)
    {
        return static_cast<float>(value) / static_cast<float>(ClimateMaxByte);
    }

    int ClimateSystem::temperatureSeed() const
    {
        return TemperatureNoiseSeed + config_.activeWorldSeedSalt;
    }

    int ClimateSystem::precipitationSeed() const
    {
        return PrecipitationNoiseSeed + config_.activeWorldSeedSalt;
    }

    std::vector<float> ClimateSystem::buildTileableNoise(
        int sampleSize,
        float featureScale,
        float simplexScale,
        int octaveCount,
        float lacunarity,
        float gain,
        int seed) const
    {
        auto generator = climateNoiseGenerator(simplexScale, octaveCount, lacunarity, gain);
        if (!generator || sampleSize <= 0)
        {
            return {};
        }

        constexpr float TwoPi = 6.28318530718f;
        const float angleScale = TwoPi / static_cast<float>(TerrainTilePeriod);
        const float radius = static_cast<float>(TerrainTilePeriod) / (TwoPi * featureScale);
        const size_t sampleCount = static_cast<size_t>(sampleSize) * static_cast<size_t>(sampleSize);
        std::vector<float> xPositions(sampleCount);
        std::vector<float> yPositions(sampleCount);
        std::vector<float> zPositions(sampleCount);
        std::vector<float> wPositions(sampleCount);
        std::vector<float> noise(sampleCount);

        for (int y = 0; y < sampleSize; ++y)
        {
            const int worldZ = (y * WorldSizeBlocks) / sampleSize;
            const float zAngle = static_cast<float>(positiveModulo(worldZ, TerrainTilePeriod)) * angleScale;
            const float zCos = std::cos(zAngle) * radius;
            const float zSin = std::sin(zAngle) * radius;
            for (int x = 0; x < sampleSize; ++x)
            {
                const int worldX = (x * WorldSizeBlocks) / sampleSize;
                const float xAngle = static_cast<float>(positiveModulo(worldX, TerrainTilePeriod)) * angleScale;
                const size_t index = static_cast<size_t>(y) * static_cast<size_t>(sampleSize) + static_cast<size_t>(x);
                xPositions[index] = std::cos(xAngle) * radius;
                yPositions[index] = zCos;
                zPositions[index] = std::sin(xAngle) * radius;
                wPositions[index] = zSin;
            }
        }

        generator->GenPositionArray4D(
            noise.data(),
            static_cast<int>(noise.size()),
            xPositions.data(),
            yPositions.data(),
            zPositions.data(),
            wPositions.data(),
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            seed);

        return noise;
    }

    float ClimateSystem::sampleTileableNoise(
        int wrappedX,
        int wrappedZ,
        float featureScale,
        float simplexScale,
        int octaveCount,
        float lacunarity,
        float gain,
        int seed) const
    {
        auto generator = climateNoiseGenerator(simplexScale, octaveCount, lacunarity, gain);
        if (!generator)
        {
            return 0.0f;
        }

        constexpr float TwoPi = 6.28318530718f;
        const float angleScale = TwoPi / static_cast<float>(TerrainTilePeriod);
        const float radius = static_cast<float>(TerrainTilePeriod) / (TwoPi * featureScale);
        const float xAngle = static_cast<float>(positiveModulo(wrappedX, TerrainTilePeriod)) * angleScale;
        const float zAngle = static_cast<float>(positiveModulo(wrappedZ, TerrainTilePeriod)) * angleScale;
        float xPosition = std::cos(xAngle) * radius;
        float yPosition = std::cos(zAngle) * radius;
        float zPosition = std::sin(xAngle) * radius;
        float wPosition = std::sin(zAngle) * radius;
        float noise = 0.0f;

        generator->GenPositionArray4D(
            &noise,
            1,
            &xPosition,
            &yPosition,
            &zPosition,
            &wPosition,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            seed);

        return noise;
    }

    void ClimateSystem::populateChunkClimate(ChunkData& chunk) const
    {
        TerrainBuilder(config_).populateChunkClimate(chunk);
    }

    float ClimateSystem::temperatureAtWrapped(int wrappedZ, float noise) const
    {
        return TerrainBuilder(config_).temperatureAtWrapped(wrappedZ, noise);
    }

    float ClimateSystem::precipitationAtNoise(float noise) const
    {
        return TerrainBuilder(config_).precipitationAtNoise(noise);
    }

    const TerrainBuilderConfig& ClimateSystem::config() const
    {
        return config_;
    }
}
