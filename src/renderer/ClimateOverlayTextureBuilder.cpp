#include "renderer/ClimateOverlayTextureBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace dolbuto
{
    namespace
    {
        constexpr int WorldSizeBlocks = 65536;
        constexpr int TerrainOverlayWorldExtentBlocks = 4096;

        struct Color
        {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
        };

        Color lerp(Color a, Color b, float t)
        {
            return {
                a.r + (b.r - a.r) * t,
                a.g + (b.g - a.g) * t,
                a.b + (b.b - a.b) * t
            };
        }

        float minusOneToOne(float value)
        {
            return std::clamp((value + 1.0f) * 0.5f, 0.0f, 1.0f);
        }

        Color threeStop(Color low, Color mid, Color high, float t)
        {
            if (t < 0.5f)
            {
                return lerp(low, mid, t * 2.0f);
            }
            return lerp(mid, high, (t - 0.5f) * 2.0f);
        }
    }

    std::vector<unsigned char> ClimateOverlayTextureBuilder::buildPixels(int mode, const world::ClimateSystem& climate)
    {
        std::vector<unsigned char> pixels(static_cast<size_t>(OverlaySize) * static_cast<size_t>(OverlaySize) * 4u, 255u);
        auto writePixel = [&](int x, int y, float r, float g, float b)
        {
            const size_t index = (static_cast<size_t>(y) * OverlaySize + static_cast<size_t>(x)) * 4u;
            pixels[index + 0u] = static_cast<unsigned char>(std::clamp(std::lround(r * 255.0f), 0l, 255l));
            pixels[index + 1u] = static_cast<unsigned char>(std::clamp(std::lround(g * 255.0f), 0l, 255l));
            pixels[index + 2u] = static_cast<unsigned char>(std::clamp(std::lround(b * 255.0f), 0l, 255l));
        };

        const world::TerrainBuilderConfig& config = climate.config();
        if (mode == ClimateOverlayTextureBuilder::Temperature)
        {
            const std::vector<float> noise = climate.buildTileableNoise(
                OverlaySize,
                config.temperatureNoiseFeatureScale,
                config.temperatureNoiseSimplexScale,
                config.temperatureNoiseOctaveCount,
                config.temperatureNoiseLacunarity,
                config.temperatureNoiseGain,
                climate.temperatureSeed());
            for (int y = 0; y < OverlaySize; ++y)
            {
                const int worldZ = (y * WorldSizeBlocks) / OverlaySize;
                for (int x = 0; x < OverlaySize; ++x)
                {
                    const size_t index = static_cast<size_t>(y) * OverlaySize + static_cast<size_t>(x);
                    const float temperature = climate.temperatureAtWrapped(worldZ, index < noise.size() ? noise[index] : 0.0f);
                    writePixel(x, y, temperature, 0.0f, 1.0f - temperature);
                }
            }
            return pixels;
        }

        if (mode >= ClimateOverlayTextureBuilder::Groundness && mode <= ClimateOverlayTextureBuilder::Pv)
        {
            world::TerrainDebugNoise noiseMode = world::TerrainDebugNoise::Groundness;
            if (mode == ClimateOverlayTextureBuilder::Smoothness)
            {
                noiseMode = world::TerrainDebugNoise::Smoothness;
            }
            else if (mode == ClimateOverlayTextureBuilder::Weirdness)
            {
                noiseMode = world::TerrainDebugNoise::Weirdness;
            }
            else if (mode == ClimateOverlayTextureBuilder::Pv)
            {
                noiseMode = world::TerrainDebugNoise::Pv;
            }

            const std::vector<float> samples =
                world::TerrainBuilder(config).buildTerrainDebugNoise(noiseMode, OverlaySize, TerrainOverlayWorldExtentBlocks);
            if (samples.empty())
            {
                return pixels;
            }

            for (int y = 0; y < OverlaySize; ++y)
            {
                for (int x = 0; x < OverlaySize; ++x)
                {
                    const size_t index = static_cast<size_t>(y) * OverlaySize + static_cast<size_t>(x);
                    const float sample = samples[index];
                    Color color{};
                    if (mode == ClimateOverlayTextureBuilder::Groundness)
                    {
                        color = threeStop(
                            Color{0.03f, 0.10f, 0.20f},
                            Color{0.25f, 0.42f, 0.29f},
                            Color{0.90f, 0.84f, 0.69f},
                            minusOneToOne(sample));
                    }
                    else if (mode == ClimateOverlayTextureBuilder::Smoothness)
                    {
                        color = threeStop(
                            Color{0.48f, 0.12f, 0.12f},
                            Color{0.82f, 0.70f, 0.25f},
                            Color{0.37f, 0.78f, 0.78f},
                            minusOneToOne(sample));
                    }
                    else
                    {
                        const float gray = 1.0f - minusOneToOne(sample);
                        color = {gray, gray, gray};
                    }
                    writePixel(x, y, color.r, color.g, color.b);
                }
            }
            return pixels;
        }

        const std::vector<float> noise = climate.buildTileableNoise(
            OverlaySize,
            config.precipitationNoiseFeatureScale,
            config.precipitationNoiseSimplexScale,
            config.precipitationNoiseOctaveCount,
            config.precipitationNoiseLacunarity,
            config.precipitationNoiseGain,
            climate.precipitationSeed());
        if (noise.empty())
        {
            return pixels;
        }

        for (int y = 0; y < OverlaySize; ++y)
        {
            for (int x = 0; x < OverlaySize; ++x)
            {
                const size_t index = static_cast<size_t>(y) * OverlaySize + static_cast<size_t>(x);
                const float precipitation = climate.precipitationAtNoise(noise[index]);
                const float gray = 0.45f;
                writePixel(x, y, gray * (1.0f - precipitation), gray * (1.0f - precipitation) + 0.35f * precipitation, gray * (1.0f - precipitation) + precipitation);
            }
        }

        return pixels;
    }
}
