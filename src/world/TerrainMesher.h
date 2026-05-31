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
        using BlockStateSampler = std::function<uint16_t(int, int, int)>;
        using LightSampler = std::function<uint8_t(int, int, int)>;
        using SolidSubchunkBuilder = std::function<TerrainSubchunkBuildData(const std::shared_ptr<ChunkData>&, int, const BlockSampler&, const BlockStateSampler&, const LightSampler&)>;
        using BlockOcclusionPredicate = std::function<bool(uint16_t)>;
        using WorldBlockSampler = std::function<uint16_t(int, int, int)>;
        using WorldBlockStateSampler = std::function<uint16_t(int, int, int)>;
        using WorldLightSampler = std::function<uint8_t(int, int, int)>;

        CompletedChunkMesh buildChunkMesh(
            const std::array<std::shared_ptr<ChunkData>, 9>& chunks,
            uint64_t generation,
            const SolidSubchunkBuilder& buildSolidSubchunk,
            const BlockOcclusionPredicate& blockOccludesFluid) const;

        TerrainSubchunkBuildData buildEditedSubchunkMesh(
            const std::shared_ptr<ChunkData>& chunk,
            int subchunkY,
            const WorldBlockSampler& blockAtWorld,
            const WorldBlockStateSampler& blockStateAtWorld,
            const WorldLightSampler& lightAtWorld,
            const SolidSubchunkBuilder& buildSolidSubchunk) const;

        TerrainBuildData buildFluidSubchunkMesh(
            const std::array<std::shared_ptr<ChunkData>, 9>& chunks,
            int subchunkY,
            const LightSampler& lightAt,
            const BlockOcclusionPredicate& blockOccludesFluid) const;
    };
}
