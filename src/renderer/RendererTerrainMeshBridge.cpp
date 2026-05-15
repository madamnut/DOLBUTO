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

    TerrainBuildData RendererTerrainMeshBridge::buildEditedSubchunkMesh(
        const std::shared_ptr<ChunkData>& chunk,
        int subchunkY,
        const world::TerrainMesher::WorldBlockSampler& blockAtWorld) const
    {
        const TerrainGeometryBuilder geometryBuilder(
            content_.blockDefinitions(),
            content_.blockTextureLayers(),
            assets_.propMeshesByBlock);

        return world::TerrainMesher().buildEditedSubchunkMesh(
            chunk,
            subchunkY,
            blockAtWorld,
            [&geometryBuilder](const std::shared_ptr<ChunkData>& sourceChunk, int sourceSubchunkY, const world::TerrainMesher::BlockSampler& blockAt)
            {
                return geometryBuilder.buildSubchunkMesh(sourceChunk, sourceSubchunkY, blockAt);
            });
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
            [&geometryBuilder](const std::shared_ptr<ChunkData>& chunk, int subchunkY, const world::TerrainMesher::BlockSampler& blockAt)
            {
                return geometryBuilder.buildSubchunkMesh(chunk, subchunkY, blockAt);
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
