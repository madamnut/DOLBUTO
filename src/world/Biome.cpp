#include "world/Biome.h"

#include <algorithm>

namespace dolbuto::world
{
    int climateBand5(float value)
    {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return std::clamp(static_cast<int>(clamped * 5.0f), 0, 4);
    }

    BiomeSample classifyBiome(float temperature, float precipitation, float groundness)
    {
        static constexpr BiomeId LandTable[5][5] = {
            { BiomeId::SnowPlain, BiomeId::SnowPlain, BiomeId::Taiga, BiomeId::SnowForest, BiomeId::SnowForest },
            { BiomeId::DryGrass, BiomeId::Grassland, BiomeId::Taiga, BiomeId::Forest, BiomeId::Swamp },
            { BiomeId::DryGrass, BiomeId::Plains, BiomeId::Forest, BiomeId::Forest, BiomeId::Swamp },
            { BiomeId::Desert, BiomeId::DryGrass, BiomeId::Savanna, BiomeId::Forest, BiomeId::Jungle },
            { BiomeId::Desert, BiomeId::Desert, BiomeId::Savanna, BiomeId::TropicalForest, BiomeId::Jungle },
        };

        static constexpr BiomeId OceanTable[5][5] = {
            { BiomeId::FrozenOcean, BiomeId::FrozenOcean, BiomeId::ColdOcean, BiomeId::ColdOcean, BiomeId::ColdOcean },
            { BiomeId::ColdOcean, BiomeId::ColdOcean, BiomeId::ColdOcean, BiomeId::TemperateOcean, BiomeId::TemperateOcean },
            { BiomeId::TemperateOcean, BiomeId::TemperateOcean, BiomeId::Ocean, BiomeId::WarmOcean, BiomeId::WarmOcean },
            { BiomeId::WarmOcean, BiomeId::WarmOcean, BiomeId::WarmOcean, BiomeId::TropicalOcean, BiomeId::TropicalOcean },
            { BiomeId::WarmOcean, BiomeId::TropicalOcean, BiomeId::TropicalOcean, BiomeId::TropicalOcean, BiomeId::TropicalOcean },
        };

        BiomeSample sample{};
        sample.temperatureBand = climateBand5(temperature);
        sample.precipitationBand = climateBand5(precipitation);
        sample.groundnessBand = groundness < 0.0f ? 0 : 1;
        sample.id = sample.groundnessBand == 0 ?
            OceanTable[sample.temperatureBand][sample.precipitationBand] :
            LandTable[sample.temperatureBand][sample.precipitationBand];
        return sample;
    }

    const char* biomeName(BiomeId biome)
    {
        switch (biome)
        {
        case BiomeId::SnowPlain: return "SnowPlain";
        case BiomeId::Taiga: return "Taiga";
        case BiomeId::SnowForest: return "SnowForest";
        case BiomeId::DryGrass: return "DryGrass";
        case BiomeId::Grassland: return "Grassland";
        case BiomeId::Plains: return "Plains";
        case BiomeId::Forest: return "Forest";
        case BiomeId::Swamp: return "Swamp";
        case BiomeId::Desert: return "Desert";
        case BiomeId::Savanna: return "Savanna";
        case BiomeId::TropicalForest: return "TropicalForest";
        case BiomeId::Jungle: return "Jungle";
        case BiomeId::FrozenOcean: return "FrozenOcean";
        case BiomeId::ColdOcean: return "ColdOcean";
        case BiomeId::TemperateOcean: return "TemperateOcean";
        case BiomeId::Ocean: return "Ocean";
        case BiomeId::WarmOcean: return "WarmOcean";
        case BiomeId::TropicalOcean: return "TropicalOcean";
        }
        return "Unknown";
    }

    bool biomeIsOcean(BiomeId biome)
    {
        switch (biome)
        {
        case BiomeId::FrozenOcean:
        case BiomeId::ColdOcean:
        case BiomeId::TemperateOcean:
        case BiomeId::Ocean:
        case BiomeId::WarmOcean:
        case BiomeId::TropicalOcean:
            return true;
        default:
            return false;
        }
    }
}
