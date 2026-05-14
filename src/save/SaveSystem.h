#pragma once

#include "save/SaveFormat.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

namespace dolbuto::save
{
    struct SaveStats
    {
        uint64_t savedChunkCount = 0;
        uint64_t savedFeatureCount = 0;
        uint64_t failedSaveCount = 0;
        uint64_t pendingLoadHitCount = 0;
        uint64_t regionLoadHitCount = 0;
        uint64_t loadMissCount = 0;
    };

    class SaveSystem
    {
    public:
        using SavedCallback = std::function<void(const SaveChunkSnapshot&, bool)>;

        SaveSystem() = default;
        ~SaveSystem();

        SaveSystem(const SaveSystem&) = delete;
        SaveSystem& operator=(const SaveSystem&) = delete;

        void start(const std::filesystem::path& worldDirectory, SavedCallback savedCallback);
        void stop();
        void clear();

        void enqueue(SaveChunkSnapshot snapshot);
        std::optional<SaveChunkSnapshot> load(int chunkX, int chunkZ);
        void markClean(int chunkX, int chunkZ, uint64_t revision);
        SaveStats stats() const;

    private:
        struct RegionChunkEntry
        {
            uint32_t offsetSector = 0;
            uint32_t sectorCount = 0;
            uint32_t storedSize = 0;
            uint32_t rawSize = 0;
        };

        struct RegionHeaderCache
        {
            bool exists = false;
            std::array<RegionChunkEntry, RegionSizeChunks * RegionSizeChunks> entries{};
        };

        void workerLoop();
        void saveSnapshot(const SaveChunkSnapshot& snapshot);

        std::filesystem::path worldDirectory_;
        SavedCallback savedCallback_;

        std::thread worker_;
        mutable std::mutex jobMutex_;
        std::condition_variable jobCondition_;
        std::deque<SaveChunkSnapshot> jobs_;
        std::unordered_map<uint64_t, SaveChunkSnapshot> pendingSnapshots_;
        bool stopRequested_ = false;

        mutable std::mutex savedChunkMutex_;
        std::unordered_map<uint64_t, uint64_t> savedCleanRevisions_;

        mutable std::mutex regionIoMutex_;
        mutable std::mutex regionHeaderCacheMutex_;
        std::unordered_map<uint64_t, RegionHeaderCache> regionHeaderCache_;

        std::atomic<uint64_t> savedChunkCount_{0};
        std::atomic<uint64_t> savedFeatureCount_{0};
        std::atomic<uint64_t> failedSaveCount_{0};
        std::atomic<uint64_t> pendingLoadHitCount_{0};
        std::atomic<uint64_t> regionLoadHitCount_{0};
        std::atomic<uint64_t> loadMissCount_{0};
    };
}
