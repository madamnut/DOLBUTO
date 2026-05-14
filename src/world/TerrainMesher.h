#pragma once

#include "renderer/TerrainTypes.h"
#include "world/WorldTypes.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace dolbuto::world
{
    class TerrainMesher
    {
    public:
        using BlockSampler = std::function<uint16_t(int, int, int)>;
        using SolidSubchunkBuilder = std::function<TerrainBuildData(const std::shared_ptr<ChunkData>&, int, const BlockSampler&)>;
        using BlockOcclusionPredicate = std::function<bool(uint16_t)>;
        using WorldBlockSampler = std::function<uint16_t(int, int, int)>;

        CompletedChunkMesh buildChunkMesh(
            const std::array<std::shared_ptr<ChunkData>, 9>& chunks,
            uint64_t generation,
            const SolidSubchunkBuilder& buildSolidSubchunk,
            const BlockOcclusionPredicate& blockOccludesFluid) const;

        TerrainBuildData buildEditedSubchunkMesh(
            const std::shared_ptr<ChunkData>& chunk,
            int subchunkY,
            const WorldBlockSampler& blockAtWorld,
            const SolidSubchunkBuilder& buildSolidSubchunk) const;

        TerrainBuildData buildFluidSubchunkMesh(
            const std::array<std::shared_ptr<ChunkData>, 9>& chunks,
            int subchunkY,
            const BlockOcclusionPredicate& blockOccludesFluid) const;
    };
}
