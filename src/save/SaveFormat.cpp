#include "save/SaveFormat.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dolbuto::save
{
    namespace
    {
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeY = 512;
        constexpr int ChunkSizeZ = 16;
        constexpr uint8_t WorldEntityFlagGrounded = 1u << 0u;

        int positiveModulo(int value, int divisor)
        {
            const int result = value % divisor;
            return result < 0 ? result + divisor : result;
        }

        void writeU8(std::vector<uint8_t>& bytes, uint8_t value)
        {
            bytes.push_back(value);
        }

        void writeU16(std::vector<uint8_t>& bytes, uint16_t value)
        {
            bytes.push_back(static_cast<uint8_t>(value & 0xFFu));
            bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
        }

        void writeU32(std::vector<uint8_t>& bytes, uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
            {
                bytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
            }
        }

        void writeU64(std::vector<uint8_t>& bytes, uint64_t value)
        {
            for (int i = 0; i < 8; ++i)
            {
                bytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
            }
        }

        uint8_t readU8(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            if (offset >= bytes.size())
            {
                throw std::runtime_error("Chunk payload read overflow.");
            }
            return bytes[offset++];
        }

        uint16_t readU16(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            if (offset + 2 > bytes.size())
            {
                throw std::runtime_error("Chunk payload read overflow.");
            }
            const uint16_t value = static_cast<uint16_t>(bytes[offset]) |
                static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8u);
            offset += 2;
            return value;
        }

        uint32_t readU32(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            if (offset + 4 > bytes.size())
            {
                throw std::runtime_error("Chunk payload read overflow.");
            }
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                value |= static_cast<uint32_t>(bytes[offset + static_cast<size_t>(i)]) << (i * 8);
            }
            offset += 4;
            return value;
        }

        uint64_t readU64(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            if (offset + 8 > bytes.size())
            {
                throw std::runtime_error("Chunk payload read overflow.");
            }
            uint64_t value = 0;
            for (int i = 0; i < 8; ++i)
            {
                value |= static_cast<uint64_t>(bytes[offset + static_cast<size_t>(i)]) << (i * 8);
            }
            offset += 8;
            return value;
        }

        uint32_t peekU32(const std::vector<uint8_t>& bytes, size_t offset)
        {
            if (offset + 4 > bytes.size())
            {
                throw std::runtime_error("Chunk payload read overflow.");
            }
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                value |= static_cast<uint32_t>(bytes[offset + static_cast<size_t>(i)]) << (i * 8);
            }
            return value;
        }

        bool blockStateSectionFits(const std::vector<uint8_t>& payload, size_t offset)
        {
            if (offset + 8 == payload.size())
            {
                return true;
            }
            if (offset + 4 + 8 > payload.size())
            {
                return false;
            }

            const uint32_t runCount = peekU32(payload, offset);
            offset += 4;
            uint64_t totalCount = 0;
            for (uint32_t run = 0; run < runCount; ++run)
            {
                if (offset + 8 + 8 > payload.size())
                {
                    return false;
                }
                offset += 4;
                const uint32_t count = peekU32(payload, offset);
                offset += 4;
                totalCount += count;
                if (totalCount > ChunkBlockCount)
                {
                    return false;
                }
            }
            return offset + 8 == payload.size() && (runCount == 0 || totalCount == ChunkBlockCount);
        }

        void writeF32(std::vector<uint8_t>& bytes, float value)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            writeU32(bytes, bits);
        }

        float readF32At(const std::vector<uint8_t>& bytes, size_t offset)
        {
            float value = 0.0f;
            if (offset + sizeof(value) <= bytes.size())
            {
                std::memcpy(&value, bytes.data() + offset, sizeof(value));
            }
            return value;
        }

        float readF32(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            if (offset + sizeof(float) > bytes.size())
            {
                throw std::runtime_error("Chunk payload read overflow.");
            }
            const float value = readF32At(bytes, offset);
            offset += sizeof(float);
            return value;
        }
    }

    int wrapChunkCoordinate(int value)
    {
        return positiveModulo(value, StorageWorldSizeChunks);
    }

    uint64_t chunkKey(int chunkX, int chunkZ)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32u) |
            static_cast<uint32_t>(chunkZ);
    }

    uint64_t storageChunkKey(int chunkX, int chunkZ)
    {
        return chunkKey(wrapChunkCoordinate(chunkX), wrapChunkCoordinate(chunkZ));
    }

    bool hasIncomingFeatureSlots(const SaveChunkSnapshot& snapshot)
    {
        for (const FeatureWriteListPtr& slot : snapshot.incomingFeatureSlots)
        {
            if (slot && !slot->empty())
            {
                return true;
            }
        }
        return false;
    }

    uint32_t readU32At(const std::vector<uint8_t>& bytes, size_t offset)
    {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
        {
            value |= static_cast<uint32_t>(bytes[offset + static_cast<size_t>(i)]) << (i * 8);
        }
        return value;
    }

    void writeU32At(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
        {
            bytes[offset + static_cast<size_t>(i)] = static_cast<uint8_t>((value >> (i * 8)) & 0xFFu);
        }
    }

    std::vector<uint8_t> serializeChunkPayload(const SaveChunkSnapshot& value)
    {
        std::vector<uint8_t> payload;
        writeU8(payload, static_cast<uint8_t>(value.genState));
        writeU8(payload, value.incomingFeatureMask);
        for (const FeatureWriteListPtr& slot : value.incomingFeatureSlots)
        {
            const size_t count = slot ? slot->size() : 0;
            writeU16(payload, static_cast<uint16_t>(std::min<size_t>(count, std::numeric_limits<uint16_t>::max())));
        }

        const std::vector<uint16_t>* blocks = nullptr;
        if (value.chunkData && !value.chunkData->blocks.empty())
        {
            blocks = &value.chunkData->blocks;
        }
        else if (!value.blocks.empty())
        {
            blocks = &value.blocks;
        }

        const std::vector<uint16_t>* blockStates = nullptr;
        if (value.chunkData && !value.chunkData->blockStates.empty())
        {
            blockStates = &value.chunkData->blockStates;
        }
        else if (!value.blockStates.empty())
        {
            blockStates = &value.blockStates;
        }

        const std::vector<uint16_t>* fluids = nullptr;
        if (value.chunkData && !value.chunkData->fluids.empty())
        {
            fluids = &value.chunkData->fluids;
        }
        else if (!value.fluids.empty())
        {
            fluids = &value.fluids;
        }

        const std::vector<uint8_t>* light = nullptr;
        if (value.chunkData && !value.chunkData->light.empty())
        {
            light = &value.chunkData->light;
        }
        else if (!value.light.empty())
        {
            light = &value.light;
        }

        const std::array<uint8_t, ChunkColumnCount>* temperature = value.chunkData ? &value.chunkData->temperature : &value.temperature;
        const std::array<uint8_t, ChunkColumnCount>* precipitation = value.chunkData ? &value.chunkData->precipitation : &value.precipitation;

        auto writeRuns = [&](const std::vector<uint16_t>* values)
        {
            if (!value.hasData || !values || values->empty())
            {
                writeU32(payload, 0);
                return;
            }

            const size_t runCountOffset = payload.size();
            writeU32(payload, 0);
            uint32_t runCount = 0;
            uint32_t current = (*values)[0];
            uint32_t count = 1;
            for (size_t i = 1; i < values->size(); ++i)
            {
                const uint32_t item = (*values)[i];
                if (item == current && count < std::numeric_limits<uint32_t>::max())
                {
                    ++count;
                    continue;
                }
                writeU32(payload, current);
                writeU32(payload, count);
                ++runCount;
                current = item;
                count = 1;
            }
            writeU32(payload, current);
            writeU32(payload, count);
            ++runCount;
            writeU32At(payload, runCountOffset, runCount);
        };

        auto writeByteRuns = [&](const std::vector<uint8_t>* values)
        {
            if (!value.hasData || !values || values->empty())
            {
                if (value.hasData)
                {
                    writeU32(payload, 1);
                    writeU8(payload, 0);
                    writeU32(payload, static_cast<uint32_t>(ChunkBlockCount));
                    return;
                }
                writeU32(payload, 0);
                return;
            }

            const size_t runCountOffset = payload.size();
            writeU32(payload, 0);
            uint32_t runCount = 0;
            uint8_t current = (*values)[0];
            uint32_t count = 1;
            for (size_t i = 1; i < values->size(); ++i)
            {
                const uint8_t item = (*values)[i];
                if (item == current && count < std::numeric_limits<uint32_t>::max())
                {
                    ++count;
                    continue;
                }
                writeU8(payload, current);
                writeU32(payload, count);
                ++runCount;
                current = item;
                count = 1;
            }
            writeU8(payload, current);
            writeU32(payload, count);
            ++runCount;
            writeU32At(payload, runCountOffset, runCount);
        };

        writeRuns(blocks);
        writeRuns(fluids);
        writeByteRuns(light);
        if (value.hasData)
        {
            payload.insert(payload.end(), temperature->begin(), temperature->end());
            payload.insert(payload.end(), precipitation->begin(), precipitation->end());
        }

        for (const FeatureWriteListPtr& slot : value.incomingFeatureSlots)
        {
            if (!slot)
            {
                continue;
            }

            size_t written = 0;
            for (const FeatureWrite& write : *slot)
            {
                if (written >= std::numeric_limits<uint16_t>::max())
                {
                    break;
                }
                writeU8(payload, static_cast<uint8_t>(std::clamp(write.localX, 0, ChunkSizeX - 1)));
                writeU8(payload, static_cast<uint8_t>(std::clamp(write.localZ, 0, ChunkSizeZ - 1)));
                writeU16(payload, static_cast<uint16_t>(std::clamp(write.y, 0, ChunkSizeY - 1)));
                writeU32(payload, write.block);
                ++written;
            }
        }

        const std::vector<WorldEntity>* entities = value.chunkData ? &value.chunkData->entities : &value.entities;
        const size_t maxEntityCount = std::min<size_t>(entities ? entities->size() : 0, std::numeric_limits<uint16_t>::max());
        const size_t entityCountOffset = payload.size();
        writeU16(payload, 0);
        uint16_t writtenEntities = 0;
        const float worldXStart = static_cast<float>(value.chunkX * ChunkSizeX);
        const float worldZStart = static_cast<float>(value.chunkZ * ChunkSizeZ);
        for (size_t i = 0; entities && i < maxEntityCount; ++i)
        {
            const WorldEntity& entity = (*entities)[i];
            if (entity.type != WorldEntityType::DroppedItem ||
                entity.entityId == 0 ||
                entity.droppedItem.stack.itemId == 0 ||
                entity.droppedItem.stack.count == 0)
            {
                continue;
            }

            writeU16(payload, static_cast<uint16_t>(entity.type));
            writeU64(payload, entity.entityId);
            writeF32(payload, std::clamp(entity.position.x - worldXStart, 0.0f, static_cast<float>(ChunkSizeX) - 0.0001f));
            writeF32(payload, entity.position.y);
            writeF32(payload, std::clamp(entity.position.z - worldZStart, 0.0f, static_cast<float>(ChunkSizeZ) - 0.0001f));
            writeF32(payload, entity.velocity.x);
            writeF32(payload, entity.velocity.y);
            writeF32(payload, entity.velocity.z);
            writeU8(payload, static_cast<uint8_t>(entity.flags & WorldEntityFlagGrounded));
            writeU16(payload, entity.droppedItem.stack.itemId);
            writeU16(payload, entity.droppedItem.stack.count);
            writeU16(payload, entity.droppedItem.stack.durability);
            writeU32(payload, entity.droppedItem.processingTicks);
            ++writtenEntities;
        }
        payload[entityCountOffset] = static_cast<uint8_t>(writtenEntities & 0xFFu);
        payload[entityCountOffset + 1] = static_cast<uint8_t>((writtenEntities >> 8u) & 0xFFu);

        const std::vector<BlockEntity>* blockEntities = value.chunkData ? &value.chunkData->blockEntities : &value.blockEntities;
        const size_t maxBlockEntityCount = std::min<size_t>(blockEntities ? blockEntities->size() : 0, std::numeric_limits<uint16_t>::max());
        const size_t blockEntityCountOffset = payload.size();
        writeU16(payload, 0);
        uint16_t writtenBlockEntities = 0;
        for (size_t i = 0; blockEntities && i < maxBlockEntityCount; ++i)
        {
            const BlockEntity& entity = (*blockEntities)[i];
            if (entity.type == BlockEntityType::None ||
                entity.localX >= ChunkSizeX ||
                entity.localZ >= ChunkSizeZ ||
                entity.y >= ChunkSizeY)
            {
                continue;
            }

            writeU16(payload, static_cast<uint16_t>(entity.type));
            writeU8(payload, entity.localX);
            writeU8(payload, entity.localZ);
            writeU16(payload, entity.y);
            writeU32(payload, entity.remainingBurnTicks);
            writeU8(payload, static_cast<uint8_t>(entity.fireMode));
            writeU8(payload, 0);
            writeU16(payload, entity.carbonizingOutputItemId);
            writeU16(payload, entity.carbonizingOutputCount);
            ++writtenBlockEntities;
        }
        payload[blockEntityCountOffset] = static_cast<uint8_t>(writtenBlockEntities & 0xFFu);
        payload[blockEntityCountOffset + 1] = static_cast<uint8_t>((writtenBlockEntities >> 8u) & 0xFFu);
        writeRuns(blockStates);
        writeU64(payload, value.revision);
        return payload;
    }

    std::optional<SaveChunkSnapshot> deserializeChunkPayload(const std::vector<uint8_t>& payload, int chunkX, int chunkZ)
    {
        try
        {
            SaveChunkSnapshot value{};
            value.chunkX = chunkX;
            value.chunkZ = chunkZ;
            size_t offset = 0;
            value.genState = static_cast<ChunkGenState>(readU8(payload, offset));
            value.incomingFeatureMask = readU8(payload, offset);
            std::array<uint16_t, FeatureNeighborCount> featureCounts{};
            for (uint16_t& count : featureCounts)
            {
                count = readU16(payload, offset);
            }

            const uint32_t blockRunCount = readU32(payload, offset);
            if (blockRunCount > 0)
            {
                value.hasData = true;
                value.blocks.reserve(ChunkBlockCount);
                uint64_t totalCount = 0;
                for (uint32_t run = 0; run < blockRunCount; ++run)
                {
                    const uint16_t block = static_cast<uint16_t>(readU32(payload, offset) & 0xFFFFu);
                    const uint32_t count = readU32(payload, offset);
                    totalCount += count;
                    if (totalCount > ChunkBlockCount)
                    {
                        return std::nullopt;
                    }
                    value.blocks.insert(value.blocks.end(), count, block);
                }
                if (value.blocks.size() != ChunkBlockCount)
                {
                    return std::nullopt;
                }
            }

            const uint32_t fluidRunCount = readU32(payload, offset);
            if (fluidRunCount > 0)
            {
                value.hasData = true;
                value.fluids.reserve(ChunkBlockCount);
                uint64_t totalCount = 0;
                for (uint32_t run = 0; run < fluidRunCount; ++run)
                {
                    const uint16_t fluid = static_cast<uint16_t>(readU32(payload, offset) & 0xFFFFu);
                    const uint32_t count = readU32(payload, offset);
                    totalCount += count;
                    if (totalCount > ChunkBlockCount)
                    {
                        return std::nullopt;
                    }
                    value.fluids.insert(value.fluids.end(), count, fluid);
                }
                if (value.fluids.size() != ChunkBlockCount)
                {
                    return std::nullopt;
                }
            }

            const uint32_t lightRunCount = readU32(payload, offset);
            if (value.hasData)
            {
                if (lightRunCount == 0)
                {
                    return std::nullopt;
                }

                value.light.reserve(ChunkBlockCount);
                uint64_t totalCount = 0;
                for (uint32_t run = 0; run < lightRunCount; ++run)
                {
                    const uint8_t light = readU8(payload, offset);
                    const uint32_t count = readU32(payload, offset);
                    totalCount += count;
                    if (totalCount > ChunkBlockCount)
                    {
                        return std::nullopt;
                    }
                    value.light.insert(value.light.end(), count, light);
                }
                if (value.light.size() != ChunkBlockCount)
                {
                    return std::nullopt;
                }
            }

            if (value.hasData)
            {
                for (uint8_t& item : value.temperature)
                {
                    item = readU8(payload, offset);
                }
                for (uint8_t& item : value.precipitation)
                {
                    item = readU8(payload, offset);
                }
            }

            for (size_t slot = 0; slot < FeatureNeighborCount; ++slot)
            {
                if (featureCounts[slot] == 0)
                {
                    continue;
                }
                auto writes = std::make_shared<FeatureWriteList>();
                writes->reserve(featureCounts[slot]);
                for (uint16_t i = 0; i < featureCounts[slot]; ++i)
                {
                    FeatureWrite write{};
                    write.localX = readU8(payload, offset);
                    write.localZ = readU8(payload, offset);
                    write.y = readU16(payload, offset);
                    write.block = static_cast<uint16_t>(readU32(payload, offset) & 0xFFFFu);
                    writes->push_back(write);
                }
                value.incomingFeatureSlots[slot] = std::move(writes);
            }

            if (offset + 8 != payload.size() && offset + 2 <= payload.size())
            {
                const uint16_t entityCount = readU16(payload, offset);
                value.entities.reserve(entityCount);
                const float worldXStart = static_cast<float>(chunkX * ChunkSizeX);
                const float worldZStart = static_cast<float>(chunkZ * ChunkSizeZ);
                constexpr size_t DroppedItemEntityLegacyBytes = 39;
                constexpr size_t DroppedItemEntityBytes = 41;
                constexpr size_t DroppedItemEntityProcessingBytes = 45;
                auto peekU16At = [&](size_t readOffset) -> std::optional<uint16_t>
                {
                    if (readOffset + 2 > payload.size())
                    {
                        return std::optional<uint16_t>{};
                    }
                    return std::optional<uint16_t>{static_cast<uint16_t>(
                        static_cast<uint16_t>(payload[readOffset]) |
                        static_cast<uint16_t>(payload[readOffset + 1]) << 8u)};
                };
                auto payloadFitsAfterEntities = [&](size_t entityBytes)
                {
                    const size_t afterEntities = offset + static_cast<size_t>(entityCount) * entityBytes;
                    if (afterEntities + 8 == payload.size())
                    {
                        return true;
                    }
                    const std::optional<uint16_t> blockEntityCount = peekU16At(afterEntities);
                    if (!blockEntityCount.has_value())
                    {
                        return false;
                    }
                    const size_t afterBlockEntities = afterEntities + 2u + static_cast<size_t>(*blockEntityCount) * 16u;
                    return afterBlockEntities <= payload.size() && blockStateSectionFits(payload, afterBlockEntities);
                };

                const bool hasEntityProcessing = payloadFitsAfterEntities(DroppedItemEntityProcessingBytes);
                const bool hasEntityDurability = hasEntityProcessing || payloadFitsAfterEntities(DroppedItemEntityBytes);
                if (!hasEntityProcessing &&
                    !hasEntityDurability &&
                    !payloadFitsAfterEntities(DroppedItemEntityLegacyBytes))
                {
                    return std::nullopt;
                }
                for (uint16_t i = 0; i < entityCount; ++i)
                {
                    WorldEntity entity{};
                    entity.type = static_cast<WorldEntityType>(readU16(payload, offset));
                    entity.entityId = readU64(payload, offset);
                    const float localX = readF32(payload, offset);
                    const float y = readF32(payload, offset);
                    const float localZ = readF32(payload, offset);
                    entity.position = {
                        worldXStart + std::clamp(localX, 0.0f, static_cast<float>(ChunkSizeX) - 0.0001f),
                        y,
                        worldZStart + std::clamp(localZ, 0.0f, static_cast<float>(ChunkSizeZ) - 0.0001f)
                    };
                    entity.previousPosition = entity.position;
                    entity.velocity.x = readF32(payload, offset);
                    entity.velocity.y = readF32(payload, offset);
                    entity.velocity.z = readF32(payload, offset);
                    entity.flags = static_cast<uint8_t>(readU8(payload, offset) & WorldEntityFlagGrounded);

                    if (entity.type != WorldEntityType::DroppedItem)
                    {
                        return std::nullopt;
                    }
                    entity.droppedItem.stack.itemId = readU16(payload, offset);
                    entity.droppedItem.stack.count = readU16(payload, offset);
                    entity.droppedItem.stack.durability = hasEntityDurability ? readU16(payload, offset) : 0;
                    entity.droppedItem.processingTicks = hasEntityProcessing ? readU32(payload, offset) : 0;
                    if (entity.entityId != 0 &&
                        entity.droppedItem.stack.itemId != 0 &&
                        entity.droppedItem.stack.count != 0)
                    {
                        value.entities.push_back(entity);
                    }
                }
            }
            if (offset + 8 != payload.size() && offset + 2 <= payload.size())
            {
                const uint16_t blockEntityCount = readU16(payload, offset);
                constexpr size_t CurrentBlockEntityBytes = 16;
                const size_t currentEndOffset = offset + static_cast<size_t>(blockEntityCount) * CurrentBlockEntityBytes;
                if (currentEndOffset > payload.size() || !blockStateSectionFits(payload, currentEndOffset))
                {
                    return std::nullopt;
                }
                value.blockEntities.reserve(blockEntityCount);
                for (uint16_t i = 0; i < blockEntityCount; ++i)
                {
                    BlockEntity entity{};
                    entity.type = static_cast<BlockEntityType>(readU16(payload, offset));
                    entity.localX = readU8(payload, offset);
                    entity.localZ = readU8(payload, offset);
                    entity.y = readU16(payload, offset);
                    entity.remainingBurnTicks = readU32(payload, offset);
                    const uint8_t savedFireMode = readU8(payload, offset);
                    entity.fireMode = savedFireMode == static_cast<uint8_t>(FireMode::Firing)
                        ? FireMode::Firing
                        : (savedFireMode == static_cast<uint8_t>(FireMode::Pyrolysis)
                            ? FireMode::Pyrolysis
                            : FireMode::Normal);
                    (void)readU8(payload, offset);
                    entity.carbonizingOutputItemId = readU16(payload, offset);
                    entity.carbonizingOutputCount = readU16(payload, offset);
                    if (entity.type != BlockEntityType::None &&
                        entity.localX < ChunkSizeX &&
                        entity.localZ < ChunkSizeZ &&
                        entity.y < ChunkSizeY)
                    {
                        value.blockEntities.push_back(entity);
                    }
                }
            }
            if (offset + 8 != payload.size() && offset + 4 <= payload.size())
            {
                const uint32_t blockStateRunCount = readU32(payload, offset);
                if (blockStateRunCount > 0)
                {
                    value.blockStates.reserve(ChunkBlockCount);
                    uint64_t totalCount = 0;
                    for (uint32_t run = 0; run < blockStateRunCount; ++run)
                    {
                        const uint16_t state = static_cast<uint16_t>(readU32(payload, offset) & 0xFFFFu);
                        const uint32_t count = readU32(payload, offset);
                        totalCount += count;
                        if (totalCount > ChunkBlockCount)
                        {
                            return std::nullopt;
                        }
                        value.blockStates.insert(value.blockStates.end(), count, state);
                    }
                    if (value.blockStates.size() != ChunkBlockCount)
                    {
                        return std::nullopt;
                    }
                }
            }
            if (offset + 8 <= payload.size())
            {
                value.revision = readU64(payload, offset);
            }
            return value;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::vector<uint8_t> encodePayload(const std::vector<uint8_t>& raw)
    {
        std::vector<uint8_t> encoded;
        encoded.reserve(raw.size() + raw.size() / 255 + 16);
        const size_t literalLength = raw.size();
        const uint8_t tokenLiteral = static_cast<uint8_t>(std::min<size_t>(literalLength, 15u));
        encoded.push_back(static_cast<uint8_t>(tokenLiteral << 4u));
        if (literalLength >= 15)
        {
            size_t remaining = literalLength - 15;
            while (remaining >= 255)
            {
                encoded.push_back(255);
                remaining -= 255;
            }
            encoded.push_back(static_cast<uint8_t>(remaining));
        }
        encoded.insert(encoded.end(), raw.begin(), raw.end());
        return encoded;
    }

    std::vector<uint8_t> decodePayload(const std::vector<uint8_t>& encoded, size_t rawSize)
    {
        std::vector<uint8_t> decoded;
        decoded.reserve(rawSize);
        size_t offset = 0;
        while (offset < encoded.size() && decoded.size() < rawSize)
        {
            const uint8_t token = encoded[offset++];
            size_t literalLength = token >> 4u;
            if (literalLength == 15)
            {
                uint8_t lengthByte = 0;
                do
                {
                    if (offset >= encoded.size())
                    {
                        throw std::runtime_error("Invalid LZ4 literal length.");
                    }
                    lengthByte = encoded[offset++];
                    literalLength += lengthByte;
                } while (lengthByte == 255);
            }

            if (offset + literalLength > encoded.size())
            {
                throw std::runtime_error("Invalid LZ4 literal data.");
            }
            decoded.insert(decoded.end(), encoded.begin() + static_cast<std::ptrdiff_t>(offset), encoded.begin() + static_cast<std::ptrdiff_t>(offset + literalLength));
            offset += literalLength;
            if (decoded.size() >= rawSize)
            {
                break;
            }

            if (offset + 2 > encoded.size())
            {
                throw std::runtime_error("Invalid LZ4 match offset.");
            }
            const size_t matchOffset = static_cast<size_t>(encoded[offset]) |
                (static_cast<size_t>(encoded[offset + 1]) << 8u);
            offset += 2;
            size_t matchLength = token & 0x0Fu;
            if (matchLength == 15)
            {
                uint8_t lengthByte = 0;
                do
                {
                    if (offset >= encoded.size())
                    {
                        throw std::runtime_error("Invalid LZ4 match length.");
                    }
                    lengthByte = encoded[offset++];
                    matchLength += lengthByte;
                } while (lengthByte == 255);
            }
            matchLength += 4;
            if (matchOffset == 0 || matchOffset > decoded.size())
            {
                throw std::runtime_error("Invalid LZ4 match distance.");
            }
            for (size_t i = 0; i < matchLength; ++i)
            {
                decoded.push_back(decoded[decoded.size() - matchOffset]);
            }
        }

        if (decoded.size() != rawSize)
        {
            throw std::runtime_error("Invalid LZ4 decoded size.");
        }
        return decoded;
    }
}
