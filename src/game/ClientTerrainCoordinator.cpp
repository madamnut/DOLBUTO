#include "game/ClientTerrainCoordinator.h"

#include "world/WorldRuntime.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace dolbuto::game
{
    namespace
    {
        int absInt(int value)
        {
            return value < 0 ? -value : value;
        }

        bool lightResolvedOrLater(ChunkGenState state)
        {
            return state == ChunkGenState::LightResolved ||
                state == ChunkGenState::Meshed;
        }

        bool terrainSourceReadyOrLater(ChunkGenState state)
        {
            return static_cast<int>(state) >= static_cast<int>(ChunkGenState::TerrainSourceReady);
        }

        bool localLightReadyOrLater(ChunkGenState state)
        {
            return static_cast<int>(state) >= static_cast<int>(ChunkGenState::LocalLightReady);
        }

        bool inSquareWithoutCorners(int dx, int dz, int radius)
        {
            const int ax = absInt(dx);
            const int az = absInt(dz);
            return ax <= radius && az <= radius && !(ax == radius && az == radius);
        }

        bool inCardinalNeighborhood(int dx, int dz)
        {
            return absInt(dx) + absInt(dz) <= 1;
        }

    }

    ClientTerrainCoordinator::ClientTerrainCoordinator(
        ClientWorldRuntime& runtime,
        uint64_t generation,
        ChunkLoadEnqueue enqueueChunkLoad,
        TerrainJobEnqueue enqueueTerrainJob,
        MeshReadyQuery meshReady) :
        runtime_(runtime),
        generation_(generation),
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

        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                runtime_.requestLightResolveTicket(chunkX + dx, chunkZ + dz, generation_, priority, enqueueChunkLoad_);
            }
        }
        for (int dz = -2; dz <= 2; ++dz)
        {
            for (int dx = -2; dx <= 2; ++dx)
            {
                if (!inSquareWithoutCorners(dx, dz, 2))
                {
                    continue;
                }
                runtime_.requestLocalLightTicket(chunkX + dx, chunkZ + dz, generation_, priority, enqueueChunkLoad_);
            }
        }
        for (int dz = -3; dz <= 3; ++dz)
        {
            for (int dx = -3; dx <= 3; ++dx)
            {
                if (!inSquareWithoutCorners(dx, dz, 3))
                {
                    continue;
                }
                requestTerrainSource(chunkX + dx, chunkZ + dz, priority);
            }
        }
        scheduleAround(chunkX, chunkZ, 3);
    }

    void ClientTerrainCoordinator::requestLightCascade(int chunkX, int chunkZ, uint32_t priority)
    {
        RuntimeChunk& chunk = runtime_.requestLightResolveTicket(chunkX, chunkZ, generation_, priority, enqueueChunkLoad_);

        if (lightResolvedOrLater(chunk.genState))
        {
            return;
        }

        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (!inCardinalNeighborhood(dx, dz))
                {
                    continue;
                }
                runtime_.requestLocalLightTicket(chunkX + dx, chunkZ + dz, generation_, priority, enqueueChunkLoad_);
            }
        }
        for (int dz = -2; dz <= 2; ++dz)
        {
            for (int dx = -2; dx <= 2; ++dx)
            {
                if (!inSquareWithoutCorners(dx, dz, 2))
                {
                    continue;
                }
                requestTerrainSource(chunkX + dx, chunkZ + dz, priority);
            }
        }
        scheduleAround(chunkX, chunkZ, 2);
    }

    void ClientTerrainCoordinator::resumeAfterChunkLoad(const CompletedChunkLoad& completed, const world::WorldRuntime::RuntimeChunkLoadState& loadState)
    {
        (void)loadState;
        const uint64_t key = world::WorldRuntime::chunkKey(completed.chunkX, completed.chunkZ);
        const RuntimeChunk* readyChunk = runtime_.worldRuntime.find(key);
        if (readyChunk == nullptr)
        {
            return;
        }

        if (!terrainSourceReadyOrLater(readyChunk->genState))
        {
            scheduleChunkIfReady(key);
            return;
        }

        queueLocalLightJobsAround(completed.chunkX, completed.chunkZ);
        if (localLightReadyOrLater(readyChunk->genState))
        {
            queueLightJobsAround(completed.chunkX, completed.chunkZ);
        }
        if (lightResolvedOrLater(readyChunk->genState))
        {
            queueMeshesAround(completed.chunkX, completed.chunkZ);
        }
    }

    void ClientTerrainCoordinator::queueLocalLightIfReady(uint64_t key)
    {
        std::optional<TerrainJob> job = runtime_.makeLocalLightJobIfReady(key, generation_);
        if (!job)
        {
            return;
        }

        enqueueTerrainJob_(std::move(*job));
    }

    void ClientTerrainCoordinator::queueLightResolveIfReady(uint64_t key)
    {
        std::optional<TerrainJob> job = runtime_.makeLightResolveJobIfReady(key, generation_);
        if (!job)
        {
            return;
        }

        enqueueTerrainJob_(std::move(*job));
    }

    void ClientTerrainCoordinator::scheduleChunkIfReady(uint64_t key)
    {
        const RuntimeChunk* chunk = runtime_.worldRuntime.find(key);
        if (chunk == nullptr)
        {
            return;
        }

        if (static_cast<int>(chunk->targetGenState) >= static_cast<int>(ChunkGenState::Meshed))
        {
            const uint64_t meshKey = world::WorldRuntime::chunkKey(chunk->chunkX, chunk->chunkZ);
            if (runtime_.desiredRenderChunks.find(meshKey) != runtime_.desiredRenderChunks.end())
            {
                queueMeshIfReady(chunk->chunkX, chunk->chunkZ);
            }
        }
        if (static_cast<int>(chunk->targetGenState) >= static_cast<int>(ChunkGenState::LightResolved))
        {
            queueLightResolveIfReady(key);
        }
        if (static_cast<int>(chunk->targetGenState) >= static_cast<int>(ChunkGenState::LocalLightReady))
        {
            queueLocalLightIfReady(key);
        }
        if (static_cast<int>(chunk->targetGenState) >= static_cast<int>(ChunkGenState::TerrainSourceReady))
        {
            RuntimeChunk* mutableChunk = runtime_.worldRuntime.find(key);
            if (mutableChunk != nullptr)
            {
                std::optional<TerrainJob> job = runtime_.makeTerrainSourceJobIfNeeded(*mutableChunk, generation_);
                if (job)
                {
                    enqueueTerrainJob_(std::move(*job));
                }
            }
        }
    }

    void ClientTerrainCoordinator::scheduleAround(int chunkX, int chunkZ, int radius)
    {
        for (int dz = -radius; dz <= radius; ++dz)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                scheduleChunkIfReady(world::WorldRuntime::chunkKey(chunkX + dx, chunkZ + dz));
            }
        }
    }

    void ClientTerrainCoordinator::queueLocalLightJobsAround(int chunkX, int chunkZ)
    {
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                queueLocalLightIfReady(world::WorldRuntime::chunkKey(chunkX + dx, chunkZ + dz));
            }
        }
    }

    void ClientTerrainCoordinator::queueLightJobsAround(int chunkX, int chunkZ)
    {
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (!inCardinalNeighborhood(dx, dz))
                {
                    continue;
                }
                queueLightResolveIfReady(world::WorldRuntime::chunkKey(chunkX + dx, chunkZ + dz));
            }
        }
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

    void ClientTerrainCoordinator::requestTerrainSource(int chunkX, int chunkZ, uint32_t priority)
    {
        RuntimeChunk& chunk = runtime_.ensureRuntimeChunk(chunkX, chunkZ, generation_, enqueueChunkLoad_);
        raiseTarget(chunk, ChunkGenState::TerrainSourceReady);
        chunk.sourceTicket = generation_;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);

        std::optional<TerrainJob> job = runtime_.makeTerrainSourceJobIfNeeded(chunk, generation_);
        if (!job)
        {
            return;
        }

        enqueueTerrainJob_(std::move(*job));
    }

    bool ClientTerrainCoordinator::chunkMeshReady(uint64_t key) const
    {
        return meshReady_ && meshReady_(key);
    }

    void ClientTerrainCoordinator::raiseTarget(RuntimeChunk& chunk, ChunkGenState target)
    {
        if (static_cast<int>(chunk.targetGenState) < static_cast<int>(target))
        {
            chunk.targetGenState = target;
        }
    }
}
