#include "game/ClientTerrainCompletionHandler.h"

#include "world/WorldRuntime.h"

#include <memory>
#include <utility>

namespace dolbuto::game
{
    ClientTerrainCompletionHandler::ClientTerrainCompletionHandler(
        ClientWorldRuntime& runtime,
        ClientTerrainCoordinator& coordinator,
        world::TerrainBuilderConfig terrainConfig) :
        runtime_(runtime),
        coordinator_(coordinator),
        terrainConfig_(terrainConfig)
    {
    }

    ClientTerrainCompletionHandler::Result ClientTerrainCompletionHandler::handle(
        ClientWorldRuntime::CompletedWorkBatch work,
        const ClientWorldRuntime::EntityNormalizer& normalizeEntity)
    {
        Result result{};
        result.terrainStatsDirty =
            !work.terrain.completedChunks.empty() ||
            !work.terrain.completedMergedChunks.empty() ||
            !work.terrain.completedMeshes.empty();

        handleCompletedLoads(work.completedLoads, work.generation, normalizeEntity, result);
        handleCompletedChunks(work.terrain.completedChunks, result);
        handleCompletedMergedChunks(work.terrain.completedMergedChunks, result);
        handleCompletedMeshes(work.terrain.completedMeshes, result);
        return result;
    }

    void ClientTerrainCompletionHandler::handleCompletedLoads(
        std::vector<CompletedChunkLoad>& completedLoads,
        uint64_t generation,
        const ClientWorldRuntime::EntityNormalizer& normalizeEntity,
        Result& result)
    {
        for (CompletedChunkLoad& completed : completedLoads)
        {
            const uint64_t key = world::WorldRuntime::chunkKey(completed.chunkX, completed.chunkZ);
            ClientWorldRuntime::ChunkLoadCompletion loadCompletion = runtime_.finishChunkLoad(completed);
            if (loadCompletion.chunk == nullptr)
            {
                continue;
            }

            const world::WorldRuntime::RuntimeChunkLoadState loadState = loadCompletion.loadState;
            if (!loadCompletion.desired)
            {
                continue;
            }

            if (completed.snapshot)
            {
                RuntimeChunk loaded = runtime_.runtimeChunkFromSnapshot(*completed.snapshot, generation, normalizeEntity);
                runtime_.mergeLoadStateIncomingFeatures(loaded, loadState, terrainConfig_);
                runtime_.worldRuntime.installLoadedChunk(std::move(loaded), loadState);
                result.refreshDroppedItemChunkKeys.push_back(key);
            }

            coordinator_.resumeAfterChunkLoad(completed, loadState);
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
                runtime_.enqueueChunkDataSnapshot(chunk, ChunkGenState::Featuring);
                coordinator_.applyFeaturePropagationResult(runtime_.acceptSavedFeaturingChunkFeatures(completed, terrainConfig_));
                continue;
            }

            RuntimeChunk& runtimeChunk = runtime_.worldRuntime.installFeaturingChunk(chunk, std::move(completed.outgoingFeatureSlots));
            result.refreshDroppedItemChunkKeys.push_back(key);
            coordinator_.publishFeatureSlots(runtimeChunk);
            coordinator_.queueFeatureFinalizeIfReady(key);
            coordinator_.queueMeshesAround(chunk->chunkX, chunk->chunkZ);
        }
    }

    void ClientTerrainCompletionHandler::handleCompletedMergedChunks(std::vector<std::shared_ptr<ChunkData>>& completedMergedChunks, Result& result)
    {
        for (const std::shared_ptr<ChunkData>& chunk : completedMergedChunks)
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
                runtime_.enqueueChunkDataSnapshot(chunk, ChunkGenState::Full);
                continue;
            }

            runtime_.worldRuntime.installFullChunk(chunk);
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
