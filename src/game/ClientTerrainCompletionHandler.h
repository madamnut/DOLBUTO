#pragma once

#include "game/ClientTerrainCoordinator.h"
#include "game/ClientWorldRuntime.h"
#include "world/WorldTypes.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace dolbuto::game
{
    class ClientTerrainCompletionHandler
    {
    public:
        struct Result
        {
            std::vector<uint64_t> refreshDroppedItemChunkKeys;
            std::vector<CompletedChunkMesh> meshesToInstall;
            double loadHandleMs = 0.0;
            double sourceHandleMs = 0.0;
            double localLightHandleMs = 0.0;
            double lightHandleMs = 0.0;
            double meshHandleMs = 0.0;
            double saveQueueMs = 0.0;
            double loadFinishMs = 0.0;
            double loadSnapshotMs = 0.0;
            double loadInstallMs = 0.0;
            double loadResumeMs = 0.0;
            double popMs = 0.0;
            double handleMs = 0.0;
            uint32_t popCount = 0;
            uint32_t terrainCount = 0;
            uint32_t loadCount = 0;
            uint32_t buildMeshCount = 0;
            bool terrainStatsDirty = false;
        };

        ClientTerrainCompletionHandler(
            ClientWorldRuntime& runtime,
            ClientTerrainCoordinator& coordinator);

        Result handle(ClientWorldRuntime::CompletedWorkBatch work, const ClientWorldRuntime::EntityNormalizer& normalizeEntity);

    private:
        void handleCompletedLoads(
            std::vector<PreparedChunkLoad>& completedLoads,
            uint64_t generation,
            const ClientWorldRuntime::EntityNormalizer& normalizeEntity,
            Result& result);
        void handleCompletedChunks(std::vector<CompletedChunkData>& completedChunks, Result& result);
        void handleCompletedLocalLightChunks(std::vector<std::shared_ptr<ChunkData>>& completedLocalLightChunks, Result& result);
        void handleCompletedLightChunks(std::vector<std::shared_ptr<ChunkData>>& completedLightChunks, Result& result);
        void handleCompletedMeshes(std::vector<CompletedChunkMesh>& completedMeshes, Result& result);

        ClientWorldRuntime& runtime_;
        ClientTerrainCoordinator& coordinator_;
    };
}
