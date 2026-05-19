#pragma once

namespace dolbuto::world
{
    enum class BiomeId
    {
        SnowPlain,
        Taiga,
        SnowForest,
        DryGrass,
        Grassland,
        Plains,
        Forest,
        Swamp,
        Desert,
        Savanna,
        TropicalForest,
        Jungle,
        FrozenOcean,
        ColdOcean,
        TemperateOcean,
        Ocean,
        WarmOcean,
        TropicalOcean
    };

    struct BiomeSample
    {
        int temperatureBand = 0;
        int precipitationBand = 0;
        int groundnessBand = 0;
        BiomeId id = BiomeId::FrozenOcean;
    };

    int climateBand5(float value);
    BiomeSample classifyBiome(float temperature, float precipitation, float groundness);
    const char* biomeName(BiomeId biome);
    bool biomeIsOcean(BiomeId biome);
}
