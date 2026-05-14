#include "world/TerrainJobSystem.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace dolbuto::world
{
    namespace
    {
        int stageRank(TerrainJob::Type type)
        {
            switch (type)
            {
            case TerrainJob::Type::BuildChunkMesh:
                return 0;
            case TerrainJob::Type::FinalizeFeatures:
                return 1;
            case TerrainJob::Type::BuildFeaturing:
                return 2;
            }
            return 3;
        }

        bool jobLess(const TerrainJob& left, const TerrainJob& right)
        {
            if (left.priority != right.priority)
            {
                return left.priority < right.priority;
            }
            const int leftStage = stageRank(left.type);
            const int rightStage = stageRank(right.type);
            if (leftStage != rightStage)
            {
                return leftStage < rightStage;
            }
            return left.sequence < right.sequence;
        }
    }

    TerrainJobSystem::~TerrainJobSystem()
    {
        stop();
    }

    void TerrainJobSystem::start(int workerCount, GenerationProvider generationProvider, JobProcessor jobProcessor)
    {
        stop();
        generationProvider_ = std::move(generationProvider);
        jobProcessor_ = std::move(jobProcessor);
        stopRequested_ = false;
        workers_.reserve(static_cast<size_t>(std::max(0, workerCount)));
        for (int i = 0; i < workerCount; ++i)
        {
            workers_.emplace_back(&TerrainJobSystem::workerLoop, this);
        }
    }

    void TerrainJobSystem::stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopRequested_ = true;
            featureJobs_.clear();
            finalizeJobs_.clear();
            meshJobs_.clear();
        }
        condition_.notify_all();

        for (std::thread& worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        workers_.clear();
    }

    void TerrainJobSystem::clearQueuedJobsAndMeshes()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        featureJobs_.clear();
        finalizeJobs_.clear();
        meshJobs_.clear();
        completedChunkMeshes_.clear();
    }

    void TerrainJobSystem::enqueue(TerrainJob job)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            job.sequence = ++jobSequence_;
            if (job.type == TerrainJob::Type::BuildFeaturing)
            {
                featureJobs_.push_back(std::move(job));
            }
            else if (job.type == TerrainJob::Type::FinalizeFeatures)
            {
                finalizeJobs_.push_back(std::move(job));
            }
            else
            {
                meshJobs_.push_back(std::move(job));
            }
        }
        condition_.notify_one();
    }

    TerrainCompletedBatch TerrainJobSystem::drainCompleted(const MeshDropPredicate& shouldDropMesh, const MeshDrainPredicate& canDrainMesh)
    {
        TerrainCompletedBatch batch;
        std::lock_guard<std::mutex> lock(mutex_);

        while (!completedChunkData_.empty())
        {
            batch.completedChunks.push_back(std::move(completedChunkData_.front()));
            completedChunkData_.pop_front();
        }
        while (!completedMergedChunks_.empty())
        {
            batch.completedMergedChunks.push_back(std::move(completedMergedChunks_.front()));
            completedMergedChunks_.pop_front();
        }
        while (!completedChunkMeshes_.empty())
        {
            const CompletedChunkMesh& frontMesh = completedChunkMeshes_.front();
            if (shouldDropMesh && shouldDropMesh(frontMesh))
            {
                completedChunkMeshes_.pop_front();
                continue;
            }
            if (canDrainMesh && !canDrainMesh(frontMesh))
            {
                break;
            }
            batch.completedMeshes.push_back(std::move(completedChunkMeshes_.front()));
            completedChunkMeshes_.pop_front();
        }

        return batch;
    }

    TerrainCompletedBatch TerrainJobSystem::drainForShutdown()
    {
        TerrainCompletedBatch batch;
        std::lock_guard<std::mutex> lock(mutex_);
        while (!completedChunkData_.empty())
        {
            batch.completedChunks.push_back(std::move(completedChunkData_.front()));
            completedChunkData_.pop_front();
        }
        while (!completedMergedChunks_.empty())
        {
            batch.completedMergedChunks.push_back(std::move(completedMergedChunks_.front()));
            completedMergedChunks_.pop_front();
        }
        completedChunkMeshes_.clear();
        return batch;
    }

    void TerrainJobSystem::workerLoop()
    {
        for (;;)
        {
            TerrainJob job{};
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this]
                {
                    return stopRequested_ || !featureJobs_.empty() || !finalizeJobs_.empty() || !meshJobs_.empty();
                });

                if (stopRequested_)
                {
                    return;
                }

                if (!popNextJob(job))
                {
                    continue;
                }
            }

            if (generationProvider_ && job.generation != generationProvider_())
            {
                continue;
            }
            if (!jobProcessor_)
            {
                continue;
            }

            TerrainJobResult result = jobProcessor_(std::move(job));
            std::lock_guard<std::mutex> lock(mutex_);
            if (result.completedChunkData)
            {
                completedChunkData_.push_back(std::move(*result.completedChunkData));
            }
            if (result.completedMergedChunk)
            {
                completedMergedChunks_.push_back(std::move(result.completedMergedChunk));
            }
            if (result.completedChunkMesh)
            {
                completedChunkMeshes_.push_back(std::move(*result.completedChunkMesh));
            }
        }
    }

    bool TerrainJobSystem::popNextJob(TerrainJob& job)
    {
        auto bestQueue = &featureJobs_;
        auto bestIt = featureJobs_.begin();
        bool hasBest = bestIt != featureJobs_.end();

        auto considerQueue = [&](std::deque<TerrainJob>& jobs)
        {
            for (auto it = jobs.begin(); it != jobs.end(); ++it)
            {
                if (!hasBest || jobLess(*it, *bestIt))
                {
                    bestQueue = &jobs;
                    bestIt = it;
                    hasBest = true;
                }
            }
        };

        considerQueue(finalizeJobs_);
        considerQueue(meshJobs_);

        if (!hasBest)
        {
            return false;
        }

        job = std::move(*bestIt);
        bestQueue->erase(bestIt);
        return true;
    }
}
