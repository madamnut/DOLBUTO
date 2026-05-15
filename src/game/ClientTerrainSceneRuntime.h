#pragma once

#include "game/ClientTerrainCompletionHandler.h"
#include "game/ClientWorldRuntime.h"
#include "world/TerrainBuilder.h"
#include "world/TerrainJobSystem.h"
#include "world/WorldTypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_set>

namespace dolbuto::game
{
    class ClientTerrainSceneRuntime
    {
    public:
        using MeshReadyPredicate = std::function<bool(uint64_t)>;
        using RenderMeshJobProcessor = std::function<world::TerrainJobResult(TerrainJob)>;

        struct TerrainLoadResult
        {
            bool requested = false;
            std::size_t droppedItemTrackingCapacity = 0;
            std::size_t terrainRenderCapacity = 0;
            const std::unordered_set<uint64_t>* desiredRenderChunks = nullptr;
        };

        struct PendingTerrainUnload
        {
            uint64_t key = 0;
        };

        explicit ClientTerrainSceneRuntime(ClientWorldRuntime& runtime);

        TerrainLoadResult requestTerrainLoad(
            int centerGroupChunkX,
            int centerGroupChunkZ,
            int loadGridScale,
            int loadGridUnitChunks,
            world::TerrainBuilderConfig terrainConfig,
            const MeshReadyPredicate& meshReady);

        ClientTerrainCompletionHandler::Result processCompletedTerrainJobs(
            uint32_t maxMeshUploads,
            world::TerrainBuilderConfig terrainConfig,
            const MeshReadyPredicate& meshReady,
            const ClientWorldRuntime::EntityNormalizer& normalizeEntity);

        void startTerrainWorkers(
            int terrainWorkerCount,
            world::TerrainBuilderConfig terrainConfig,
            const RenderMeshJobProcessor& processRenderMeshJob);
        void stopTerrainWorkers();
        void startChunkLoadWorker();
        void stopChunkLoadWorker();
        void startSaveWorker();
        void stopSaveWorker();
        std::optional<PendingTerrainUnload> processNextPendingTerrainUnload();
        void resetLoadRequest();
        bool gameSceneLoaded() const;
        void setGameSceneLoaded(bool loaded);
        uint64_t terrainGeneration() const;

    private:
        void enqueueChunkLoadJob(int chunkX, int chunkZ, uint64_t generation);
        void enqueueTerrainJob(TerrainJob job);

        ClientWorldRuntime& runtime_;
    };
}
