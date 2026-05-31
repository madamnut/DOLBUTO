#pragma once

#include "assets/PropModelLoader.h"
#include "renderer/TerrainTypes.h"
#include "world/BlockData.h"
#include "world/TerrainMesher.h"
#include "world/WorldTypes.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace dolbuto
{
    class TerrainGeometryBuilder
    {
    public:
        TerrainGeometryBuilder(
            const std::vector<BlockDefinition>& blockDefinitions,
            const std::vector<BlockTextureLayers>& blockTextureLayers,
            const std::unordered_map<uint16_t, assets::PropMesh>& propMeshesByBlock);

        TerrainSubchunkBuildData buildSubchunkMesh(
            const std::shared_ptr<ChunkData>& chunk,
            int subchunkY,
            const world::TerrainMesher::BlockSampler& blockAt,
            const world::TerrainMesher::BlockStateSampler& blockStateAt,
            const world::TerrainMesher::LightSampler& lightAt) const;

    private:
        const BlockDefinition& blockDefinition(uint16_t block) const;
        uint32_t blockFaceTextureLayer(uint16_t block, int face) const;
        bool blockUsesCubeMesh(uint16_t block) const;
        bool blockContributesAo(uint16_t block) const;
        bool neighborCullsFace(uint16_t block, uint16_t neighbor) const;

        const std::vector<BlockDefinition>& blockDefinitions_;
        const std::vector<BlockTextureLayers>& blockTextureLayers_;
        const std::unordered_map<uint16_t, assets::PropMesh>& propMeshesByBlock_;
    };
}
