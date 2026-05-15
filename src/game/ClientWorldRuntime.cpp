#include "game/ClientWorldRuntime.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace dolbuto::game
{
    namespace
    {
        constexpr uint8_t AllFeatureSourcesMask = 0xFFu;
        constexpr uint16_t FluidNone = 0;

        struct FeatureNeighborOffset
        {
            int x = 0;
            int z = 0;
        };

        constexpr std::array<FeatureNeighborOffset, 8> FeatureNeighborOffsets = {
            FeatureNeighborOffset{-1, -1},
            FeatureNeighborOffset{0, -1},
            FeatureNeighborOffset{1, -1},
            FeatureNeighborOffset{-1, 0},
            FeatureNeighborOffset{1, 0},
            FeatureNeighborOffset{-1, 1},
            FeatureNeighborOffset{0, 1},
            FeatureNeighborOffset{1, 1}
        };

        std::optional<size_t> featureNeighborIndex(int offsetX, int offsetZ)
        {
            for (size_t i = 0; i < FeatureNeighborOffsets.size(); ++i)
            {
                if (FeatureNeighborOffsets[i].x == offsetX && FeatureNeighborOffsets[i].z == offsetZ)
                {
                    return i;
                }
            }
            return std::nullopt;
        }

        void appendFeatureResult(ClientWorldRuntime::FeaturePropagationResult& target, ClientWorldRuntime::FeaturePropagationResult source)
        {
            target.meshCenters.insert(target.meshCenters.end(), source.meshCenters.begin(), source.meshCenters.end());
            target.finalizeKeys.insert(target.finalizeKeys.end(), source.finalizeKeys.begin(), source.finalizeKeys.end());
        }
    }

    void ClientWorldRuntime::setActiveWorld(std::filesystem::path worldDirectory, uint64_t worldSeed)
    {
        activeWorldDirectory = std::move(worldDirectory);
        activeWorldSeed = worldSeed;
        activeWorldSeedSalt = static_cast<int>((worldSeed ^ (worldSeed >> 32u)) & 0x7fffffffu);
    }

    void ClientWorldRuntime::resetLoadRequest()
    {
        terrainLoadRequested = false;
        loadedChunkDiameter = 0;
        loadedCenterGroupChunkX = 0;
        loadedCenterGroupChunkZ = 0;
    }

    void ClientWorldRuntime::resetSceneRuntime()
    {
        pendingUnloadChunks.clear();
        pendingUnloadSet.clear();
        requestedChunkJobs.clear();
        requestedMeshJobs.clear();
        desiredTerrainChunks.clear();
        desiredFeatureChunks.clear();
        desiredRenderChunks.clear();
        worldRuntime.clear();
        resetLoadRequest();
    }

    ClientWorldRuntime::TerrainLoadPlan ClientWorldRuntime::beginTerrainLoadRequest(
        int centerGroupChunkX,
        int centerGroupChunkZ,
        int loadGridScale,
        int loadGridUnitChunks)
    {
        loadedChunkDiameter = std::max(1, loadGridScale) * loadGridUnitChunks;
        loadedCenterGroupChunkX = centerGroupChunkX;
        loadedCenterGroupChunkZ = centerGroupChunkZ;
        const uint64_t generation = ++terrainGeneration;

        desiredTerrainChunks.clear();
        desiredFeatureChunks.clear();
        desiredRenderChunks.clear();
        const std::size_t runtimeCapacity = static_cast<std::size_t>(loadedChunkDiameter + 4) * static_cast<std::size_t>(loadedChunkDiameter + 4);
        const std::size_t featureCapacity = static_cast<std::size_t>(loadedChunkDiameter + 2) * static_cast<std::size_t>(loadedChunkDiameter + 2);
        const std::size_t renderCapacity = static_cast<std::size_t>(loadedChunkDiameter) * static_cast<std::size_t>(loadedChunkDiameter);
        desiredTerrainChunks.reserve(runtimeCapacity);
        desiredFeatureChunks.reserve(featureCapacity);
        desiredRenderChunks.reserve(renderCapacity);
        worldRuntime.reserve(runtimeCapacity + 256u);
        pendingUnloadSet.reserve(runtimeCapacity + 256u);
        requestedChunkJobs.reserve(featureCapacity + 256u);
        requestedMeshJobs.reserve(renderCapacity + 256u);
        requestedChunkJobs.clear();
        requestedMeshJobs.clear();

        TerrainLoadPlan plan{};
        plan.generation = generation;
        plan.loadedChunkDiameter = loadedChunkDiameter;
        plan.renderMin = -(loadedChunkDiameter / 2 - 1);
        plan.renderMax = loadedChunkDiameter / 2;
        plan.runtimeKeepMin = plan.renderMin - 2;
        plan.runtimeKeepMax = plan.renderMax + 2;
        plan.runtimeCapacity = runtimeCapacity;
        plan.featureCapacity = featureCapacity;
        plan.renderCapacity = renderCapacity;
        return plan;
    }

    void ClientWorldRuntime::rebuildLoadOrderIfNeeded()
    {
        const int dataDiameter = loadedChunkDiameter + 4;
        if (loadOrderDiameter == dataDiameter && !loadOrder.empty())
        {
            return;
        }

        loadOrderDiameter = dataDiameter;
        loadOrder.clear();
        loadOrder.reserve(static_cast<std::size_t>(dataDiameter) * static_cast<std::size_t>(dataDiameter));

        const int min = -(dataDiameter / 2 - 1);
        const int max = dataDiameter / 2;
        for (int z = min; z <= max; ++z)
        {
            for (int x = min; x <= max; ++x)
            {
                loadOrder.push_back({x, z});
            }
        }

        auto distanceToCenterGroupSquared = [](const ChunkOffset& offset)
        {
            const int dx = offset.x < 0 ? -offset.x : (offset.x > 1 ? offset.x - 1 : 0);
            const int dz = offset.z < 0 ? -offset.z : (offset.z > 1 ? offset.z - 1 : 0);
            return dx * dx + dz * dz;
        };

        std::stable_sort(loadOrder.begin(), loadOrder.end(), [&](const ChunkOffset& left, const ChunkOffset& right)
        {
            return distanceToCenterGroupSquared(left) < distanceToCenterGroupSquared(right);
        });
    }

    void ClientWorldRuntime::resetRuntimePriorities()
    {
        for (auto& entry : worldRuntime.chunks())
        {
            entry.second.bestPriority = UINT32_MAX;
        }
    }

    void ClientWorldRuntime::collectPendingUnloadsOutsideDesired()
    {
        for (const auto& entry : worldRuntime.chunks())
        {
            if (desiredTerrainChunks.find(entry.first) == desiredTerrainChunks.end())
            {
                if (pendingUnloadSet.insert(entry.first).second)
                {
                    pendingUnloadChunks.push_back(entry.first);
                }
            }
            else
            {
                pendingUnloadSet.erase(entry.first);
            }
        }
    }

    bool ClientWorldRuntime::isTerrainDesired(uint64_t key) const
    {
        return desiredTerrainChunks.find(key) != desiredTerrainChunks.end();
    }

    bool ClientWorldRuntime::isRenderDesired(uint64_t key) const
    {
        return desiredRenderChunks.find(key) != desiredRenderChunks.end();
    }

    ClientWorldRuntime::CompletedWorkBatch ClientWorldRuntime::drainCompletedWork(uint32_t maxMeshUploads)
    {
        CompletedWorkBatch batch{};
        batch.generation = terrainGeneration.load();

        std::vector<uint64_t> uploadChunkKeys;
        uploadChunkKeys.reserve(maxMeshUploads);
        uint32_t uploadChunkCount = 0;
        auto canUploadChunk = [&](uint64_t key) -> bool
        {
            for (uint64_t uploadKey : uploadChunkKeys)
            {
                if (uploadKey == key)
                {
                    return true;
                }
            }

            if (uploadChunkCount >= maxMeshUploads)
            {
                return false;
            }

            uploadChunkKeys.push_back(key);
            ++uploadChunkCount;
            return true;
        };

        batch.terrain = terrainJobSystem.drainCompleted(
            [&](const CompletedChunkMesh& mesh)
            {
                const uint64_t key = world::WorldRuntime::chunkKey(mesh.chunkX, mesh.chunkZ);
                return mesh.generation != batch.generation || !isRenderDesired(key);
            },
            [&](const CompletedChunkMesh& mesh)
            {
                return canUploadChunk(world::WorldRuntime::chunkKey(mesh.chunkX, mesh.chunkZ));
            });
        batch.completedLoads = chunkLoadSystem.drainCompleted();
        return batch;
    }

    RuntimeChunk& ClientWorldRuntime::ensureRuntimeChunk(
        int chunkX,
        int chunkZ,
        uint64_t generation,
        const ChunkLoadEnqueue& enqueueChunkLoad)
    {
        const uint64_t key = world::WorldRuntime::chunkKey(chunkX, chunkZ);
        desiredTerrainChunks.insert(key);
        pendingUnloadSet.erase(key);

        bool created = false;
        RuntimeChunk& chunk = worldRuntime.ensureChunkShell(chunkX, chunkZ, created);
        if (created)
        {
            chunk.snapshotLoadRequested = true;
            chunk.snapshotLoadFinished = false;
            enqueueChunkLoad(chunkX, chunkZ, generation);
        }

        if (!chunk.data && !chunk.snapshotLoadRequested && !chunk.snapshotLoadFinished)
        {
            chunk.snapshotLoadRequested = true;
            enqueueChunkLoad(chunkX, chunkZ, generation);
        }
        if (chunk.data)
        {
            chunk.data->generation = generation;
        }
        return chunk;
    }

    RuntimeChunk& ClientWorldRuntime::requestRenderTicket(
        int chunkX,
        int chunkZ,
        uint64_t generation,
        uint32_t priority,
        const ChunkLoadEnqueue& enqueueChunkLoad)
    {
        const uint64_t key = world::WorldRuntime::chunkKey(chunkX, chunkZ);
        RuntimeChunk& chunk = ensureRuntimeChunk(chunkX, chunkZ, generation, enqueueChunkLoad);
        desiredRenderChunks.insert(key);
        chunk.renderTicket = generation;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);
        return chunk;
    }

    RuntimeChunk& ClientWorldRuntime::requestMeshTicket(
        int chunkX,
        int chunkZ,
        uint64_t generation,
        uint32_t priority,
        const ChunkLoadEnqueue& enqueueChunkLoad)
    {
        RuntimeChunk& chunk = ensureRuntimeChunk(chunkX, chunkZ, generation, enqueueChunkLoad);
        chunk.meshTicket = generation;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);
        return chunk;
    }

    RuntimeChunk& ClientWorldRuntime::requestFullTicket(
        int chunkX,
        int chunkZ,
        uint64_t generation,
        uint32_t priority,
        const ChunkLoadEnqueue& enqueueChunkLoad)
    {
        RuntimeChunk& chunk = ensureRuntimeChunk(chunkX, chunkZ, generation, enqueueChunkLoad);
        chunk.fullTicket = generation;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);
        return chunk;
    }

    RuntimeChunk& ClientWorldRuntime::requestFeaturingTicket(
        int chunkX,
        int chunkZ,
        uint64_t generation,
        uint32_t priority,
        const ChunkLoadEnqueue& enqueueChunkLoad)
    {
        RuntimeChunk& chunk = ensureRuntimeChunk(chunkX, chunkZ, generation, enqueueChunkLoad);
        desiredFeatureChunks.insert(world::WorldRuntime::chunkKey(chunkX, chunkZ));
        chunk.featuringTicket = generation;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);
        return chunk;
    }

    bool ClientWorldRuntime::shouldPublishFeatures(const RuntimeChunk& chunk, uint64_t generation) const
    {
        if (chunk.outgoingPublishedTicket == generation)
        {
            return false;
        }
        return chunk.genState == ChunkGenState::Featuring ||
            chunk.genState == ChunkGenState::Full ||
            chunk.genState == ChunkGenState::Meshed;
    }

    std::optional<TerrainJob> ClientWorldRuntime::makeBuildFeaturingJobIfNeeded(RuntimeChunk& chunk, uint64_t generation)
    {
        if (!chunk.data && chunk.snapshotLoadRequested && !chunk.snapshotLoadFinished)
        {
            return std::nullopt;
        }
        if (chunk.genState == ChunkGenState::Featuring ||
            chunk.genState == ChunkGenState::Full ||
            chunk.genState == ChunkGenState::Meshed)
        {
            return std::nullopt;
        }
        if (shouldPublishFeatures(chunk, generation))
        {
            return std::nullopt;
        }
        if (chunk.buildQueuedTicket == generation)
        {
            return std::nullopt;
        }

        TerrainJob job{};
        job.type = TerrainJob::Type::BuildFeaturing;
        job.generation = generation;
        job.priority = chunk.bestPriority;
        job.chunkX = chunk.chunkX;
        job.chunkZ = chunk.chunkZ;
        requestedChunkJobs.insert(world::WorldRuntime::chunkKey(chunk.chunkX, chunk.chunkZ));
        chunk.buildQueuedTicket = generation;
        return job;
    }

    std::optional<TerrainJob> ClientWorldRuntime::makeFeatureFinalizeJobIfReady(uint64_t key, uint64_t generation)
    {
        RuntimeChunk* chunk = worldRuntime.find(key);
        if (chunk == nullptr ||
            chunk->fullTicket != generation ||
            chunk->genState != ChunkGenState::Featuring ||
            !chunk->data ||
            chunk->finalizeQueuedTicket == generation)
        {
            return std::nullopt;
        }

        if ((chunk->incomingFeatureMask & AllFeatureSourcesMask) != AllFeatureSourcesMask)
        {
            return std::nullopt;
        }

        TerrainJob job{};
        job.type = TerrainJob::Type::FinalizeFeatures;
        job.generation = generation;
        job.priority = chunk->bestPriority;
        job.chunkX = chunk->chunkX;
        job.chunkZ = chunk->chunkZ;
        job.chunk = chunk->data;
        job.incomingFeatureSlots = chunk->incomingFeatureSlots;
        chunk->finalizeQueuedTicket = generation;
        return job;
    }

    std::optional<TerrainJob> ClientWorldRuntime::makeMeshJobIfReady(int chunkX, int chunkZ, uint64_t generation, bool meshAlreadyReady)
    {
        const uint64_t key = world::WorldRuntime::chunkKey(chunkX, chunkZ);
        RuntimeChunk* target = worldRuntime.find(key);
        if (target == nullptr ||
            desiredRenderChunks.find(key) == desiredRenderChunks.end() ||
            target->meshTicket != generation ||
            requestedMeshJobs.find(key) != requestedMeshJobs.end() ||
            target->meshQueuedTicket == generation ||
            (target->genState != ChunkGenState::Full && target->genState != ChunkGenState::Meshed))
        {
            return std::nullopt;
        }

        std::array<std::shared_ptr<ChunkData>, 9> chunks{};
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const RuntimeChunk* chunk = worldRuntime.find(world::WorldRuntime::chunkKey(chunkX + dx, chunkZ + dz));
                if (chunk == nullptr || !chunk->data || chunk->genState == ChunkGenState::Empty)
                {
                    return std::nullopt;
                }
                chunks[static_cast<std::size_t>((dz + 1) * 3 + (dx + 1))] = chunk->data;
            }
        }

        if (meshAlreadyReady)
        {
            return std::nullopt;
        }

        TerrainJob job{};
        job.type = TerrainJob::Type::BuildChunkMesh;
        job.generation = generation;
        job.revision = chunks[4]->revision;
        job.priority = target->bestPriority;
        job.chunkX = chunkX;
        job.chunkZ = chunkZ;
        job.meshChunks = chunks;
        requestedMeshJobs.insert(key);
        target->meshQueuedTicket = generation;
        return job;
    }

    ClientWorldRuntime::ChunkLoadCompletion ClientWorldRuntime::finishChunkLoad(const CompletedChunkLoad& completed)
    {
        const uint64_t key = world::WorldRuntime::chunkKey(completed.chunkX, completed.chunkZ);
        ChunkLoadCompletion completion{};
        completion.chunk = worldRuntime.finishSnapshotLoad(key);
        if (completion.chunk == nullptr)
        {
            return completion;
        }

        completion.loadState = world::WorldRuntime::captureLoadState(*completion.chunk);
        completion.desired = isTerrainDesired(key);
        return completion;
    }

    SaveChunkSnapshot ClientWorldRuntime::makeSaveSnapshot(const RuntimeChunk& chunk) const
    {
        SaveChunkSnapshot snapshot{};
        snapshot.chunkX = chunk.chunkX;
        snapshot.chunkZ = chunk.chunkZ;
        snapshot.genState = chunk.genState == ChunkGenState::Meshed ? ChunkGenState::Full : chunk.genState;
        snapshot.incomingFeatureMask = snapshot.genState == ChunkGenState::Full ? 0 : chunk.incomingFeatureMask;
        if (chunk.data)
        {
            snapshot.hasData = true;
            snapshot.revision = chunk.data->revision;
            snapshot.hasSavedBacking = chunk.hasSavedBacking;
            snapshot.forceSave = chunk.dataDirtyForSave || !chunk.hasSavedBacking;
            snapshot.dataDirtySerial = chunk.dataDirtySerial;
            snapshot.chunkData = chunk.data;
        }
        if (snapshot.genState != ChunkGenState::Full)
        {
            snapshot.incomingFeatureSlots = chunk.incomingFeatureSlots;
        }
        return snapshot;
    }

    void ClientWorldRuntime::enqueueSaveSnapshot(SaveChunkSnapshot snapshot)
    {
        saveSystem.enqueue(std::move(snapshot));
    }

    void ClientWorldRuntime::enqueueChunkDataSnapshot(const std::shared_ptr<ChunkData>& chunk, ChunkGenState genState)
    {
        if (!chunk)
        {
            return;
        }

        SaveChunkSnapshot snapshot{};
        snapshot.chunkX = chunk->chunkX;
        snapshot.chunkZ = chunk->chunkZ;
        snapshot.genState = genState;
        snapshot.revision = chunk->revision;
        snapshot.hasData = true;
        snapshot.chunkData = chunk;
        enqueueSaveSnapshot(std::move(snapshot));
    }

    void ClientWorldRuntime::enqueueSaveAllRuntimeChunks()
    {
        for (const auto& entry : worldRuntime.chunks())
        {
            enqueueSaveSnapshot(makeSaveSnapshot(entry.second));
        }
    }

    void ClientWorldRuntime::enqueueCompletedTerrainSnapshots(world::TerrainCompletedBatch& completed)
    {
        for (CompletedChunkData& completedData : completed.completedChunks)
        {
            enqueueChunkDataSnapshot(completedData.chunk, ChunkGenState::Featuring);
        }

        for (const std::shared_ptr<ChunkData>& chunk : completed.completedMergedChunks)
        {
            enqueueChunkDataSnapshot(chunk, ChunkGenState::Full);
        }
    }

    RuntimeChunk ClientWorldRuntime::runtimeChunkFromSnapshot(
        const SaveChunkSnapshot& snapshot,
        uint64_t generation,
        const EntityNormalizer& normalizeEntity)
    {
        RuntimeChunk chunk{};
        chunk.chunkX = snapshot.chunkX;
        chunk.chunkZ = snapshot.chunkZ;
        chunk.genState = snapshot.genState == ChunkGenState::Meshed ? ChunkGenState::Full : snapshot.genState;
        chunk.incomingFeatureMask = chunk.genState == ChunkGenState::Full ? 0 : snapshot.incomingFeatureMask;
        chunk.incomingFeatureSlots = chunk.genState == ChunkGenState::Full ? std::array<FeatureWriteListPtr, FeatureNeighborCount>{} : snapshot.incomingFeatureSlots;
        chunk.hasSavedBacking = true;
        chunk.dataDirtyForSave = false;

        if (snapshot.hasData &&
            ((snapshot.chunkData && snapshot.chunkData->blocks.size() == ChunkBlockCount) ||
                snapshot.blocks.size() == ChunkBlockCount))
        {
            auto data = std::make_shared<ChunkData>();
            if (snapshot.chunkData && snapshot.chunkData->blocks.size() == ChunkBlockCount)
            {
                *data = *snapshot.chunkData;
            }
            else
            {
                data->revision = snapshot.revision;
                data->chunkX = snapshot.chunkX;
                data->chunkZ = snapshot.chunkZ;
                data->blocks = snapshot.blocks;
                data->fluids = snapshot.fluids;
                data->temperature = snapshot.temperature;
                data->precipitation = snapshot.precipitation;
            }
            if (data->fluids.size() != ChunkBlockCount)
            {
                data->fluids.assign(ChunkBlockCount, FluidNone);
            }
            world::WorldRuntime::rebuildDerivedCaches(*data);
            if (!snapshot.chunkData)
            {
                data->entities = snapshot.entities;
            }
            data->generation = generation;
            data->revision = snapshot.revision;
            data->chunkX = snapshot.chunkX;
            data->chunkZ = snapshot.chunkZ;
            for (WorldEntity& entity : data->entities)
            {
                if (normalizeEntity)
                {
                    normalizeEntity(entity);
                }
            }
            chunk.data = std::move(data);
        }

        if (chunk.genState == ChunkGenState::Full)
        {
            saveSystem.markClean(chunk.chunkX, chunk.chunkZ, chunk.data ? chunk.data->revision : 0);
        }
        return chunk;
    }

    void ClientWorldRuntime::mergeLoadStateIncomingFeatures(
        RuntimeChunk& loaded,
        const world::WorldRuntime::RuntimeChunkLoadState& loadState,
        const world::TerrainBuilderConfig& terrainConfig) const
    {
        if (loadState.incomingFeatureMask == 0)
        {
            return;
        }

        if ((loaded.genState == ChunkGenState::Full || loaded.genState == ChunkGenState::Meshed) && loaded.data)
        {
            if (world::TerrainBuilder(terrainConfig).applyFeatureWrites(loaded.data, loadState.incomingFeatureSlots))
            {
                loaded.genState = ChunkGenState::Full;
            }
            return;
        }

        for (size_t slot = 0; slot < FeatureNeighborCount; ++slot)
        {
            const uint8_t bit = static_cast<uint8_t>(1u << static_cast<uint32_t>(slot));
            if ((loadState.incomingFeatureMask & bit) != 0 && loadState.incomingFeatureSlots[slot] && !loaded.incomingFeatureSlots[slot])
            {
                loaded.incomingFeatureSlots[slot] = loadState.incomingFeatureSlots[slot];
                loaded.incomingFeatureMask |= bit;
            }
        }
    }

    ClientWorldRuntime::FeaturePropagationResult ClientWorldRuntime::acceptFeatureSlot(
        int targetChunkX,
        int targetChunkZ,
        size_t sourceSlot,
        FeatureWriteListPtr writes,
        const world::TerrainBuilderConfig& terrainConfig)
    {
        FeaturePropagationResult result{};
        if (sourceSlot >= FeatureNeighborCount || !writes)
        {
            return result;
        }

        const uint64_t targetKey = world::WorldRuntime::chunkKey(targetChunkX, targetChunkZ);
        RuntimeChunk* existingTarget = worldRuntime.find(targetKey);
        if (existingTarget == nullptr && !isTerrainDesired(targetKey))
        {
            return result;
        }

        bool created = false;
        RuntimeChunk& target = existingTarget != nullptr
            ? *existingTarget
            : worldRuntime.ensureChunkShell(targetChunkX, targetChunkZ, created);

        if ((target.genState == ChunkGenState::Full || target.genState == ChunkGenState::Meshed) && target.data)
        {
            std::array<FeatureWriteListPtr, FeatureNeighborCount> singleSlot{};
            singleSlot[sourceSlot] = std::move(writes);
            if (world::TerrainBuilder(terrainConfig).applyFeatureWrites(target.data, singleSlot))
            {
                target.genState = ChunkGenState::Full;
                result.meshCenters.push_back({targetChunkX, targetChunkZ});
            }
            return result;
        }

        const uint8_t sourceBit = static_cast<uint8_t>(1u << static_cast<uint32_t>(sourceSlot));
        if ((target.incomingFeatureMask & sourceBit) != 0)
        {
            return result;
        }

        target.incomingFeatureSlots[sourceSlot] = std::move(writes);
        target.incomingFeatureMask |= sourceBit;
        result.finalizeKeys.push_back(targetKey);
        return result;
    }

    ClientWorldRuntime::FeaturePropagationResult ClientWorldRuntime::acceptSavedFeaturingChunkFeatures(
        const CompletedChunkData& completed,
        const world::TerrainBuilderConfig& terrainConfig)
    {
        FeaturePropagationResult result{};
        if (!completed.chunk)
        {
            return result;
        }

        for (size_t slot = 0; slot < FeatureNeighborOffsets.size(); ++slot)
        {
            const int targetChunkX = completed.chunk->chunkX + FeatureNeighborOffsets[slot].x;
            const int targetChunkZ = completed.chunk->chunkZ + FeatureNeighborOffsets[slot].z;
            const std::optional<size_t> sourceSlot = featureNeighborIndex(-FeatureNeighborOffsets[slot].x, -FeatureNeighborOffsets[slot].z);
            if (!sourceSlot)
            {
                continue;
            }

            if (isTerrainDesired(world::WorldRuntime::chunkKey(targetChunkX, targetChunkZ)))
            {
                appendFeatureResult(result, acceptFeatureSlot(
                    targetChunkX,
                    targetChunkZ,
                    *sourceSlot,
                    completed.outgoingFeatureSlots[slot],
                    terrainConfig));
            }
        }
        return result;
    }

    ClientWorldRuntime::FeaturePropagationResult ClientWorldRuntime::publishFeatureSlots(
        RuntimeChunk& sourceChunk,
        uint64_t generation,
        const world::TerrainBuilderConfig& terrainConfig)
    {
        FeaturePropagationResult result{};
        if (sourceChunk.genState == ChunkGenState::Empty)
        {
            return result;
        }

        if (sourceChunk.outgoingPublishedTicket == generation)
        {
            return result;
        }

        const bool hasOutgoingSlots = std::any_of(sourceChunk.outgoingFeatureSlots.begin(), sourceChunk.outgoingFeatureSlots.end(), [](const FeatureWriteListPtr& slot)
        {
            return slot != nullptr;
        });
        if (!hasOutgoingSlots && sourceChunk.data)
        {
            const world::TerrainBuilder terrainBuilder(terrainConfig);
            const std::array<int, ChunkColumnCount> heights = terrainBuilder.buildChunkHeightmap(sourceChunk.chunkX, sourceChunk.chunkZ);
            sourceChunk.outgoingFeatureSlots = terrainBuilder.buildTreeFeatures(sourceChunk.data, heights);
        }

        for (size_t slot = 0; slot < FeatureNeighborOffsets.size(); ++slot)
        {
            const int targetChunkX = sourceChunk.chunkX + FeatureNeighborOffsets[slot].x;
            const int targetChunkZ = sourceChunk.chunkZ + FeatureNeighborOffsets[slot].z;
            const std::optional<size_t> sourceSlot = featureNeighborIndex(-FeatureNeighborOffsets[slot].x, -FeatureNeighborOffsets[slot].z);
            if (!sourceSlot)
            {
                continue;
            }
            appendFeatureResult(result, acceptFeatureSlot(targetChunkX, targetChunkZ, *sourceSlot, sourceChunk.outgoingFeatureSlots[slot], terrainConfig));
        }
        sourceChunk.outgoingPublishedTicket = generation;
        return result;
    }

    ClientWorldRuntime::CompletedChunkDecision ClientWorldRuntime::decideCompletedChunk(const ChunkData& chunk) const
    {
        const uint64_t generation = terrainGeneration.load();
        const uint64_t key = world::WorldRuntime::chunkKey(chunk.chunkX, chunk.chunkZ);
        if (chunk.generation == generation && isTerrainDesired(key))
        {
            return CompletedChunkDecision::Install;
        }
        if (chunk.generation != generation && isTerrainDesired(key))
        {
            return CompletedChunkDecision::Ignore;
        }
        return CompletedChunkDecision::Save;
    }

    ClientWorldRuntime::CompletedMeshDecision ClientWorldRuntime::decideCompletedMesh(const CompletedChunkMesh& mesh) const
    {
        const uint64_t generation = terrainGeneration.load();
        const uint64_t key = world::WorldRuntime::chunkKey(mesh.chunkX, mesh.chunkZ);
        if (mesh.generation != generation || !isRenderDesired(key))
        {
            return CompletedMeshDecision::Ignore;
        }
        if (!worldRuntime.meshRevisionMatches(key, mesh.revision))
        {
            return CompletedMeshDecision::Retry;
        }
        return CompletedMeshDecision::Install;
    }

    std::optional<uint64_t> ClientWorldRuntime::takePendingTerrainUnload()
    {
        if (pendingUnloadChunks.empty())
        {
            return std::nullopt;
        }

        const uint64_t key = pendingUnloadChunks.front();
        pendingUnloadChunks.pop_front();
        return key;
    }

    bool ClientWorldRuntime::cancelPendingTerrainUnloadIfDesired(uint64_t key)
    {
        if (!isTerrainDesired(key))
        {
            return false;
        }

        pendingUnloadSet.erase(key);
        return true;
    }

    void ClientWorldRuntime::finishPendingTerrainUnload(uint64_t key)
    {
        worldRuntime.erase(key);
        clearTerrainRequests(key);
        pendingUnloadSet.erase(key);
    }

    void ClientWorldRuntime::clearRequestedChunkJob(uint64_t key)
    {
        requestedChunkJobs.erase(key);
    }

    void ClientWorldRuntime::clearRequestedMeshJob(uint64_t key)
    {
        requestedMeshJobs.erase(key);
        worldRuntime.clearMeshQueued(key);
    }

    void ClientWorldRuntime::clearTerrainRequests(uint64_t key)
    {
        requestedChunkJobs.erase(key);
        clearRequestedMeshJob(key);
    }
}
