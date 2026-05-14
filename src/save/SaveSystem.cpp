#include "save/SaveSystem.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace dolbuto::save
{
    namespace
    {
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeY = 512;
        constexpr int ChunkSizeZ = 16;
        constexpr uint16_t BlockAir = 0;
        constexpr uint16_t BlockLeaves = 9;
        constexpr uint16_t BlockPlant = 10000;

        bool applySavedIncomingFeatureSlots(SaveChunkSnapshot& snapshot)
        {
            if (!snapshot.hasData)
            {
                return false;
            }

            bool changed = false;
            for (const FeatureWriteListPtr& writes : snapshot.incomingFeatureSlots)
            {
                if (!writes)
                {
                    continue;
                }
                for (const FeatureWrite& write : *writes)
                {
                    if (write.block != BlockLeaves ||
                        write.localX < 0 || write.localX >= ChunkSizeX ||
                        write.localZ < 0 || write.localZ >= ChunkSizeZ ||
                        write.y < 0 || write.y >= ChunkSizeY)
                    {
                        continue;
                    }

                    const size_t index = static_cast<size_t>((write.y * ChunkSizeZ + write.localZ) * ChunkSizeX + write.localX);
                    uint16_t& existing = snapshot.blocks[index];
                    if (existing == BlockAir || existing == BlockPlant)
                    {
                        existing = BlockLeaves;
                        changed = true;
                    }
                }
            }
            return changed;
        }
    }

    SaveSystem::~SaveSystem()
    {
        stop();
    }

    void SaveSystem::start(const std::filesystem::path& worldDirectory, SavedCallback savedCallback)
    {
        stop();
        worldDirectory_ = worldDirectory;
        savedCallback_ = std::move(savedCallback);
        stopRequested_ = false;
        worker_ = std::thread(&SaveSystem::workerLoop, this);
    }

    void SaveSystem::stop()
    {
        {
            std::lock_guard<std::mutex> lock(jobMutex_);
            stopRequested_ = true;
        }
        jobCondition_.notify_all();

        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    void SaveSystem::clear()
    {
        {
            std::lock_guard<std::mutex> lock(jobMutex_);
            jobs_.clear();
            pendingSnapshots_.clear();
        }
        {
            std::lock_guard<std::mutex> lock(savedChunkMutex_);
            savedCleanRevisions_.clear();
        }
        {
            std::lock_guard<std::mutex> lock(regionHeaderCacheMutex_);
            regionHeaderCache_.clear();
        }
    }

    void SaveSystem::enqueue(SaveChunkSnapshot snapshot)
    {
        const uint64_t key = storageChunkKey(snapshot.chunkX, snapshot.chunkZ);
        if (snapshot.genState == ChunkGenState::Meshed)
        {
            snapshot.genState = ChunkGenState::Full;
        }

        if (snapshot.hasData && !snapshot.forceSave)
        {
            return;
        }
        if (!snapshot.hasData && snapshot.incomingFeatureMask == 0 && !hasIncomingFeatureSlots(snapshot))
        {
            return;
        }

        if (snapshot.genState == ChunkGenState::Full)
        {
            snapshot.incomingFeatureMask = 0;
            snapshot.incomingFeatureSlots = {};
            if (snapshot.hasData)
            {
                std::lock_guard<std::mutex> lock(savedChunkMutex_);
                const auto savedIt = savedCleanRevisions_.find(key);
                if (!snapshot.forceSave && savedIt != savedCleanRevisions_.end() && savedIt->second == snapshot.revision)
                {
                    return;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(jobMutex_);
            SaveChunkSnapshot& pending = pendingSnapshots_[key];
            if (snapshot.hasData)
            {
                const SaveChunkSnapshot previous = pending;
                if (snapshot.hasSavedBacking && !snapshot.forceSave && previous.hasData &&
                    previous.genState == ChunkGenState::Full &&
                    (snapshot.genState != ChunkGenState::Full || previous.revision > snapshot.revision))
                {
                    return;
                }
                pending = snapshot;
                if (pending.genState != ChunkGenState::Full)
                {
                    pending.incomingFeatureMask |= previous.incomingFeatureMask;
                    for (size_t slot = 0; slot < FeatureNeighborCount; ++slot)
                    {
                        if (!pending.incomingFeatureSlots[slot])
                        {
                            pending.incomingFeatureSlots[slot] = previous.incomingFeatureSlots[slot];
                        }
                    }
                }
            }
            else
            {
                if (pending.chunkX == 0 && pending.chunkZ == 0 && key != storageChunkKey(0, 0))
                {
                    pending.chunkX = snapshot.chunkX;
                    pending.chunkZ = snapshot.chunkZ;
                }
                pending.incomingFeatureMask |= snapshot.incomingFeatureMask;
                for (size_t slot = 0; slot < FeatureNeighborCount; ++slot)
                {
                    if (snapshot.incomingFeatureSlots[slot])
                    {
                        pending.incomingFeatureSlots[slot] = snapshot.incomingFeatureSlots[slot];
                    }
                }
            }
            jobs_.push_back(std::move(snapshot));
        }
        jobCondition_.notify_one();
    }

    std::optional<SaveChunkSnapshot> SaveSystem::load(int chunkX, int chunkZ)
    {
        const uint64_t key = storageChunkKey(chunkX, chunkZ);
        auto loadMiss = [this]() -> std::optional<SaveChunkSnapshot>
        {
            loadMissCount_.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        };

        {
            std::lock_guard<std::mutex> lock(jobMutex_);
            const auto pendingIt = pendingSnapshots_.find(key);
            if (pendingIt != pendingSnapshots_.end())
            {
                pendingLoadHitCount_.fetch_add(1, std::memory_order_relaxed);
                SaveChunkSnapshot snapshot = pendingIt->second;
                snapshot.chunkX = chunkX;
                snapshot.chunkZ = chunkZ;
                return snapshot;
            }
        }

        std::lock_guard<std::mutex> regionIoLock(regionIoMutex_);
        const int storageChunkX = wrapChunkCoordinate(chunkX);
        const int storageChunkZ = wrapChunkCoordinate(chunkZ);
        const int regionX = storageChunkX / RegionSizeChunks;
        const int regionZ = storageChunkZ / RegionSizeChunks;
        const int localX = storageChunkX % RegionSizeChunks;
        const int localZ = storageChunkZ % RegionSizeChunks;
        const size_t entryIndex = static_cast<size_t>(localZ * RegionSizeChunks + localX);
        const std::filesystem::path regionPath = worldDirectory_ /
            "regions" /
            ("r." + std::to_string(regionX) + "." + std::to_string(regionZ) + ".region");

        const uint64_t regionCacheKey = chunkKey(regionX, regionZ);
        RegionChunkEntry entry{};
        bool regionExists = false;
        bool regionCached = false;
        {
            std::lock_guard<std::mutex> lock(regionHeaderCacheMutex_);
            const auto cacheIt = regionHeaderCache_.find(regionCacheKey);
            if (cacheIt != regionHeaderCache_.end())
            {
                regionCached = true;
                regionExists = cacheIt->second.exists;
                if (regionExists)
                {
                    entry = cacheIt->second.entries[entryIndex];
                }
            }
        }

        if (!regionCached)
        {
            RegionHeaderCache cache{};
            std::ifstream headerFile(regionPath, std::ios::binary);
            if (headerFile.is_open())
            {
                std::vector<uint8_t> header(RegionSectorSize, 0);
                headerFile.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
                if (headerFile)
                {
                    cache.exists = true;
                    for (size_t i = 0; i < cache.entries.size(); ++i)
                    {
                        const size_t entryOffset = i * RegionChunkEntrySize;
                        cache.entries[i] = RegionChunkEntry{
                            readU32At(header, entryOffset),
                            readU32At(header, entryOffset + 4),
                            readU32At(header, entryOffset + 8),
                            readU32At(header, entryOffset + 12)};
                    }
                    entry = cache.entries[entryIndex];
                    regionExists = true;
                }
            }

            {
                std::lock_guard<std::mutex> lock(regionHeaderCacheMutex_);
                const auto cacheIt = regionHeaderCache_.find(regionCacheKey);
                if (cacheIt == regionHeaderCache_.end())
                {
                    regionHeaderCache_.emplace(regionCacheKey, cache);
                }
                else if (!cacheIt->second.exists && cache.exists)
                {
                    cacheIt->second = cache;
                }
            }
        }

        if (!regionExists ||
            entry.offsetSector == 0 ||
            entry.sectorCount == 0 ||
            entry.storedSize == 0 ||
            entry.rawSize == 0)
        {
            return loadMiss();
        }

        std::ifstream file(regionPath, std::ios::binary);
        if (!file.is_open())
        {
            return loadMiss();
        }

        std::vector<uint8_t> stored(entry.storedSize);
        file.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(entry.offsetSector) * RegionSectorSize));
        file.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size()));
        if (!file)
        {
            return loadMiss();
        }

        std::vector<uint8_t> payload;
        try
        {
            payload = decodePayload(stored, entry.rawSize);
        }
        catch (...)
        {
            return loadMiss();
        }

        std::optional<SaveChunkSnapshot> snapshot = deserializeChunkPayload(payload, chunkX, chunkZ);
        if (!snapshot)
        {
            return loadMiss();
        }

        if (snapshot->genState == ChunkGenState::Full && snapshot->hasData)
        {
            markClean(chunkX, chunkZ, snapshot->revision);
        }
        regionLoadHitCount_.fetch_add(1, std::memory_order_relaxed);
        return snapshot;
    }

    void SaveSystem::markClean(int chunkX, int chunkZ, uint64_t revision)
    {
        std::lock_guard<std::mutex> lock(savedChunkMutex_);
        savedCleanRevisions_[storageChunkKey(chunkX, chunkZ)] = revision;
    }

    SaveStats SaveSystem::stats() const
    {
        return SaveStats{
            savedChunkCount_.load(std::memory_order_relaxed),
            savedFeatureCount_.load(std::memory_order_relaxed),
            failedSaveCount_.load(std::memory_order_relaxed),
            pendingLoadHitCount_.load(std::memory_order_relaxed),
            regionLoadHitCount_.load(std::memory_order_relaxed),
            loadMissCount_.load(std::memory_order_relaxed)};
    }

    void SaveSystem::workerLoop()
    {
        for (;;)
        {
            SaveChunkSnapshot snapshot{};
            {
                std::unique_lock<std::mutex> lock(jobMutex_);
                jobCondition_.wait(lock, [this]
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

                snapshot = std::move(jobs_.front());
                jobs_.pop_front();
            }

            try
            {
                const bool savedChunkData = snapshot.hasData;
                saveSnapshot(snapshot);
                const uint64_t key = storageChunkKey(snapshot.chunkX, snapshot.chunkZ);
                {
                    std::lock_guard<std::mutex> lock(jobMutex_);
                    const auto pendingIt = pendingSnapshots_.find(key);
                    if (pendingIt != pendingSnapshots_.end() &&
                        pendingIt->second.hasData == snapshot.hasData &&
                        pendingIt->second.revision == snapshot.revision &&
                        pendingIt->second.dataDirtySerial == snapshot.dataDirtySerial &&
                        pendingIt->second.genState == snapshot.genState)
                    {
                        pendingSnapshots_.erase(pendingIt);
                    }
                }

                if (savedCallback_)
                {
                    savedCallback_(snapshot, savedChunkData);
                }
                if (savedChunkData)
                {
                    savedChunkCount_.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    savedFeatureCount_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            catch (...)
            {
                failedSaveCount_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    void SaveSystem::saveSnapshot(const SaveChunkSnapshot& snapshot)
    {
        if (!snapshot.hasData && snapshot.incomingFeatureMask == 0 && !hasIncomingFeatureSlots(snapshot))
        {
            return;
        }

        const int storageChunkX = wrapChunkCoordinate(snapshot.chunkX);
        const int storageChunkZ = wrapChunkCoordinate(snapshot.chunkZ);
        const int regionX = storageChunkX / RegionSizeChunks;
        const int regionZ = storageChunkZ / RegionSizeChunks;
        const int localX = storageChunkX % RegionSizeChunks;
        const int localZ = storageChunkZ % RegionSizeChunks;
        const size_t entryIndex = static_cast<size_t>(localZ * RegionSizeChunks + localX);
        const std::filesystem::path regionDirectory = worldDirectory_ / "regions";
        std::filesystem::create_directories(regionDirectory);
        const std::filesystem::path regionPath = regionDirectory / ("r." + std::to_string(regionX) + "." + std::to_string(regionZ) + ".region");

        std::lock_guard<std::mutex> regionIoLock(regionIoMutex_);
        if (!std::filesystem::exists(regionPath))
        {
            std::ofstream createFile(regionPath, std::ios::binary);
            std::vector<uint8_t> emptyHeader(RegionSectorSize, 0);
            createFile.write(reinterpret_cast<const char*>(emptyHeader.data()), static_cast<std::streamsize>(emptyHeader.size()));
        }

        std::fstream file(regionPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open())
        {
            return;
        }

        std::vector<uint8_t> header(RegionSectorSize, 0);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));

        const size_t entryOffset = entryIndex * RegionChunkEntrySize;
        const uint32_t existingOffsetSector = readU32At(header, entryOffset);
        const uint32_t existingSectorCount = readU32At(header, entryOffset + 4);
        const uint32_t existingStoredSize = readU32At(header, entryOffset + 8);
        const uint32_t existingRawSize = readU32At(header, entryOffset + 12);

        std::optional<SaveChunkSnapshot> existingSnapshot;
        if (existingOffsetSector != 0 && existingSectorCount != 0 && existingStoredSize != 0 && existingRawSize != 0)
        {
            std::vector<uint8_t> stored(existingStoredSize);
            file.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(existingOffsetSector) * RegionSectorSize));
            file.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size()));
            try
            {
                existingSnapshot = deserializeChunkPayload(decodePayload(stored, existingRawSize), snapshot.chunkX, snapshot.chunkZ);
            }
            catch (...)
            {
                existingSnapshot.reset();
            }
        }

        SaveChunkSnapshot merged = existingSnapshot.value_or(SaveChunkSnapshot{});
        if (!existingSnapshot)
        {
            merged.chunkX = snapshot.chunkX;
            merged.chunkZ = snapshot.chunkZ;
            merged.genState = ChunkGenState::Empty;
        }

        if (snapshot.hasData)
        {
            const std::optional<SaveChunkSnapshot> previous = existingSnapshot;
            if (snapshot.hasSavedBacking && !snapshot.forceSave && previous && previous->hasData &&
                previous->genState == ChunkGenState::Full &&
                (snapshot.genState != ChunkGenState::Full || previous->revision > snapshot.revision))
            {
                return;
            }
            merged = snapshot;
            if (merged.genState == ChunkGenState::Meshed)
            {
                merged.genState = ChunkGenState::Full;
            }
            if (merged.genState == ChunkGenState::Full)
            {
                merged.incomingFeatureMask = 0;
                merged.incomingFeatureSlots = {};
            }
            else if (previous)
            {
                merged.incomingFeatureMask |= previous->incomingFeatureMask;
                for (size_t slot = 0; slot < FeatureNeighborCount; ++slot)
                {
                    if (!merged.incomingFeatureSlots[slot])
                    {
                        merged.incomingFeatureSlots[slot] = previous->incomingFeatureSlots[slot];
                    }
                }
            }
        }
        else
        {
            if (merged.genState != ChunkGenState::Full)
            {
                merged.incomingFeatureMask |= snapshot.incomingFeatureMask;
            }
            for (size_t slot = 0; slot < FeatureNeighborCount; ++slot)
            {
                if (!snapshot.incomingFeatureSlots[slot])
                {
                    continue;
                }
                if (merged.genState == ChunkGenState::Full && merged.hasData)
                {
                    std::array<FeatureWriteListPtr, FeatureNeighborCount> singleSlot{};
                    singleSlot[slot] = snapshot.incomingFeatureSlots[slot];
                    merged.incomingFeatureSlots = singleSlot;
                    if (applySavedIncomingFeatureSlots(merged))
                    {
                        merged.revision += 1;
                    }
                    merged.incomingFeatureSlots = {};
                    merged.incomingFeatureMask = 0;
                }
                else
                {
                    merged.incomingFeatureSlots[slot] = snapshot.incomingFeatureSlots[slot];
                    merged.incomingFeatureMask |= static_cast<uint8_t>(1u << static_cast<uint32_t>(slot));
                }
            }
        }

        if (merged.genState == ChunkGenState::Meshed)
        {
            merged.genState = ChunkGenState::Full;
        }
        if (merged.genState == ChunkGenState::Full)
        {
            merged.incomingFeatureSlots = {};
            merged.incomingFeatureMask = 0;
        }

        const std::vector<uint8_t> rawPayload = serializeChunkPayload(merged);
        const std::vector<uint8_t> storedPayload = encodePayload(rawPayload);
        const uint32_t storedSize = static_cast<uint32_t>(storedPayload.size());
        const uint32_t rawSize = static_cast<uint32_t>(rawPayload.size());
        const uint32_t sectorCount = (storedSize + RegionSectorSize - 1u) / RegionSectorSize;

        file.seekp(0, std::ios::end);
        std::streamoff endOffset = file.tellp();
        const uint32_t paddingBefore = static_cast<uint32_t>((RegionSectorSize - (static_cast<uint64_t>(endOffset) % RegionSectorSize)) % RegionSectorSize);
        if (paddingBefore > 0)
        {
            std::vector<uint8_t> padding(paddingBefore, 0);
            file.write(reinterpret_cast<const char*>(padding.data()), static_cast<std::streamsize>(padding.size()));
            endOffset += paddingBefore;
        }

        const uint32_t offsetSector = static_cast<uint32_t>(static_cast<uint64_t>(endOffset) / RegionSectorSize);
        file.write(reinterpret_cast<const char*>(storedPayload.data()), static_cast<std::streamsize>(storedPayload.size()));
        const uint32_t paddingAfter = sectorCount * RegionSectorSize - storedSize;
        if (paddingAfter > 0)
        {
            std::vector<uint8_t> padding(paddingAfter, 0);
            file.write(reinterpret_cast<const char*>(padding.data()), static_cast<std::streamsize>(padding.size()));
        }

        writeU32At(header, entryOffset, offsetSector);
        writeU32At(header, entryOffset + 4, sectorCount);
        writeU32At(header, entryOffset + 8, storedSize);
        writeU32At(header, entryOffset + 12, rawSize);
        file.seekp(0);
        file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));

        {
            std::lock_guard<std::mutex> lock(regionHeaderCacheMutex_);
            RegionHeaderCache& cache = regionHeaderCache_[chunkKey(regionX, regionZ)];
            cache.exists = true;
            cache.entries[entryIndex] = RegionChunkEntry{
                offsetSector,
                sectorCount,
                storedSize,
                rawSize};
        }

        if (merged.genState == ChunkGenState::Full && merged.hasData)
        {
            markClean(merged.chunkX, merged.chunkZ, merged.revision);
        }
    }
}
