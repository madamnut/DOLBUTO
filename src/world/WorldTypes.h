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
    inline constexpr uint32_t InitialFireBurnTicks = 200u;

    enum class WorldEntityType : uint16_t
    {
        None = 0,
        DroppedItem = 1
    };

    struct DroppedItemEntityData
    {
        ItemStack stack{};
        uint32_t processingTicks = 0;
        uint8_t processingType = 0;
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

    enum class TerrainFeatureType : uint8_t
    {
        Tree = 1
    };

    struct TerrainFeatureCandidate
    {
        TerrainFeatureType type = TerrainFeatureType::Tree;
        uint8_t localX = 0;
        uint8_t localZ = 0;
        int16_t baseY = 0;
    };

    struct ChunkSourceData
    {
        std::array<uint8_t, ChunkColumnCount> temperature{};
        std::array<uint8_t, ChunkColumnCount> precipitation{};
        std::array<int16_t, ChunkColumnCount> terrainHeight{};
        std::array<int16_t, ChunkColumnCount> terrainSurfaceY{};
        std::vector<TerrainFeatureCandidate> terrainFeatureCandidates;
        bool terrainSourceCacheValid = false;
        bool terrainFeatureCandidatesValid = false;
    };

    enum class BlockEntityType : uint16_t
    {
        None = 0,
        Fire = 1,
        Crucible = 2,
        Mold = 3
    };

    enum class FireMode : uint8_t
    {
        Exposed = 0,
        Pyrolysis = 1,
        Firing = 2
    };

    struct BlockEntity
    {
        BlockEntityType type = BlockEntityType::None;
        uint8_t localX = 0;
        uint8_t localZ = 0;
        uint16_t y = 0;
        uint32_t remainingBurnTicks = 0;
        FireMode fireMode = FireMode::Exposed;
        uint8_t fireHeatLevel = 0;
        uint16_t burnRemainderItemId = 0;
        uint16_t burnRemainderCount = 0;
        uint16_t moltenFluidId = 0;
        uint16_t moltenAmount = 0;
        uint16_t coolingTicks = 0;
    };

    struct ChunkBlockData
    {
        uint64_t revision = 0;
        std::vector<uint16_t> blocks;
        std::vector<uint16_t> blockStates;
        std::vector<uint16_t> fluids;
        std::vector<WorldEntity> entities;
        std::vector<BlockEntity> blockEntities;
    };

    struct ChunkLightData
    {
        std::vector<uint8_t> localLight;
        std::vector<uint8_t> light;
    };

    struct ChunkDerivedCache
    {
        std::array<uint16_t, SubchunkCount> fluidSubchunkCounts{};
        std::array<bool, SubchunkCount> emptySubchunks{};
    };

    struct ChunkData :
        ChunkSourceData,
        ChunkBlockData,
        ChunkLightData,
        ChunkDerivedCache
    {
        uint64_t generation = 0;
        int chunkX = 0;
        int chunkZ = 0;
    };

    enum class ChunkGenState : uint8_t
    {
        Empty,
        TerrainSourceReady,
        LocalLightReady,
        LightResolved,
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
            BuildTerrainSource,
            ResolveFeatures,
            ResolveLight,
            BuildChunkMesh
        };

        Type type = Type::BuildTerrainSource;
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
        std::array<TerrainBuildData, SubchunkCount> blendSubchunks;
        std::array<TerrainBuildData, SubchunkCount> fluidSubchunks;
    };

    struct RuntimeChunk
    {
        ChunkGenState genState = ChunkGenState::Empty;
        ChunkGenState targetGenState = ChunkGenState::Empty;
        int chunkX = 0;
        int chunkZ = 0;
        std::shared_ptr<ChunkData> data;
        std::array<FeatureWriteListPtr, FeatureNeighborCount> incomingFeatureSlots{};
        std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingFeatureSlots{};
        uint8_t incomingFeatureMask = 0;
        uint64_t outgoingPublishedTicket = 0;
        uint64_t renderTicket = 0;
        uint64_t meshTicket = 0;
        uint64_t sourceTicket = 0;
        uint64_t lightTicket = 0;
        uint64_t localLightTicket = 0;
        uint32_t bestPriority = UINT32_MAX;
        uint64_t buildQueuedTicket = 0;
        uint64_t finalizeQueuedTicket = 0;
        uint64_t lightQueuedTicket = 0;
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
        std::vector<uint16_t> blockStates;
        std::vector<uint16_t> fluids;
        std::vector<uint8_t> light;
        std::vector<BlockEntity> blockEntities;
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

    struct PreparedChunkLoad
    {
        CompletedChunkLoad completed;
        std::optional<RuntimeChunk> preparedChunk;
    };

    struct WorldEntityHandle
    {
        uint64_t chunkKey = 0;
        std::size_t index = 0;
    };

}
