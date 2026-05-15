#pragma once

#include "game/ClientWorldRuntime.h"
#include "world/TerrainBuilder.h"
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
            world::TerrainBuilderConfig terrainConfig,
            ChunkLoadEnqueue enqueueChunkLoad,
            TerrainJobEnqueue enqueueTerrainJob,
            MeshReadyQuery meshReady);

        RuntimeChunk& ensureRuntimeChunk(int chunkX, int chunkZ);
        void requestRenderCascade(int chunkX, int chunkZ, uint32_t priority);
        void requestMeshCascade(int chunkX, int chunkZ, uint32_t priority);
        void requestFullCascade(int chunkX, int chunkZ, uint32_t priority);
        void requestFeaturingCascade(int chunkX, int chunkZ, uint32_t priority);
        void resumeAfterChunkLoad(const CompletedChunkLoad& completed, const world::WorldRuntime::RuntimeChunkLoadState& loadState);
        void applyFeaturePropagationResult(const ClientWorldRuntime::FeaturePropagationResult& result);
        void publishFeatureSlots(RuntimeChunk& sourceChunk);
        void queueFeatureFinalizeIfReady(uint64_t key);
        void queueMeshIfReady(int chunkX, int chunkZ);
        void queueMeshesAround(int chunkX, int chunkZ);

    private:
        bool chunkMeshReady(uint64_t key) const;

        ClientWorldRuntime& runtime_;
        uint64_t generation_ = 0;
        world::TerrainBuilderConfig terrainConfig_;
        ChunkLoadEnqueue enqueueChunkLoad_;
        TerrainJobEnqueue enqueueTerrainJob_;
        MeshReadyQuery meshReady_;
    };
}
