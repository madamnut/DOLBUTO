#include "world/WorldRuntime.h"

#include "world/SkyLightSystem.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>

namespace dolbuto::world
{
    namespace
    {
        constexpr int FluidAmountBits = 7;
        constexpr uint16_t FluidAmountMask = (1u << FluidAmountBits) - 1u;
        constexpr uint16_t FluidNone = 0;
        constexpr uint16_t FluidWater = 1;
        constexpr uint16_t FluidFullAmount = 100;
        constexpr uint16_t BlockRock = 1;
        constexpr uint16_t BlockGrass = 2;
        constexpr uint16_t BlockDirt = 3;
        constexpr uint16_t BlockSand = 4;
        constexpr uint16_t BlockSandstone = 5;
        constexpr uint16_t BlockMud = 6;
        constexpr uint16_t BlockClay = 7;
        constexpr uint16_t BlockIce = 11;
        constexpr uint16_t BlockBedrock = 65535;

        uint16_t fluidId(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid >> FluidAmountBits);
        }

        uint16_t fluidAmount(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid & FluidAmountMask);
        }

        uint16_t packFluid(uint16_t id, uint16_t amount)
        {
            if (id == FluidNone || amount == 0)
            {
                return FluidNone;
            }
            return static_cast<uint16_t>((id << FluidAmountBits) | std::min<uint16_t>(amount, FluidFullAmount));
        }

        bool fluidCellPresent(uint16_t fluid)
        {
            return fluidId(fluid) != FluidNone && fluidAmount(fluid) != 0;
        }

        bool terrainSurfaceBlock(uint16_t block)
        {
            return block == BlockRock ||
                block == BlockGrass ||
                block == BlockDirt ||
                block == BlockSand ||
                block == BlockSandstone ||
                block == BlockMud ||
                block == BlockClay ||
                block == BlockIce ||
                block == BlockBedrock;
        }

        constexpr size_t blockIndex(int x, int y, int z)
        {
            return static_cast<size_t>((y * WorldRuntime::ChunkSizeZ + z) * WorldRuntime::ChunkSizeX + x);
        }

        constexpr int blockX(size_t index)
        {
            return static_cast<int>(index & 0x0Fu);
        }

        constexpr int blockZ(size_t index)
        {
            return static_cast<int>((index >> 4u) & 0x0Fu);
        }

        constexpr int blockY(size_t index)
        {
            return static_cast<int>(index >> 8u);
        }

        void adjustFluidSubchunkCount(const std::shared_ptr<ChunkData>& chunk, int y, uint16_t previousFluid, uint16_t nextFluid)
        {
            if (!chunk || y < 0 || y >= WorldRuntime::ChunkSizeY)
            {
                return;
            }

            const bool hadFluid = fluidCellPresent(previousFluid);
            const bool hasFluid = fluidCellPresent(nextFluid);
            if (hadFluid == hasFluid)
            {
                return;
            }

            uint16_t& count = chunk->fluidSubchunkCounts[static_cast<size_t>(y / WorldRuntime::SubchunkSize)];
            if (hasFluid)
            {
                if (count != UINT16_MAX)
                {
                    ++count;
                }
            }
            else if (count > 0)
            {
                --count;
            }
        }

        uint8_t fallbackBlockLightAttenuation(uint16_t block)
        {
            return block == WorldRuntime::BlockAir ? 1 : MaxSkyLight;
        }

        uint8_t blockLightAttenuation(const LightAttenuationTables* lightAttenuation, uint16_t block)
        {
            if (lightAttenuation != nullptr && static_cast<size_t>(block) < lightAttenuation->block.size())
            {
                return lightAttenuation->block[block];
            }
            return fallbackBlockLightAttenuation(block);
        }

        uint8_t fluidLightAttenuation(const LightAttenuationTables* lightAttenuation, uint16_t fluid)
        {
            if (lightAttenuation != nullptr && static_cast<size_t>(fluid) < lightAttenuation->fluid.size())
            {
                return lightAttenuation->fluid[fluid];
            }
            return fluid == FluidNone ? 0 : 2;
        }

        uint8_t attenuationForCell(const ChunkData& chunk, size_t index, const LightAttenuationTables* lightAttenuation)
        {
            if (index >= chunk.blocks.size())
            {
                return MaxSkyLight;
            }

            uint8_t attenuation = blockLightAttenuation(lightAttenuation, chunk.blocks[index]);
            if (index < chunk.fluids.size() && fluidId(chunk.fluids[index]) != FluidNone && fluidAmount(chunk.fluids[index]) != 0)
            {
                attenuation = std::max<uint8_t>(attenuation, fluidLightAttenuation(lightAttenuation, fluidId(chunk.fluids[index])));
            }
            return attenuation;
        }

        void setSkyLight(std::vector<uint8_t>& light, size_t index, uint8_t skyLight)
        {
            const uint8_t blockLight = index < light.size() ? blockLightFromPacked(light[index]) : 0;
            light[index] = packLight(skyLight, blockLight);
        }

        void setBlockLight(std::vector<uint8_t>& light, size_t index, uint8_t blockLight)
        {
            const uint8_t skyLight = index < light.size() ? skyLightFromPacked(light[index]) : 0;
            light[index] = packLight(skyLight, blockLight);
        }

        struct LightNode
        {
            size_t index = 0;
            uint8_t light = 0;
        };

        struct SubchunkKey
        {
            int chunkX = 0;
            int chunkZ = 0;
            int subchunkY = 0;
        };

        bool sameSubchunk(const SubchunkKey& a, const SubchunkKey& b)
        {
            return a.chunkX == b.chunkX && a.chunkZ == b.chunkZ && a.subchunkY == b.subchunkY;
        }

        bool containsSubchunk(const std::vector<SubchunkKey>& subchunks, const SubchunkKey& target)
        {
            return std::any_of(subchunks.begin(), subchunks.end(), [&](const SubchunkKey& subchunk)
            {
                return sameSubchunk(subchunk, target);
            });
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

    std::size_t WorldRuntime::FluidTickCellHash::operator()(const FluidTickCell& cell) const
    {
        std::size_t seed = static_cast<std::size_t>(static_cast<uint32_t>(cell.x));
        seed ^= static_cast<std::size_t>(static_cast<uint32_t>(cell.y)) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        seed ^= static_cast<std::size_t>(static_cast<uint32_t>(cell.z)) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        return seed;
    }

    std::size_t WorldRuntime::BlockTickCellHash::operator()(const BlockTickCell& cell) const
    {
        std::size_t seed = static_cast<std::size_t>(static_cast<uint32_t>(cell.x));
        seed ^= static_cast<std::size_t>(static_cast<uint32_t>(cell.y)) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        seed ^= static_cast<std::size_t>(static_cast<uint32_t>(cell.z)) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        return seed;
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

    void WorldRuntime::rebuildDerivedCaches(ChunkData& chunk, const LightAttenuationTables* lightAttenuation)
    {
        chunk.emptySubchunks.fill(true);
        chunk.fluidSubchunkCounts.fill(0);
        chunk.terrainSourceCacheValid = false;
        chunk.terrainFeatureCandidatesValid = false;
        chunk.terrainFeatureCandidates.clear();
        if (chunk.blocks.size() != ChunkBlockCount)
        {
            return;
        }
        if (chunk.fluids.size() != ChunkBlockCount)
        {
            chunk.fluids.assign(ChunkBlockCount, 0);
        }
        if (chunk.light.size() != ChunkBlockCount)
        {
            recomputeChunkSkyLight(chunk, lightAttenuation);
        }
        if (chunk.localLight.size() != ChunkBlockCount)
        {
            chunk.localLight = computeLocalSkyLight(chunk, lightAttenuation);
        }

        constexpr size_t BlocksPerLayer = ChunkSizeX * ChunkSizeZ;
        chunk.terrainHeight.fill(0);
        chunk.terrainSurfaceY.fill(-1);
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

        for (int z = 0; z < ChunkSizeZ; ++z)
        {
            for (int x = 0; x < ChunkSizeX; ++x)
            {
                const size_t column = static_cast<size_t>(z * ChunkSizeX + x);
                for (int y = ChunkSizeY - 1; y >= 0; --y)
                {
                    const size_t index = static_cast<size_t>((y * ChunkSizeZ + z) * ChunkSizeX + x);
                    if (terrainSurfaceBlock(chunk.blocks[index]))
                    {
                        chunk.terrainSurfaceY[column] = static_cast<int16_t>(y);
                        chunk.terrainHeight[column] = static_cast<int16_t>(y + 1);
                        break;
                    }
                }
            }
        }
        chunk.terrainSourceCacheValid = true;
    }

    WorldRuntime::RuntimeChunkMap& WorldRuntime::chunks()
    {
        return chunks_;
    }

    const WorldRuntime::RuntimeChunkMap& WorldRuntime::chunks() const
    {
        return chunks_;
    }

    void WorldRuntime::setLightAttenuationTables(LightAttenuationTablesPtr lightAttenuationTables)
    {
        lightAttenuationTables_ = std::move(lightAttenuationTables);
    }

    const LightAttenuationTables* WorldRuntime::lightAttenuationTables() const
    {
        return lightAttenuationTables_.get();
    }

    void WorldRuntime::reserve(size_t capacity)
    {
        chunks_.reserve(capacity);
    }

    void WorldRuntime::clear()
    {
        chunks_.clear();
        nextBlockTicks_.clear();
        nextFluidTicks_.clear();
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
        state.sourceTicket = chunk.sourceTicket;
        state.lightTicket = chunk.lightTicket;
        state.localLightTicket = chunk.localLightTicket;
        state.lightQueuedTicket = chunk.lightQueuedTicket;
        state.targetGenState = chunk.targetGenState;
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
        loaded.sourceTicket = state.sourceTicket;
        loaded.lightTicket = state.lightTicket;
        loaded.localLightTicket = state.localLightTicket;
        loaded.lightQueuedTicket = state.lightQueuedTicket;
        loaded.targetGenState = state.targetGenState;
        loaded.bestPriority = state.bestPriority;
        loaded.snapshotLoadRequested = false;
        loaded.snapshotLoadFinished = true;

        bool created = false;
        RuntimeChunk& target = ensureChunkShell(loaded.chunkX, loaded.chunkZ, created);
        target = std::move(loaded);
        return target;
    }

    RuntimeChunk& WorldRuntime::installTerrainSourceChunk(const std::shared_ptr<ChunkData>& chunk, std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingFeatureSlots)
    {
        bool created = false;
        RuntimeChunk& runtimeChunk = ensureChunkShell(chunk->chunkX, chunk->chunkZ, created);
        runtimeChunk.data = chunk;
        runtimeChunk.outgoingFeatureSlots = std::move(outgoingFeatureSlots);
        runtimeChunk.genState = ChunkGenState::TerrainSourceReady;
        runtimeChunk.buildQueuedTicket = 0;
        runtimeChunk.finalizeQueuedTicket = 0;
        return runtimeChunk;
    }

    RuntimeChunk& WorldRuntime::installLocalLightChunk(const std::shared_ptr<ChunkData>& chunk)
    {
        bool created = false;
        RuntimeChunk& runtimeChunk = ensureChunkShell(chunk->chunkX, chunk->chunkZ, created);
        runtimeChunk.data = chunk;
        runtimeChunk.incomingFeatureSlots = {};
        runtimeChunk.incomingFeatureMask = 0;
        runtimeChunk.finalizeQueuedTicket = 0;
        runtimeChunk.lightQueuedTicket = 0;
        runtimeChunk.genState = ChunkGenState::LocalLightReady;
        return runtimeChunk;
    }

    RuntimeChunk& WorldRuntime::installLightResolvedChunk(const std::shared_ptr<ChunkData>& chunk)
    {
        bool created = false;
        RuntimeChunk& runtimeChunk = ensureChunkShell(chunk->chunkX, chunk->chunkZ, created);
        runtimeChunk.data = chunk;
        runtimeChunk.meshQueuedTicket = 0;
        runtimeChunk.lightQueuedTicket = 0;
        runtimeChunk.genState = ChunkGenState::LightResolved;
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
            (chunk->genState != ChunkGenState::LocalLightReady &&
                chunk->genState != ChunkGenState::LightResolved &&
                chunk->genState != ChunkGenState::Meshed))
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

    BlockEntity* WorldRuntime::blockEntityAtWorld(int x, int y, int z)
    {
        if (y < 0 || y >= ChunkSizeY)
        {
            return nullptr;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        RuntimeChunk* chunk = findChunk(chunkX, chunkZ);
        if (chunk == nullptr || !chunk->data)
        {
            return nullptr;
        }

        const uint8_t localX = static_cast<uint8_t>(positiveModulo(x, ChunkSizeX));
        const uint8_t localZ = static_cast<uint8_t>(positiveModulo(z, ChunkSizeZ));
        const uint16_t localY = static_cast<uint16_t>(y);
        for (BlockEntity& entity : chunk->data->blockEntities)
        {
            if (entity.localX == localX && entity.localZ == localZ && entity.y == localY)
            {
                return &entity;
            }
        }
        return nullptr;
    }

    const BlockEntity* WorldRuntime::blockEntityAtWorld(int x, int y, int z) const
    {
        return const_cast<WorldRuntime*>(this)->blockEntityAtWorld(x, y, z);
    }

    BlockEntity* WorldRuntime::ensureFireBlockEntityAtWorld(int x, int y, int z, uint32_t remainingBurnTicks)
    {
        if (y < 0 || y >= ChunkSizeY)
        {
            return nullptr;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        RuntimeChunk* chunk = findChunk(chunkX, chunkZ);
        if (chunk == nullptr || !chunk->data)
        {
            return nullptr;
        }

        const uint8_t localX = static_cast<uint8_t>(positiveModulo(x, ChunkSizeX));
        const uint8_t localZ = static_cast<uint8_t>(positiveModulo(z, ChunkSizeZ));
        const uint16_t localY = static_cast<uint16_t>(y);
        for (BlockEntity& entity : chunk->data->blockEntities)
        {
            if (entity.localX == localX && entity.localZ == localZ && entity.y == localY)
            {
                bool changed = false;
                if (entity.type != BlockEntityType::Fire)
                {
                    entity.type = BlockEntityType::Fire;
                    if (entity.remainingBurnTicks == 0)
                    {
                        entity.remainingBurnTicks = remainingBurnTicks;
                    }
                    changed = true;
                }
                if (changed)
                {
                    markDataDirty(*chunk);
                }
                return &entity;
            }
        }

        BlockEntity entity{};
        entity.type = BlockEntityType::Fire;
        entity.localX = localX;
        entity.localZ = localZ;
        entity.y = localY;
        entity.remainingBurnTicks = remainingBurnTicks;
        chunk->data->blockEntities.push_back(entity);
        markDataDirty(*chunk);
        return &chunk->data->blockEntities.back();
    }

    bool WorldRuntime::removeBlockEntityAtWorld(int x, int y, int z)
    {
        if (y < 0 || y >= ChunkSizeY)
        {
            return false;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        RuntimeChunk* chunk = findChunk(chunkX, chunkZ);
        if (chunk == nullptr || !chunk->data)
        {
            return false;
        }

        const uint8_t localX = static_cast<uint8_t>(positiveModulo(x, ChunkSizeX));
        const uint8_t localZ = static_cast<uint8_t>(positiveModulo(z, ChunkSizeZ));
        const uint16_t localY = static_cast<uint16_t>(y);
        auto& entities = chunk->data->blockEntities;
        const auto it = std::remove_if(entities.begin(), entities.end(), [&](const BlockEntity& entity)
        {
            return entity.localX == localX && entity.localZ == localZ && entity.y == localY;
        });
        if (it == entities.end())
        {
            return false;
        }

        entities.erase(it, entities.end());
        markDataDirty(*chunk);
        return true;
    }

    uint8_t WorldRuntime::lightAtWorld(int x, int y, int z) const
    {
        if (y >= ChunkSizeY)
        {
            return packLight(MaxSkyLight, 0);
        }
        if (y < 0)
        {
            return 0;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        const RuntimeChunk* chunk = findChunk(chunkX, chunkZ);
        if (chunk == nullptr || !chunk->data ||
            (chunk->genState != ChunkGenState::LightResolved && chunk->genState != ChunkGenState::Meshed))
        {
            return 0;
        }

        const int localX = positiveModulo(x, ChunkSizeX);
        const int localZ = positiveModulo(z, ChunkSizeZ);
        const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
        if (index >= chunk->data->light.size())
        {
            return 0;
        }

        return chunk->data->light[index];
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
            (chunk->genState != ChunkGenState::LocalLightReady &&
                chunk->genState != ChunkGenState::LightResolved &&
                chunk->genState != ChunkGenState::Meshed))
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
            (runtimeChunk->genState != ChunkGenState::LocalLightReady &&
                runtimeChunk->genState != ChunkGenState::LightResolved &&
                runtimeChunk->genState != ChunkGenState::Meshed))
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
        if (runtimeChunk->data->fluids.size() != ChunkBlockCount)
        {
            runtimeChunk->data->fluids.assign(ChunkBlockCount, FluidNone);
        }
        if (block != BlockAir && index < runtimeChunk->data->fluids.size())
        {
            const uint16_t previousFluid = runtimeChunk->data->fluids[index];
            runtimeChunk->data->fluids[index] = FluidNone;
            adjustFluidSubchunkCount(runtimeChunk->data, y, previousFluid, FluidNone);
        }
        runtimeChunk->data->localLight = computeLocalSkyLight(*runtimeChunk->data, lightAttenuationTables_.get());
        if (runtimeChunk->data->light.size() != ChunkBlockCount)
        {
            runtimeChunk->data->light = runtimeChunk->data->localLight;
        }
        ++runtimeChunk->data->revision;
        markDataDirty(*runtimeChunk);
        updateChunkEmptySubchunk(runtimeChunk->data, y / SubchunkSize);
        scheduleBlockTickNeighborhood(x, y, z);
        scheduleFluidTickNeighborhood(x, y, z);
        return true;
    }

    void WorldRuntime::scheduleBlockTickAtWorld(int x, int y, int z)
    {
        if (y < 0 || y >= ChunkSizeY)
        {
            return;
        }

        nextBlockTicks_.insert(BlockTickCell{x, y, z});
    }

    void WorldRuntime::scheduleBlockTickNeighborhood(int x, int y, int z)
    {
        scheduleBlockTickAtWorld(x, y, z);
        scheduleBlockTickAtWorld(x + 1, y, z);
        scheduleBlockTickAtWorld(x - 1, y, z);
        scheduleBlockTickAtWorld(x, y + 1, z);
        scheduleBlockTickAtWorld(x, y - 1, z);
        scheduleBlockTickAtWorld(x, y, z + 1);
        scheduleBlockTickAtWorld(x, y, z - 1);
    }

    std::vector<WorldRuntime::BlockTickCell> WorldRuntime::takeScheduledBlockTicks(uint32_t maxCells)
    {
        std::vector<BlockTickCell> cells;
        if (maxCells == 0 || nextBlockTicks_.empty())
        {
            return cells;
        }

        std::unordered_set<BlockTickCell, BlockTickCellHash> processing;
        processing.swap(nextBlockTicks_);
        const std::size_t cellLimit = static_cast<std::size_t>(maxCells);
        cells.reserve(std::min(processing.size(), cellLimit));
        for (const BlockTickCell& cell : processing)
        {
            if (cells.size() >= cellLimit)
            {
                nextBlockTicks_.insert(cell);
                continue;
            }
            cells.push_back(cell);
        }
        return cells;
    }

    void WorldRuntime::scheduleFluidTickAtWorld(int x, int y, int z)
    {
        if (y < 0 || y >= ChunkSizeY)
        {
            return;
        }

        nextFluidTicks_.insert(FluidTickCell{x, y, z});
    }

    void WorldRuntime::scheduleFluidTickNeighborhood(int x, int y, int z)
    {
        scheduleFluidTickAtWorld(x, y, z);
        scheduleFluidTickAtWorld(x + 1, y, z);
        scheduleFluidTickAtWorld(x - 1, y, z);
        scheduleFluidTickAtWorld(x, y + 1, z);
        scheduleFluidTickAtWorld(x, y - 1, z);
        scheduleFluidTickAtWorld(x, y, z + 1);
        scheduleFluidTickAtWorld(x, y, z - 1);
    }

    uint16_t WorldRuntime::fluidAtWorld(int x, int y, int z) const
    {
        if (y < 0 || y >= ChunkSizeY)
        {
            return FluidNone;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        const RuntimeChunk* chunk = findChunk(chunkX, chunkZ);
        if (chunk == nullptr || !chunk->data ||
            (chunk->genState != ChunkGenState::LocalLightReady &&
                chunk->genState != ChunkGenState::LightResolved &&
                chunk->genState != ChunkGenState::Meshed))
        {
            return FluidNone;
        }

        const int localX = positiveModulo(x, ChunkSizeX);
        const int localZ = positiveModulo(z, ChunkSizeZ);
        const size_t index = blockIndex(localX, y, localZ);
        if (index >= chunk->data->fluids.size())
        {
            return FluidNone;
        }

        return chunk->data->fluids[index];
    }

    bool WorldRuntime::cellCanContainFluid(int x, int y, int z) const
    {
        if (y < 0 || y >= ChunkSizeY)
        {
            return false;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        const RuntimeChunk* chunk = findChunk(chunkX, chunkZ);
        if (chunk == nullptr || !chunk->data ||
            (chunk->genState != ChunkGenState::LocalLightReady &&
                chunk->genState != ChunkGenState::LightResolved &&
                chunk->genState != ChunkGenState::Meshed))
        {
            return false;
        }

        const int localX = positiveModulo(x, ChunkSizeX);
        const int localZ = positiveModulo(z, ChunkSizeZ);
        const size_t index = blockIndex(localX, y, localZ);
        return index < chunk->data->blocks.size() && chunk->data->blocks[index] == BlockAir;
    }

    void WorldRuntime::addChangedFluidCell(int x, int y, int z, FluidTickResult& result) const
    {
        const FluidTickCell cell{x, y, z};
        if (std::find(result.changedCells.begin(), result.changedCells.end(), cell) == result.changedCells.end())
        {
            result.changedCells.push_back(cell);
        }

        auto addSubchunk = [&](int chunkX, int chunkZ, int subchunkY)
        {
            if (subchunkY < 0 || subchunkY >= SubchunksPerChunk)
            {
                return;
            }
            const EditedSubchunk edited{chunkX, chunkZ, subchunkY};
            const auto existing = std::find_if(result.changedSubchunks.begin(), result.changedSubchunks.end(), [&](const EditedSubchunk& other)
            {
                return other.chunkX == edited.chunkX && other.chunkZ == edited.chunkZ && other.subchunkY == edited.subchunkY;
            });
            if (existing == result.changedSubchunks.end())
            {
                result.changedSubchunks.push_back(edited);
            }
        };

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        const int subchunkY = y / SubchunkSize;
        addSubchunk(chunkX, chunkZ, subchunkY);
        if (positiveModulo(x, ChunkSizeX) == 0)
        {
            addSubchunk(chunkX - 1, chunkZ, subchunkY);
        }
        if (positiveModulo(x, ChunkSizeX) == ChunkSizeX - 1)
        {
            addSubchunk(chunkX + 1, chunkZ, subchunkY);
        }
        if (positiveModulo(z, ChunkSizeZ) == 0)
        {
            addSubchunk(chunkX, chunkZ - 1, subchunkY);
        }
        if (positiveModulo(z, ChunkSizeZ) == ChunkSizeZ - 1)
        {
            addSubchunk(chunkX, chunkZ + 1, subchunkY);
        }
        if (positiveModulo(y, SubchunkSize) == 0)
        {
            addSubchunk(chunkX, chunkZ, subchunkY - 1);
        }
        if (positiveModulo(y, SubchunkSize) == SubchunkSize - 1)
        {
            addSubchunk(chunkX, chunkZ, subchunkY + 1);
        }
    }

    void WorldRuntime::addLightChangedFluidCell(int x, int y, int z, FluidTickResult& result) const
    {
        const FluidTickCell cell{x, y, z};
        if (std::find(result.lightChangedCells.begin(), result.lightChangedCells.end(), cell) == result.lightChangedCells.end())
        {
            result.lightChangedCells.push_back(cell);
        }
    }

    bool WorldRuntime::setFluidAtWorld(int x, int y, int z, uint16_t fluid, FluidTickResult& result)
    {
        if (y < 0 || y >= ChunkSizeY)
        {
            return false;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        RuntimeChunk* runtimeChunk = findChunk(chunkX, chunkZ);
        if (runtimeChunk == nullptr || !runtimeChunk->data ||
            (runtimeChunk->genState != ChunkGenState::LocalLightReady &&
                runtimeChunk->genState != ChunkGenState::LightResolved &&
                runtimeChunk->genState != ChunkGenState::Meshed))
        {
            return false;
        }

        const int localX = positiveModulo(x, ChunkSizeX);
        const int localZ = positiveModulo(z, ChunkSizeZ);
        const size_t index = blockIndex(localX, y, localZ);
        if (index >= runtimeChunk->data->blocks.size() || runtimeChunk->data->blocks[index] != BlockAir)
        {
            return false;
        }
        if (runtimeChunk->data->fluids.size() != ChunkBlockCount)
        {
            runtimeChunk->data->fluids.assign(ChunkBlockCount, FluidNone);
        }
        if (index >= runtimeChunk->data->fluids.size())
        {
            return false;
        }

        const uint16_t previousFluid = runtimeChunk->data->fluids[index];
        const uint16_t normalizedFluid = packFluid(fluidId(fluid), fluidAmount(fluid));
        if (previousFluid == normalizedFluid)
        {
            return false;
        }

        runtimeChunk->data->fluids[index] = normalizedFluid;
        adjustFluidSubchunkCount(runtimeChunk->data, y, previousFluid, normalizedFluid);
        ++runtimeChunk->data->revision;
        markDataDirty(*runtimeChunk);
        addChangedFluidCell(x, y, z, result);
        const uint16_t previousLightFluid = fluidAmount(previousFluid) == 0 ? FluidNone : fluidId(previousFluid);
        const uint16_t nextLightFluid = fluidAmount(normalizedFluid) == 0 ? FluidNone : fluidId(normalizedFluid);
        if (previousLightFluid != nextLightFluid)
        {
            addLightChangedFluidCell(x, y, z, result);
        }
        scheduleFluidTickNeighborhood(x, y, z);
        return true;
    }

    WorldRuntime::FluidTickResult WorldRuntime::tickFluidSimulation(uint32_t maxCells)
    {
        FluidTickResult result{};
        if (maxCells == 0 || nextFluidTicks_.empty())
        {
            return result;
        }

        std::unordered_set<FluidTickCell, FluidTickCellHash> processing;
        processing.swap(nextFluidTicks_);
        for (const FluidTickCell& cell : processing)
        {
            if (result.processedCells >= maxCells)
            {
                nextFluidTicks_.insert(cell);
                continue;
            }
            ++result.processedCells;

            const uint16_t currentFluid = fluidAtWorld(cell.x, cell.y, cell.z);
            if (fluidId(currentFluid) != FluidWater || fluidAmount(currentFluid) == 0 || !cellCanContainFluid(cell.x, cell.y, cell.z))
            {
                continue;
            }

            uint16_t currentAmount = fluidAmount(currentFluid);
            const uint16_t belowFluid = fluidAtWorld(cell.x, cell.y - 1, cell.z);
            if (cellCanContainFluid(cell.x, cell.y - 1, cell.z) &&
                (fluidId(belowFluid) == FluidNone || fluidId(belowFluid) == FluidWater))
            {
                const uint16_t belowAmount = fluidId(belowFluid) == FluidWater ? fluidAmount(belowFluid) : 0;
                if (belowAmount < FluidFullAmount)
                {
                    const uint16_t moveAmount = std::min<uint16_t>(currentAmount, FluidFullAmount - belowAmount);
                    setFluidAtWorld(cell.x, cell.y - 1, cell.z, packFluid(FluidWater, belowAmount + moveAmount), result);
                    currentAmount = static_cast<uint16_t>(currentAmount - moveAmount);
                    setFluidAtWorld(cell.x, cell.y, cell.z, packFluid(FluidWater, currentAmount), result);
                    if (currentAmount == 0)
                    {
                        continue;
                    }
                }
            }

            struct HorizontalCell
            {
                int x = 0;
                int z = 0;
                uint16_t amount = 0;
            };
            std::array<HorizontalCell, 5> horizontal{{
                {cell.x, cell.z, currentAmount},
                {cell.x + 1, cell.z, 0},
                {cell.x - 1, cell.z, 0},
                {cell.x, cell.z + 1, 0},
                {cell.x, cell.z - 1, 0}
            }};

            uint16_t cellCount = 1;
            uint32_t totalAmount = currentAmount;
            uint16_t minAmount = currentAmount;
            uint16_t maxAmount = currentAmount;
            for (size_t i = 1; i < horizontal.size(); ++i)
            {
                if (!cellCanContainFluid(horizontal[i].x, cell.y, horizontal[i].z))
                {
                    continue;
                }

                const uint16_t neighborFluid = fluidAtWorld(horizontal[i].x, cell.y, horizontal[i].z);
                if (fluidId(neighborFluid) != FluidNone && fluidId(neighborFluid) != FluidWater)
                {
                    continue;
                }

                horizontal[cellCount].x = horizontal[i].x;
                horizontal[cellCount].z = horizontal[i].z;
                horizontal[cellCount].amount = fluidId(neighborFluid) == FluidWater ? fluidAmount(neighborFluid) : 0;
                totalAmount += horizontal[cellCount].amount;
                minAmount = std::min<uint16_t>(minAmount, horizontal[cellCount].amount);
                maxAmount = std::max<uint16_t>(maxAmount, horizontal[cellCount].amount);
                ++cellCount;
            }
            if (cellCount <= 1 || maxAmount <= static_cast<uint16_t>(minAmount + 1u))
            {
                continue;
            }

            const uint16_t baseAmount = static_cast<uint16_t>(totalAmount / cellCount);
            uint16_t remainder = static_cast<uint16_t>(totalAmount % cellCount);
            for (uint16_t i = 0; i < cellCount; ++i)
            {
                const uint16_t targetAmount = static_cast<uint16_t>(baseAmount + (remainder > 0 ? 1u : 0u));
                if (remainder > 0)
                {
                    --remainder;
                }
                setFluidAtWorld(horizontal[i].x, cell.y, horizontal[i].z, packFluid(FluidWater, targetAmount), result);
            }
        }

        std::vector<uint64_t> updatedLightChunks;
        for (const FluidTickCell& cell : result.lightChangedCells)
        {
            const int chunkX = floorDiv(cell.x, ChunkSizeX);
            const int chunkZ = floorDiv(cell.z, ChunkSizeZ);
            const uint64_t key = chunkKey(chunkX, chunkZ);
            if (std::find(updatedLightChunks.begin(), updatedLightChunks.end(), key) != updatedLightChunks.end())
            {
                continue;
            }

            RuntimeChunk* chunk = find(key);
            if (chunk == nullptr || !chunk->data)
            {
                continue;
            }
            chunk->data->localLight = computeLocalSkyLight(*chunk->data, lightAttenuationTables_.get());
            if (chunk->data->light.size() != ChunkBlockCount)
            {
                chunk->data->light = chunk->data->localLight;
            }
            updatedLightChunks.push_back(key);
        }

        return result;
    }

    std::vector<WorldRuntime::EditedSubchunk> WorldRuntime::resolveEditedSkyLightAtWorld(int x, int y, int z)
    {
        std::vector<EditedSubchunk> changedSubchunks;
        if (y < 0 || y >= ChunkSizeY)
        {
            return changedSubchunks;
        }

        std::deque<SubchunkKey> queue;
        std::vector<SubchunkKey> queued;
        std::vector<uint64_t> markedDirtyChunks;
        auto enqueueSubchunk = [&](SubchunkKey subchunk)
        {
            if (subchunk.subchunkY < 0 || subchunk.subchunkY >= SubchunksPerChunk || containsSubchunk(queued, subchunk))
            {
                return;
            }
            queue.push_back(subchunk);
            queued.push_back(subchunk);
        };
        auto removeQueued = [&](const SubchunkKey& subchunk)
        {
            queued.erase(
                std::remove_if(queued.begin(), queued.end(), [&](const SubchunkKey& queuedSubchunk)
                {
                    return sameSubchunk(queuedSubchunk, subchunk);
                }),
                queued.end());
        };
        auto lightAtWorld = [&](int worldX, int worldY, int worldZ) -> uint8_t
        {
            if (worldY >= ChunkSizeY)
            {
                return packLight(MaxSkyLight, 0);
            }
            if (worldY < 0)
            {
                return 0;
            }

            const int sampleChunkX = floorDiv(worldX, ChunkSizeX);
            const int sampleChunkZ = floorDiv(worldZ, ChunkSizeZ);
            const RuntimeChunk* sampleChunk = findChunk(sampleChunkX, sampleChunkZ);
            if (sampleChunk == nullptr || !sampleChunk->data || sampleChunk->data->light.size() != ChunkBlockCount)
            {
                return 0;
            }

            const int localX = positiveModulo(worldX, ChunkSizeX);
            const int localZ = positiveModulo(worldZ, ChunkSizeZ);
            return sampleChunk->data->light[blockIndex(localX, worldY, localZ)];
        };
        auto markSubchunkChanged = [&](RuntimeChunk& chunk, const SubchunkKey& subchunk)
        {
            const uint64_t key = chunkKey(subchunk.chunkX, subchunk.chunkZ);
            if (std::find(markedDirtyChunks.begin(), markedDirtyChunks.end(), key) == markedDirtyChunks.end())
            {
                if (chunk.data)
                {
                    ++chunk.data->revision;
                }
                markDataDirty(chunk);
                markedDirtyChunks.push_back(key);
            }
            if (std::none_of(changedSubchunks.begin(), changedSubchunks.end(), [&](const EditedSubchunk& existing)
                {
                    return existing.chunkX == subchunk.chunkX && existing.chunkZ == subchunk.chunkZ && existing.subchunkY == subchunk.subchunkY;
                }))
            {
                changedSubchunks.push_back(EditedSubchunk{subchunk.chunkX, subchunk.chunkZ, subchunk.subchunkY});
            }
        };

        enqueueSubchunk(SubchunkKey{floorDiv(x, ChunkSizeX), floorDiv(z, ChunkSizeZ), y / SubchunkSize});

        size_t processedCount = 0;
        const size_t maxProcessedCount = std::max<size_t>(64, chunks_.size() * SubchunksPerChunk);
        while (!queue.empty() && processedCount < maxProcessedCount)
        {
            const SubchunkKey subchunk = queue.front();
            queue.pop_front();
            removeQueued(subchunk);
            ++processedCount;

            RuntimeChunk* runtimeChunk = findChunk(subchunk.chunkX, subchunk.chunkZ);
            if (runtimeChunk == nullptr || !runtimeChunk->data ||
                (runtimeChunk->genState != ChunkGenState::LightResolved && runtimeChunk->genState != ChunkGenState::Meshed))
            {
                continue;
            }

            ChunkData& chunk = *runtimeChunk->data;
            if (chunk.blocks.size() != ChunkBlockCount)
            {
                continue;
            }
            if (chunk.fluids.size() != ChunkBlockCount)
            {
                chunk.fluids.assign(ChunkBlockCount, 0);
            }
            if (chunk.light.size() != ChunkBlockCount)
            {
                chunk.light = chunk.localLight.size() == ChunkBlockCount
                    ? chunk.localLight
                    : computeLocalSkyLight(chunk, lightAttenuationTables_.get());
            }

            std::vector<uint8_t> resolved = chunk.light;
            std::deque<LightNode> skyQueue;
            std::deque<LightNode> blockQueue;
            const int yStart = subchunk.subchunkY * SubchunkSize;
            const int yEnd = yStart + SubchunkSize;
            auto enqueueLight = [&](size_t index, uint8_t skyLight)
            {
                if (skyLight <= skyLightFromPacked(resolved[index]))
                {
                    return;
                }
                setSkyLight(resolved, index, skyLight);
                skyQueue.push_back(LightNode{index, skyLight});
            };
            auto enqueueBlockLight = [&](size_t index, uint8_t blockLight)
            {
                if (blockLight <= blockLightFromPacked(resolved[index]))
                {
                    return;
                }
                setBlockLight(resolved, index, blockLight);
                blockQueue.push_back(LightNode{index, blockLight});
            };
            auto trySeedFromNeighbor = [&](int localX, int localY, int localZ, int neighborWorldX, int neighborWorldY, int neighborWorldZ)
            {
                const size_t index = blockIndex(localX, localY, localZ);
                const uint8_t attenuation = attenuationForCell(chunk, index, lightAttenuationTables_.get());
                const uint8_t neighborPackedLight = lightAtWorld(neighborWorldX, neighborWorldY, neighborWorldZ);
                const uint8_t neighborSkyLight = skyLightFromPacked(neighborPackedLight);
                const uint8_t neighborBlockLight = blockLightFromPacked(neighborPackedLight);
                if (attenuation < neighborSkyLight)
                {
                    enqueueLight(index, static_cast<uint8_t>(neighborSkyLight - attenuation));
                }
                if (attenuation < neighborBlockLight)
                {
                    enqueueBlockLight(index, static_cast<uint8_t>(neighborBlockLight - attenuation));
                }
            };

            for (int localY = yStart; localY < yEnd; ++localY)
            {
                for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                {
                    for (int localX = 0; localX < ChunkSizeX; ++localX)
                    {
                        const size_t index = blockIndex(localX, localY, localZ);
                        const uint8_t localBlockLight = chunk.localLight.size() == ChunkBlockCount
                            ? blockLightFromPacked(chunk.localLight[index])
                            : 0;
                        resolved[index] = packLight(0, localBlockLight);
                    }
                }
            }

            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const int worldX = subchunk.chunkX * ChunkSizeX + localX;
                    const int worldZ = subchunk.chunkZ * ChunkSizeZ + localZ;
                    const bool skyOpenFromTop = yEnd == ChunkSizeY || skyLightFromPacked(lightAtWorld(worldX, yEnd, worldZ)) == MaxSkyLight;
                    if (!skyOpenFromTop)
                    {
                        continue;
                    }

                    for (int localY = yEnd - 1; localY >= yStart; --localY)
                    {
                        const size_t index = blockIndex(localX, localY, localZ);
                        if (attenuationForCell(chunk, index, lightAttenuationTables_.get()) >= MaxSkyLight)
                        {
                            break;
                        }
                        enqueueLight(index, MaxSkyLight);
                    }
                }
            }

            for (int localY = yStart; localY < yEnd; ++localY)
            {
                const int worldY = localY;
                for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                {
                    const int worldZ = subchunk.chunkZ * ChunkSizeZ + localZ;
                    trySeedFromNeighbor(0, localY, localZ, subchunk.chunkX * ChunkSizeX - 1, worldY, worldZ);
                    trySeedFromNeighbor(ChunkSizeX - 1, localY, localZ, (subchunk.chunkX + 1) * ChunkSizeX, worldY, worldZ);
                }
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const int worldX = subchunk.chunkX * ChunkSizeX + localX;
                    trySeedFromNeighbor(localX, localY, 0, worldX, worldY, subchunk.chunkZ * ChunkSizeZ - 1);
                    trySeedFromNeighbor(localX, localY, ChunkSizeZ - 1, worldX, worldY, (subchunk.chunkZ + 1) * ChunkSizeZ);
                }
            }
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                const int worldZ = subchunk.chunkZ * ChunkSizeZ + localZ;
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const int worldX = subchunk.chunkX * ChunkSizeX + localX;
                    trySeedFromNeighbor(localX, yStart, localZ, worldX, yStart - 1, worldZ);
                    trySeedFromNeighbor(localX, yEnd - 1, localZ, worldX, yEnd, worldZ);
                }
            }

            while (!skyQueue.empty())
            {
                const LightNode node = skyQueue.front();
                skyQueue.pop_front();
                if (skyLightFromPacked(resolved[node.index]) != node.light)
                {
                    continue;
                }

                const int localX = blockX(node.index);
                const int localY = blockY(node.index);
                const int localZ = blockZ(node.index);
                auto tryPropagate = [&](int nextX, int nextY, int nextZ)
                {
                    if (nextX < 0 || nextX >= ChunkSizeX ||
                        nextY < yStart || nextY >= yEnd ||
                        nextZ < 0 || nextZ >= ChunkSizeZ)
                    {
                        return;
                    }

                    const size_t nextIndex = blockIndex(nextX, nextY, nextZ);
                    const uint8_t attenuation = attenuationForCell(chunk, nextIndex, lightAttenuationTables_.get());
                    if (attenuation < node.light)
                    {
                        enqueueLight(nextIndex, static_cast<uint8_t>(node.light - attenuation));
                    }
                };

                tryPropagate(localX + 1, localY, localZ);
                tryPropagate(localX - 1, localY, localZ);
                tryPropagate(localX, localY + 1, localZ);
                tryPropagate(localX, localY - 1, localZ);
                tryPropagate(localX, localY, localZ + 1);
                tryPropagate(localX, localY, localZ - 1);
            }

            while (!blockQueue.empty())
            {
                const LightNode node = blockQueue.front();
                blockQueue.pop_front();
                if (blockLightFromPacked(resolved[node.index]) != node.light)
                {
                    continue;
                }

                const int localX = blockX(node.index);
                const int localY = blockY(node.index);
                const int localZ = blockZ(node.index);
                auto tryPropagate = [&](int nextX, int nextY, int nextZ)
                {
                    if (nextX < 0 || nextX >= ChunkSizeX ||
                        nextY < yStart || nextY >= yEnd ||
                        nextZ < 0 || nextZ >= ChunkSizeZ)
                    {
                        return;
                    }

                    const size_t nextIndex = blockIndex(nextX, nextY, nextZ);
                    const uint8_t attenuation = attenuationForCell(chunk, nextIndex, lightAttenuationTables_.get());
                    if (attenuation < node.light)
                    {
                        enqueueBlockLight(nextIndex, static_cast<uint8_t>(node.light - attenuation));
                    }
                };

                tryPropagate(localX + 1, localY, localZ);
                tryPropagate(localX - 1, localY, localZ);
                tryPropagate(localX, localY + 1, localZ);
                tryPropagate(localX, localY - 1, localZ);
                tryPropagate(localX, localY, localZ + 1);
                tryPropagate(localX, localY, localZ - 1);
            }

            bool subchunkChanged = false;
            bool changedNegX = false;
            bool changedPosX = false;
            bool changedNegY = false;
            bool changedPosY = false;
            bool changedNegZ = false;
            bool changedPosZ = false;
            for (int localY = yStart; localY < yEnd; ++localY)
            {
                for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                {
                    for (int localX = 0; localX < ChunkSizeX; ++localX)
                    {
                        const size_t index = blockIndex(localX, localY, localZ);
                        if (chunk.light[index] == resolved[index])
                        {
                            continue;
                        }

                        chunk.light[index] = resolved[index];
                        subchunkChanged = true;
                        changedNegX = changedNegX || localX == 0;
                        changedPosX = changedPosX || localX == ChunkSizeX - 1;
                        changedNegY = changedNegY || localY == yStart;
                        changedPosY = changedPosY || localY == yEnd - 1;
                        changedNegZ = changedNegZ || localZ == 0;
                        changedPosZ = changedPosZ || localZ == ChunkSizeZ - 1;
                    }
                }
            }

            if (!subchunkChanged)
            {
                continue;
            }

            markSubchunkChanged(*runtimeChunk, subchunk);
            if (changedNegX)
            {
                enqueueSubchunk(SubchunkKey{subchunk.chunkX - 1, subchunk.chunkZ, subchunk.subchunkY});
            }
            if (changedPosX)
            {
                enqueueSubchunk(SubchunkKey{subchunk.chunkX + 1, subchunk.chunkZ, subchunk.subchunkY});
            }
            if (changedNegZ)
            {
                enqueueSubchunk(SubchunkKey{subchunk.chunkX, subchunk.chunkZ - 1, subchunk.subchunkY});
            }
            if (changedPosZ)
            {
                enqueueSubchunk(SubchunkKey{subchunk.chunkX, subchunk.chunkZ + 1, subchunk.subchunkY});
            }
            if (changedNegY)
            {
                enqueueSubchunk(SubchunkKey{subchunk.chunkX, subchunk.chunkZ, subchunk.subchunkY - 1});
            }
            if (changedPosY)
            {
                enqueueSubchunk(SubchunkKey{subchunk.chunkX, subchunk.chunkZ, subchunk.subchunkY + 1});
            }
        }

        return changedSubchunks;
    }
}
