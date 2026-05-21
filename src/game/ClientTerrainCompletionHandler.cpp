#include "game/ClientTerrainCompletionHandler.h"

#include "world/WorldRuntime.h"

#include <chrono>
#include <memory>
#include <utility>

namespace dolbuto::game
{
    namespace
    {
        double millisecondsSince(const std::chrono::steady_clock::time_point& start)
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
    }

    ClientTerrainCompletionHandler::ClientTerrainCompletionHandler(
        ClientWorldRuntime& runtime,
        ClientTerrainCoordinator& coordinator) :
        runtime_(runtime),
        coordinator_(coordinator)
    {
    }

    ClientTerrainCompletionHandler::Result ClientTerrainCompletionHandler::handle(
        ClientWorldRuntime::CompletedWorkBatch work,
        const ClientWorldRuntime::EntityNormalizer& normalizeEntity)
    {
        Result result{};
        result.terrainStatsDirty =
            !work.terrain.completedChunks.empty() ||
            !work.terrain.completedLocalLightChunks.empty() ||
            !work.terrain.completedLightChunks.empty() ||
            !work.terrain.completedMeshes.empty();

        auto sectionStart = std::chrono::steady_clock::now();
        handleCompletedLoads(work.completedLoads, work.generation, normalizeEntity, result);
        result.loadHandleMs = millisecondsSince(sectionStart);

        sectionStart = std::chrono::steady_clock::now();
        handleCompletedChunks(work.terrain.completedChunks, result);
        result.sourceHandleMs = millisecondsSince(sectionStart);

        sectionStart = std::chrono::steady_clock::now();
        handleCompletedLocalLightChunks(work.terrain.completedLocalLightChunks, result);
        result.localLightHandleMs = millisecondsSince(sectionStart);

        sectionStart = std::chrono::steady_clock::now();
        handleCompletedLightChunks(work.terrain.completedLightChunks, result);
        result.lightHandleMs = millisecondsSince(sectionStart);

        sectionStart = std::chrono::steady_clock::now();
        handleCompletedMeshes(work.terrain.completedMeshes, result);
        result.meshHandleMs = millisecondsSince(sectionStart);
        return result;
    }

    void ClientTerrainCompletionHandler::handleCompletedLoads(
        std::vector<PreparedChunkLoad>& completedLoads,
        uint64_t generation,
        const ClientWorldRuntime::EntityNormalizer& normalizeEntity,
        Result& result)
    {
        for (PreparedChunkLoad& prepared : completedLoads)
        {
            CompletedChunkLoad& completed = prepared.completed;
            const uint64_t key = world::WorldRuntime::chunkKey(completed.chunkX, completed.chunkZ);
            auto sectionStart = std::chrono::steady_clock::now();
            ClientWorldRuntime::ChunkLoadCompletion loadCompletion = runtime_.finishChunkLoad(completed);
            result.loadFinishMs += millisecondsSince(sectionStart);
            if (loadCompletion.chunk == nullptr)
            {
                continue;
            }

            const world::WorldRuntime::RuntimeChunkLoadState loadState = loadCompletion.loadState;
            if (!loadCompletion.desired)
            {
                continue;
            }

            if (prepared.preparedChunk)
            {
                sectionStart = std::chrono::steady_clock::now();
                RuntimeChunk loaded = std::move(*prepared.preparedChunk);
                if (loaded.data)
                {
                    loaded.data->generation = generation;
                    for (WorldEntity& entity : loaded.data->entities)
                    {
                        if (normalizeEntity)
                        {
                            normalizeEntity(entity);
                        }
                    }
                }
                if (loaded.genState == ChunkGenState::LightResolved)
                {
                    runtime_.saveSystem.markClean(loaded.chunkX, loaded.chunkZ, loaded.data ? loaded.data->revision : 0);
                }
                result.loadSnapshotMs += millisecondsSince(sectionStart);
                sectionStart = std::chrono::steady_clock::now();
                runtime_.worldRuntime.installLoadedChunk(std::move(loaded), loadState);
                result.loadInstallMs += millisecondsSince(sectionStart);
                result.refreshDroppedItemChunkKeys.push_back(key);
            }

            sectionStart = std::chrono::steady_clock::now();
            coordinator_.resumeAfterChunkLoad(completed, loadState);
            result.loadResumeMs += millisecondsSince(sectionStart);
        }
    }

