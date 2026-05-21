#pragma once

#include "world/WorldTypes.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace dolbuto::world
{
    class ChunkPrepareSystem
    {
    public:
        using ChunkPreparer = std::function<PreparedChunkLoad(CompletedChunkLoad)>;

        ChunkPrepareSystem() = default;
        ~ChunkPrepareSystem();

        ChunkPrepareSystem(const ChunkPrepareSystem&) = delete;
        ChunkPrepareSystem& operator=(const ChunkPrepareSystem&) = delete;

        void start(ChunkPreparer preparer);
        void stop();
        void clear();

        void enqueue(CompletedChunkLoad completed);
        std::vector<PreparedChunkLoad> drainCompleted();

    private:
        void workerLoop();

        ChunkPreparer preparer_;
        std::thread worker_;
        std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<CompletedChunkLoad> jobs_;
        std::deque<PreparedChunkLoad> completed_;
        bool stopRequested_ = false;
    };
}
