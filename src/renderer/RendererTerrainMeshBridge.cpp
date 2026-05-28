#include "renderer/RendererTerrainMeshBridge.h"

#include "renderer/TerrainGeometryBuilder.h"

#include <cstddef>
#include <cstdint>

namespace dolbuto
{
    namespace
    {
        constexpr uint16_t BlockAir = 0;
    }

    RendererTerrainMeshBridge::RendererTerrainMeshBridge(const game::ClientContent& content, const RendererAssetStore& assets) :
        content_(content),
        assets_(assets)
    {
    }

    TerrainSubchunkBuildData RendererTerrainMeshBridge::buildEditedSubchunkMesh(
        const std::array<std::shared_ptr<ChunkData>, 9>& chunks,
        int subchunkY,
        const world::TerrainMesher::WorldBlockSampler& blockAtWorld,
        const world::TerrainMesher::WorldLightSampler& lightAtWorld) const
    {
        const std::shared_ptr<ChunkData>& chunk = chunks[4];
        if (!chunk)
        {
            return {};
        }

        const TerrainGeometryBuilder geometryBuilder(
            content_.blockDefinitions(),
            content_.blockTextureLayers(),
            assets_.propMeshesByBlock);

        world::TerrainMesher mesher;
        TerrainSubchunkBuildData result = mesher.buildEditedSubchunkMesh(
            chunk,
            subchunkY,
            blockAtWorld,
            lightAtWorld,
            [&geometryBuilder](const std::shared_ptr<ChunkData>& sourceChunk, int sourceSubchunkY, const world::TerrainMesher::BlockSampler& blockAt, const world::TerrainMesher::LightSampler& lightAt)
            {
                return geometryBuilder.buildSubchunkMesh(sourceChunk, sourceSubchunkY, blockAt, lightAt);
            });

        if (subchunkY >= 0 &&
            subchunkY < static_cast<int>(chunk->fluidSubchunkCounts.size()) &&
            chunk->fluidSubchunkCounts[static_cast<std::size_t>(subchunkY)] > 0)
        {
            const int worldXStart = chunk->chunkX * 16;
            const int worldZStart = chunk->chunkZ * 16;
            result.fluid = mesher.buildFluidSubchunkMesh(
                chunks,
                subchunkY,
                [&](int localX, int y, int localZ)
                {
                    return lightAtWorld(worldXStart + localX, y, worldZStart + localZ);
                },
                [this](uint16_t block)
                {
                    return blockOccludesFluid(block);
                });
        }
        return result;
    }

    CompletedChunkMesh RendererTerrainMeshBridge::buildChunkMesh(
        const std::array<std::shared_ptr<ChunkData>, 9>& chunks,
        uint64_t generation) const
    {
        const TerrainGeometryBuilder geometryBuilder(
            content_.blockDefinitions(),
            content_.blockTextureLayers(),
            assets_.propMeshesByBlock);

        return world::TerrainMesher().buildChunkMesh(
            chunks,
            generation,
            [&geometryBuilder](const std::shared_ptr<ChunkData>& chunk, int subchunkY, const world::TerrainMesher::BlockSampler& blockAt, const world::TerrainMesher::LightSampler& lightAt)
            {
                return geometryBuilder.buildSubchunkMesh(chunk, subchunkY, blockAt, lightAt);
            },
            [this](uint16_t block)
            {
                return blockOccludesFluid(block);
            });
    }

    const BlockDefinition& RendererTerrainMeshBridge::blockDefinition(uint16_t block) const
    {
        static const BlockDefinition fallback{};
        if (static_cast<size_t>(block) >= content_.blockDefinitions().size())
        {
            return fallback;
        }
        return content_.blockDefinitions()[block];
    }

    bool RendererTerrainMeshBridge::blockOccludesFluid(uint16_t block) const
    {
        return block != BlockAir && blockDefinition(block).faceOcclusion == BlockFaceOcclusion::Opaque;
    }
}
