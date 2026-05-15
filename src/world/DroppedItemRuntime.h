#pragma once

#include "items/ItemData.h"
#include "world/BlockData.h"
#include "world/WorldRuntime.h"
#include "world/WorldTypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace dolbuto::world
{
    class DroppedItemRuntime
    {
    public:
        using MarkDirtyFn = std::function<void(RuntimeChunk&)>;
        using TerrainCollisionFn = std::function<bool(int, int, int)>;
        using AddInventoryFn = std::function<uint16_t(ItemStack)>;
        using PickupSoundFn = std::function<void()>;

        DroppedItemRuntime() = default;
        DroppedItemRuntime(WorldRuntime* worldRuntime, const std::vector<ItemDefinition>* itemDefinitions);

        void setContext(WorldRuntime* worldRuntime, const std::vector<ItemDefinition>* itemDefinitions);

        uint64_t allocateEntityId();
        void resetEntityIds();
        void reserveTracking(std::size_t capacity);
        void resetTracking();
        void resetFrameClock(double timestamp = 0.0);
        void resetForScene(double timestamp);
        void resetForUnload();

        void normalizeLoadedEntity(WorldEntity& entity);
        bool addWorldEntity(WorldEntity entity, const MarkDirtyFn& markDirty);
        void spawnBlockDrops(int x, int y, int z, const BlockDefinition& block, const MarkDirtyFn& markDirty);
        WorldEntity createManualDropEntity(ItemStack stack, DVec3 playerPosition, Vec3 direction);

        bool pickupInView(DVec3 origin, Vec3 direction, const MarkDirtyFn& markDirty);
        bool raycast(DVec3 origin, Vec3 direction, WorldEntityHandle& itemHandle) const;

        void update(
            Vec3 playerPosition,
            double now,
            const TerrainCollisionFn& terrainCellBlocksPlayer,
            const AddInventoryFn& addToPlayerInventory,
            const PickupSoundFn& playPickupSound,
            const MarkDirtyFn& markDirty);

        void refreshChunkTracking(uint64_t key);
        void removeChunkTracking(uint64_t key);

        std::size_t loadedItemCount() const;
        float renderAlpha() const;
        const std::unordered_map<uint64_t, std::size_t>& trackedChunkCounts() const;

    private:
        WorldRuntime& worldRuntime();
        const WorldRuntime& worldRuntime() const;
        const std::vector<ItemDefinition>& itemDefinitions() const;

        uint64_t entityChunkKey(const WorldEntity& entity) const;
        RuntimeChunk* runtimeChunkForEntity(const WorldEntity& entity);
        std::size_t countDroppedItemsInChunk(const RuntimeChunk& chunk) const;
        uint16_t mergeIntoNearby(WorldEntity& source, const MarkDirtyFn& markDirty);
        void updateTick(
            Vec3 playerPosition,
            float dt,
            const TerrainCollisionFn& terrainCellBlocksPlayer,
            const AddInventoryFn& addToPlayerInventory,
            const PickupSoundFn& playPickupSound,
            const MarkDirtyFn& markDirty);

        WorldRuntime* worldRuntime_ = nullptr;
        const std::vector<ItemDefinition>* itemDefinitions_ = nullptr;
        uint64_t nextEntityId_ = 1;
        std::size_t loadedItemCount_ = 0;
        std::unordered_map<uint64_t, std::size_t> droppedItemCountsByChunk_;
        double lastUpdateTime_ = 0.0;
        float tickAccumulator_ = 0.0f;
        float renderAlpha_ = 0.0f;
    };
}
