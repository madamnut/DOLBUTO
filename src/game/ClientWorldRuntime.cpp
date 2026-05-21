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
        constexpr uint16_t FluidNone = 0;

        bool terrainSourceReadyOrLater(ChunkGenState state)
        {
            return state == ChunkGenState::TerrainSourceReady ||
                state == ChunkGenState::LocalLightReady ||
                state == ChunkGenState::LightResolved ||
                state == ChunkGenState::Meshed;
        }

        bool localLightReadyOrLater(ChunkGenState state)
        {
            return state == ChunkGenState::LocalLightReady ||
                state == ChunkGenState::LightResolved ||
                state == ChunkGenState::Meshed;
        }

        bool lightResolvedOrLater(ChunkGenState state)
        {
            return state == ChunkGenState::LightResolved ||
                state == ChunkGenState::Meshed;
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
        desiredRenderChunks.clear();
        worldRuntime.clear();
        chunkPrepareSystem.clear();
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
        desiredRenderChunks.clear();
        const std::size_t runtimeCapacity = static_cast<std::size_t>(loadedChunkDiameter + 6) * static_cast<std::size_t>(loadedChunkDiameter + 6);
        const std::size_t renderCapacity = static_cast<std::size_t>(loadedChunkDiameter) * static_cast<std::size_t>(loadedChunkDiameter);
        desiredTerrainChunks.reserve(runtimeCapacity);
        desiredRenderChunks.reserve(renderCapacity);
        worldRuntime.reserve(runtimeCapacity + 256u);
        pendingUnloadSet.reserve(runtimeCapacity + 256u);
        requestedChunkJobs.reserve(runtimeCapacity + 256u);
        requestedMeshJobs.reserve(renderCapacity + 256u);
        requestedChunkJobs.clear();
        requestedMeshJobs.clear();

        TerrainLoadPlan plan{};
        plan.generation = generation;
        plan.loadedChunkDiameter = loadedChunkDiameter;
        plan.renderMin = -(loadedChunkDiameter / 2 - 1);
        plan.renderMax = loadedChunkDiameter / 2;
        plan.runtimeKeepMin = plan.renderMin - 3;
        plan.runtimeKeepMax = plan.renderMax + 3;
        plan.runtimeCapacity = runtimeCapacity;
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
            entry.second.targetGenState = ChunkGenState::Empty;
            entry.second.renderTicket = 0;
            entry.second.meshTicket = 0;
            entry.second.sourceTicket = 0;
            entry.second.lightTicket = 0;
            entry.second.localLightTicket = 0;
            entry.second.buildQueuedTicket = 0;
            entry.second.finalizeQueuedTicket = 0;
            entry.second.lightQueuedTicket = 0;
            entry.second.meshQueuedTicket = 0;
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
        std::vector<CompletedChunkLoad> completedLoads = chunkLoadSystem.drainCompleted();
        for (CompletedChunkLoad& completed : completedLoads)
        {
            chunkPrepareSystem.enqueue(std::move(completed));
        }
        batch.completedLoads = chunkPrepareSystem.drainCompleted();
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
        if (static_cast<int>(chunk.targetGenState) < static_cast<int>(ChunkGenState::Meshed))
        {
            chunk.targetGenState = ChunkGenState::Meshed;
        }
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
        if (static_cast<int>(chunk.targetGenState) < static_cast<int>(ChunkGenState::Meshed))
        {
            chunk.targetGenState = ChunkGenState::Meshed;
        }
        chunk.meshTicket = generation;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);
        return chunk;
    }

    RuntimeChunk& ClientWorldRuntime::requestLightResolveTicket(
        int chunkX,
        int chunkZ,
        uint64_t generation,
        uint32_t priority,
        const ChunkLoadEnqueue& enqueueChunkLoad)
    {
        RuntimeChunk& chunk = ensureRuntimeChunk(chunkX, chunkZ, generation, enqueueChunkLoad);
        if (static_cast<int>(chunk.targetGenState) < static_cast<int>(ChunkGenState::LightResolved))
        {
            chunk.targetGenState = ChunkGenState::LightResolved;
        }
        chunk.lightTicket = generation;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);
        return chunk;
    }

    RuntimeChunk& ClientWorldRuntime::requestLocalLightTicket(
        int chunkX,
        int chunkZ,
        uint64_t generation,
        uint32_t priority,
        const ChunkLoadEnqueue& enqueueChunkLoad)
    {
        RuntimeChunk& chunk = ensureRuntimeChunk(chunkX, chunkZ, generation, enqueueChunkLoad);
        if (static_cast<int>(chunk.targetGenState) < static_cast<int>(ChunkGenState::LocalLightReady))
        {
            chunk.targetGenState = ChunkGenState::LocalLightReady;
        }
        chunk.localLightTicket = generation;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);
        return chunk;
    }

    std::optional<TerrainJob> ClientWorldRuntime::makeTerrainSourceJobIfNeeded(RuntimeChunk& chunk, uint64_t generation)
    {
        if (!chunk.data && chunk.snapshotLoadRequested && !chunk.snapshotLoadFinished)
        {
            return std::nullopt;
        }
        if (static_cast<int>(chunk.targetGenState) < static_cast<int>(ChunkGenState::TerrainSourceReady))
        {
            return std::nullopt;
        }
        if (terrainSourceReadyOrLater(chunk.genState))
        {
            return std::nullopt;
        }
        if (chunk.buildQueuedTicket == generation)
        {
            return std::nullopt;
        }

        TerrainJob job{};
        job.type = TerrainJob::Type::BuildTerrainSource;
        job.generation = generation;
        job.priority = chunk.bestPriority;
        job.chunkX = chunk.chunkX;
        job.chunkZ = chunk.chunkZ;
        requestedChunkJobs.insert(world::WorldRuntime::chunkKey(chunk.chunkX, chunk.chunkZ));
        chunk.buildQueuedTicket = generation;
        return job;
    }

    std::optional<TerrainJob> ClientWorldRuntime::makeLocalLightJobIfReady(uint64_t key, uint64_t generation)
    {
        RuntimeChunk* chunk = worldRuntime.find(key);
        if (chunk == nullptr ||
            static_cast<int>(chunk->targetGenState) < static_cast<int>(ChunkGenState::LocalLightReady) ||
            chunk->genState != ChunkGenState::TerrainSourceReady ||
            !chunk->data ||
            chunk->finalizeQueuedTicket == generation)
        {
            return std::nullopt;
        }

        std::array<std::shared_ptr<ChunkData>, 9> chunks{};
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const RuntimeChunk* source = worldRuntime.find(world::WorldRuntime::chunkKey(chunk->chunkX + dx, chunk->chunkZ + dz));
                if (source == nullptr || !source->data || !terrainSourceReadyOrLater(source->genState))
                {
                    return std::nullopt;
                }
                chunks[static_cast<std::size_t>((dz + 1) * 3 + (dx + 1))] = source->data;
            }
        }

        TerrainJob job{};
        job.type = TerrainJob::Type::ResolveFeatures;
        job.generation = generation;
        job.priority = chunk->bestPriority;
        job.chunkX = chunk->chunkX;
        job.chunkZ = chunk->chunkZ;
        job.meshChunks = chunks;
        chunk->finalizeQueuedTicket = generation;
        return job;
    }

    std::optional<TerrainJob> ClientWorldRuntime::makeLightResolveJobIfReady(uint64_t key, uint64_t generation)
    {
        RuntimeChunk* chunk = worldRuntime.find(key);
        if (chunk == nullptr ||
            static_cast<int>(chunk->targetGenState) < static_cast<int>(ChunkGenState::LightResolved) ||
            chunk->genState != ChunkGenState::LocalLightReady ||
            !chunk->data ||
            chunk->lightQueuedTicket == generation)
        {
            return std::nullopt;
        }

        std::array<std::shared_ptr<ChunkData>, 9> chunks{};
        constexpr std::array<ChunkOffset, 5> RequiredOffsets = {{
            {0, 0},
            {0, -1},
            {-1, 0},
            {1, 0},
            {0, 1}
        }};
        for (const ChunkOffset& offset : RequiredOffsets)
        {
            const RuntimeChunk* source = worldRuntime.find(world::WorldRuntime::chunkKey(chunk->chunkX + offset.x, chunk->chunkZ + offset.z));
            if (source == nullptr || !source->data || !localLightReadyOrLater(source->genState))
            {
                return std::nullopt;
            }
            chunks[static_cast<std::size_t>((offset.z + 1) * 3 + (offset.x + 1))] = source->data;
        }

        TerrainJob job{};
        job.type = TerrainJob::Type::ResolveLight;
        job.generation = generation;
        job.priority = chunk->bestPriority;
        job.chunkX = chunk->chunkX;
        job.chunkZ = chunk->chunkZ;
        job.meshChunks = chunks;
        chunk->lightQueuedTicket = generation;
        return job;
    }

    std::optional<TerrainJob> ClientWorldRuntime::makeMeshJobIfReady(int chunkX, int chunkZ, uint64_t generation, bool meshAlreadyReady)
    {
        const uint64_t key = world::WorldRuntime::chunkKey(chunkX, chunkZ);
        RuntimeChunk* target = worldRuntime.find(key);
        if (target == nullptr ||
            desiredRenderChunks.find(key) == desiredRenderChunks.end() ||
            target->meshTicket != generation ||
            static_cast<int>(target->targetGenState) < static_cast<int>(ChunkGenState::Meshed) ||
            requestedMeshJobs.find(key) != requestedMeshJobs.end() ||
            target->meshQueuedTicket == generation ||
            !lightResolvedOrLater(target->genState))
        {
            return std::nullopt;
        }

        std::array<std::shared_ptr<ChunkData>, 9> chunks{};
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const RuntimeChunk* chunk = worldRuntime.find(world::WorldRuntime::chunkKey(chunkX + dx, chunkZ + dz));
                if (chunk == nullptr || !chunk->data || !lightResolvedOrLater(chunk->genState))
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
        snapshot.genState = chunk.genState == ChunkGenState::Meshed ? ChunkGenState::LightResolved : chunk.genState;
        snapshot.incomingFeatureMask = static_cast<int>(snapshot.genState) >= static_cast<int>(ChunkGenState::LocalLightReady) ? 0 : chunk.incomingFeatureMask;
        if (chunk.data)
        {
            snapshot.hasData = true;
            snapshot.revision = chunk.data->revision;
            snapshot.hasSavedBacking = chunk.hasSavedBacking;
            snapshot.forceSave = chunk.dataDirtyForSave || !chunk.hasSavedBacking;
            snapshot.dataDirtySerial = chunk.dataDirtySerial;
            snapshot.chunkData = chunk.data;
        }
        if (static_cast<int>(snapshot.genState) < static_cast<int>(ChunkGenState::LocalLightReady))
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
            enqueueChunkDataSnapshot(completedData.chunk, ChunkGenState::TerrainSourceReady);
        }

        for (const std::shared_ptr<ChunkData>& chunk : completed.completedLocalLightChunks)
        {
            enqueueChunkDataSnapshot(chunk, ChunkGenState::LocalLightReady);
        }

        for (const std::shared_ptr<ChunkData>& chunk : completed.completedLightChunks)
        {
            enqueueChunkDataSnapshot(chunk, ChunkGenState::LightResolved);
        }
    }

    RuntimeChunk ClientWorldRuntime::prepareRuntimeChunkFromSnapshot(
        const SaveChunkSnapshot& snapshot,
        uint64_t generation,
        const LightAttenuationTables* lightAttenuation)
    {
        RuntimeChunk chunk{};
        chunk.chunkX = snapshot.chunkX;
        chunk.chunkZ = snapshot.chunkZ;
        chunk.genState = snapshot.genState == ChunkGenState::Meshed ? ChunkGenState::LightResolved : snapshot.genState;
        chunk.incomingFeatureMask = static_cast<int>(chunk.genState) >= static_cast<int>(ChunkGenState::LocalLightReady) ? 0 : snapshot.incomingFeatureMask;
        chunk.incomingFeatureSlots = static_cast<int>(chunk.genState) >= static_cast<int>(ChunkGenState::LocalLightReady) ? std::array<FeatureWriteListPtr, FeatureNeighborCount>{} : snapshot.incomingFeatureSlots;
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
                data->light = snapshot.light;
                data->temperature = snapshot.temperature;
                data->precipitation = snapshot.precipitation;
            }
            if (data->fluids.size() != ChunkBlockCount)
            {
                data->fluids.assign(ChunkBlockCount, FluidNone);
            }
            world::WorldRuntime::rebuildDerivedCaches(*data, lightAttenuation);
            if (!snapshot.chunkData)
            {
                data->entities = snapshot.entities;
            }
            data->generation = generation;
            data->revision = snapshot.revision;
            data->chunkX = snapshot.chunkX;
            data->chunkZ = snapshot.chunkZ;
            chunk.data = std::move(data);
        }

        return chunk;
    }

    RuntimeChunk ClientWorldRuntime::runtimeChunkFromSnapshot(
        const SaveChunkSnapshot& snapshot,
        uint64_t generation,
        const EntityNormalizer& normalizeEntity)
    {
        RuntimeChunk chunk = prepareRuntimeChunkFromSnapshot(snapshot, generation, worldRuntime.lightAttenuationTables());
        if (chunk.data)
        {
            for (WorldEntity& entity : chunk.data->entities)
            {
                if (normalizeEntity)
                {
                    normalizeEntity(entity);
                }
            }
        }
        if (chunk.genState == ChunkGenState::LightResolved)
        {
            saveSystem.markClean(chunk.chunkX, chunk.chunkZ, chunk.data ? chunk.data->revision : 0);
        }
        return chunk;
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
