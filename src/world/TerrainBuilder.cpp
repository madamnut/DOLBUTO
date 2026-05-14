#include "world/TerrainBuilder.h"

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
        constexpr int TerrainNoiseSeed = 1337;
        constexpr int TemperatureNoiseSeed = 2400;
        constexpr int PrecipitationNoiseSeed = 2401;
        constexpr float DefaultTerrainNoiseLacunarity = 2.0f;
        constexpr float HeightLutNoiseMin = -2.0f;
        constexpr float HeightLutNoiseMax = 2.0f;
        constexpr uint16_t BlockAir = 0;
        constexpr uint16_t BlockRock = 1;
        constexpr uint16_t BlockGrass = 2;
        constexpr uint16_t BlockDirt = 3;
        constexpr uint16_t BlockSand = 4;
        constexpr uint16_t BlockTrunk = 8;
        constexpr uint16_t BlockLeaves = 9;
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

        struct FeatureNeighborOffset
        {
            int x = 0;
            int z = 0;
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

        uint8_t encodeClimateValue(float value)
        {
            return static_cast<uint8_t>(std::clamp(
                std::lround(std::clamp(value, 0.0f, 1.0f) * static_cast<float>(ClimateMaxByte)),
                static_cast<long>(ClimateMinByte),
                static_cast<long>(ClimateMaxByte)));
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

        FastNoise::SmartNode<> terrainNoiseGenerator(float simplexScale, int octaveCount, float lacunarity, float gain)
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

        int heightFromLut(const std::array<uint16_t, TerrainHeightLutCount>& heightLut, float noise)
        {
            constexpr float scale = static_cast<float>(TerrainHeightLutCount - 1u) / (HeightLutNoiseMax - HeightLutNoiseMin);
            const float normalized = (noise - HeightLutNoiseMin) * scale;
            const int index = std::clamp(
                static_cast<int>(normalized + 0.5f),
                0,
                static_cast<int>(TerrainHeightLutCount - 1u));
            return static_cast<int>(heightLut[static_cast<size_t>(index)]);
        }

        void convertNoiseToHeights(
            const std::array<uint16_t, TerrainHeightLutCount>& heightLut,
            const std::array<float, ChunkColumnCount>& noise,
            std::array<int, ChunkColumnCount>& heights)
        {
            constexpr float scale = static_cast<float>(TerrainHeightLutCount - 1u) / (HeightLutNoiseMax - HeightLutNoiseMin);
            constexpr int maxIndex = static_cast<int>(TerrainHeightLutCount - 1u);
            for (size_t i = 0; i < noise.size(); ++i)
            {
                const float normalized = (noise[i] - HeightLutNoiseMin) * scale;
                const int index = std::clamp(static_cast<int>(normalized + 0.5f), 0, maxIndex);
                heights[i] = static_cast<int>(heightLut[static_cast<size_t>(index)]);
            }
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
        populateChunkClimate(*chunk);

        std::array<int, ChunkColumnCount> heights = buildChunkHeightmap(chunkX, chunkZ);
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
        }

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

                const int aboveY = surfaceY + 1;
                const bool waterAbove = aboveY >= 0 && aboveY < ChunkSizeY &&
                    chunk->fluids[static_cast<size_t>((aboveY * ChunkSizeZ + localZ) * ChunkSizeX + localX)] != FluidNone;
                const uint16_t surfaceBlock = waterAbove ? BlockSand : BlockGrass;
                const uint16_t subsurfaceBlock = waterAbove ? BlockSand : BlockDirt;
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

                    if (placedBlock != BlockAir)
                    {
                        chunk->blocks[plantIndex] = placedBlock;
                        chunk->emptySubchunks[static_cast<size_t>(placeY / SubchunkSize)] = false;
                    }
                }
            }
        }

        return chunk;
    }

    std::array<int, ChunkColumnCount> TerrainBuilder::buildChunkHeightmap(int chunkX, int chunkZ) const
    {
        std::array<int, ChunkColumnCount> heights{};
        auto generator = terrainNoiseGenerator(
            config_.terrainNoiseSimplexScale,
            config_.terrainNoiseOctaveCount,
            config_.terrainNoiseLacunarity,
            config_.terrainNoiseGain);
        if (!generator)
        {
            heights.fill(heightFromLut(config_.heightLut, 0.0f));
            return heights;
        }

        constexpr float TwoPi = 6.28318530718f;
        const float angleScale = TwoPi / static_cast<float>(TerrainTilePeriod);
        const float radius = static_cast<float>(TerrainTilePeriod) / (TwoPi * config_.terrainNoiseFeatureScale);

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

        std::array<float, ChunkColumnCount> xPositions{};
        std::array<float, ChunkColumnCount> yPositions{};
        std::array<float, ChunkColumnCount> zPositions{};
        std::array<float, ChunkColumnCount> wPositions{};
        std::array<float, ChunkColumnCount> noise{};
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

        if (config_.terrainDomainWarpEnabled && config_.terrainDomainWarpAmplitude > 0.0f)
        {
            auto warpGenerator = terrainNoiseGenerator(
                config_.terrainDomainWarpFrequency,
                config_.terrainDomainWarpOctaveCount,
                DefaultTerrainNoiseLacunarity,
                config_.terrainDomainWarpGain);
            if (warpGenerator)
            {
                std::array<float, ChunkColumnCount> xWarp{};
                std::array<float, ChunkColumnCount> yWarp{};
                std::array<float, ChunkColumnCount> zWarp{};
                std::array<float, ChunkColumnCount> wWarp{};

                warpGenerator->GenPositionArray4D(xWarp.data(), static_cast<int>(xWarp.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, terrainSeed(101));
                warpGenerator->GenPositionArray4D(yWarp.data(), static_cast<int>(yWarp.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, terrainSeed(202));
                warpGenerator->GenPositionArray4D(zWarp.data(), static_cast<int>(zWarp.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, terrainSeed(303));
                warpGenerator->GenPositionArray4D(wWarp.data(), static_cast<int>(wWarp.size()), xPositions.data(), yPositions.data(), zPositions.data(), wPositions.data(), 0.0f, 0.0f, 0.0f, 0.0f, terrainSeed(404));

                for (size_t i = 0; i < xPositions.size(); ++i)
                {
                    xPositions[i] += xWarp[i] * config_.terrainDomainWarpAmplitude;
                    yPositions[i] += yWarp[i] * config_.terrainDomainWarpAmplitude;
                    zPositions[i] += zWarp[i] * config_.terrainDomainWarpAmplitude;
                    wPositions[i] += wWarp[i] * config_.terrainDomainWarpAmplitude;
                }
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
            terrainSeed());

        convertNoiseToHeights(config_.heightLut, noise, heights);
        return heights;
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

    bool TerrainBuilder::applyFeatureWrites(
        const std::shared_ptr<ChunkData>& chunk,
        const std::array<FeatureWriteListPtr, FeatureNeighborCount>& incomingFeatureSlots) const
    {
        bool changed = false;
        for (const FeatureWriteListPtr& writes : incomingFeatureSlots)
        {
            if (!writes)
            {
                continue;
            }

            for (const FeatureWrite& write : *writes)
            {
                if (write.block != BlockLeaves ||
                    write.localX < 0 || write.localX >= ChunkSizeX ||
                    write.localZ < 0 || write.localZ >= ChunkSizeZ ||
                    write.y < 0 || write.y >= ChunkSizeY)
                {
                    continue;
                }

                const size_t index = static_cast<size_t>((write.y * ChunkSizeZ + write.localZ) * ChunkSizeX + write.localX);
                uint16_t& existing = chunk->blocks[index];
                if (existing == BlockAir || existing == BlockPlant)
                {
                    existing = BlockLeaves;
                    chunk->emptySubchunks[static_cast<size_t>(write.y / SubchunkSize)] = false;
                    changed = true;
                }
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
        auto generator = terrainNoiseGenerator(simplexScale, octaveCount, lacunarity, gain);
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

    int TerrainBuilder::terrainSeed(int offset) const
    {
        return TerrainNoiseSeed + config_.activeWorldSeedSalt + offset;
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
