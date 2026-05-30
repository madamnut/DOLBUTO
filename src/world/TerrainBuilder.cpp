#include "world/TerrainBuilder.h"

#include "world/Biome.h"

#include <FastNoise/FastNoise.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

namespace dolbuto::world
{
    namespace
    {
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeY = 512;
        constexpr int ChunkSizeZ = 16;
        constexpr int SubchunkSize = 16;
        constexpr int SubchunksPerChunk = ChunkSizeY / SubchunkSize;
        constexpr int TerrainTilePeriod = 65536;
        constexpr int WorldSizeBlocks = TerrainTilePeriod;
        constexpr int GroundnessNoiseSeed = 1801;
        constexpr int BaseNoiseSeed = 1802;
        constexpr int SmoothnessNoiseSeed = 1803;
        constexpr int WeirdnessNoiseSeed = 1804;
        constexpr int TemperatureNoiseSeed = 2400;
        constexpr int PrecipitationNoiseSeed = 2401;
        constexpr float DefaultTerrainNoiseLacunarity = 2.0f;
        constexpr float SplineLutInputMin = -2.0f;
        constexpr float SplineLutInputMax = 2.0f;
        constexpr float PvLutInputMin = -1.0f;
        constexpr float PvLutInputMax = 1.0f;
        constexpr float HeightLutInputMin = 0.0f;
        constexpr float HeightLutInputMax = 2.0f;
        constexpr float TerrainValueNormalizeRange = 3.5f;
        constexpr float FixedSimplexScale = 1.0f;
        constexpr uint16_t BlockAir = 0;
        constexpr uint16_t BlockRock = 1;
        constexpr uint16_t BlockGrass = 2;
        constexpr uint16_t BlockDirt = 3;
        constexpr uint16_t BlockSand = 4;
        constexpr uint16_t BlockSandstone = 5;
        constexpr uint16_t BlockMud = 6;
        constexpr uint16_t BlockClay = 7;
        constexpr uint16_t BlockTrunk = 8;
        constexpr uint16_t BlockLeaves = 9;
        constexpr uint16_t BlockIce = 11;
        constexpr uint16_t BlockPlant = 10000;
        constexpr uint16_t BlockStoneProp = 20000;
        constexpr uint16_t BlockBranchProp = 20001;
        constexpr uint16_t BlockBedrock = 65535;
        constexpr uint16_t FluidNone = 0;
        constexpr uint16_t FluidWater = 1;
        constexpr uint16_t FluidFullAmount = 100;
        constexpr int FluidAmountBits = 7;
        constexpr uint16_t FluidAmountMask = (1u << FluidAmountBits) - 1u;
        constexpr uint8_t ClimateMinByte = 0;
        constexpr uint8_t ClimateMaxByte = 255;
        constexpr uint32_t BedrockHeightSalt = 0xBEEFBEDu;
        constexpr uint32_t PlantPlacementSalt = 0x9A7D3E21u;
        constexpr uint8_t PlantPlacementMax = 151;
        constexpr uint8_t StonePlacementMax = 159;
        constexpr uint8_t BranchPlacementMax = 167;
        constexpr uint8_t TreePlacementMin = 168;
        constexpr uint8_t TreePlacementMax = 170;
        constexpr uint32_t OrePlacementSalt = 0x0EED5EEDu;

        struct FeatureNeighborOffset
        {
            int x = 0;
            int z = 0;
        };

        struct SurfaceRule
        {
            uint16_t airSurface = BlockGrass;
            uint16_t airSubsurface = BlockDirt;
            uint16_t underwaterSurface = BlockSand;
            uint16_t underwaterSubsurface = BlockSand;
        };

        constexpr std::array<FeatureNeighborOffset, 8> FeatureNeighborOffsets = {
            FeatureNeighborOffset{-1, -1},
            FeatureNeighborOffset{0, -1},
            FeatureNeighborOffset{1, -1},
            FeatureNeighborOffset{-1, 0},
            FeatureNeighborOffset{1, 0},
            FeatureNeighborOffset{-1, 1},
            FeatureNeighborOffset{0, 1},
            FeatureNeighborOffset{1, 1}
        };

        uint32_t worldRandomHash(int x, int y, int z, uint32_t salt)
        {
            uint32_t hash = static_cast<uint32_t>(x) * 0x8da6b343u;
            hash ^= static_cast<uint32_t>(y) * 0xd8163841u;
            hash ^= static_cast<uint32_t>(z) * 0xcb1ab31fu;
            hash ^= salt;
            hash ^= hash >> 16u;
            hash *= 0x7feb352du;
            hash ^= hash >> 15u;
            hash *= 0x846ca68bu;
            hash ^= hash >> 16u;
            return hash;
        }

        int positiveModulo(int value, int divisor)
        {
            int result = value % divisor;
            return result < 0 ? result + divisor : result;
        }

        int floorDiv(int value, int divisor)
        {
            int result = value / divisor;
            const int remainder = value % divisor;
            if (remainder != 0 && ((remainder < 0) != (divisor < 0)))
            {
                --result;
            }
            return result;
        }

        int wrapBlockCoordinate(int value)
        {
            return positiveModulo(value, WorldSizeBlocks);
        }

        uint8_t worldRandom8(int x, int y, int z, uint32_t salt)
        {
            return static_cast<uint8_t>(worldRandomHash(wrapBlockCoordinate(x), y, wrapBlockCoordinate(z), salt) & 255u);
        }

        float hashUnitFloat(uint32_t hash)
        {
            return static_cast<float>(hash & 0x00ffffffu) / static_cast<float>(0x01000000u);
        }

        int bedrockHeightAt(int worldX, int worldZ)
        {
            return 1 + static_cast<int>(worldRandom8(worldX, 0, worldZ, BedrockHeightSalt) & 3u);
        }

        constexpr uint16_t packFluid(uint16_t id, uint16_t amount)
        {
            return static_cast<uint16_t>((id << FluidAmountBits) | amount);
        }

        constexpr uint16_t fluidId(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid >> FluidAmountBits);
        }

