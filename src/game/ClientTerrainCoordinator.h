#pragma once

#include "game/ClientWorldRuntime.h"
#include "world/WorldTypes.h"

#include <cstdint>
#include <functional>

namespace dolbuto::game
{
    class ClientTerrainCoordinator
    {
    public:
        using ChunkLoadEnqueue = ClientWorldRuntime::ChunkLoadEnqueue;
        using TerrainJobEnqueue = std::function<void(TerrainJob)>;
        using MeshReadyQuery = std::function<bool(uint64_t)>;

        ClientTerrainCoordinator(
            ClientWorldRuntime& runtime,
            uint64_t generation,
            ChunkLoadEnqueue enqueueChunkLoad,
            TerrainJobEnqueue enqueueTerrainJob,
            MeshReadyQuery meshReady);

        RuntimeChunk& ensureRuntimeChunk(int chunkX, int chunkZ);
        void requestRenderCascade(int chunkX, int chunkZ, uint32_t priority);
        void requestMeshCascade(int chunkX, int chunkZ, uint32_t priority);
        void requestLightCascade(int chunkX, int chunkZ, uint32_t priority);
        void resumeAfterChunkLoad(const CompletedChunkLoad& completed, const world::WorldRuntime::RuntimeChunkLoadState& loadState);
        void queueLocalLightIfReady(uint64_t key);
        void queueLightResolveIfReady(uint64_t key);
        void scheduleChunkIfReady(uint64_t key);
        void scheduleAround(int chunkX, int chunkZ, int radius);
        void queueLocalLightJobsAround(int chunkX, int chunkZ);
        void queueLightJobsAround(int chunkX, int chunkZ);
        void queueMeshIfReady(int chunkX, int chunkZ);
        void queueMeshesAround(int chunkX, int chunkZ);

    private:
        void requestTerrainSource(int chunkX, int chunkZ, uint32_t priority);
        void raiseTarget(RuntimeChunk& chunk, ChunkGenState target);
        bool chunkMeshReady(uint64_t key) const;

        ClientWorldRuntime& runtime_;
        uint64_t generation_ = 0;
        ChunkLoadEnqueue enqueueChunkLoad_;
        TerrainJobEnqueue enqueueTerrainJob_;
        MeshReadyQuery meshReady_;
    };
}
