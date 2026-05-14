#pragma once

#include "world/WorldTypes.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>
#include <vector>

namespace dolbuto::world
{
    class ChunkLoadSystem
    {
    public:
        using SnapshotLoader = std::function<std::optional<SaveChunkSnapshot>(int, int)>;

        ChunkLoadSystem() = default;
        ~ChunkLoadSystem();

        ChunkLoadSystem(const ChunkLoadSystem&) = delete;
        ChunkLoadSystem& operator=(const ChunkLoadSystem&) = delete;

        void start(SnapshotLoader snapshotLoader);
        void stop();
        void clear();

        void enqueue(int chunkX, int chunkZ, uint64_t generation);
        std::vector<CompletedChunkLoad> drainCompleted();

    private:
        struct ChunkLoadJob
        {
            int chunkX = 0;
            int chunkZ = 0;
            uint64_t generation = 0;
        };

        void workerLoop();

        SnapshotLoader snapshotLoader_;
        std::thread worker_;
        std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<ChunkLoadJob> jobs_;
        std::deque<CompletedChunkLoad> completed_;
        std::unordered_set<uint64_t> requested_;
        bool stopRequested_ = false;
    };
}
