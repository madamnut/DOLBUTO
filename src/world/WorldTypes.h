#pragma once

#include "camera/Camera.h"
#include "items/ItemData.h"
#include "renderer/TerrainTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace dolbuto
{
    inline constexpr std::size_t ChunkColumnCount = 16u * 16u;
    inline constexpr std::size_t ChunkBlockCount = 16u * 512u * 16u;
    inline constexpr std::size_t SubchunkCount = 512u / 16u;
    inline constexpr std::size_t FeatureNeighborCount = 8u;

    enum class WorldEntityType : uint16_t
    {
        None = 0,
        DroppedItem = 1
    };

    struct DroppedItemEntityData
    {
        ItemStack stack{};
    };

    struct WorldEntity
    {
        uint64_t entityId = 0;
        WorldEntityType type = WorldEntityType::None;
        Vec3 previousPosition{};
        Vec3 position{};
        Vec3 velocity{};
        uint8_t flags = 0;
        DroppedItemEntityData droppedItem{};
        float age = 0.0f;
        float renderRotationX = 0.0f;
        float renderRotation = 0.0f;
        float renderRotationZ = 0.0f;
        float renderSpinX = 0.0f;
        float renderSpin = 0.0f;
        float renderSpinZ = 0.0f;
        bool collecting = false;
        float collectAge = 0.0f;
    };

    struct ChunkData
    {
        uint64_t generation = 0;
        uint64_t revision = 0;
        int chunkX = 0;
        int chunkZ = 0;
        std::vector<uint16_t> blocks;
        std::vector<uint16_t> fluids;
        std::array<uint8_t, ChunkColumnCount> temperature{};
        std::array<uint8_t, ChunkColumnCount> precipitation{};
        std::vector<WorldEntity> entities;
        std::array<uint16_t, SubchunkCount> fluidSubchunkCounts{};
        std::array<bool, SubchunkCount> emptySubchunks{};
    };

    enum class ChunkGenState : uint8_t
    {
        Empty,
        Featuring,
        Full,
        Meshed
    };

    struct FeatureWrite
    {
        int localX = 0;
        int y = 0;
        int localZ = 0;
        uint16_t block = 0;
    };

    using FeatureWriteList = std::vector<FeatureWrite>;
    using FeatureWriteListPtr = std::shared_ptr<FeatureWriteList>;

    struct CompletedChunkData
    {
        std::shared_ptr<ChunkData> chunk;
        std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingFeatureSlots{};
    };

    struct TerrainJob
    {
        enum class Type
        {
            BuildFeaturing,
            FinalizeFeatures,
            BuildChunkMesh
        };

        Type type = Type::BuildFeaturing;
        uint64_t generation = 0;
        uint64_t revision = 0;
        uint32_t priority = UINT32_MAX;
        uint64_t sequence = 0;
        int chunkX = 0;
        int chunkZ = 0;
        std::shared_ptr<ChunkData> chunk;
        std::array<FeatureWriteListPtr, FeatureNeighborCount> incomingFeatureSlots{};
        std::array<std::shared_ptr<ChunkData>, 9> meshChunks{};
    };

    struct CompletedChunkMesh
    {
        uint64_t generation = 0;
        uint64_t revision = 0;
        int chunkX = 0;
        int chunkZ = 0;
        std::array<TerrainBuildData, SubchunkCount> solidSubchunks;
        std::array<TerrainBuildData, SubchunkCount> fluidSubchunks;
    };

    struct RuntimeChunk
    {
        ChunkGenState genState = ChunkGenState::Empty;
        int chunkX = 0;
        int chunkZ = 0;
        std::shared_ptr<ChunkData> data;
        std::array<FeatureWriteListPtr, FeatureNeighborCount> incomingFeatureSlots{};
        std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingFeatureSlots{};
        uint8_t incomingFeatureMask = 0;
        uint64_t outgoingPublishedTicket = 0;
        uint64_t renderTicket = 0;
        uint64_t meshTicket = 0;
        uint64_t fullTicket = 0;
        uint64_t featuringTicket = 0;
        uint32_t bestPriority = UINT32_MAX;
        uint64_t buildQueuedTicket = 0;
        uint64_t finalizeQueuedTicket = 0;
        uint64_t meshQueuedTicket = 0;
        bool hasSavedBacking = false;
        bool dataDirtyForSave = false;
        uint64_t dataDirtySerial = 0;
        bool snapshotLoadRequested = false;
        bool snapshotLoadFinished = false;
    };

    struct SaveChunkSnapshot
    {
        int chunkX = 0;
        int chunkZ = 0;
        ChunkGenState genState = ChunkGenState::Empty;
        uint64_t revision = 0;
        bool hasData = false;
        bool forceSave = false;
        bool hasSavedBacking = false;
        uint64_t dataDirtySerial = 0;
        std::shared_ptr<const ChunkData> chunkData;
        std::vector<uint16_t> blocks;
        std::vector<uint16_t> fluids;
        std::array<uint8_t, ChunkColumnCount> temperature{};
        std::array<uint8_t, ChunkColumnCount> precipitation{};
        std::vector<WorldEntity> entities;
        std::array<FeatureWriteListPtr, FeatureNeighborCount> incomingFeatureSlots{};
        uint8_t incomingFeatureMask = 0;
    };

    struct CompletedChunkLoad
    {
        int chunkX = 0;
        int chunkZ = 0;
        uint64_t generation = 0;
        std::optional<SaveChunkSnapshot> snapshot;
    };

    struct WorldEntityHandle
    {
        uint64_t chunkKey = 0;
        std::size_t index = 0;
    };

}
