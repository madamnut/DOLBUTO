#pragma once

#include "game/ClientTerrainCoordinator.h"
#include "game/ClientWorldRuntime.h"
#include "world/TerrainBuilder.h"
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
            bool terrainStatsDirty = false;
        };

        ClientTerrainCompletionHandler(
            ClientWorldRuntime& runtime,
            ClientTerrainCoordinator& coordinator,
            world::TerrainBuilderConfig terrainConfig);

        Result handle(ClientWorldRuntime::CompletedWorkBatch work, const ClientWorldRuntime::EntityNormalizer& normalizeEntity);

    private:
        void handleCompletedLoads(
            std::vector<CompletedChunkLoad>& completedLoads,
            uint64_t generation,
            const ClientWorldRuntime::EntityNormalizer& normalizeEntity,
            Result& result);
        void handleCompletedChunks(std::vector<CompletedChunkData>& completedChunks, Result& result);
        void handleCompletedMergedChunks(std::vector<std::shared_ptr<ChunkData>>& completedMergedChunks, Result& result);
        void handleCompletedMeshes(std::vector<CompletedChunkMesh>& completedMeshes, Result& result);

        ClientWorldRuntime& runtime_;
        ClientTerrainCoordinator& coordinator_;
        world::TerrainBuilderConfig terrainConfig_;
    };
}
