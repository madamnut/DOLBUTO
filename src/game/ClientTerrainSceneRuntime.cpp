#include "game/ClientTerrainSceneRuntime.h"

#include "game/ClientTerrainCoordinator.h"
#include "game/ClientTerrainJobProcessor.h"
#include "world/WorldRuntime.h"

#include <utility>

namespace dolbuto::game
{
    ClientTerrainSceneRuntime::ClientTerrainSceneRuntime(ClientWorldRuntime& runtime) :
        runtime_(runtime)
    {
    }

    ClientTerrainSceneRuntime::TerrainLoadResult ClientTerrainSceneRuntime::requestTerrainLoad(
        int centerGroupChunkX,
        int centerGroupChunkZ,
        int loadGridScale,
        int loadGridUnitChunks,
        world::TerrainBuilderConfig terrainConfig,
        const MeshReadyPredicate& meshReady)
    {
        if (runtime_.terrainLoadRequested &&
            centerGroupChunkX == runtime_.loadedCenterGroupChunkX &&
            centerGroupChunkZ == runtime_.loadedCenterGroupChunkZ)
        {
            return {};
        }

        runtime_.terrainLoadRequested = true;
        const ClientWorldRuntime::TerrainLoadPlan loadPlan =
            runtime_.beginTerrainLoadRequest(centerGroupChunkX, centerGroupChunkZ, loadGridScale, loadGridUnitChunks);
        runtime_.rebuildLoadOrderIfNeeded();
        runtime_.terrainJobSystem.clearQueuedJobsAndMeshes();
        runtime_.resetRuntimePriorities();

        ClientTerrainCoordinator coordinator(
            runtime_,
            loadPlan.generation,
            terrainConfig,
            [this](int loadChunkX, int loadChunkZ, uint64_t loadGeneration)
            {
                enqueueChunkLoadJob(loadChunkX, loadChunkZ, loadGeneration);
            },
            [this](TerrainJob job)
            {
                enqueueTerrainJob(std::move(job));
            },
            meshReady);

        for (const ClientWorldRuntime::ChunkOffset& offset : runtime_.loadOrder)
        {
            if (offset.x < loadPlan.runtimeKeepMin ||
                offset.x > loadPlan.runtimeKeepMax ||
                offset.z < loadPlan.runtimeKeepMin ||
                offset.z > loadPlan.runtimeKeepMax)
            {
                continue;
            }
            coordinator.ensureRuntimeChunk(
                runtime_.loadedCenterGroupChunkX + offset.x,
                runtime_.loadedCenterGroupChunkZ + offset.z);
        }

        auto distanceToCenterGroupSquared = [](const ClientWorldRuntime::ChunkOffset& offset)
        {
            const int dx = offset.x < 0 ? -offset.x : (offset.x > 1 ? offset.x - 1 : 0);
            const int dz = offset.z < 0 ? -offset.z : (offset.z > 1 ? offset.z - 1 : 0);
            return static_cast<uint32_t>(dx * dx + dz * dz);
        };

        for (const ClientWorldRuntime::ChunkOffset& offset : runtime_.loadOrder)
        {
            if (offset.x < loadPlan.renderMin ||
                offset.x > loadPlan.renderMax ||
                offset.z < loadPlan.renderMin ||
                offset.z > loadPlan.renderMax)
            {
                continue;
            }
            coordinator.requestRenderCascade(
                runtime_.loadedCenterGroupChunkX + offset.x,
                runtime_.loadedCenterGroupChunkZ + offset.z,
                distanceToCenterGroupSquared(offset));
        }

        runtime_.collectPendingUnloadsOutsideDesired();

        TerrainLoadResult result{};
        result.requested = true;
        result.droppedItemTrackingCapacity = loadPlan.runtimeCapacity + 256u;
        result.terrainRenderCapacity = loadPlan.renderCapacity + 256u;
        result.desiredRenderChunks = &runtime_.desiredRenderChunks;
        return result;
    }

    ClientTerrainCompletionHandler::Result ClientTerrainSceneRuntime::processCompletedTerrainJobs(
        uint32_t maxMeshUploads,
        world::TerrainBuilderConfig terrainConfig,
        const MeshReadyPredicate& meshReady,
        const ClientWorldRuntime::EntityNormalizer& normalizeEntity)
    {
        ClientWorldRuntime::CompletedWorkBatch work = runtime_.drainCompletedWork(maxMeshUploads);

        ClientTerrainCoordinator coordinator(
            runtime_,
            work.generation,
            terrainConfig,
            [this](int loadChunkX, int loadChunkZ, uint64_t loadGeneration)
            {
                enqueueChunkLoadJob(loadChunkX, loadChunkZ, loadGeneration);
            },
            [this](TerrainJob job)
            {
                enqueueTerrainJob(std::move(job));
            },
            meshReady);

        ClientTerrainCompletionHandler completionHandler(
            runtime_,
            coordinator,
            terrainConfig);
        return completionHandler.handle(std::move(work), normalizeEntity);
    }

