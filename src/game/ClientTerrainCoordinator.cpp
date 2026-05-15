#include "game/ClientTerrainCoordinator.h"

#include "world/WorldRuntime.h"

#include <array>
#include <optional>
#include <utility>

namespace dolbuto::game
{
    namespace
    {
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
    }

    ClientTerrainCoordinator::ClientTerrainCoordinator(
        ClientWorldRuntime& runtime,
        uint64_t generation,
        world::TerrainBuilderConfig terrainConfig,
        ChunkLoadEnqueue enqueueChunkLoad,
        TerrainJobEnqueue enqueueTerrainJob,
        MeshReadyQuery meshReady) :
        runtime_(runtime),
        generation_(generation),
        terrainConfig_(terrainConfig),
        enqueueChunkLoad_(std::move(enqueueChunkLoad)),
        enqueueTerrainJob_(std::move(enqueueTerrainJob)),
        meshReady_(std::move(meshReady))
    {
    }

    RuntimeChunk& ClientTerrainCoordinator::ensureRuntimeChunk(int chunkX, int chunkZ)
    {
        return runtime_.ensureRuntimeChunk(chunkX, chunkZ, generation_, enqueueChunkLoad_);
    }

    void ClientTerrainCoordinator::requestRenderCascade(int chunkX, int chunkZ, uint32_t priority)
    {
        const uint64_t key = world::WorldRuntime::chunkKey(chunkX, chunkZ);
        runtime_.requestRenderTicket(chunkX, chunkZ, generation_, priority, enqueueChunkLoad_);

        if (chunkMeshReady(key))
        {
            return;
        }

        requestMeshCascade(chunkX, chunkZ, priority);
    }

    void ClientTerrainCoordinator::requestMeshCascade(int chunkX, int chunkZ, uint32_t priority)
    {
        const uint64_t key = world::WorldRuntime::chunkKey(chunkX, chunkZ);
        runtime_.requestMeshTicket(chunkX, chunkZ, generation_, priority, enqueueChunkLoad_);

        if (chunkMeshReady(key))
        {
            return;
        }

        requestFullCascade(chunkX, chunkZ, priority);
        queueMeshIfReady(chunkX, chunkZ);
    }

    void ClientTerrainCoordinator::requestFullCascade(int chunkX, int chunkZ, uint32_t priority)
    {
        const uint64_t key = world::WorldRuntime::chunkKey(chunkX, chunkZ);
        RuntimeChunk& chunk = runtime_.requestFullTicket(chunkX, chunkZ, generation_, priority, enqueueChunkLoad_);

        if (chunk.genState == ChunkGenState::Full || chunk.genState == ChunkGenState::Meshed)
        {
            return;
        }

        requestFeaturingCascade(chunkX, chunkZ, priority);
        for (const FeatureNeighborOffset& offset : FeatureNeighborOffsets)
        {
            requestFeaturingCascade(chunkX + offset.x, chunkZ + offset.z, priority);
        }

        RuntimeChunk* target = runtime_.worldRuntime.find(key);
        if (target != nullptr &&
            (target->genState == ChunkGenState::Featuring ||
                target->genState == ChunkGenState::Full ||
                target->genState == ChunkGenState::Meshed))
        {
            publishFeatureSlots(*target);
        }
        queueFeatureFinalizeIfReady(key);
    }

    void ClientTerrainCoordinator::requestFeaturingCascade(int chunkX, int chunkZ, uint32_t priority)
    {
        RuntimeChunk& chunk = runtime_.requestFeaturingTicket(chunkX, chunkZ, generation_, priority, enqueueChunkLoad_);
        if (runtime_.shouldPublishFeatures(chunk, generation_))
        {
            publishFeatureSlots(chunk);
            return;
        }

        std::optional<TerrainJob> job = runtime_.makeBuildFeaturingJobIfNeeded(chunk, generation_);
        if (!job)
        {
            return;
        }

        enqueueTerrainJob_(std::move(*job));
    }

    void ClientTerrainCoordinator::resumeAfterChunkLoad(const CompletedChunkLoad& completed, const world::WorldRuntime::RuntimeChunkLoadState& loadState)
    {
        const uint64_t key = world::WorldRuntime::chunkKey(completed.chunkX, completed.chunkZ);
        if (runtime_.isRenderDesired(key))
        {
            requestRenderCascade(completed.chunkX, completed.chunkZ, loadState.bestPriority);
        }
        else if (loadState.meshTicket == generation_)
        {
            requestMeshCascade(completed.chunkX, completed.chunkZ, loadState.bestPriority);
        }
        else if (loadState.fullTicket == generation_)
        {
            requestFullCascade(completed.chunkX, completed.chunkZ, loadState.bestPriority);
        }
        else if (loadState.featuringTicket == generation_)
        {
            requestFeaturingCascade(completed.chunkX, completed.chunkZ, loadState.bestPriority);
        }

        const RuntimeChunk* readyChunk = runtime_.worldRuntime.find(key);
        if (readyChunk != nullptr &&
            readyChunk->data &&
            (readyChunk->genState == ChunkGenState::Full ||
                readyChunk->genState == ChunkGenState::Meshed))
        {
            queueMeshesAround(completed.chunkX, completed.chunkZ);
        }
    }

    void ClientTerrainCoordinator::applyFeaturePropagationResult(const ClientWorldRuntime::FeaturePropagationResult& result)
    {
        for (uint64_t key : result.finalizeKeys)
        {
            queueFeatureFinalizeIfReady(key);
        }
        for (const ClientWorldRuntime::ChunkCoordinate& center : result.meshCenters)
        {
            queueMeshesAround(center.x, center.z);
        }
    }

    void ClientTerrainCoordinator::publishFeatureSlots(RuntimeChunk& sourceChunk)
    {
        applyFeaturePropagationResult(runtime_.publishFeatureSlots(sourceChunk, generation_, terrainConfig_));
    }

    void ClientTerrainCoordinator::queueFeatureFinalizeIfReady(uint64_t key)
    {
        std::optional<TerrainJob> job = runtime_.makeFeatureFinalizeJobIfReady(key, generation_);
        if (!job)
        {
            return;
        }

        enqueueTerrainJob_(std::move(*job));
    }

    void ClientTerrainCoordinator::queueMeshIfReady(int chunkX, int chunkZ)
    {
        const uint64_t key = world::WorldRuntime::chunkKey(chunkX, chunkZ);
        std::optional<TerrainJob> job = runtime_.makeMeshJobIfReady(chunkX, chunkZ, generation_, chunkMeshReady(key));
        if (!job)
        {
            return;
        }

        enqueueTerrainJob_(std::move(*job));
    }

    void ClientTerrainCoordinator::queueMeshesAround(int chunkX, int chunkZ)
    {
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                queueMeshIfReady(chunkX + dx, chunkZ + dz);
            }
        }
    }

    bool ClientTerrainCoordinator::chunkMeshReady(uint64_t key) const
    {
        return meshReady_ && meshReady_(key);
    }
}
