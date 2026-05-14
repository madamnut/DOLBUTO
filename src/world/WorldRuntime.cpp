#include "world/WorldRuntime.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace dolbuto::world
{
    namespace
    {
        constexpr int FluidAmountBits = 7;
        constexpr uint16_t FluidAmountMask = (1u << FluidAmountBits) - 1u;
        constexpr uint16_t FluidNone = 0;

        uint16_t fluidId(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid >> FluidAmountBits);
        }

        uint16_t fluidAmount(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid & FluidAmountMask);
        }
    }

    int WorldRuntime::floorDiv(int value, int divisor)
    {
        int quotient = value / divisor;
        const int remainder = value % divisor;
        if (remainder != 0 && ((remainder < 0) != (divisor < 0)))
        {
            --quotient;
        }
        return quotient;
    }

    int WorldRuntime::positiveModulo(int value, int divisor)
    {
        const int result = value % divisor;
        return result < 0 ? result + divisor : result;
    }

    int WorldRuntime::blockCoordinateXz(double worldCoordinate)
    {
        return static_cast<int>(std::floor(worldCoordinate + 0.5));
    }

    int WorldRuntime::blockCoordinateY(double worldCoordinate)
    {
        return static_cast<int>(std::floor(worldCoordinate));
    }

    uint64_t WorldRuntime::chunkKey(int chunkX, int chunkZ)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32u) |
            static_cast<uint64_t>(static_cast<uint32_t>(chunkZ));
    }

    void WorldRuntime::markDataDirty(RuntimeChunk& chunk)
    {
        chunk.dataDirtyForSave = true;
        ++chunk.dataDirtySerial;
        if (chunk.dataDirtySerial == 0)
        {
            chunk.dataDirtySerial = 1;
        }
    }

    void WorldRuntime::updateChunkEmptySubchunk(const std::shared_ptr<ChunkData>& chunk, int subchunkY)
    {
        if (!chunk || subchunkY < 0 || subchunkY >= SubchunksPerChunk)
        {
            return;
        }

        const int yStart = subchunkY * SubchunkSize;
        const int yEnd = yStart + SubchunkSize;
        bool empty = true;
        for (int y = yStart; y < yEnd && empty; ++y)
        {
            for (int z = 0; z < ChunkSizeZ && empty; ++z)
            {
                for (int x = 0; x < ChunkSizeX; ++x)
                {
                    const size_t index = static_cast<size_t>((y * ChunkSizeZ + z) * ChunkSizeX + x);
                    if (index < chunk->blocks.size() && chunk->blocks[index] != BlockAir)
                    {
                        empty = false;
                        break;
                    }
                }
            }
        }

        chunk->emptySubchunks[static_cast<size_t>(subchunkY)] = empty;
    }

    void WorldRuntime::rebuildDerivedCaches(ChunkData& chunk)
    {
        chunk.emptySubchunks.fill(true);
        chunk.fluidSubchunkCounts.fill(0);
        if (chunk.blocks.size() != ChunkBlockCount)
        {
            return;
        }
        if (chunk.fluids.size() != ChunkBlockCount)
        {
            chunk.fluids.assign(ChunkBlockCount, 0);
        }

        constexpr size_t BlocksPerLayer = ChunkSizeX * ChunkSizeZ;
        for (int subchunkY = 0; subchunkY < SubchunksPerChunk; ++subchunkY)
        {
            bool empty = true;
            uint16_t fluidCount = 0;
            const int yStart = subchunkY * SubchunkSize;
            const int yEnd = yStart + SubchunkSize;
            for (int y = yStart; y < yEnd; ++y)
            {
                const size_t layerOffset = static_cast<size_t>(y) * BlocksPerLayer;
                for (size_t i = 0; i < BlocksPerLayer; ++i)
                {
                    const size_t index = layerOffset + i;
                    if (chunk.blocks[index] != BlockAir)
                    {
                        empty = false;
                    }
                    if (fluidId(chunk.fluids[index]) != FluidNone && fluidAmount(chunk.fluids[index]) != 0 && fluidCount != UINT16_MAX)
                    {
                        ++fluidCount;
                    }
                }
            }
            chunk.emptySubchunks[static_cast<size_t>(subchunkY)] = empty;
            chunk.fluidSubchunkCounts[static_cast<size_t>(subchunkY)] = fluidCount;
        }
    }

    WorldRuntime::RuntimeChunkMap& WorldRuntime::chunks()
    {
        return chunks_;
    }

    const WorldRuntime::RuntimeChunkMap& WorldRuntime::chunks() const
    {
        return chunks_;
    }

    void WorldRuntime::reserve(size_t capacity)
    {
        chunks_.reserve(capacity);
    }

    void WorldRuntime::clear()
    {
        chunks_.clear();
    }

    RuntimeChunk* WorldRuntime::find(uint64_t key)
    {
        auto it = chunks_.find(key);
        return it == chunks_.end() ? nullptr : &it->second;
    }

    const RuntimeChunk* WorldRuntime::find(uint64_t key) const
    {
        auto it = chunks_.find(key);
        return it == chunks_.end() ? nullptr : &it->second;
    }

    RuntimeChunk* WorldRuntime::findChunk(int chunkX, int chunkZ)
    {
        return find(chunkKey(chunkX, chunkZ));
    }

    const RuntimeChunk* WorldRuntime::findChunk(int chunkX, int chunkZ) const
    {
        return find(chunkKey(chunkX, chunkZ));
    }

    RuntimeChunk& WorldRuntime::ensureChunkShell(int chunkX, int chunkZ, bool& created)
    {
        const uint64_t key = chunkKey(chunkX, chunkZ);
        auto it = chunks_.find(key);
        created = it == chunks_.end();
        if (created)
        {
            RuntimeChunk chunk{};
            chunk.chunkX = chunkX;
            chunk.chunkZ = chunkZ;
            it = chunks_.emplace(key, std::move(chunk)).first;
        }
        it->second.chunkX = chunkX;
        it->second.chunkZ = chunkZ;
        return it->second;
    }

    void WorldRuntime::erase(uint64_t key)
    {
        chunks_.erase(key);
    }

    WorldRuntime::RuntimeChunkLoadState WorldRuntime::captureLoadState(const RuntimeChunk& chunk)
    {
        RuntimeChunkLoadState state{};
        state.renderTicket = chunk.renderTicket;
        state.meshTicket = chunk.meshTicket;
        state.fullTicket = chunk.fullTicket;
        state.featuringTicket = chunk.featuringTicket;
        state.bestPriority = chunk.bestPriority;
        state.incomingFeatureSlots = chunk.incomingFeatureSlots;
        state.incomingFeatureMask = chunk.incomingFeatureMask;
        return state;
    }

    RuntimeChunk* WorldRuntime::finishSnapshotLoad(uint64_t key)
    {
        RuntimeChunk* chunk = find(key);
        if (chunk == nullptr)
        {
            return nullptr;
        }

        chunk->snapshotLoadRequested = false;
        chunk->snapshotLoadFinished = true;
        return chunk;
    }

    RuntimeChunk& WorldRuntime::installLoadedChunk(RuntimeChunk loaded, const RuntimeChunkLoadState& state)
    {
        loaded.renderTicket = state.renderTicket;
        loaded.meshTicket = state.meshTicket;
        loaded.fullTicket = state.fullTicket;
        loaded.featuringTicket = state.featuringTicket;
        loaded.bestPriority = state.bestPriority;
        loaded.snapshotLoadRequested = false;
        loaded.snapshotLoadFinished = true;

        bool created = false;
        RuntimeChunk& target = ensureChunkShell(loaded.chunkX, loaded.chunkZ, created);
        target = std::move(loaded);
        return target;
    }

    RuntimeChunk& WorldRuntime::installFeaturingChunk(const std::shared_ptr<ChunkData>& chunk, std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingFeatureSlots)
    {
        bool created = false;
        RuntimeChunk& runtimeChunk = ensureChunkShell(chunk->chunkX, chunk->chunkZ, created);
        runtimeChunk.data = chunk;
        runtimeChunk.outgoingFeatureSlots = std::move(outgoingFeatureSlots);
        runtimeChunk.genState = ChunkGenState::Featuring;
        runtimeChunk.buildQueuedTicket = 0;
        return runtimeChunk;
    }

    RuntimeChunk& WorldRuntime::installFullChunk(const std::shared_ptr<ChunkData>& chunk)
    {
        bool created = false;
        RuntimeChunk& runtimeChunk = ensureChunkShell(chunk->chunkX, chunk->chunkZ, created);
        runtimeChunk.data = chunk;
        runtimeChunk.incomingFeatureSlots = {};
        runtimeChunk.incomingFeatureMask = 0;
        runtimeChunk.finalizeQueuedTicket = 0;
        runtimeChunk.genState = ChunkGenState::Full;
        return runtimeChunk;
    }

    void WorldRuntime::clearMeshQueued(uint64_t key)
    {
        RuntimeChunk* chunk = find(key);
        if (chunk != nullptr)
        {
            chunk->meshQueuedTicket = 0;
        }
    }

    bool WorldRuntime::meshRevisionMatches(uint64_t key, uint64_t revision) const
    {
        const RuntimeChunk* chunk = find(key);
        return chunk != nullptr && chunk->data && chunk->data->revision == revision;
    }

    void WorldRuntime::markMeshed(uint64_t key)
    {
        RuntimeChunk* chunk = find(key);
        if (chunk != nullptr)
        {
            chunk->genState = ChunkGenState::Meshed;
        }
    }

    uint16_t WorldRuntime::blockAtWorld(int x, int y, int z) const
    {
        if (y < 0 || y >= ChunkSizeY)
        {
            return BlockAir;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        const RuntimeChunk* chunk = findChunk(chunkX, chunkZ);
        if (chunk == nullptr || !chunk->data ||
            (chunk->genState != ChunkGenState::Full && chunk->genState != ChunkGenState::Meshed))
        {
            return BlockAir;
        }

        const int localX = positiveModulo(x, ChunkSizeX);
        const int localZ = positiveModulo(z, ChunkSizeZ);
        const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
        if (index >= chunk->data->blocks.size())
        {
            return BlockAir;
        }

        return chunk->data->blocks[index];
    }

    bool WorldRuntime::terrainCellBlocksPlayer(int x, int y, int z, const BlockDefinitionProvider& blockDefinition) const
    {
        if (y < 0)
        {
            return true;
        }
        if (y >= ChunkSizeY)
        {
            return false;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        const RuntimeChunk* chunk = findChunk(chunkX, chunkZ);
        if (chunk == nullptr || !chunk->data ||
            (chunk->genState != ChunkGenState::Full && chunk->genState != ChunkGenState::Meshed))
        {
            return true;
        }

        const int localX = positiveModulo(x, ChunkSizeX);
        const int localZ = positiveModulo(z, ChunkSizeZ);
        const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
        if (index >= chunk->data->blocks.size())
        {
            return true;
        }

        return !blockDefinition || blockDefinition(chunk->data->blocks[index]).collision;
    }

    bool WorldRuntime::setBlockAtWorld(int x, int y, int z, uint16_t block)
    {
        if (y < 0 || y >= ChunkSizeY)
        {
            return false;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        RuntimeChunk* runtimeChunk = findChunk(chunkX, chunkZ);
        if (runtimeChunk == nullptr || !runtimeChunk->data ||
            (runtimeChunk->genState != ChunkGenState::Full && runtimeChunk->genState != ChunkGenState::Meshed))
        {
            return false;
        }

        const int localX = positiveModulo(x, ChunkSizeX);
        const int localZ = positiveModulo(z, ChunkSizeZ);
        const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
        if (index >= runtimeChunk->data->blocks.size() || runtimeChunk->data->blocks[index] == block)
        {
            return false;
        }

        runtimeChunk->data->blocks[index] = block;
        ++runtimeChunk->data->revision;
        markDataDirty(*runtimeChunk);
        updateChunkEmptySubchunk(runtimeChunk->data, y / SubchunkSize);
        return true;
    }
}