    void ClientTerrainSceneRuntime::startTerrainWorkers(
        int terrainWorkerCount,
        world::TerrainBuilderConfig terrainConfig,
        const RenderMeshJobProcessor& processRenderMeshJob)
    {
        runtime_.terrainJobSystem.start(
            terrainWorkerCount,
            [this]
            {
                return runtime_.terrainGeneration.load();
            },
            [terrainConfig, processRenderMeshJob](TerrainJob job)
            {
                if (ClientTerrainJobProcessor::canProcess(job))
                {
                    return ClientTerrainJobProcessor(terrainConfig).process(std::move(job));
                }

                return processRenderMeshJob(std::move(job));
            });
    }

    void ClientTerrainSceneRuntime::stopTerrainWorkers()
    {
        runtime_.terrainJobSystem.stop();
        world::TerrainCompletedBatch completed = runtime_.terrainJobSystem.drainForShutdown();
        runtime_.enqueueCompletedTerrainSnapshots(completed);
    }

    void ClientTerrainSceneRuntime::startChunkLoadWorker()
    {
        runtime_.chunkLoadSystem.start([this](int chunkX, int chunkZ)
        {
            return runtime_.saveSystem.load(chunkX, chunkZ);
        });
    }

    void ClientTerrainSceneRuntime::stopChunkLoadWorker()
    {
        runtime_.chunkLoadSystem.stop();
    }

    void ClientTerrainSceneRuntime::startSaveWorker()
    {
        runtime_.saveSystem.start(runtime_.activeWorldDirectory, [this](const SaveChunkSnapshot& snapshot, bool savedChunkData)
        {
            if (!savedChunkData)
            {
                return;
            }

            const uint64_t runtimeKey = world::WorldRuntime::chunkKey(snapshot.chunkX, snapshot.chunkZ);
            RuntimeChunk* runtimeChunk = runtime_.worldRuntime.find(runtimeKey);
            if (runtimeChunk != nullptr &&
                runtimeChunk->data &&
                runtimeChunk->data->revision == snapshot.revision)
            {
                runtimeChunk->hasSavedBacking = true;
                if (runtimeChunk->dataDirtySerial == snapshot.dataDirtySerial)
                {
                    runtimeChunk->dataDirtyForSave = false;
                }
            }
        });
    }

    void ClientTerrainSceneRuntime::stopSaveWorker()
    {
        runtime_.saveSystem.stop();
    }

    void ClientTerrainSceneRuntime::enqueueChunkLoadJob(int chunkX, int chunkZ, uint64_t generation)
    {
        runtime_.chunkLoadSystem.enqueue(chunkX, chunkZ, generation);
    }

    void ClientTerrainSceneRuntime::enqueueTerrainJob(TerrainJob job)
    {
        runtime_.terrainJobSystem.enqueue(std::move(job));
    }

    std::optional<ClientTerrainSceneRuntime::PendingTerrainUnload> ClientTerrainSceneRuntime::processNextPendingTerrainUnload()
    {
        while (true)
        {
            const std::optional<uint64_t> pendingKey = runtime_.takePendingTerrainUnload();
            if (!pendingKey)
            {
                return std::nullopt;
            }

            const uint64_t key = *pendingKey;
            if (runtime_.cancelPendingTerrainUnloadIfDesired(key))
            {
                continue;
            }

            RuntimeChunk* runtimeChunk = runtime_.worldRuntime.find(key);
            if (runtimeChunk != nullptr)
            {
                runtime_.enqueueSaveSnapshot(runtime_.makeSaveSnapshot(*runtimeChunk));
            }
            runtime_.finishPendingTerrainUnload(key);

            PendingTerrainUnload unload{};
            unload.key = key;
            return unload;
        }
    }

    void ClientTerrainSceneRuntime::resetLoadRequest()
    {
        runtime_.resetLoadRequest();
    }

    bool ClientTerrainSceneRuntime::gameSceneLoaded() const
    {
        return runtime_.gameSceneLoaded;
    }

    void ClientTerrainSceneRuntime::setGameSceneLoaded(bool loaded)
    {
        runtime_.gameSceneLoaded = loaded;
    }

    uint64_t ClientTerrainSceneRuntime::terrainGeneration() const
    {
        return runtime_.terrainGeneration.load();
    }
}
