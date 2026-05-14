#pragma once

#include "world/BlockData.h"
#include "world/WorldTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

namespace dolbuto::world
{
    class WorldRuntime
    {
    public:
        using RuntimeChunkMap = std::unordered_map<uint64_t, RuntimeChunk>;
        using BlockDefinitionProvider = std::function<const BlockDefinition&(uint16_t)>;

        struct RuntimeChunkLoadState
        {
            uint64_t renderTicket = 0;
            uint64_t meshTicket = 0;
            uint64_t fullTicket = 0;
            uint64_t featuringTicket = 0;
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
        static void rebuildDerivedCaches(ChunkData& chunk);

        RuntimeChunkMap& chunks();
        const RuntimeChunkMap& chunks() const;
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
        RuntimeChunk& installFeaturingChunk(const std::shared_ptr<ChunkData>& chunk, std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingFeatureSlots);
        RuntimeChunk& installFullChunk(const std::shared_ptr<ChunkData>& chunk);
        void clearMeshQueued(uint64_t key);
        bool meshRevisionMatches(uint64_t key, uint64_t revision) const;
        void markMeshed(uint64_t key);

        uint16_t blockAtWorld(int x, int y, int z) const;
        bool terrainCellBlocksPlayer(int x, int y, int z, const BlockDefinitionProvider& blockDefinition) const;
        bool setBlockAtWorld(int x, int y, int z, uint16_t block);

    private:
        RuntimeChunkMap chunks_;
    };
}