        constexpr uint16_t fluidAmount(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid & FluidAmountMask);
        }

        int16_t clampCacheY(int value)
        {
            return static_cast<int16_t>(std::clamp(value, -1, ChunkSizeY));
        }

        uint8_t encodeClimateValue(float value)
        {
            return static_cast<uint8_t>(std::clamp(
                std::lround(std::clamp(value, 0.0f, 1.0f) * static_cast<float>(ClimateMaxByte)),
                static_cast<long>(ClimateMinByte),
                static_cast<long>(ClimateMaxByte)));
        }

        float decodeClimateValue(uint8_t value)
        {
            return static_cast<float>(value) / static_cast<float>(ClimateMaxByte);
        }

        SurfaceRule surfaceRuleForBiome(BiomeId biome)
        {
            if (biomeIsOcean(biome))
            {
                return {BlockGrass, BlockDirt, BlockSand, BlockSand};
            }

            switch (biome)
            {
            case BiomeId::Desert:
                return {BlockSand, BlockSandstone, BlockSand, BlockSand};
            case BiomeId::Swamp:
                return {BlockMud, BlockClay, BlockSand, BlockSand};
            case BiomeId::Jungle:
                return {BlockDirt, BlockDirt, BlockSand, BlockSand};
            default:
                return {BlockGrass, BlockDirt, BlockSand, BlockSand};
            }
        }

        std::optional<size_t> featureNeighborIndex(int offsetX, int offsetZ)
        {
            for (size_t i = 0; i < FeatureNeighborOffsets.size(); ++i)
            {
                if (FeatureNeighborOffsets[i].x == offsetX && FeatureNeighborOffsets[i].z == offsetZ)
                {
                    return i;
                }
            }
            return std::nullopt;
        }

        FastNoise::SmartNode<> fbmNoiseGenerator(float simplexScale, int octaveCount, float lacunarity, float gain)
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

        float sampleSplineLut(
            const std::array<float, TerrainSplineLutCount>& lut,
            float input,
            float inputMin,
            float inputMax)
        {
            const float scale = static_cast<float>(TerrainSplineLutCount - 1u) / (inputMax - inputMin);
            const float normalized = (input - inputMin) * scale;
            const int index = std::clamp(
                static_cast<int>(normalized + 0.5f),
                0,
                static_cast<int>(TerrainSplineLutCount - 1u));
            return lut[static_cast<size_t>(index)];
        }

        float sampleSplineLut(const std::array<float, TerrainSplineLutCount>& lut, float input)
        {
            return sampleSplineLut(lut, input, SplineLutInputMin, SplineLutInputMax);
        }

        int heightFromLut(const std::array<float, TerrainSplineLutCount>& heightLut, float terrainValue)
        {
            return std::clamp(
                static_cast<int>(std::lround(sampleSplineLut(heightLut, terrainValue, HeightLutInputMin, HeightLutInputMax))),
                0,
                ChunkSizeY);
        }

        TerrainDebugSample terrainSampleFromNoise(
            const TerrainBuilderConfig& config,
            float groundness,
            float smoothness,
            float weirdness,
            float baseNoise)
        {
            TerrainDebugSample sample{};
            const float groundnessBaseline = sampleSplineLut(config.groundnessBaselineLut, groundness);
            const float groundnessInfluence = sampleSplineLut(config.groundnessInfluenceLut, groundness);
            const float smoothnessInfluence = sampleSplineLut(config.smoothnessInfluenceLut, smoothness);
            const float pv = 1.0f - std::abs(3.0f * std::abs(weirdness) - 2.0f);
            const float baseline = groundnessBaseline;
            const float influence = groundnessInfluence * smoothnessInfluence;
            const float rawTerrainValue = baseline + baseNoise * influence;
            const float normalizedTerrainValue = std::clamp(
                (rawTerrainValue + TerrainValueNormalizeRange) / TerrainValueNormalizeRange,
                HeightLutInputMin,
                HeightLutInputMax);
            const float pvWeight =
                sampleSplineLut(config.pvWeightLut, pv, PvLutInputMin, PvLutInputMax) *
                sampleSplineLut(config.groundnessPvWeightLut, groundness) *
                sampleSplineLut(config.smoothnessPvWeightLut, smoothness);
            const float pvMultiplier = std::clamp(1.0f - pvWeight, 0.0f, 1.0f);
            const float terrainValue = normalizedTerrainValue * pvMultiplier;

            sample.groundness = groundness;
            sample.smoothness = smoothness;
            sample.weirdness = weirdness;
            sample.pv = pv;
            sample.baseline = baseline;
            sample.influence = influence;
            sample.rawTerrainValue = rawTerrainValue;
            sample.normalizedTerrainValue = normalizedTerrainValue;
            sample.pvWeight = pvWeight;
            sample.pvMultiplier = pvMultiplier;
            sample.terrainValue = terrainValue;
            sample.height = heightFromLut(config.heightLut, terrainValue);
            return sample;
        }

    }

    TerrainBuilder::TerrainBuilder(TerrainBuilderConfig config) :
        config_(std::move(config))
    {
    }

    std::shared_ptr<ChunkData> TerrainBuilder::buildChunkData(int chunkX, int chunkZ) const
    {
        auto chunk = std::make_shared<ChunkData>();
        chunk->chunkX = chunkX;
        chunk->chunkZ = chunkZ;
        chunk->blocks.assign(ChunkBlockCount, BlockAir);
        chunk->fluids.assign(ChunkBlockCount, FluidNone);
        chunk->fluidSubchunkCounts.fill(0);
        chunk->emptySubchunks.fill(true);
        chunk->terrainFeatureCandidates.clear();
        chunk->terrainFeatureCandidates.reserve(8);
        populateChunkClimate(*chunk);

        const std::array<TerrainDebugSample, ChunkColumnCount> terrainSamples = buildChunkTerrainDebugSamples(chunkX, chunkZ);
        std::array<int, ChunkColumnCount> heights{};
        std::array<BiomeSample, ChunkColumnCount> biomes{};
        for (size_t column = 0; column < heights.size(); ++column)
        {
            heights[column] = terrainSamples[column].height;
            biomes[column] = classifyBiome(
                decodeClimateValue(chunk->temperature[column]),
                decodeClimateValue(chunk->precipitation[column]),
                terrainSamples[column].groundness);
        }

        std::array<int, ChunkColumnCount> terrainTopY{};
        std::array<int, ChunkColumnCount> bedrockHeights{};
        terrainTopY.fill(-1);
        int maxHeight = 0;
        int minHeight = ChunkSizeY;
        for (size_t column = 0; column < heights.size(); ++column)
        {
            const int localX = static_cast<int>(column % ChunkSizeX);
            const int localZ = static_cast<int>(column / ChunkSizeX);
            const int height = heights[column];
            bedrockHeights[column] = bedrockHeightAt(chunkX * ChunkSizeX + localX, chunkZ * ChunkSizeZ + localZ);
            maxHeight = std::max(maxHeight, height);
            minHeight = std::min(minHeight, height);
            if (height > 0)
            {
                terrainTopY[column] = std::min(height - 1, ChunkSizeY - 1);
            }
            chunk->terrainHeight[column] = clampCacheY(height);
            chunk->terrainSurfaceY[column] = clampCacheY(terrainTopY[column]);
        }
        chunk->terrainSourceCacheValid = true;

        const int solidHeightLimit = std::min(maxHeight, ChunkSizeY);
        const int filledSubchunks = std::min(SubchunksPerChunk, (solidHeightLimit + SubchunkSize - 1) / SubchunkSize);
        for (int subchunkY = 0; subchunkY < filledSubchunks; ++subchunkY)
        {
            chunk->emptySubchunks[static_cast<size_t>(subchunkY)] = false;
        }

        constexpr size_t BlocksPerLayer = ChunkSizeX * ChunkSizeZ;
        const int worldXStart = chunkX * ChunkSizeX;
        const int worldZStart = chunkZ * ChunkSizeZ;
        const int commonSolidHeight = std::clamp(minHeight, 0, solidHeightLimit);
        for (int y = 0; y < solidHeightLimit; ++y)
        {
            uint16_t* layer = chunk->blocks.data() + static_cast<size_t>(y) * BlocksPerLayer;
            if (y < commonSolidHeight)
            {
                std::fill(layer, layer + BlocksPerLayer, BlockRock);
                continue;
            }

            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                    if (y < heights[column])
                    {
                        layer[column] = BlockRock;
                    }
                }
            }
        }

        constexpr uint16_t FullWater = packFluid(FluidWater, FluidFullAmount);
        const int seaY = std::clamp(config_.seaLevel, 0, ChunkSizeY - 1);
        const int waterStartY = std::clamp(minHeight, 0, seaY + 1);
        for (int y = waterStartY; y <= seaY; ++y)
        {
            uint16_t* fluidLayer = chunk->fluids.data() + static_cast<size_t>(y) * BlocksPerLayer;
            uint16_t& fluidSubchunkCount = chunk->fluidSubchunkCounts[static_cast<size_t>(y / SubchunkSize)];
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                    if (y >= heights[column])
                    {
                        if (y == seaY && biomes[column].id == BiomeId::FrozenOcean)
                        {
                            const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                            chunk->blocks[index] = BlockIce;
                            chunk->emptySubchunks[static_cast<size_t>(y / SubchunkSize)] = false;
                            continue;
                        }
                        fluidLayer[column] = FullWater;
                        ++fluidSubchunkCount;
                    }
                }
            }
        }

        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            for (int localX = 0; localX < ChunkSizeX; ++localX)
            {
                const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                const int bedrockHeight = bedrockHeights[column];
                for (int y = 0; y < bedrockHeight && y < ChunkSizeY; ++y)
                {
                    const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                    chunk->blocks[index] = BlockBedrock;
                    chunk->emptySubchunks[static_cast<size_t>(y / SubchunkSize)] = false;
                    uint16_t& fluid = chunk->fluids[index];
                    if (fluidId(fluid) != FluidNone && fluidAmount(fluid) != 0)
                    {
                        fluid = FluidNone;
                        uint16_t& count = chunk->fluidSubchunkCounts[static_cast<size_t>(y / SubchunkSize)];
                        if (count > 0)
                        {
                            --count;
                        }
                    }
                }
            }
        }

        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            for (int localX = 0; localX < ChunkSizeX; ++localX)
            {
                const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                const int surfaceY = terrainTopY[column];
                if (surfaceY < 0 || surfaceY >= ChunkSizeY)
                {
                    continue;
                }

                const size_t surfaceIndex = static_cast<size_t>((surfaceY * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                if (surfaceIndex >= chunk->blocks.size() || chunk->blocks[surfaceIndex] == BlockBedrock)
                {
                    continue;
                }

                const SurfaceRule rule = surfaceRuleForBiome(biomes[column].id);
                const bool underwaterSurface = surfaceY < seaY;
                const uint16_t surfaceBlock = underwaterSurface ? rule.underwaterSurface : rule.airSurface;
                const uint16_t subsurfaceBlock = underwaterSurface ? rule.underwaterSubsurface : rule.airSubsurface;
                chunk->blocks[surfaceIndex] = surfaceBlock;

                const int bedrockHeight = bedrockHeights[column];
                const int subsurfaceStartY = std::max(bedrockHeight, surfaceY - 4);
                for (int y = subsurfaceStartY; y < surfaceY; ++y)
                {
                    if (y < 0 || y >= ChunkSizeY)
                    {
                        continue;
                    }
                    const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                    if (index < chunk->blocks.size() && chunk->blocks[index] != BlockBedrock)
                    {
                        chunk->blocks[index] = subsurfaceBlock;
                    }
                }
            }
        }

        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            for (int localX = 0; localX < ChunkSizeX; ++localX)
            {
                const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                const int surfaceY = terrainTopY[column];
                const int placeY = surfaceY + 1;
                if (surfaceY < 0 || placeY >= ChunkSizeY)
                {
                    continue;
                }

                const size_t topIndex = static_cast<size_t>((surfaceY * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                const size_t plantIndex = static_cast<size_t>((placeY * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                if (topIndex < chunk->blocks.size() &&
                    plantIndex < chunk->blocks.size() &&
                    chunk->blocks[topIndex] == BlockGrass)
                {
                    const uint8_t placement = worldRandom8(worldXStart + localX, placeY, worldZStart + localZ, PlantPlacementSalt);
                    uint16_t placedBlock = BlockAir;
                    if (placement <= PlantPlacementMax)
                    {
                        placedBlock = BlockPlant;
                    }
                    else if (placement <= StonePlacementMax)
                    {
                        placedBlock = BlockStoneProp;
                    }
                    else if (placement <= BranchPlacementMax)
                    {
                        placedBlock = BlockBranchProp;
                    }
                    else if (placement >= TreePlacementMin && placement <= TreePlacementMax)
                    {
                        chunk->terrainFeatureCandidates.push_back(TerrainFeatureCandidate{
                            TerrainFeatureType::Tree,
                            static_cast<uint8_t>(localX),
                            static_cast<uint8_t>(localZ),
                            clampCacheY(placeY)});
                    }

                    if (placedBlock != BlockAir)
                    {
                        chunk->blocks[plantIndex] = placedBlock;
                        chunk->emptySubchunks[static_cast<size_t>(placeY / SubchunkSize)] = false;
                    }
                }
            }
        }
        chunk->terrainFeatureCandidatesValid = true;

        return chunk;
    }

    std::array<TerrainDebugSample, ChunkColumnCount> TerrainBuilder::buildChunkTerrainDebugSamples(int chunkX, int chunkZ) const
    {
        std::array<TerrainDebugSample, ChunkColumnCount> samples{};
        auto groundnessGenerator = fbmNoiseGenerator(
            FixedSimplexScale,
            config_.groundnessNoiseOctaveCount,
            config_.groundnessNoiseLacunarity,
            config_.groundnessNoiseGain);
        auto smoothnessGenerator = fbmNoiseGenerator(
            FixedSimplexScale,
            config_.smoothnessNoiseOctaveCount,
            config_.smoothnessNoiseLacunarity,
            config_.smoothnessNoiseGain);
        auto weirdnessGenerator = fbmNoiseGenerator(
            FixedSimplexScale,
            config_.weirdnessNoiseOctaveCount,
            config_.weirdnessNoiseLacunarity,
            config_.weirdnessNoiseGain);
        auto baseGenerator = fbmNoiseGenerator(
            FixedSimplexScale,
            config_.baseNoiseOctaveCount,
            config_.baseNoiseLacunarity,
            config_.baseNoiseGain);
        if (!groundnessGenerator || !smoothnessGenerator || !weirdnessGenerator || !baseGenerator)
        {
            const int fallbackHeight = heightFromLut(config_.heightLut, 0.0f);
            for (TerrainDebugSample& sample : samples)
            {
                sample.height = fallbackHeight;
            }
            return samples;
        }

        const auto fillTileablePositions = [chunkX, chunkZ](
            float featureScale,
            std::array<float, ChunkColumnCount>& xPositions,
            std::array<float, ChunkColumnCount>& yPositions,
            std::array<float, ChunkColumnCount>& zPositions,
            std::array<float, ChunkColumnCount>& wPositions)
        {
            constexpr float TwoPi = 6.28318530718f;
            const float angleScale = TwoPi / static_cast<float>(TerrainTilePeriod);
            const float radius = static_cast<float>(TerrainTilePeriod) / (TwoPi * featureScale);

            std::array<float, ChunkSizeX> xCos{};
            std::array<float, ChunkSizeX> xSin{};
            std::array<float, ChunkSizeZ> zCos{};
            std::array<float, ChunkSizeZ> zSin{};
            for (int localX = 0; localX < ChunkSizeX; ++localX)
            {
                const int worldX = chunkX * ChunkSizeX + localX;
                const float angle = static_cast<float>(positiveModulo(worldX, TerrainTilePeriod)) * angleScale;
                xCos[localX] = std::cos(angle) * radius;
                xSin[localX] = std::sin(angle) * radius;
            }
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                const int worldZ = chunkZ * ChunkSizeZ + localZ;
                const float angle = static_cast<float>(positiveModulo(worldZ, TerrainTilePeriod)) * angleScale;
                zCos[localZ] = std::cos(angle) * radius;
                zSin[localZ] = std::sin(angle) * radius;
            }
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const size_t index = static_cast<size_t>(localZ * ChunkSizeX + localX);
                    xPositions[index] = xCos[localX];
                    yPositions[index] = zCos[localZ];
                    zPositions[index] = xSin[localX];
                    wPositions[index] = zSin[localZ];
                }
            }
        };

        std::array<float, ChunkColumnCount> groundnessX{};
        std::array<float, ChunkColumnCount> groundnessY{};
        std::array<float, ChunkColumnCount> groundnessZ{};
        std::array<float, ChunkColumnCount> groundnessW{};
        std::array<float, ChunkColumnCount> baseX{};
        std::array<float, ChunkColumnCount> baseY{};
        std::array<float, ChunkColumnCount> baseZ{};
        std::array<float, ChunkColumnCount> baseW{};
        std::array<float, ChunkColumnCount> smoothnessX{};
        std::array<float, ChunkColumnCount> smoothnessY{};
        std::array<float, ChunkColumnCount> smoothnessZ{};
        std::array<float, ChunkColumnCount> smoothnessW{};
        std::array<float, ChunkColumnCount> weirdnessX{};
        std::array<float, ChunkColumnCount> weirdnessY{};
        std::array<float, ChunkColumnCount> weirdnessZ{};
        std::array<float, ChunkColumnCount> weirdnessW{};
        std::array<float, ChunkColumnCount> groundnessNoise{};
        std::array<float, ChunkColumnCount> smoothnessNoise{};
        std::array<float, ChunkColumnCount> weirdnessNoise{};
        std::array<float, ChunkColumnCount> baseNoise{};

        fillTileablePositions(config_.groundnessNoiseFeatureScale, groundnessX, groundnessY, groundnessZ, groundnessW);
        fillTileablePositions(config_.smoothnessNoiseFeatureScale, smoothnessX, smoothnessY, smoothnessZ, smoothnessW);
        fillTileablePositions(config_.weirdnessNoiseFeatureScale, weirdnessX, weirdnessY, weirdnessZ, weirdnessW);
        fillTileablePositions(config_.baseNoiseFeatureScale, baseX, baseY, baseZ, baseW);

        if (config_.groundnessDomainWarpEnabled && config_.groundnessDomainWarpAmplitude > 0.0f)
        {
            auto warpGenerator = fbmNoiseGenerator(
                config_.groundnessDomainWarpFrequency,
                config_.groundnessDomainWarpOctaveCount,
                DefaultTerrainNoiseLacunarity,
                config_.groundnessDomainWarpGain);
            if (warpGenerator)
            {
                std::array<float, ChunkColumnCount> xWarp{};
                std::array<float, ChunkColumnCount> yWarp{};
                std::array<float, ChunkColumnCount> zWarp{};
                std::array<float, ChunkColumnCount> wWarp{};

                warpGenerator->GenPositionArray4D(xWarp.data(), static_cast<int>(xWarp.size()), groundnessX.data(), groundnessY.data(), groundnessZ.data(), groundnessW.data(), 0.0f, 0.0f, 0.0f, 0.0f, groundnessSeed(101));
                warpGenerator->GenPositionArray4D(yWarp.data(), static_cast<int>(yWarp.size()), groundnessX.data(), groundnessY.data(), groundnessZ.data(), groundnessW.data(), 0.0f, 0.0f, 0.0f, 0.0f, groundnessSeed(202));
                warpGenerator->GenPositionArray4D(zWarp.data(), static_cast<int>(zWarp.size()), groundnessX.data(), groundnessY.data(), groundnessZ.data(), groundnessW.data(), 0.0f, 0.0f, 0.0f, 0.0f, groundnessSeed(303));
                warpGenerator->GenPositionArray4D(wWarp.data(), static_cast<int>(wWarp.size()), groundnessX.data(), groundnessY.data(), groundnessZ.data(), groundnessW.data(), 0.0f, 0.0f, 0.0f, 0.0f, groundnessSeed(404));

                for (size_t i = 0; i < groundnessX.size(); ++i)
                {
                    groundnessX[i] += xWarp[i] * config_.groundnessDomainWarpAmplitude;
                    groundnessY[i] += yWarp[i] * config_.groundnessDomainWarpAmplitude;
                    groundnessZ[i] += zWarp[i] * config_.groundnessDomainWarpAmplitude;
                    groundnessW[i] += wWarp[i] * config_.groundnessDomainWarpAmplitude;
                }
            }
        }

        if (config_.weirdnessDomainWarpEnabled && config_.weirdnessDomainWarpAmplitude > 0.0f)
        {
            auto warpGenerator = fbmNoiseGenerator(
                config_.weirdnessDomainWarpFrequency,
                config_.weirdnessDomainWarpOctaveCount,
                DefaultTerrainNoiseLacunarity,
                config_.weirdnessDomainWarpGain);
            if (warpGenerator)
            {
                std::array<float, ChunkColumnCount> xWarp{};
                std::array<float, ChunkColumnCount> yWarp{};
                std::array<float, ChunkColumnCount> zWarp{};
                std::array<float, ChunkColumnCount> wWarp{};

                warpGenerator->GenPositionArray4D(xWarp.data(), static_cast<int>(xWarp.size()), weirdnessX.data(), weirdnessY.data(), weirdnessZ.data(), weirdnessW.data(), 0.0f, 0.0f, 0.0f, 0.0f, weirdnessSeed(101));
                warpGenerator->GenPositionArray4D(yWarp.data(), static_cast<int>(yWarp.size()), weirdnessX.data(), weirdnessY.data(), weirdnessZ.data(), weirdnessW.data(), 0.0f, 0.0f, 0.0f, 0.0f, weirdnessSeed(202));
                warpGenerator->GenPositionArray4D(zWarp.data(), static_cast<int>(zWarp.size()), weirdnessX.data(), weirdnessY.data(), weirdnessZ.data(), weirdnessW.data(), 0.0f, 0.0f, 0.0f, 0.0f, weirdnessSeed(303));
                warpGenerator->GenPositionArray4D(wWarp.data(), static_cast<int>(wWarp.size()), weirdnessX.data(), weirdnessY.data(), weirdnessZ.data(), weirdnessW.data(), 0.0f, 0.0f, 0.0f, 0.0f, weirdnessSeed(404));

                for (size_t i = 0; i < weirdnessX.size(); ++i)
                {
                    weirdnessX[i] += xWarp[i] * config_.weirdnessDomainWarpAmplitude;
                    weirdnessY[i] += yWarp[i] * config_.weirdnessDomainWarpAmplitude;
                    weirdnessZ[i] += zWarp[i] * config_.weirdnessDomainWarpAmplitude;
                    weirdnessW[i] += wWarp[i] * config_.weirdnessDomainWarpAmplitude;
                }
            }
        }

        groundnessGenerator->GenPositionArray4D(
            groundnessNoise.data(),
            static_cast<int>(groundnessNoise.size()),
            groundnessX.data(),
            groundnessY.data(),
            groundnessZ.data(),
            groundnessW.data(),
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            groundnessSeed());
        smoothnessGenerator->GenPositionArray4D(
            smoothnessNoise.data(),
            static_cast<int>(smoothnessNoise.size()),
            smoothnessX.data(),
            smoothnessY.data(),
            smoothnessZ.data(),
            smoothnessW.data(),
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            smoothnessSeed());
        weirdnessGenerator->GenPositionArray4D(
            weirdnessNoise.data(),
            static_cast<int>(weirdnessNoise.size()),
            weirdnessX.data(),
            weirdnessY.data(),
            weirdnessZ.data(),
            weirdnessW.data(),
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            weirdnessSeed());
        baseGenerator->GenPositionArray4D(
            baseNoise.data(),
            static_cast<int>(baseNoise.size()),
            baseX.data(),
            baseY.data(),
            baseZ.data(),
            baseW.data(),
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            baseNoiseSeed());

        for (size_t i = 0; i < samples.size(); ++i)
        {
            samples[i] = terrainSampleFromNoise(
                config_,
                groundnessNoise[i],
                smoothnessNoise[i],
                weirdnessNoise[i],
                baseNoise[i]);
        }

        return samples;
    }

    std::array<int, ChunkColumnCount> TerrainBuilder::buildChunkHeightmap(int chunkX, int chunkZ) const
    {
        std::array<int, ChunkColumnCount> heights{};
        const std::array<TerrainDebugSample, ChunkColumnCount> samples = buildChunkTerrainDebugSamples(chunkX, chunkZ);
        for (size_t i = 0; i < samples.size(); ++i)
        {
            heights[i] = samples[i].height;
        }
        return heights;
    }

    std::vector<TerrainDebugSample> TerrainBuilder::buildTerrainDebugSamples(int sampleSize, int worldExtentBlocks) const
    {
        if (sampleSize <= 0 || worldExtentBlocks <= 0)
        {
            return {};
        }

        const size_t sampleCount = static_cast<size_t>(sampleSize) * static_cast<size_t>(sampleSize);
        std::vector<TerrainDebugSample> samples(sampleCount);
        auto groundnessGenerator = fbmNoiseGenerator(
            FixedSimplexScale,
            config_.groundnessNoiseOctaveCount,
            config_.groundnessNoiseLacunarity,
            config_.groundnessNoiseGain);
        auto smoothnessGenerator = fbmNoiseGenerator(
            FixedSimplexScale,
            config_.smoothnessNoiseOctaveCount,
            config_.smoothnessNoiseLacunarity,
            config_.smoothnessNoiseGain);
        auto weirdnessGenerator = fbmNoiseGenerator(
            FixedSimplexScale,
            config_.weirdnessNoiseOctaveCount,
            config_.weirdnessNoiseLacunarity,
            config_.weirdnessNoiseGain);
        auto baseGenerator = fbmNoiseGenerator(
            FixedSimplexScale,
            config_.baseNoiseOctaveCount,
            config_.baseNoiseLacunarity,
            config_.baseNoiseGain);
        if (!groundnessGenerator || !smoothnessGenerator || !weirdnessGenerator || !baseGenerator)
        {
            return samples;
        }

        auto fillTileablePositions = [sampleSize, worldExtentBlocks](
            float featureScale,
            std::vector<float>& xPositions,
            std::vector<float>& yPositions,
            std::vector<float>& zPositions,
            std::vector<float>& wPositions)
        {
            constexpr float TwoPi = 6.28318530718f;
            const float angleScale = TwoPi / static_cast<float>(TerrainTilePeriod);
            const float radius = static_cast<float>(TerrainTilePeriod) / (TwoPi * featureScale);

            xPositions.resize(static_cast<size_t>(sampleSize) * static_cast<size_t>(sampleSize));
            yPositions.resize(xPositions.size());
            zPositions.resize(xPositions.size());
            wPositions.resize(xPositions.size());

            for (int y = 0; y < sampleSize; ++y)
            {
                const int worldZ = (y * worldExtentBlocks) / sampleSize;
                const float zAngle = static_cast<float>(positiveModulo(worldZ, TerrainTilePeriod)) * angleScale;
                const float zCos = std::cos(zAngle) * radius;
                const float zSin = std::sin(zAngle) * radius;
                for (int x = 0; x < sampleSize; ++x)
                {
                    const int worldX = (x * worldExtentBlocks) / sampleSize;
                    const float xAngle = static_cast<float>(positiveModulo(worldX, TerrainTilePeriod)) * angleScale;
                    const size_t index = static_cast<size_t>(y) * static_cast<size_t>(sampleSize) + static_cast<size_t>(x);
                    xPositions[index] = std::cos(xAngle) * radius;
                    yPositions[index] = zCos;
                    zPositions[index] = std::sin(xAngle) * radius;
                    wPositions[index] = zSin;
                }
            }
        };

        auto applyDomainWarp = [](
            std::vector<float>& xPositions,
            std::vector<float>& yPositions,
            std::vector<float>& zPositions,
            std::vector<float>& wPositions,
            float amplitude,
            const FastNoise::SmartNode<>& warpGenerator,
            int seedBase)
        {
            std::vector<float> xWarp(xPositions.size());
            std::vector<float> yWarp(xPositions.size());
            std::vector<float> zWarp(xPositions.size());
            std::vector<float> wWarp(xPositions.size());
            warpGenerator->GenPositionArray4D(xWarp.data(), static_cast<int>(xWarp.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, seedBase + 101);
            warpGenerator->GenPositionArray4D(yWarp.data(), static_cast<int>(yWarp.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, seedBase + 202);
            warpGenerator->GenPositionArray4D(zWarp.data(), static_cast<int>(zWarp.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, seedBase + 303);
            warpGenerator->GenPositionArray4D(wWarp.data(), static_cast<int>(wWarp.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, seedBase + 404);
            for (size_t i = 0; i < xPositions.size(); ++i)
            {
                xPositions[i] += xWarp[i] * amplitude;
                yPositions[i] += yWarp[i] * amplitude;
                zPositions[i] += zWarp[i] * amplitude;
                wPositions[i] += wWarp[i] * amplitude;
            }
        };

        auto generateNoise = [&fillTileablePositions](
            int sampleSize,
            float featureScale,
            const FastNoise::SmartNode<>& generator,
            int seed,
            std::vector<float>& noise,
            std::vector<float>& xPositions,
            std::vector<float>& yPositions,
            std::vector<float>& zPositions,
            std::vector<float>& wPositions)
        {
            fillTileablePositions(featureScale, xPositions, yPositions, zPositions, wPositions);
            noise.resize(static_cast<size_t>(sampleSize) * static_cast<size_t>(sampleSize));
            generator->GenPositionArray4D(noise.data(), static_cast<int>(noise.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, seed);
        };

        std::vector<float> xPositions;
        std::vector<float> yPositions;
        std::vector<float> zPositions;
        std::vector<float> wPositions;
        std::vector<float> groundnessNoise;
        std::vector<float> smoothnessNoise;
        std::vector<float> weirdnessNoise;
        std::vector<float> baseNoise;

        fillTileablePositions(config_.groundnessNoiseFeatureScale, xPositions, yPositions, zPositions, wPositions);
        if (config_.groundnessDomainWarpEnabled && config_.groundnessDomainWarpAmplitude > 0.0f)
        {
            auto warpGenerator = fbmNoiseGenerator(config_.groundnessDomainWarpFrequency, config_.groundnessDomainWarpOctaveCount, DefaultTerrainNoiseLacunarity, config_.groundnessDomainWarpGain);
            if (warpGenerator)
            {
                applyDomainWarp(xPositions, yPositions, zPositions, wPositions, config_.groundnessDomainWarpAmplitude, warpGenerator, groundnessSeed());
            }
        }
        groundnessNoise.resize(sampleCount);
        groundnessGenerator->GenPositionArray4D(groundnessNoise.data(), static_cast<int>(groundnessNoise.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, groundnessSeed());

        generateNoise(sampleSize, config_.smoothnessNoiseFeatureScale, smoothnessGenerator, smoothnessSeed(), smoothnessNoise, xPositions, yPositions, zPositions, wPositions);

        fillTileablePositions(config_.weirdnessNoiseFeatureScale, xPositions, yPositions, zPositions, wPositions);
        if (config_.weirdnessDomainWarpEnabled && config_.weirdnessDomainWarpAmplitude > 0.0f)
        {
            auto warpGenerator = fbmNoiseGenerator(config_.weirdnessDomainWarpFrequency, config_.weirdnessDomainWarpOctaveCount, DefaultTerrainNoiseLacunarity, config_.weirdnessDomainWarpGain);
            if (warpGenerator)
            {
                applyDomainWarp(xPositions, yPositions, zPositions, wPositions, config_.weirdnessDomainWarpAmplitude, warpGenerator, weirdnessSeed());
            }
        }
        weirdnessNoise.resize(sampleCount);
        weirdnessGenerator->GenPositionArray4D(weirdnessNoise.data(), static_cast<int>(weirdnessNoise.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, weirdnessSeed());

        generateNoise(sampleSize, config_.baseNoiseFeatureScale, baseGenerator, baseNoiseSeed(), baseNoise, xPositions, yPositions, zPositions, wPositions);

        for (size_t i = 0; i < samples.size(); ++i)
        {
            samples[i] = terrainSampleFromNoise(
                config_,
                groundnessNoise[i],
                smoothnessNoise[i],
                weirdnessNoise[i],
                baseNoise[i]);
        }

        return samples;
    }

    std::vector<float> TerrainBuilder::buildTerrainDebugNoise(TerrainDebugNoise noise, int sampleSize, int worldExtentBlocks) const
    {
        if (sampleSize <= 0 || worldExtentBlocks <= 0)
        {
            return {};
        }

        const size_t sampleCount = static_cast<size_t>(sampleSize) * static_cast<size_t>(sampleSize);
        std::vector<float> samples(sampleCount);
        float featureScale = config_.smoothnessNoiseFeatureScale;
        int octaveCount = config_.smoothnessNoiseOctaveCount;
        float lacunarity = config_.smoothnessNoiseLacunarity;
        float gain = config_.smoothnessNoiseGain;
        int seed = smoothnessSeed();
        bool domainWarpEnabled = false;
        float domainWarpAmplitude = 0.0f;
        float domainWarpFrequency = 1.0f;
        int domainWarpOctaveCount = 1;
        float domainWarpGain = 0.5f;
        int domainWarpSeed = 0;

        switch (noise)
        {
        case TerrainDebugNoise::Groundness:
            featureScale = config_.groundnessNoiseFeatureScale;
            octaveCount = config_.groundnessNoiseOctaveCount;
            lacunarity = config_.groundnessNoiseLacunarity;
            gain = config_.groundnessNoiseGain;
            seed = groundnessSeed();
            domainWarpEnabled = config_.groundnessDomainWarpEnabled;
            domainWarpAmplitude = config_.groundnessDomainWarpAmplitude;
            domainWarpFrequency = config_.groundnessDomainWarpFrequency;
            domainWarpOctaveCount = config_.groundnessDomainWarpOctaveCount;
            domainWarpGain = config_.groundnessDomainWarpGain;
            domainWarpSeed = groundnessSeed();
            break;
        case TerrainDebugNoise::Smoothness:
            break;
        case TerrainDebugNoise::Weirdness:
        case TerrainDebugNoise::Pv:
            featureScale = config_.weirdnessNoiseFeatureScale;
            octaveCount = config_.weirdnessNoiseOctaveCount;
            lacunarity = config_.weirdnessNoiseLacunarity;
            gain = config_.weirdnessNoiseGain;
            seed = weirdnessSeed();
            domainWarpEnabled = config_.weirdnessDomainWarpEnabled;
            domainWarpAmplitude = config_.weirdnessDomainWarpAmplitude;
            domainWarpFrequency = config_.weirdnessDomainWarpFrequency;
            domainWarpOctaveCount = config_.weirdnessDomainWarpOctaveCount;
            domainWarpGain = config_.weirdnessDomainWarpGain;
            domainWarpSeed = weirdnessSeed();
            break;
        }

        auto generator = fbmNoiseGenerator(FixedSimplexScale, octaveCount, lacunarity, gain);
        if (!generator)
        {
            return samples;
        }

        constexpr float TwoPi = 6.28318530718f;
        const float angleScale = TwoPi / static_cast<float>(TerrainTilePeriod);
        const float radius = static_cast<float>(TerrainTilePeriod) / (TwoPi * featureScale);
        std::vector<float> xPositions(sampleCount);
        std::vector<float> yPositions(sampleCount);
        std::vector<float> zPositions(sampleCount);
        std::vector<float> wPositions(sampleCount);

        for (int y = 0; y < sampleSize; ++y)
        {
            const int worldZ = (y * worldExtentBlocks) / sampleSize;
            const float zAngle = static_cast<float>(positiveModulo(worldZ, TerrainTilePeriod)) * angleScale;
            const float zCos = std::cos(zAngle) * radius;
            const float zSin = std::sin(zAngle) * radius;
            for (int x = 0; x < sampleSize; ++x)
            {
                const int worldX = (x * worldExtentBlocks) / sampleSize;
                const float xAngle = static_cast<float>(positiveModulo(worldX, TerrainTilePeriod)) * angleScale;
                const size_t index = static_cast<size_t>(y) * static_cast<size_t>(sampleSize) + static_cast<size_t>(x);
                xPositions[index] = std::cos(xAngle) * radius;
                yPositions[index] = zCos;
                zPositions[index] = std::sin(xAngle) * radius;
                wPositions[index] = zSin;
            }
        }

        if (domainWarpEnabled && domainWarpAmplitude > 0.0f)
        {
            auto warpGenerator = fbmNoiseGenerator(domainWarpFrequency, domainWarpOctaveCount, DefaultTerrainNoiseLacunarity, domainWarpGain);
            if (warpGenerator)
            {
                std::vector<float> xWarp(sampleCount);
                std::vector<float> yWarp(sampleCount);
                std::vector<float> zWarp(sampleCount);
                std::vector<float> wWarp(sampleCount);
                warpGenerator->GenPositionArray4D(xWarp.data(), static_cast<int>(xWarp.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, domainWarpSeed + 101);
                warpGenerator->GenPositionArray4D(yWarp.data(), static_cast<int>(yWarp.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, domainWarpSeed + 202);
                warpGenerator->GenPositionArray4D(zWarp.data(), static_cast<int>(zWarp.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, domainWarpSeed + 303);
                warpGenerator->GenPositionArray4D(wWarp.data(), static_cast<int>(wWarp.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, domainWarpSeed + 404);
                for (size_t i = 0; i < sampleCount; ++i)
                {
                    xPositions[i] += xWarp[i] * domainWarpAmplitude;
                    yPositions[i] += yWarp[i] * domainWarpAmplitude;
                    zPositions[i] += zWarp[i] * domainWarpAmplitude;
                    wPositions[i] += wWarp[i] * domainWarpAmplitude;
                }
            }
        }

        generator->GenPositionArray4D(samples.data(), static_cast<int>(samples.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, seed);

        if (noise == TerrainDebugNoise::Pv)
        {
            for (float& value : samples)
            {
                value = 1.0f - std::abs(3.0f * std::abs(value) - 2.0f);
            }
        }

        return samples;
    }

    float TerrainBuilder::groundnessAtWorld(int worldX, int worldZ) const
    {
        return sampleTerrainAtWorld(worldX, worldZ).groundness;
    }

    TerrainDebugSample TerrainBuilder::sampleTerrainAtWorld(int worldX, int worldZ) const
    {
        const int chunkX = floorDiv(worldX, ChunkSizeX);
        const int chunkZ = floorDiv(worldZ, ChunkSizeZ);
        const int localX = positiveModulo(worldX, ChunkSizeX);
        const int localZ = positiveModulo(worldZ, ChunkSizeZ);
        const std::array<TerrainDebugSample, ChunkColumnCount> samples = buildChunkTerrainDebugSamples(chunkX, chunkZ);
        return samples[static_cast<size_t>(localZ * ChunkSizeX + localX)];
    }

    uint16_t TerrainBuilder::surfaceBlockAtWorld(int worldX, int worldZ, int* surfaceY) const
    {
        const int chunkX = floorDiv(worldX, ChunkSizeX);
        const int chunkZ = floorDiv(worldZ, ChunkSizeZ);
        const int localX = positiveModulo(worldX, ChunkSizeX);
        const int localZ = positiveModulo(worldZ, ChunkSizeZ);
        const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);

        const std::array<TerrainDebugSample, ChunkColumnCount> terrainSamples = buildChunkTerrainDebugSamples(chunkX, chunkZ);
        const int topY = terrainSamples[column].height > 0 ? std::min(terrainSamples[column].height - 1, ChunkSizeY - 1) : -1;
        if (surfaceY != nullptr)
        {
            *surfaceY = topY;
        }
        if (topY < 0 || topY >= ChunkSizeY)
        {
            return BlockAir;
        }

        const std::array<float, ChunkColumnCount> temperatureNoise = buildChunkTileableClimateNoise(
            chunkX,
            chunkZ,
            config_.temperatureNoiseFeatureScale,
            config_.temperatureNoiseSimplexScale,
            config_.temperatureNoiseOctaveCount,
            config_.temperatureNoiseLacunarity,
            config_.temperatureNoiseGain,
            temperatureSeed());
        const std::array<float, ChunkColumnCount> precipitationNoise = buildChunkTileableClimateNoise(
            chunkX,
            chunkZ,
            config_.precipitationNoiseFeatureScale,
            config_.precipitationNoiseSimplexScale,
            config_.precipitationNoiseOctaveCount,
            config_.precipitationNoiseLacunarity,
            config_.precipitationNoiseGain,
            precipitationSeed());

        const BiomeSample biome = classifyBiome(
            temperatureAtWrapped(wrapBlockCoordinate(worldZ), temperatureNoise[column]),
            precipitationAtNoise(precipitationNoise[column]),
            terrainSamples[column].groundness);
        const SurfaceRule rule = surfaceRuleForBiome(biome.id);
        return topY < config_.seaLevel ? rule.underwaterSurface : rule.airSurface;
    }

    std::array<FeatureWriteListPtr, FeatureNeighborCount> TerrainBuilder::buildTreeFeatures(
        const std::shared_ptr<ChunkData>& chunk,
        const std::array<int, ChunkColumnCount>& heights) const
    {
        std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingSlots{};
        for (FeatureWriteListPtr& slot : outgoingSlots)
        {
            slot = std::make_shared<FeatureWriteList>();
        }

        auto outgoingSlotForTarget = [&](int targetChunkX, int targetChunkZ) -> FeatureWriteList*
        {
            const int offsetX = targetChunkX - chunk->chunkX;
            const int offsetZ = targetChunkZ - chunk->chunkZ;
            const std::optional<size_t> slotIndex = featureNeighborIndex(offsetX, offsetZ);
            if (!slotIndex)
            {
                return nullptr;
            }
            return outgoingSlots[*slotIndex].get();
        };

        auto canPlaceTrunk = [](uint16_t existing)
        {
            return existing == BlockAir || existing == BlockPlant || existing == BlockLeaves;
        };

        auto canPlaceLeaves = [](uint16_t existing)
        {
            return existing == BlockAir || existing == BlockPlant;
        };

        auto setInternalBlock = [&](int localX, int y, int localZ, uint16_t block)
        {
            if (localX < 0 || localX >= ChunkSizeX || localZ < 0 || localZ >= ChunkSizeZ || y < 0 || y >= ChunkSizeY)
            {
                return;
            }

            const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
            uint16_t& existing = chunk->blocks[index];
            const bool canPlace = block == BlockTrunk ? canPlaceTrunk(existing) : canPlaceLeaves(existing);
            if (canPlace)
            {
                existing = block;
                chunk->emptySubchunks[static_cast<size_t>(y / SubchunkSize)] = false;
            }
        };

        auto emitLeaves = [&](int worldX, int y, int worldZ)
        {
            if (y < 0 || y >= ChunkSizeY)
            {
                return;
            }

            const int targetChunkX = floorDiv(worldX, ChunkSizeX);
            const int targetChunkZ = floorDiv(worldZ, ChunkSizeZ);
            if (targetChunkX == chunk->chunkX && targetChunkZ == chunk->chunkZ)
            {
                setInternalBlock(positiveModulo(worldX, ChunkSizeX), y, positiveModulo(worldZ, ChunkSizeZ), BlockLeaves);
                return;
            }

            FeatureWriteList* writes = outgoingSlotForTarget(targetChunkX, targetChunkZ);
            if (writes == nullptr)
            {
                return;
            }

            writes->push_back(FeatureWrite{
                positiveModulo(worldX, ChunkSizeX),
                y,
                positiveModulo(worldZ, ChunkSizeZ),
                BlockLeaves});
        };

        const int worldXStart = chunk->chunkX * ChunkSizeX;
        const int worldZStart = chunk->chunkZ * ChunkSizeZ;
        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            for (int localX = 0; localX < ChunkSizeX; ++localX)
            {
                const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                const int height = std::clamp(heights[column], 0, ChunkSizeY);
                if (height <= 0 || height + 5 >= ChunkSizeY)
                {
                    continue;
                }

                const int worldX = worldXStart + localX;
                const int worldZ = worldZStart + localZ;
                const uint8_t vegetationRandom = worldRandom8(worldX, height, worldZ, PlantPlacementSalt);
                if (vegetationRandom < TreePlacementMin || vegetationRandom > TreePlacementMax)
                {
                    continue;
                }

                const size_t topIndex = static_cast<size_t>(((height - 1) * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                if (topIndex >= chunk->blocks.size() || chunk->blocks[topIndex] != BlockGrass)
                {
                    continue;
                }

                for (int y = height; y <= height + 3; ++y)
                {
                    setInternalBlock(localX, y, localZ, BlockTrunk);
                }

                for (int y = height + 2; y <= height + 3; ++y)
                {
                    for (int dz = -2; dz <= 2; ++dz)
                    {
                        for (int dx = -2; dx <= 2; ++dx)
                        {
                            if (std::abs(dx) == 2 && std::abs(dz) == 2)
                            {
                                continue;
                            }
                            emitLeaves(worldX + dx, y, worldZ + dz);
                        }
                    }
                }

                for (int dz = -1; dz <= 1; ++dz)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        emitLeaves(worldX + dx, height + 4, worldZ + dz);
                    }
                }
            }
        }

        return outgoingSlots;
    }

    std::shared_ptr<ChunkData> TerrainBuilder::resolveFeaturesForCenter(
        const std::array<std::shared_ptr<ChunkData>, 9>& sourceChunks) const
    {
        const std::shared_ptr<ChunkData>& center = sourceChunks[4];
        if (!center || center->blocks.size() != ChunkBlockCount)
        {
            return nullptr;
        }

        auto result = std::make_shared<ChunkData>(*center);
        result->localLight.clear();
        result->light.clear();

        auto setCenterOre = [&](int worldX, int y, int worldZ, const TerrainBuilderConfig::OreFeature& ore)
        {
            if (y < 0 || y >= ChunkSizeY)
            {
                return;
            }
            const int targetChunkX = floorDiv(worldX, ChunkSizeX);
            const int targetChunkZ = floorDiv(worldZ, ChunkSizeZ);
            if (targetChunkX != center->chunkX || targetChunkZ != center->chunkZ)
            {
                return;
            }

            const int localX = positiveModulo(worldX, ChunkSizeX);
            const int localZ = positiveModulo(worldZ, ChunkSizeZ);
            const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
            uint16_t& existing = result->blocks[index];
            if (existing == ore.replace)
            {
                existing = ore.block;
                result->emptySubchunks[static_cast<size_t>(y / SubchunkSize)] = false;
            }
        };

        auto emitOreBlob = [&](const TerrainBuilderConfig::OreFeature& ore, int sourceChunkX, int sourceChunkZ, int attempt)
        {
            if (ore.block == 0 || ore.replace == 0 || ore.attemptsPerChunk <= 0 || ore.size <= 0)
            {
                return;
            }

            constexpr float Pi = 3.14159265358979323846f;
            constexpr int WrappedChunkPeriod = WorldSizeBlocks / ChunkSizeX;
            const int minY = std::clamp(ore.minY, 0, ChunkSizeY - 1);
            const int maxYExclusive = std::clamp(ore.maxY, minY + 1, ChunkSizeY);
            const int size = std::max(1, ore.size);
            const int wrappedChunkX = positiveModulo(sourceChunkX, WrappedChunkPeriod);
            const int wrappedChunkZ = positiveModulo(sourceChunkZ, WrappedChunkPeriod);
            auto randomUnit = [&](uint32_t salt)
            {
                return hashUnitFloat(worldRandomHash(
                    wrappedChunkX,
                    attempt,
                    wrappedChunkZ,
                    OrePlacementSalt + ore.salt + salt));
            };

            const int anchorX = sourceChunkX * ChunkSizeX + std::clamp(static_cast<int>(randomUnit(0x11u) * ChunkSizeX), 0, ChunkSizeX - 1);
            const int anchorZ = sourceChunkZ * ChunkSizeZ + std::clamp(static_cast<int>(randomUnit(0x22u) * ChunkSizeZ), 0, ChunkSizeZ - 1);
            const int anchorY = minY + std::clamp(
                static_cast<int>(randomUnit(0x33u) * static_cast<float>(maxYExclusive - minY)),
                0,
                maxYExclusive - minY - 1);
            const float angle = randomUnit(0x44u) * Pi * 2.0f;
            const float lineRadius = static_cast<float>(size) / 8.0f;
            const float startX = static_cast<float>(anchorX) + std::sin(angle) * lineRadius;
            const float endX = static_cast<float>(anchorX) - std::sin(angle) * lineRadius;
            const float startZ = static_cast<float>(anchorZ) + std::cos(angle) * lineRadius;
            const float endZ = static_cast<float>(anchorZ) - std::cos(angle) * lineRadius;
            const float startY = static_cast<float>(anchorY + static_cast<int>(randomUnit(0x55u) * 5.0f) - 2);
            const float endY = static_cast<float>(anchorY + static_cast<int>(randomUnit(0x66u) * 5.0f) - 2);

            for (int step = 0; step < size; ++step)
            {
                const float progress = size <= 1 ? 0.0f : static_cast<float>(step) / static_cast<float>(size - 1);
                const float centerX = startX + (endX - startX) * progress;
                const float centerY = startY + (endY - startY) * progress;
                const float centerZ = startZ + (endZ - startZ) * progress;
                const float bulge = (std::sin(Pi * progress) + 1.0f) * 0.5f;
                const float radius = std::max(0.75f, bulge * (0.75f + randomUnit(0x100u + static_cast<uint32_t>(step)) * static_cast<float>(size) / 16.0f));
                const float radiusY = std::max(0.65f, radius * 0.75f);
                const int minX = static_cast<int>(std::floor(centerX - radius));
                const int maxX = static_cast<int>(std::floor(centerX + radius));
                const int sampleMinY = std::max(minY, static_cast<int>(std::floor(centerY - radiusY)));
                const int sampleMaxY = std::min(maxYExclusive - 1, static_cast<int>(std::floor(centerY + radiusY)));
                const int minZ = static_cast<int>(std::floor(centerZ - radius));
                const int maxZ = static_cast<int>(std::floor(centerZ + radius));

                for (int y = sampleMinY; y <= sampleMaxY; ++y)
                {
                    for (int z = minZ; z <= maxZ; ++z)
                    {
                        for (int x = minX; x <= maxX; ++x)
                        {
                            const float dx = (static_cast<float>(x) + 0.5f - centerX) / radius;
                            const float dy = (static_cast<float>(y) + 0.5f - centerY) / radiusY;
                            const float dz = (static_cast<float>(z) + 0.5f - centerZ) / radius;
                            if (dx * dx + dy * dy + dz * dz <= 1.0f)
                            {
                                setCenterOre(x, y, z, ore);
                            }
                        }
                    }
                }
            }
        };

        auto canPlaceTrunk = [](uint16_t existing)
        {
            return existing == BlockAir || existing == BlockPlant || existing == BlockLeaves;
        };

        auto canPlaceLeaves = [](uint16_t existing)
        {
            return existing == BlockAir || existing == BlockPlant;
        };

        auto setCenterBlock = [&](int worldX, int y, int worldZ, uint16_t block)
        {
            if (y < 0 || y >= ChunkSizeY)
            {
                return;
            }
            const int targetChunkX = floorDiv(worldX, ChunkSizeX);
            const int targetChunkZ = floorDiv(worldZ, ChunkSizeZ);
            if (targetChunkX != center->chunkX || targetChunkZ != center->chunkZ)
            {
                return;
            }

            const int localX = positiveModulo(worldX, ChunkSizeX);
            const int localZ = positiveModulo(worldZ, ChunkSizeZ);
            const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
            uint16_t& existing = result->blocks[index];
            const bool canPlace = block == BlockTrunk ? canPlaceTrunk(existing) : canPlaceLeaves(existing);
            if (canPlace)
            {
                existing = block;
                result->emptySubchunks[static_cast<size_t>(y / SubchunkSize)] = false;
            }
        };

        auto emitTree = [&](int worldX, int height, int worldZ)
        {
            if (height <= 0 || height + 5 >= ChunkSizeY)
            {
                return;
            }

            for (int y = height; y <= height + 3; ++y)
            {
                setCenterBlock(worldX, y, worldZ, BlockTrunk);
            }

            for (int y = height + 2; y <= height + 3; ++y)
            {
                for (int dz = -2; dz <= 2; ++dz)
                {
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        if (std::abs(dx) == 2 && std::abs(dz) == 2)
                        {
                            continue;
                        }
                        setCenterBlock(worldX + dx, y, worldZ + dz, BlockLeaves);
                    }
                }
            }

            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    setCenterBlock(worldX + dx, height + 4, worldZ + dz, BlockLeaves);
                }
            }
        };

        for (const TerrainBuilderConfig::OreFeature& ore : config_.oreFeatures)
        {
            for (const std::shared_ptr<ChunkData>& source : sourceChunks)
            {
                if (!source || source->blocks.size() != ChunkBlockCount)
                {
                    continue;
                }

                for (int attempt = 0; attempt < ore.attemptsPerChunk; ++attempt)
                {
                    emitOreBlob(ore, source->chunkX, source->chunkZ, attempt);
                }
            }
        }

        for (const std::shared_ptr<ChunkData>& source : sourceChunks)
        {
            if (!source || source->blocks.size() != ChunkBlockCount)
            {
                continue;
            }

            const int worldXStart = source->chunkX * ChunkSizeX;
            const int worldZStart = source->chunkZ * ChunkSizeZ;
            if (source->terrainFeatureCandidatesValid)
            {
                for (const TerrainFeatureCandidate& candidate : source->terrainFeatureCandidates)
                {
                    if (candidate.type != TerrainFeatureType::Tree)
                    {
                        continue;
                    }
                    emitTree(
                        worldXStart + static_cast<int>(candidate.localX),
                        std::clamp(static_cast<int>(candidate.baseY), 0, ChunkSizeY),
                        worldZStart + static_cast<int>(candidate.localZ));
                }
                continue;
            }

            std::array<int, ChunkColumnCount> fallbackHeights{};
            const bool useCachedHeights = source->terrainSourceCacheValid;
            if (!useCachedHeights)
            {
                fallbackHeights = buildChunkHeightmap(source->chunkX, source->chunkZ);
            }
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                    const int height = useCachedHeights
                        ? std::clamp(static_cast<int>(source->terrainHeight[column]), 0, ChunkSizeY)
                        : std::clamp(fallbackHeights[column], 0, ChunkSizeY);
                    if (height <= 0 || height + 5 >= ChunkSizeY)
                    {
                        continue;
                    }

                    const int worldX = worldXStart + localX;
                    const int worldZ = worldZStart + localZ;
                    const uint8_t vegetationRandom = worldRandom8(worldX, height, worldZ, PlantPlacementSalt);
                    if (vegetationRandom < TreePlacementMin || vegetationRandom > TreePlacementMax)
                    {
                        continue;
                    }

                    const size_t topIndex = static_cast<size_t>(((height - 1) * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                    if (topIndex >= source->blocks.size() || source->blocks[topIndex] != BlockGrass)
                    {
                        continue;
                    }

                    emitTree(worldX, height, worldZ);
                }
            }
        }

        if (result->blocks != center->blocks)
        {
            ++result->revision;
        }
        return result;
    }

    bool TerrainBuilder::applyFeatureWrites(
        const std::shared_ptr<ChunkData>& chunk,
        const std::array<FeatureWriteListPtr, FeatureNeighborCount>& incomingFeatureSlots) const
    {
        struct OrderedFeatureWrite
        {
            FeatureWrite write;
            size_t sourceSlot = 0;
            size_t order = 0;
        };

        auto blockPriority = [](uint16_t block)
        {
            if (block == BlockTrunk)
            {
                return 300;
            }
            if (block == BlockLeaves)
            {
                return 200;
            }
            if (block == BlockPlant)
            {
                return 100;
            }
            return 0;
        };

        auto canReplace = [](uint16_t block, uint16_t existing)
        {
            if (block == BlockTrunk)
            {
                return existing == BlockAir || existing == BlockPlant || existing == BlockLeaves;
            }
            if (block == BlockLeaves)
            {
                return existing == BlockAir || existing == BlockPlant;
            }
            if (block == BlockPlant)
            {
                return existing == BlockAir;
            }
            return false;
        };

        std::vector<OrderedFeatureWrite> orderedWrites;
        for (size_t sourceSlot = 0; sourceSlot < incomingFeatureSlots.size(); ++sourceSlot)
        {
            const FeatureWriteListPtr& writes = incomingFeatureSlots[sourceSlot];
            if (!writes)
            {
                continue;
            }

            orderedWrites.reserve(orderedWrites.size() + writes->size());
            for (size_t i = 0; i < writes->size(); ++i)
            {
                orderedWrites.push_back(OrderedFeatureWrite{(*writes)[i], sourceSlot, i});
            }
        }

        std::stable_sort(orderedWrites.begin(), orderedWrites.end(), [&](const OrderedFeatureWrite& left, const OrderedFeatureWrite& right)
        {
            const FeatureWrite& a = left.write;
            const FeatureWrite& b = right.write;
            if (a.y != b.y)
            {
                return a.y < b.y;
            }
            if (a.localZ != b.localZ)
            {
                return a.localZ < b.localZ;
            }
            if (a.localX != b.localX)
            {
                return a.localX < b.localX;
            }
            const int leftPriority = blockPriority(a.block);
            const int rightPriority = blockPriority(b.block);
            if (leftPriority != rightPriority)
            {
                return leftPriority > rightPriority;
            }
            if (left.sourceSlot != right.sourceSlot)
            {
                return left.sourceSlot < right.sourceSlot;
            }
            return left.order < right.order;
        });

        bool changed = false;
        for (const OrderedFeatureWrite& orderedWrite : orderedWrites)
        {
            const FeatureWrite& write = orderedWrite.write;
            if (write.localX < 0 || write.localX >= ChunkSizeX ||
                write.localZ < 0 || write.localZ >= ChunkSizeZ ||
                write.y < 0 || write.y >= ChunkSizeY)
            {
                continue;
            }

            const size_t index = static_cast<size_t>((write.y * ChunkSizeZ + write.localZ) * ChunkSizeX + write.localX);
            uint16_t& existing = chunk->blocks[index];
            if (canReplace(write.block, existing))
            {
                existing = write.block;
                chunk->emptySubchunks[static_cast<size_t>(write.y / SubchunkSize)] = false;
                changed = true;
            }
        }

        if (changed)
        {
            ++chunk->revision;
        }
        return changed;
    }

    std::array<float, ChunkColumnCount> TerrainBuilder::buildChunkTileableClimateNoise(
        int chunkX,
        int chunkZ,
        float featureScale,
        float simplexScale,
        int octaveCount,
        float lacunarity,
        float gain,
        int seed) const
    {
        std::array<float, ChunkColumnCount> noise{};
        auto generator = fbmNoiseGenerator(simplexScale, octaveCount, lacunarity, gain);
        if (!generator)
        {
            return noise;
        }

        constexpr float TwoPi = 6.28318530718f;
        const float angleScale = TwoPi / static_cast<float>(TerrainTilePeriod);
        const float radius = static_cast<float>(TerrainTilePeriod) / (TwoPi * featureScale);
        std::array<float, ChunkColumnCount> xPositions{};
        std::array<float, ChunkColumnCount> yPositions{};
        std::array<float, ChunkColumnCount> zPositions{};
        std::array<float, ChunkColumnCount> wPositions{};

        const int worldXStart = chunkX * ChunkSizeX;
        const int worldZStart = chunkZ * ChunkSizeZ;
        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            const float zAngle = static_cast<float>(positiveModulo(worldZStart + localZ, TerrainTilePeriod)) * angleScale;
            const float zCos = std::cos(zAngle) * radius;
            const float zSin = std::sin(zAngle) * radius;
            for (int localX = 0; localX < ChunkSizeX; ++localX)
            {
                const float xAngle = static_cast<float>(positiveModulo(worldXStart + localX, TerrainTilePeriod)) * angleScale;
                const size_t index = static_cast<size_t>(localZ * ChunkSizeX + localX);
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

    void TerrainBuilder::populateChunkClimate(ChunkData& chunk) const
    {
        const std::array<float, ChunkColumnCount> temperatureNoise = buildChunkTileableClimateNoise(
            chunk.chunkX,
            chunk.chunkZ,
            config_.temperatureNoiseFeatureScale,
            config_.temperatureNoiseSimplexScale,
            config_.temperatureNoiseOctaveCount,
            config_.temperatureNoiseLacunarity,
            config_.temperatureNoiseGain,
            temperatureSeed());
        const std::array<float, ChunkColumnCount> precipitationNoise = buildChunkTileableClimateNoise(
            chunk.chunkX,
            chunk.chunkZ,
            config_.precipitationNoiseFeatureScale,
            config_.precipitationNoiseSimplexScale,
            config_.precipitationNoiseOctaveCount,
            config_.precipitationNoiseLacunarity,
            config_.precipitationNoiseGain,
            precipitationSeed());

        const int worldZStart = chunk.chunkZ * ChunkSizeZ;
        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            const int wrappedZ = wrapBlockCoordinate(worldZStart + localZ);
            for (int localX = 0; localX < ChunkSizeX; ++localX)
            {
                const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                chunk.temperature[column] = encodeClimateValue(temperatureAtWrapped(wrappedZ, temperatureNoise[column]));
                chunk.precipitation[column] = encodeClimateValue(precipitationAtNoise(precipitationNoise[column]));
            }
        }
    }

    float TerrainBuilder::temperatureAtWrapped(int wrappedZ, float noise) const
    {
        const float base = baseTemperatureAtWrappedZ(wrappedZ);
        const float midLatitudeMask = 1.0f - std::abs(base * 2.0f - 1.0f);
        return std::clamp(base + noise * config_.temperatureNoiseStrength * midLatitudeMask, 0.0f, 1.0f);
    }

    float TerrainBuilder::precipitationAtNoise(float noise) const
    {
        return std::clamp(noise * 0.5f + 0.5f, 0.0f, 1.0f);
    }

    int TerrainBuilder::groundnessSeed(int offset) const
    {
        return GroundnessNoiseSeed + config_.activeWorldSeedSalt + offset;
    }

    int TerrainBuilder::smoothnessSeed() const
    {
        return SmoothnessNoiseSeed + config_.activeWorldSeedSalt;
    }

    int TerrainBuilder::weirdnessSeed(int offset) const
    {
        return WeirdnessNoiseSeed + config_.activeWorldSeedSalt + offset;
    }

    int TerrainBuilder::baseNoiseSeed() const
    {
        return BaseNoiseSeed + config_.activeWorldSeedSalt;
    }

    int TerrainBuilder::temperatureSeed() const
    {
        return TemperatureNoiseSeed + config_.activeWorldSeedSalt;
    }

    int TerrainBuilder::precipitationSeed() const
    {
        return PrecipitationNoiseSeed + config_.activeWorldSeedSalt;
    }

    float TerrainBuilder::baseTemperatureAtWrappedZ(int wrappedZ) const
    {
        const float normalizedZ = static_cast<float>(positiveModulo(wrappedZ, WorldSizeBlocks)) / static_cast<float>(WorldSizeBlocks);
        return std::clamp(1.0f - std::abs(normalizedZ * 2.0f - 1.0f), 0.0f, 1.0f);
    }
}
