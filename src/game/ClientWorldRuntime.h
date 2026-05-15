#pragma once

#include "save/SaveSystem.h"
#include "world/ChunkLoadSystem.h"
#include "world/TerrainBuilder.h"
#include "world/TerrainJobSystem.h"
#include "world/WorldRuntime.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace dolbuto::game
{
    class ClientWorldRuntime
    {
    public:
        using ChunkLoadEnqueue = std::function<void(int, int, uint64_t)>;
        using EntityNormalizer = std::function<void(WorldEntity&)>;

        struct ChunkOffset
        {
            int x = 0;
            int z = 0;
        };

        struct TerrainLoadPlan
        {
            uint64_t generation = 0;
            int loadedChunkDiameter = 0;
            int renderMin = 0;
            int renderMax = 0;
            int runtimeKeepMin = 0;
            int runtimeKeepMax = 0;
            std::size_t runtimeCapacity = 0;
            std::size_t featureCapacity = 0;
            std::size_t renderCapacity = 0;
        };

        enum class CompletedChunkDecision
        {
            Install,
            Save,
            Ignore
        };

        enum class CompletedMeshDecision
        {
            Install,
            Retry,
            Ignore
        };

        struct CompletedWorkBatch
        {
            uint64_t generation = 0;
            world::TerrainCompletedBatch terrain;
            std::vector<CompletedChunkLoad> completedLoads;
        };

        struct ChunkLoadCompletion
        {
            RuntimeChunk* chunk = nullptr;
            world::WorldRuntime::RuntimeChunkLoadState loadState;
            bool desired = false;
        };

        struct ChunkCoordinate
        {
            int x = 0;
            int z = 0;
        };

        struct FeaturePropagationResult
        {
            std::vector<ChunkCoordinate> meshCenters;
            std::vector<uint64_t> finalizeKeys;
        };

        std::filesystem::path activeWorldDirectory;
        uint64_t activeWorldSeed = 0;
        int activeWorldSeedSalt = 0;
        int loadedChunkDiameter = 0;
        int loadedCenterGroupChunkX = 0;
        int loadedCenterGroupChunkZ = 0;
        bool terrainLoadRequested = false;
        bool gameSceneLoaded = false;
        std::atomic<uint64_t> terrainGeneration{0};
        int loadOrderDiameter = 0;
        std::vector<ChunkOffset> loadOrder;

        std::unordered_set<uint64_t> desiredTerrainChunks;
        std::unordered_set<uint64_t> desiredFeatureChunks;
        std::unordered_set<uint64_t> desiredRenderChunks;
        std::unordered_set<uint64_t> requestedChunkJobs;
        std::unordered_set<uint64_t> requestedMeshJobs;
        std::unordered_set<uint64_t> pendingUnloadSet;
        std::deque<uint64_t> pendingUnloadChunks;

        world::WorldRuntime worldRuntime;
        save::SaveSystem saveSystem;
        world::ChunkLoadSystem chunkLoadSystem;
        world::TerrainJobSystem terrainJobSystem;

        void setActiveWorld(std::filesystem::path worldDirectory, uint64_t worldSeed);
        void resetLoadRequest();
        void resetSceneRuntime();
        TerrainLoadPlan beginTerrainLoadRequest(int centerGroupChunkX, int centerGroupChunkZ, int loadGridScale, int loadGridUnitChunks);
        void rebuildLoadOrderIfNeeded();
        void resetRuntimePriorities();
        void collectPendingUnloadsOutsideDesired();
        bool isTerrainDesired(uint64_t key) const;
        bool isRenderDesired(uint64_t key) const;
        CompletedWorkBatch drainCompletedWork(uint32_t maxMeshUploads);

        RuntimeChunk& ensureRuntimeChunk(int chunkX, int chunkZ, uint64_t generation, const ChunkLoadEnqueue& enqueueChunkLoad);
        RuntimeChunk& requestRenderTicket(int chunkX, int chunkZ, uint64_t generation, uint32_t priority, const ChunkLoadEnqueue& enqueueChunkLoad);
        RuntimeChunk& requestMeshTicket(int chunkX, int chunkZ, uint64_t generation, uint32_t priority, const ChunkLoadEnqueue& enqueueChunkLoad);
        RuntimeChunk& requestFullTicket(int chunkX, int chunkZ, uint64_t generation, uint32_t priority, const ChunkLoadEnqueue& enqueueChunkLoad);
        RuntimeChunk& requestFeaturingTicket(int chunkX, int chunkZ, uint64_t generation, uint32_t priority, const ChunkLoadEnqueue& enqueueChunkLoad);
        bool shouldPublishFeatures(const RuntimeChunk& chunk, uint64_t generation) const;
        std::optional<TerrainJob> makeBuildFeaturingJobIfNeeded(RuntimeChunk& chunk, uint64_t generation);
        std::optional<TerrainJob> makeFeatureFinalizeJobIfReady(uint64_t key, uint64_t generation);
        std::optional<TerrainJob> makeMeshJobIfReady(int chunkX, int chunkZ, uint64_t generation, bool meshAlreadyReady);
        ChunkLoadCompletion finishChunkLoad(const CompletedChunkLoad& completed);
        SaveChunkSnapshot makeSaveSnapshot(const RuntimeChunk& chunk) const;
        void enqueueSaveSnapshot(SaveChunkSnapshot snapshot);
        void enqueueChunkDataSnapshot(const std::shared_ptr<ChunkData>& chunk, ChunkGenState genState);
        void enqueueSaveAllRuntimeChunks();
        void enqueueCompletedTerrainSnapshots(world::TerrainCompletedBatch& completed);
        RuntimeChunk runtimeChunkFromSnapshot(const SaveChunkSnapshot& snapshot, uint64_t generation, const EntityNormalizer& normalizeEntity);
        void mergeLoadStateIncomingFeatures(RuntimeChunk& loaded, const world::WorldRuntime::RuntimeChunkLoadState& loadState, const world::TerrainBuilderConfig& terrainConfig) const;
        FeaturePropagationResult acceptFeatureSlot(int targetChunkX, int targetChunkZ, size_t sourceSlot, FeatureWriteListPtr writes, const world::TerrainBuilderConfig& terrainConfig);
        FeaturePropagationResult acceptSavedFeaturingChunkFeatures(const CompletedChunkData& completed, const world::TerrainBuilderConfig& terrainConfig);
        FeaturePropagationResult publishFeatureSlots(RuntimeChunk& sourceChunk, uint64_t generation, const world::TerrainBuilderConfig& terrainConfig);
        CompletedChunkDecision decideCompletedChunk(const ChunkData& chunk) const;
        CompletedMeshDecision decideCompletedMesh(const CompletedChunkMesh& mesh) const;
        std::optional<uint64_t> takePendingTerrainUnload();
        bool cancelPendingTerrainUnloadIfDesired(uint64_t key);
        void finishPendingTerrainUnload(uint64_t key);
        void clearRequestedChunkJob(uint64_t key);
        void clearRequestedMeshJob(uint64_t key);
        void clearTerrainRequests(uint64_t key);
    };
}
