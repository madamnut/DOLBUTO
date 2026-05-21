#include "world/TerrainMesher.h"

#include "world/SkyLightSystem.h"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace dolbuto::world
{
    namespace
    {
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeY = 512;
        constexpr int ChunkSizeZ = 16;
        constexpr int SubchunkSize = 16;
        constexpr int SubchunksPerChunk = ChunkSizeY / SubchunkSize;
        constexpr int MeshingBorder = 1;
        constexpr int MeshingSizeX = ChunkSizeX + MeshingBorder * 2;
        constexpr int MeshingSizeZ = ChunkSizeZ + MeshingBorder * 2;
        constexpr int EditMeshingSizeY = SubchunkSize + MeshingBorder * 2;
        constexpr uint16_t BlockAir = 0;
        constexpr uint16_t FluidNone = 0;
        constexpr uint16_t FluidWater = 1;
        constexpr uint16_t FluidFullAmount = 100;
        constexpr uint16_t FluidHeightStepAmount = 10;
        constexpr uint16_t FluidHeightLevels = 10;
        constexpr float FluidSurfaceMaxHeight = 0.8f;
        constexpr int FluidAmountBits = 7;
        constexpr uint16_t FluidAmountMask = (1u << FluidAmountBits) - 1u;
        constexpr float FluidMipDistanceScale = 0.0f;

        constexpr uint16_t fluidId(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid >> FluidAmountBits);
        }

        constexpr uint16_t fluidAmount(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid & FluidAmountMask);
        }

        constexpr float fluidSurfaceHeight(uint16_t amount)
        {
            const uint16_t clampedAmount = amount > FluidFullAmount ? FluidFullAmount : amount;
            if (clampedAmount == 0)
            {
                return 0.0f;
            }
            const uint16_t level = static_cast<uint16_t>((clampedAmount + FluidHeightStepAmount - 1u) / FluidHeightStepAmount);
            return (static_cast<float>(level) / static_cast<float>(FluidHeightLevels)) * FluidSurfaceMaxHeight;
        }
    }

    CompletedChunkMesh TerrainMesher::buildChunkMesh(
        const std::array<std::shared_ptr<ChunkData>, 9>& chunks,
        uint64_t generation,
        const SolidSubchunkBuilder& buildSolidSubchunk,
        const BlockOcclusionPredicate& blockOccludesFluid) const
    {
        const std::shared_ptr<ChunkData>& chunk = chunks[4];
        CompletedChunkMesh result{};
        if (!chunk)
        {
            result.generation = generation;
            return result;
        }

        result.generation = generation;
        result.revision = chunk->revision;
        result.chunkX = chunk->chunkX;
        result.chunkZ = chunk->chunkZ;

        auto blockAt = [&](int localX, int y, int localZ) -> uint16_t
        {
            if (y < 0 || y >= ChunkSizeY)
            {
                return BlockAir;
            }

            int chunkOffsetX = 0;
            int chunkOffsetZ = 0;
            int sampleX = localX;
            int sampleZ = localZ;
            if (sampleX < 0)
            {
                chunkOffsetX = -1;
                sampleX += ChunkSizeX;
            }
            else if (sampleX >= ChunkSizeX)
            {
                chunkOffsetX = 1;
                sampleX -= ChunkSizeX;
            }

            if (sampleZ < 0)
            {
                chunkOffsetZ = -1;
                sampleZ += ChunkSizeZ;
            }
            else if (sampleZ >= ChunkSizeZ)
            {
                chunkOffsetZ = 1;
                sampleZ -= ChunkSizeZ;
            }

            if (sampleX < 0 || sampleX >= ChunkSizeX || sampleZ < 0 || sampleZ >= ChunkSizeZ)
            {
                return BlockAir;
            }

            const std::shared_ptr<ChunkData>& sampleChunk = chunks[static_cast<size_t>((chunkOffsetZ + 1) * 3 + (chunkOffsetX + 1))];
            if (!sampleChunk || sampleChunk->blocks.size() != ChunkBlockCount)
            {
                return BlockAir;
            }

            const size_t index = static_cast<size_t>((y * ChunkSizeZ + sampleZ) * ChunkSizeX + sampleX);
            return sampleChunk->blocks[index];
        };

        auto lightAt = [&](int localX, int y, int localZ) -> uint8_t
        {
            if (y >= ChunkSizeY)
            {
                return packLight(MaxSkyLight, 0);
            }
            if (y < 0)
            {
                return 0;
            }

            int chunkOffsetX = 0;
            int chunkOffsetZ = 0;
            int sampleX = localX;
            int sampleZ = localZ;
            if (sampleX < 0)
            {
                chunkOffsetX = -1;
                sampleX += ChunkSizeX;
            }
            else if (sampleX >= ChunkSizeX)
            {
                chunkOffsetX = 1;
                sampleX -= ChunkSizeX;
            }

            if (sampleZ < 0)
            {
                chunkOffsetZ = -1;
                sampleZ += ChunkSizeZ;
            }
            else if (sampleZ >= ChunkSizeZ)
            {
                chunkOffsetZ = 1;
                sampleZ -= ChunkSizeZ;
            }

            if (sampleX < 0 || sampleX >= ChunkSizeX || sampleZ < 0 || sampleZ >= ChunkSizeZ)
            {
                return 0;
            }

            const std::shared_ptr<ChunkData>& sampleChunk = chunks[static_cast<size_t>((chunkOffsetZ + 1) * 3 + (chunkOffsetX + 1))];
            if (!sampleChunk || sampleChunk->light.size() != ChunkBlockCount)
            {
                return 0;
            }

            const size_t index = static_cast<size_t>((y * ChunkSizeZ + sampleZ) * ChunkSizeX + sampleX);
            return sampleChunk->light[index];
        };

        for (int subchunkY = 0; subchunkY < SubchunksPerChunk; ++subchunkY)
        {
            TerrainSubchunkBuildData terrainSubchunk = buildSolidSubchunk(chunk, subchunkY, blockAt, lightAt);
            result.solidSubchunks[static_cast<size_t>(subchunkY)] = std::move(terrainSubchunk.solid);
            result.blendSubchunks[static_cast<size_t>(subchunkY)] = std::move(terrainSubchunk.blend);
            if (chunk->fluidSubchunkCounts[static_cast<size_t>(subchunkY)] > 0)
            {
                result.fluidSubchunks[static_cast<size_t>(subchunkY)] = buildFluidSubchunkMesh(chunks, subchunkY, lightAt, blockOccludesFluid);
            }
        }
        return result;
    }

    TerrainBuildData TerrainMesher::buildFluidSubchunkMesh(
        const std::array<std::shared_ptr<ChunkData>, 9>& chunks,
        int subchunkY,
        const LightSampler& lightAt,
        const BlockOcclusionPredicate& blockOccludesFluid) const
    {
        TerrainBuildData result{};
        if (subchunkY < 0 || subchunkY >= SubchunksPerChunk || !chunks[4])
        {
            return result;
        }

        const std::shared_ptr<ChunkData>& chunk = chunks[4];
        if (chunk->fluids.size() != ChunkBlockCount)
        {
            return result;
        }

        auto sampleChunk = [&](int localX, int localZ, int& sampleX, int& sampleZ) -> const std::shared_ptr<ChunkData>&
        {
            int chunkOffsetX = 0;
            int chunkOffsetZ = 0;
            sampleX = localX;
            sampleZ = localZ;
            if (sampleX < 0)
            {
                chunkOffsetX = -1;
                sampleX += ChunkSizeX;
            }
            else if (sampleX >= ChunkSizeX)
            {
                chunkOffsetX = 1;
                sampleX -= ChunkSizeX;
            }

            if (sampleZ < 0)
            {
                chunkOffsetZ = -1;
                sampleZ += ChunkSizeZ;
            }
            else if (sampleZ >= ChunkSizeZ)
            {
                chunkOffsetZ = 1;
                sampleZ -= ChunkSizeZ;
            }

            static const std::shared_ptr<ChunkData> EmptyChunk;
            if (sampleX < 0 || sampleX >= ChunkSizeX || sampleZ < 0 || sampleZ >= ChunkSizeZ)
            {
                return EmptyChunk;
            }
            return chunks[static_cast<size_t>((chunkOffsetZ + 1) * 3 + (chunkOffsetX + 1))];
        };

        auto blockAt = [&](int localX, int y, int localZ) -> uint16_t
        {
            if (y < 0 || y >= ChunkSizeY)
            {
                return BlockAir;
            }

            int sampleX = localX;
            int sampleZ = localZ;
            const std::shared_ptr<ChunkData>& sample = sampleChunk(localX, localZ, sampleX, sampleZ);
            if (!sample || sample->blocks.size() != ChunkBlockCount)
            {
                return BlockAir;
            }

            return sample->blocks[static_cast<size_t>((y * ChunkSizeZ + sampleZ) * ChunkSizeX + sampleX)];
        };

        auto fluidAt = [&](int localX, int y, int localZ) -> uint16_t
        {
            if (y < 0 || y >= ChunkSizeY)
            {
                return FluidNone;
            }

            int sampleX = localX;
            int sampleZ = localZ;
            const std::shared_ptr<ChunkData>& sample = sampleChunk(localX, localZ, sampleX, sampleZ);
            if (!sample || sample->fluids.size() != ChunkBlockCount)
            {
                return FluidNone;
            }

            return sample->fluids[static_cast<size_t>((y * ChunkSizeZ + sampleZ) * ChunkSizeX + sampleX)];
        };

        auto fluidRenderHeight = [&](int localX, int y, int localZ, uint16_t fluid)
        {
            if (fluidId(fluid) != FluidWater)
            {
                return 0.0f;
            }
            if (fluidId(fluidAt(localX, y + 1, localZ)) == FluidWater)
            {
                return 1.0f;
            }
            return fluidSurfaceHeight(fluidAmount(fluid));
        };

        auto appendQuad = [&](TerrainVertex a, TerrainVertex b, TerrainVertex c, TerrainVertex d)
        {
            a.textureLayer = 0.0f;
            b.textureLayer = 0.0f;
            c.textureLayer = 0.0f;
            d.textureLayer = 0.0f;
            a.mipDistanceScale = FluidMipDistanceScale;
            b.mipDistanceScale = FluidMipDistanceScale;
            c.mipDistanceScale = FluidMipDistanceScale;
            d.mipDistanceScale = FluidMipDistanceScale;

            const uint32_t baseIndex = static_cast<uint32_t>(result.vertices.size());
            result.vertices.push_back(a);
            result.vertices.push_back(b);
            result.vertices.push_back(c);
            result.vertices.push_back(d);
            result.indices.push_back(baseIndex);
            result.indices.push_back(baseIndex + 1);
            result.indices.push_back(baseIndex + 2);
            result.indices.push_back(baseIndex);
            result.indices.push_back(baseIndex + 2);
            result.indices.push_back(baseIndex + 3);
        };

        auto appendFluidFace = [&](int localX, int y, int localZ, int face, float sideBottom, float sideTop)
        {
            const int worldX = chunk->chunkX * ChunkSizeX + localX;
            const int worldZ = chunk->chunkZ * ChunkSizeZ + localZ;
            const float x0 = static_cast<float>(worldX) - 0.5f;
            const float x1 = static_cast<float>(worldX) + 0.5f;
            const float y0 = static_cast<float>(y) + sideBottom;
            const float y1 = static_cast<float>(y) + sideTop;
            const float z0 = static_cast<float>(worldZ) - 0.5f;
            const float z1 = static_cast<float>(worldZ) + 0.5f;

            const uint8_t packedLight = lightAt(localX, y, localZ);
            auto litVertex = [packedLight](float x, float y, float z, float u, float v)
            {
                TerrainVertex vertex{x, y, z, u, v, 1.0f};
                vertex.packedLight = packedLight;
                return vertex;
            };

            if (face == 0)
            {
                appendQuad(litVertex(x0, y1, z0, 0.0f, 0.0f), litVertex(x0, y1, z1, 0.0f, 1.0f), litVertex(x1, y1, z1, 1.0f, 1.0f), litVertex(x1, y1, z0, 1.0f, 0.0f));
            }
            else if (face == 1)
            {
                appendQuad(litVertex(x0, y0, z1, 0.0f, 0.0f), litVertex(x0, y0, z0, 0.0f, 1.0f), litVertex(x1, y0, z0, 1.0f, 1.0f), litVertex(x1, y0, z1, 1.0f, 0.0f));
            }
            else if (face == 2)
            {
                appendQuad(litVertex(x1, y0, z0, 0.0f, 1.0f), litVertex(x1, y1, z0, 0.0f, 0.0f), litVertex(x1, y1, z1, 1.0f, 0.0f), litVertex(x1, y0, z1, 1.0f, 1.0f));
            }
            else if (face == 3)
            {
                appendQuad(litVertex(x0, y0, z1, 0.0f, 1.0f), litVertex(x0, y1, z1, 0.0f, 0.0f), litVertex(x0, y1, z0, 1.0f, 0.0f), litVertex(x0, y0, z0, 1.0f, 1.0f));
            }
            else if (face == 4)
            {
                appendQuad(litVertex(x1, y0, z1, 0.0f, 1.0f), litVertex(x1, y1, z1, 0.0f, 0.0f), litVertex(x0, y1, z1, 1.0f, 0.0f), litVertex(x0, y0, z1, 1.0f, 1.0f));
            }
            else
            {
                appendQuad(litVertex(x0, y0, z0, 0.0f, 1.0f), litVertex(x0, y1, z0, 0.0f, 0.0f), litVertex(x1, y1, z0, 1.0f, 0.0f), litVertex(x1, y0, z0, 1.0f, 1.0f));
            }
        };

        result.vertices.reserve(256);
        result.indices.reserve(384);

        const int yStart = subchunkY * SubchunkSize;
        const int yEnd = yStart + SubchunkSize;
        for (int y = yStart; y < yEnd; ++y)
        {
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const uint16_t fluid = fluidAt(localX, y, localZ);
                    const uint16_t id = fluidId(fluid);
                    const uint16_t amount = fluidAmount(fluid);
                    if (id != FluidWater || amount == 0 || blockOccludesFluid(blockAt(localX, y, localZ)))
                    {
                        continue;
                    }

                    const float height = fluidRenderHeight(localX, y, localZ, fluid);
                    if (fluidId(fluidAt(localX, y + 1, localZ)) != FluidWater && !blockOccludesFluid(blockAt(localX, y + 1, localZ)))
                    {
                        appendFluidFace(localX, y, localZ, 0, 0.0f, height);
                    }
                    if (fluidId(fluidAt(localX, y - 1, localZ)) != FluidWater && !blockOccludesFluid(blockAt(localX, y - 1, localZ)))
                    {
                        appendFluidFace(localX, y, localZ, 1, 0.0f, height);
                    }

                    const std::array<std::array<int, 3>, 4> sideOffsets = {{{1, 0, 2}, {-1, 0, 3}, {0, 1, 4}, {0, -1, 5}}};
                    for (const std::array<int, 3>& side : sideOffsets)
                    {
                        const int neighborX = localX + side[0];
                        const int neighborZ = localZ + side[1];
                        if (blockOccludesFluid(blockAt(neighborX, y, neighborZ)))
                        {
                            continue;
                        }

                        const uint16_t neighborFluid = fluidAt(neighborX, y, neighborZ);
                        const float neighborHeight = fluidRenderHeight(neighborX, y, neighborZ, neighborFluid);
                        if (neighborHeight >= height)
                        {
                            continue;
                        }
                        appendFluidFace(localX, y, localZ, side[2], neighborHeight, height);
                    }
                }
            }
        }

        return result;
    }

    TerrainSubchunkBuildData TerrainMesher::buildEditedSubchunkMesh(
        const std::shared_ptr<ChunkData>& chunk,
        int subchunkY,
        const WorldBlockSampler& blockAtWorld,
        const WorldLightSampler& lightAtWorld,
        const SolidSubchunkBuilder& buildSolidSubchunk) const
    {
        if (!chunk)
        {
            return {};
        }

        std::vector<uint16_t> meshingBlocks(static_cast<size_t>(MeshingSizeX * EditMeshingSizeY * MeshingSizeZ), BlockAir);
        const int worldXStart = chunk->chunkX * ChunkSizeX;
        const int worldYStart = subchunkY * SubchunkSize;
        const int worldZStart = chunk->chunkZ * ChunkSizeZ;
        const int yBase = worldYStart - MeshingBorder;

        auto meshingIndex = [](int x, int y, int z) -> size_t
        {
            return static_cast<size_t>((y * MeshingSizeZ + z) * MeshingSizeX + x);
        };

        for (int meshY = 0; meshY < EditMeshingSizeY; ++meshY)
        {
            const int worldY = yBase + meshY;
            if (worldY < 0 || worldY >= ChunkSizeY)
            {
                continue;
            }

            for (int meshZ = 0; meshZ < MeshingSizeZ; ++meshZ)
            {
                const int worldZ = worldZStart + meshZ - MeshingBorder;
                for (int meshX = 0; meshX < MeshingSizeX; ++meshX)
                {
                    const int worldX = worldXStart + meshX - MeshingBorder;
                    meshingBlocks[meshingIndex(meshX, meshY, meshZ)] = blockAtWorld(worldX, worldY, worldZ);
                }
            }
        }

        auto blockAt = [&](int localX, int y, int localZ) -> uint16_t
        {
            if (y < 0 || y >= ChunkSizeY)
            {
                return BlockAir;
            }

            const int meshY = y - yBase;
            if (meshY < 0 || meshY >= EditMeshingSizeY)
            {
                return BlockAir;
            }

            const int meshX = localX + MeshingBorder;
            const int meshZ = localZ + MeshingBorder;
            if (meshX < 0 || meshX >= MeshingSizeX || meshZ < 0 || meshZ >= MeshingSizeZ)
            {
                return BlockAir;
            }
            return meshingBlocks[meshingIndex(meshX, meshY, meshZ)];
        };

        auto lightAt = [&](int localX, int y, int localZ) -> uint8_t
        {
            return lightAtWorld(worldXStart + localX, y, worldZStart + localZ);
        };

        return buildSolidSubchunk(chunk, subchunkY, blockAt, lightAt);
    }
}
