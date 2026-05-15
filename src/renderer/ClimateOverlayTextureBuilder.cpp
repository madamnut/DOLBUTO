#include "renderer/ClimateOverlayTextureBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace dolbuto
{
    namespace
    {
        constexpr int WorldSizeBlocks = 65536;
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
        if (mode == 1)
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
