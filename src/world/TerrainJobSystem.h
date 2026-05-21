#pragma once

#include "world/WorldTypes.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace dolbuto::world
{
    struct TerrainJobResult
    {
        std::optional<CompletedChunkData> completedChunkData;
        std::shared_ptr<ChunkData> completedLocalLightChunk;
        std::shared_ptr<ChunkData> completedLightChunk;
        std::optional<CompletedChunkMesh> completedChunkMesh;
    };

    struct TerrainCompletedBatch
    {
        std::vector<CompletedChunkData> completedChunks;
        std::vector<std::shared_ptr<ChunkData>> completedLocalLightChunks;
        std::vector<std::shared_ptr<ChunkData>> completedLightChunks;
        std::vector<CompletedChunkMesh> completedMeshes;
    };

    class TerrainJobSystem
    {
    public:
        using GenerationProvider = std::function<uint64_t()>;
        using JobProcessor = std::function<TerrainJobResult(TerrainJob)>;
        using MeshDropPredicate = std::function<bool(const CompletedChunkMesh&)>;
        using MeshDrainPredicate = std::function<bool(const CompletedChunkMesh&)>;

        TerrainJobSystem() = default;
        ~TerrainJobSystem();

        TerrainJobSystem(const TerrainJobSystem&) = delete;
        TerrainJobSystem& operator=(const TerrainJobSystem&) = delete;

        void start(int workerCount, GenerationProvider generationProvider, JobProcessor jobProcessor);
        void stop();

        void clearQueuedJobsAndMeshes();
        void enqueue(TerrainJob job);
        TerrainCompletedBatch drainCompleted(const MeshDropPredicate& shouldDropMesh, const MeshDrainPredicate& canDrainMesh);
        TerrainCompletedBatch drainForShutdown();

    private:
        void workerLoop();
        bool popNextJob(TerrainJob& job);

        GenerationProvider generationProvider_;
        JobProcessor jobProcessor_;
        std::vector<std::thread> workers_;
        std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<TerrainJob> featureJobs_;
        std::deque<TerrainJob> finalizeJobs_;
        std::deque<TerrainJob> lightJobs_;
        std::deque<TerrainJob> meshJobs_;
        std::deque<CompletedChunkData> completedChunkData_;
        std::deque<std::shared_ptr<ChunkData>> completedLocalLightChunks_;
        std::deque<std::shared_ptr<ChunkData>> completedLightChunks_;
        std::deque<CompletedChunkMesh> completedChunkMeshes_;
        uint64_t jobSequence_ = 0;
        bool stopRequested_ = false;
    };
}
