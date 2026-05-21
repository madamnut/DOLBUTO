#include "world/ChunkPrepareSystem.h"

#include <utility>

namespace dolbuto::world
{
    ChunkPrepareSystem::~ChunkPrepareSystem()
    {
        stop();
    }

    void ChunkPrepareSystem::start(ChunkPreparer preparer)
    {
        stop();
        preparer_ = std::move(preparer);
        stopRequested_ = false;
        worker_ = std::thread(&ChunkPrepareSystem::workerLoop, this);
    }

    void ChunkPrepareSystem::stop()
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

    void ChunkPrepareSystem::clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.clear();
        completed_.clear();
    }

    void ChunkPrepareSystem::enqueue(CompletedChunkLoad completed)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.push_back(std::move(completed));
        }
        condition_.notify_one();
    }

    std::vector<PreparedChunkLoad> ChunkPrepareSystem::drainCompleted()
    {
        std::vector<PreparedChunkLoad> preparedLoads;
        std::lock_guard<std::mutex> lock(mutex_);
        while (!completed_.empty())
        {
            preparedLoads.push_back(std::move(completed_.front()));
            completed_.pop_front();
        }
        return preparedLoads;
    }

    void ChunkPrepareSystem::workerLoop()
    {
        for (;;)
        {
            CompletedChunkLoad job{};
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

                job = std::move(jobs_.front());
                jobs_.pop_front();
            }

            PreparedChunkLoad prepared{};
            if (preparer_)
            {
                prepared = preparer_(std::move(job));
            }
            else
            {
                prepared.completed = std::move(job);
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                completed_.push_back(std::move(prepared));
            }
        }
    }
}
