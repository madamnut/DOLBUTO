#pragma once

#include "game/ClientContent.h"
#include "renderer/RendererAssetStore.h"
#include "renderer/TerrainTypes.h"
#include "world/TerrainMesher.h"
#include "world/WorldTypes.h"

#include <array>
#include <memory>

namespace dolbuto
{
    class RendererTerrainMeshBridge
    {
    public:
        RendererTerrainMeshBridge(const game::ClientContent& content, const RendererAssetStore& assets);

        TerrainSubchunkBuildData buildEditedSubchunkMesh(
            const std::array<std::shared_ptr<ChunkData>, 9>& chunks,
            int subchunkY,
            const world::TerrainMesher::WorldBlockSampler& blockAtWorld,
            const world::TerrainMesher::WorldLightSampler& lightAtWorld) const;

        CompletedChunkMesh buildChunkMesh(
            const std::array<std::shared_ptr<ChunkData>, 9>& chunks,
            uint64_t generation) const;

    private:
        const BlockDefinition& blockDefinition(uint16_t block) const;
        bool blockOccludesFluid(uint16_t block) const;

        const game::ClientContent& content_;
        const RendererAssetStore& assets_;
    };
}
