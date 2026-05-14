#pragma once

#include "world/WorldTypes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dolbuto::save
{
    inline constexpr int RegionSizeChunks = 16;
    inline constexpr uint32_t RegionSectorSize = 4096;
    inline constexpr size_t RegionChunkEntrySize = 16;
    inline constexpr int StorageWorldSizeChunks = 4096;

    int wrapChunkCoordinate(int value);
    uint64_t chunkKey(int chunkX, int chunkZ);
    uint64_t storageChunkKey(int chunkX, int chunkZ);
    bool hasIncomingFeatureSlots(const SaveChunkSnapshot& snapshot);

    uint32_t readU32At(const std::vector<uint8_t>& bytes, size_t offset);
    void writeU32At(std::vector<uint8_t>& bytes, size_t offset, uint32_t value);

    std::vector<uint8_t> serializeChunkPayload(const SaveChunkSnapshot& snapshot);
    std::optional<SaveChunkSnapshot> deserializeChunkPayload(const std::vector<uint8_t>& payload, int chunkX, int chunkZ);

    std::vector<uint8_t> encodePayload(const std::vector<uint8_t>& raw);
    std::vector<uint8_t> decodePayload(const std::vector<uint8_t>& encoded, size_t rawSize);
}
