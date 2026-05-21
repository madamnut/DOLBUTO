#pragma once

#include "world/BlockData.h"
#include "world/WorldTypes.h"

#include <array>
#include <memory>
#include <vector>

namespace dolbuto::world
{
    inline constexpr uint8_t MaxSkyLight = 15;
    inline constexpr uint8_t MaxBlockLight = 15;

    uint8_t packLight(uint8_t skyLight, uint8_t blockLight);
    uint8_t skyLightFromPacked(uint8_t packedLight);
    uint8_t blockLightFromPacked(uint8_t packedLight);
    std::vector<uint8_t> computeLocalSkyLight(const ChunkData& chunk, const LightAttenuationTables* lightAttenuation = nullptr);
    void recomputeChunkSkyLight(ChunkData& chunk, const LightAttenuationTables* lightAttenuation = nullptr);
    std::vector<uint8_t> resolveCenterSkyLight(
        const std::array<std::shared_ptr<ChunkData>, 9>& chunks,
        const LightAttenuationTables* lightAttenuation = nullptr);
}
