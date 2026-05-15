#pragma once

#include "gameplay/BlockInteractionSystem.h"
#include "gameplay/PlayerInventory.h"
#include "items/ItemData.h"
#include "world/DroppedItemRuntime.h"
#include "world/WorldRuntime.h"
#include "world/WorldTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace dolbuto::gameplay
{
    enum class BlockEditType
    {
        None,
        Break,
        Place
    };

    struct BlockEditResult
    {
        bool changed = false;
        BlockEditType type = BlockEditType::None;
        BlockRaycastHit hit{};
        uint16_t block = 0;
    };

    class ClientGameplayRuntime
    {
    public:
        using BlockSampler = BlockInteractionSystem::BlockSampler;
        using BlockDefinitionProvider = BlockInteractionSystem::BlockDefinitionProvider;
        using TerrainCollisionPredicate = BlockInteractionSystem::TerrainCollisionPredicate;
        using SetBlockFn = std::function<bool(int, int, int, uint16_t)>;
        using MarkDirtyFn = world::DroppedItemRuntime::MarkDirtyFn;
        using PickupSoundFn = world::DroppedItemRuntime::PickupSoundFn;

        ClientGameplayRuntime() = default;
        ClientGameplayRuntime(world::WorldRuntime* worldRuntime, const std::vector<ItemDefinition>* itemDefinitions);

        void setContext(world::WorldRuntime* worldRuntime, const std::vector<ItemDefinition>* itemDefinitions);

        bool playerColliderIntersectsTerrain(DVec3 playerPosition, const TerrainCollisionPredicate& terrainCellBlocksPlayer) const;
        BlockEditResult editBlockInView(
            DVec3 origin,
            Vec3 direction,
            bool placeBlock,
            uint16_t placeBlockId,
            DVec3 playerPosition,
            const BlockSampler& blockAtWorld,
            const BlockDefinitionProvider& blockDefinition,
            const SetBlockFn& setBlockAtWorld,
            const MarkDirtyFn& markDirty);
        BlockEditResult breakBlockAtHit(
            const BlockRaycastHit& hit,
            const BlockSampler& blockAtWorld,
            const BlockDefinitionProvider& blockDefinition,
            const SetBlockFn& setBlockAtWorld,
            const MarkDirtyFn& markDirty);
        BlockBreakingUpdate updateBlockBreaking(
            DVec3 origin,
            Vec3 direction,
            bool breaking,
            float deltaSeconds,
            const BlockSampler& blockAtWorld,
            const BlockDefinitionProvider& blockDefinition);
        void resetBlockBreaking();
        const BlockBreakingState& blockBreakingState() const;

        bool pickupDroppedItemInView(DVec3 origin, Vec3 direction, const MarkDirtyFn& markDirty);
        bool dropSelectedHotbarItem(bool wholeStack, DVec3 playerPosition, Vec3 direction, const MarkDirtyFn& markDirty);
        bool updateDroppedItems(
            Vec3 playerPosition,
            double now,
            const world::DroppedItemRuntime::TerrainCollisionFn& terrainCellBlocksPlayer,
            const PickupSoundFn& playPickupSound,
            const MarkDirtyFn& markDirty);

        void reserveDroppedItemTracking(std::size_t capacity);
        void refreshDroppedItemChunkTracking(uint64_t key);
        void removeDroppedItemChunkTracking(uint64_t key);
        void resetDroppedItemTracking();
        void resetForScene(double timestamp);
        void resetForUnload();
        void normalizeLoadedEntity(WorldEntity& entity);
        std::size_t loadedDroppedItemCount() const;
        float droppedItemRenderAlpha() const;
        const std::unordered_map<uint64_t, std::size_t>& droppedItemTrackedChunkCounts() const;

        void setHotbarSelectedSlot(int slot);
        int hotbarSelectedSlot() const;
        std::size_t inventorySlotCount() const;
        const ItemStack& inventorySlot(std::size_t index) const;
        const ItemStack& inventoryCursorStack() const;
        void clearInventory();
        std::array<ItemStack, PlayerInventory::SlotCount> inventorySnapshot() const;
        void setInventorySnapshot(const std::array<ItemStack, PlayerInventory::SlotCount>& slots);
        uint16_t addItemToPlayerInventory(ItemStack stack);
        bool handleInventorySlotClick(std::size_t slotIndex, InventoryClickButton button, bool shift);
        bool swapHotbarWithSlot(std::size_t slotIndex, std::size_t hotbarSlot);
        bool closeInventoryCursor();

    private:
        const std::vector<ItemDefinition>& itemDefinitions() const;

        const std::vector<ItemDefinition>* itemDefinitions_ = nullptr;
        int hotbarSelectedSlot_ = 0;
        BlockBreakingState blockBreaking_;
        PlayerInventory playerInventory_;
        world::DroppedItemRuntime droppedItemRuntime_;
    };
}