    void ClientTerrainCompletionHandler::handleCompletedChunks(std::vector<CompletedChunkData>& completedChunks, Result& result)
    {
        for (CompletedChunkData& completed : completedChunks)
        {
            const std::shared_ptr<ChunkData>& chunk = completed.chunk;
            if (!chunk)
            {
                continue;
            }

            const uint64_t key = world::WorldRuntime::chunkKey(chunk->chunkX, chunk->chunkZ);
            runtime_.clearRequestedChunkJob(key);
            const ClientWorldRuntime::CompletedChunkDecision decision = runtime_.decideCompletedChunk(*chunk);
            if (decision == ClientWorldRuntime::CompletedChunkDecision::Ignore)
            {
                continue;
            }

            if (decision == ClientWorldRuntime::CompletedChunkDecision::Save)
            {
                const auto saveStart = std::chrono::steady_clock::now();
                runtime_.enqueueChunkDataSnapshot(chunk, ChunkGenState::TerrainSourceReady);
                result.saveQueueMs += millisecondsSince(saveStart);
                continue;
            }

            RuntimeChunk& runtimeChunk = runtime_.worldRuntime.installTerrainSourceChunk(chunk, std::move(completed.outgoingFeatureSlots));
            result.refreshDroppedItemChunkKeys.push_back(key);
            (void)runtimeChunk;
            coordinator_.queueLocalLightJobsAround(chunk->chunkX, chunk->chunkZ);
        }
    }

    void ClientTerrainCompletionHandler::handleCompletedLocalLightChunks(std::vector<std::shared_ptr<ChunkData>>& completedLocalLightChunks, Result& result)
    {
        for (const std::shared_ptr<ChunkData>& chunk : completedLocalLightChunks)
        {
            if (!chunk)
            {
                continue;
            }

            const uint64_t key = world::WorldRuntime::chunkKey(chunk->chunkX, chunk->chunkZ);
            const ClientWorldRuntime::CompletedChunkDecision decision = runtime_.decideCompletedChunk(*chunk);
            if (decision == ClientWorldRuntime::CompletedChunkDecision::Ignore)
            {
                continue;
            }

            if (decision == ClientWorldRuntime::CompletedChunkDecision::Save)
            {
                const auto saveStart = std::chrono::steady_clock::now();
                runtime_.enqueueChunkDataSnapshot(chunk, ChunkGenState::LocalLightReady);
                result.saveQueueMs += millisecondsSince(saveStart);
                continue;
            }

            runtime_.worldRuntime.installLocalLightChunk(chunk);
            result.refreshDroppedItemChunkKeys.push_back(key);
            coordinator_.queueLightJobsAround(chunk->chunkX, chunk->chunkZ);
        }
    }

    void ClientTerrainCompletionHandler::handleCompletedLightChunks(std::vector<std::shared_ptr<ChunkData>>& completedLightChunks, Result& result)
    {
        for (const std::shared_ptr<ChunkData>& chunk : completedLightChunks)
        {
            if (!chunk)
            {
                continue;
            }

            const uint64_t key = world::WorldRuntime::chunkKey(chunk->chunkX, chunk->chunkZ);
            const ClientWorldRuntime::CompletedChunkDecision decision = runtime_.decideCompletedChunk(*chunk);
            if (decision == ClientWorldRuntime::CompletedChunkDecision::Ignore)
            {
                continue;
            }

            if (decision == ClientWorldRuntime::CompletedChunkDecision::Save)
            {
                const auto saveStart = std::chrono::steady_clock::now();
                runtime_.enqueueChunkDataSnapshot(chunk, ChunkGenState::LightResolved);
                result.saveQueueMs += millisecondsSince(saveStart);
                continue;
            }

            runtime_.worldRuntime.installLightResolvedChunk(chunk);
            result.refreshDroppedItemChunkKeys.push_back(key);
            coordinator_.queueMeshesAround(chunk->chunkX, chunk->chunkZ);
        }
    }

    void ClientTerrainCompletionHandler::handleCompletedMeshes(std::vector<CompletedChunkMesh>& completedMeshes, Result& result)
    {
        for (CompletedChunkMesh& mesh : completedMeshes)
        {
            const uint64_t key = world::WorldRuntime::chunkKey(mesh.chunkX, mesh.chunkZ);
            runtime_.clearRequestedMeshJob(key);

            const ClientWorldRuntime::CompletedMeshDecision decision = runtime_.decideCompletedMesh(mesh);
            if (decision == ClientWorldRuntime::CompletedMeshDecision::Ignore)
            {
                continue;
            }
            if (decision == ClientWorldRuntime::CompletedMeshDecision::Retry)
            {
                coordinator_.queueMeshIfReady(mesh.chunkX, mesh.chunkZ);
                continue;
            }

            result.meshesToInstall.push_back(std::move(mesh));
        }
    }
}
