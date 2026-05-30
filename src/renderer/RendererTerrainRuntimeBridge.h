#pragma once

#include "world/TerrainBuilder.h"
#include "world/TerrainJobSystem.h"
#include "world/WorldTypes.h"

#include <cstdint>

namespace dolbuto
{
    class DebugOverlayText;
    class ParticleRenderPath;
    class TerrainRenderPath;
    struct RendererAssetStore;
    namespace game
    {
        struct ClientRuntimeState;
    }

    class RendererTerrainRuntimeBridge
    {
    public:
        RendererTerrainRuntimeBridge(
            game::ClientRuntimeState& client,
            RendererAssetStore& rendererAssets,
            TerrainRenderPath& terrainRenderPath,
            ParticleRenderPath& particleRenderPath,
            DebugOverlayText& debugOverlayText);

        void updateLoadedChunks(DVec3 playerPosition);
        void requestTerrainLoad(int centerGroupChunkX, int centerGroupChunkZ);
        world::TerrainJobResult processRenderTerrainMeshJob(TerrainJob job);
        void processCompletedTerrainJobs();
        uint32_t processPendingTerrainUnloads();
        void processRetiredTerrainChunks();
        void markRuntimeChunkDataDirty(RuntimeChunk& chunk);
        world::TerrainBuilderConfig terrainBuilderConfig() const;
        bool setBlockAtWorld(int x, int y, int z, uint16_t block);
        void tickFluidSimulation();
        void rebuildSubchunkMeshNow(int chunkX, int chunkZ, int subchunkY);
        void rebuildEditedChunkMeshes(int blockX, int blockY, int blockZ);
        bool chunkMeshReady(uint64_t key) const;
        void destroyAllTerrainChunks();
        void updateTerrainStats();

    private:
        void refreshFireEmittersForChunk(int chunkX, int chunkZ);
        uint16_t fireBlockId() const;

        game::ClientRuntimeState& client_;
        RendererAssetStore& rendererAssets_;
        TerrainRenderPath& terrainRenderPath_;
        ParticleRenderPath& particleRenderPath_;
        DebugOverlayText& debugOverlayText_;
    };
}
