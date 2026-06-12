#pragma once

#include "items/ItemData.h"
#include "world/BlockData.h"
#include "world/WorldRuntime.h"
#include "world/WorldTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dolbuto::world
{
    class DroppedItemRuntime
    {
    public:
        using MarkDirtyFn = std::function<void(RuntimeChunk&)>;
        using TerrainCollisionFn = std::function<bool(DVec3, DVec3)>;
        using AddInventoryFn = std::function<uint16_t(ItemStack)>;
        using PickupSoundFn = std::function<void()>;

        struct Target
        {
            WorldEntityHandle handle{};
            uint64_t entityId = 0;
            ItemStack stack{};
            Vec3 position{};
        };

        struct BurnableConsumptionResult
        {
            uint32_t burnTimeTicks = 0;
            uint16_t itemId = 0;
            uint16_t heatLevel = 0;
            uint16_t remainderItemId = 0;
            uint16_t remainderCount = 0;
        };

        struct SmeltProcessingResult
        {
            bool changed = false;
            bool completed = false;
            uint16_t outputFluidId = 0;
            uint16_t outputAmount = 0;
        };

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
        WorldEntity createManualDropEntity(ItemStack stack, DVec3 sourcePosition, Vec3 direction);

        bool pickupInView(DVec3 origin, Vec3 direction, const MarkDirtyFn& markDirty);
        bool raycast(DVec3 origin, Vec3 direction, WorldEntityHandle& itemHandle) const;
        bool targetInView(DVec3 origin, Vec3 direction, Target& target) const;
        std::vector<Target> targetsInAabb(
            float minX,
            float minY,
            float minZ,
            float maxX,
            float maxY,
            float maxZ) const;
        BurnableConsumptionResult consumeHighestHeatBurnableInAabb(
            float minX,
            float minY,
            float minZ,
            float maxX,
            float maxY,
            float maxZ,
            const MarkDirtyFn& markDirty);
        bool processItemsInAabb(
            float minX,
            float minY,
            float minZ,
            float maxX,
            float maxY,
            float maxZ,
            const std::vector<ItemProcessingRecipe>& recipes,
            const std::string& type,
            uint16_t heatLevel,
            uint32_t elapsedTicks,
            const MarkDirtyFn& markDirty);
        bool processItemsInCells(
            const std::vector<std::array<int, 3>>& cells,
            const std::vector<ItemProcessingRecipe>& recipes,
            const std::string& type,
            uint16_t heatLevel,
            uint32_t elapsedTicks,
            const MarkDirtyFn& markDirty);
        SmeltProcessingResult processCrucibleSmeltInAabb(
            float minX,
            float minY,
            float minZ,
            float maxX,
            float maxY,
            float maxZ,
            const std::vector<ItemProcessingRecipe>& recipes,
            uint16_t heatLevel,
            uint16_t currentFluidId,
            uint16_t currentAmount,
            uint16_t capacity,
            uint32_t elapsedTicks,
            const MarkDirtyFn& markDirty);
        uint16_t replaceTargetItems(
            const WorldEntityHandle& itemHandle,
            uint64_t entityId,
            const std::vector<ItemInteractionOutput>& outputs,
            uint16_t targetCount,
            uint16_t maxApplications,
            const MarkDirtyFn& markDirty);
        uint16_t replaceAreaItems(
            float minX,
            float minY,
            float minZ,
            float maxX,
            float maxY,
            float maxZ,
            const std::vector<ItemInteractionIngredient>& ingredients,
            const std::vector<ItemInteractionOutput>& outputs,
            uint16_t maxApplications,
            Vec3 resultPosition,
            const MarkDirtyFn& markDirty);
        void pushItemsOutOfBlock(
            int blockX,
            int blockY,
            int blockZ,
            const BlockDefinition& placedDefinition,
            uint16_t placedBlockState,
            const TerrainCollisionFn& terrainCellBlocksItem,
            const MarkDirtyFn& markDirty);

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
