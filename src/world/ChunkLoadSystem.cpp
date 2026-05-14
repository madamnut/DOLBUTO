#include "world/ChunkLoadSystem.h"

#include <utility>

namespace dolbuto::world
{
    namespace
    {
        uint64_t chunkKey(int chunkX, int chunkZ)
        {
            return (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32u) |
                static_cast<uint32_t>(chunkZ);
        }
    }

    ChunkLoadSystem::~ChunkLoadSystem()
    {
        stop();
    }

    void ChunkLoadSystem::start(SnapshotLoader snapshotLoader)
    {
        stop();
        snapshotLoader_ = std::move(snapshotLoader);
        stopRequested_ = false;
        worker_ = std::thread(&ChunkLoadSystem::workerLoop, this);
    }

    void ChunkLoadSystem::stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopRequested_ = true;
            jobs_.clear();
        }
        condition_.notify_all();

        if (worker_.joinable())
        {
            worker_.join();
        }

        clear();
    }

    void ChunkLoadSystem::clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.clear();
        completed_.clear();
        requested_.clear();
    }

    void ChunkLoadSystem::enqueue(int chunkX, int chunkZ, uint64_t generation)
    {
        const uint64_t key = chunkKey(chunkX, chunkZ);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!requested_.insert(key).second)
            {
                return;
            }
            jobs_.push_back(ChunkLoadJob{chunkX, chunkZ, generation});
        }
        condition_.notify_one();
    }

    std::vector<CompletedChunkLoad> ChunkLoadSystem::drainCompleted()
    {
        std::vector<CompletedChunkLoad> completedLoads;
        std::lock_guard<std::mutex> lock(mutex_);
        while (!completed_.empty())
        {
            const uint64_t key = chunkKey(completed_.front().chunkX, completed_.front().chunkZ);
            requested_.erase(key);
            completedLoads.push_back(std::move(completed_.front()));
            completed_.pop_front();
        }
        return completedLoads;
    }

    void ChunkLoadSystem::workerLoop()
    {
        for (;;)
        {
            ChunkLoadJob job{};
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this]
                {
                    return stopRequested_ || !jobs_.empty();
                });

                if (jobs_.empty())
                {
                    if (stopRequested_)
                    {
                        return;
                    }
                    continue;
                }

                job = jobs_.front();
                jobs_.pop_front();
            }

            CompletedChunkLoad completed{};
            completed.chunkX = job.chunkX;
            completed.chunkZ = job.chunkZ;
            completed.generation = job.generation;
            if (snapshotLoader_)
            {
                completed.snapshot = snapshotLoader_(job.chunkX, job.chunkZ);
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                completed_.push_back(std::move(completed));
            }
        }
    }
}
