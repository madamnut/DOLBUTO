#pragma once

#include "world/BlockData.h"
#include "world/WorldTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace dolbuto::world
{
    class WorldRuntime
    {
    public:
        using RuntimeChunkMap = std::unordered_map<uint64_t, RuntimeChunk>;
        using BlockDefinitionProvider = std::function<const BlockDefinition&(uint16_t)>;

        struct EditedSubchunk
        {
            int chunkX = 0;
            int chunkZ = 0;
            int subchunkY = 0;
        };

        struct FluidTickCell
        {
            int x = 0;
            int y = 0;
            int z = 0;

            bool operator==(const FluidTickCell& other) const
            {
                return x == other.x && y == other.y && z == other.z;
            }
        };

        struct FluidTickCellHash
        {
            std::size_t operator()(const FluidTickCell& cell) const;
        };

        struct BlockTickCell
        {
            int x = 0;
            int y = 0;
            int z = 0;

            bool operator==(const BlockTickCell& other) const
            {
                return x == other.x && y == other.y && z == other.z;
            }
        };

        struct BlockTickCellHash
        {
            std::size_t operator()(const BlockTickCell& cell) const;
        };

        struct FluidTickResult
        {
            std::vector<FluidTickCell> changedCells;
            std::vector<FluidTickCell> lightChangedCells;
            std::vector<EditedSubchunk> changedSubchunks;
            uint32_t processedCells = 0;
        };

        struct RuntimeChunkLoadState
        {
            uint64_t renderTicket = 0;
            uint64_t meshTicket = 0;
            uint64_t sourceTicket = 0;
            uint64_t lightTicket = 0;
            uint64_t localLightTicket = 0;
            uint64_t lightQueuedTicket = 0;
            ChunkGenState targetGenState = ChunkGenState::Empty;
            uint32_t bestPriority = UINT32_MAX;
            std::array<FeatureWriteListPtr, FeatureNeighborCount> incomingFeatureSlots{};
            uint8_t incomingFeatureMask = 0;
        };

        static constexpr int ChunkSizeX = 16;
        static constexpr int ChunkSizeY = 512;
        static constexpr int ChunkSizeZ = 16;
        static constexpr int SubchunkSize = 16;
        static constexpr int SubchunksPerChunk = ChunkSizeY / SubchunkSize;
        static constexpr uint16_t BlockAir = 0;

        static int floorDiv(int value, int divisor);
        static int positiveModulo(int value, int divisor);
        static int blockCoordinateXz(double worldCoordinate);
        static int blockCoordinateY(double worldCoordinate);
        static uint64_t chunkKey(int chunkX, int chunkZ);
        static void markDataDirty(RuntimeChunk& chunk);
        static void updateChunkEmptySubchunk(const std::shared_ptr<ChunkData>& chunk, int subchunkY);
        static void rebuildDerivedCaches(ChunkData& chunk, const LightAttenuationTables* lightAttenuation = nullptr);

        RuntimeChunkMap& chunks();
        const RuntimeChunkMap& chunks() const;
        void setLightAttenuationTables(LightAttenuationTablesPtr lightAttenuationTables);
        const LightAttenuationTables* lightAttenuationTables() const;
        void reserve(size_t capacity);
        void clear();

        RuntimeChunk* find(uint64_t key);
        const RuntimeChunk* find(uint64_t key) const;
        RuntimeChunk* findChunk(int chunkX, int chunkZ);
        const RuntimeChunk* findChunk(int chunkX, int chunkZ) const;
        RuntimeChunk& ensureChunkShell(int chunkX, int chunkZ, bool& created);
        void erase(uint64_t key);

        static RuntimeChunkLoadState captureLoadState(const RuntimeChunk& chunk);
        RuntimeChunk* finishSnapshotLoad(uint64_t key);
        RuntimeChunk& installLoadedChunk(RuntimeChunk loaded, const RuntimeChunkLoadState& state);
        RuntimeChunk& installTerrainSourceChunk(const std::shared_ptr<ChunkData>& chunk, std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingFeatureSlots);
        RuntimeChunk& installLocalLightChunk(const std::shared_ptr<ChunkData>& chunk);
        RuntimeChunk& installLightResolvedChunk(const std::shared_ptr<ChunkData>& chunk);
        void clearMeshQueued(uint64_t key);
        bool meshRevisionMatches(uint64_t key, uint64_t revision) const;
        void markMeshed(uint64_t key);

        uint16_t blockAtWorld(int x, int y, int z) const;
        uint8_t lightAtWorld(int x, int y, int z) const;
        bool terrainCellBlocksPlayer(int x, int y, int z, const BlockDefinitionProvider& blockDefinition) const;
        bool setBlockAtWorld(int x, int y, int z, uint16_t block);
        void scheduleBlockTickAtWorld(int x, int y, int z);
        void scheduleBlockTickNeighborhood(int x, int y, int z);
        std::vector<BlockTickCell> takeScheduledBlockTicks(uint32_t maxCells);
        void scheduleFluidTickAtWorld(int x, int y, int z);
        void scheduleFluidTickNeighborhood(int x, int y, int z);
        FluidTickResult tickFluidSimulation(uint32_t maxCells);
        std::vector<EditedSubchunk> resolveEditedSkyLightAtWorld(int x, int y, int z);

    private:
        uint16_t fluidAtWorld(int x, int y, int z) const;
        bool cellCanContainFluid(int x, int y, int z) const;
        bool setFluidAtWorld(int x, int y, int z, uint16_t fluid, FluidTickResult& result);
        void addChangedFluidCell(int x, int y, int z, FluidTickResult& result) const;
        void addLightChangedFluidCell(int x, int y, int z, FluidTickResult& result) const;

        RuntimeChunkMap chunks_;
        LightAttenuationTablesPtr lightAttenuationTables_;
        std::unordered_set<BlockTickCell, BlockTickCellHash> nextBlockTicks_;
        std::unordered_set<FluidTickCell, FluidTickCellHash> nextFluidTicks_;
    };
}
