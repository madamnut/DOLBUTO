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
#include <string>
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
        bool inventoryChanged = false;
    };

    struct BlockBreakEvent
    {
        int x = 0;
        int y = 0;
        int z = 0;
        uint16_t block = 0;
    };

    struct FireSmokeRateUpdate
    {
        int x = 0;
        int y = 0;
        int z = 0;
        float multiplier = 1.0f;
    };

    struct BlockTickResult
    {
        std::vector<BlockBreakEvent> brokenBlocks;
        std::vector<FireSmokeRateUpdate> fireSmokeRateUpdates;
    };

    struct ItemInteractionActionMenu
    {
        std::string action;
        uint16_t targetCount = 1;
        std::vector<ItemInteractionCandidate> candidates;
        std::vector<std::string> actions;
        bool consumesHeldDurability = false;
        bool areaInteraction = false;
    };

    struct ItemInteractionMenu
    {
        bool hasUseTarget = false;
        bool available = false;
        uint16_t targetItemId = 0;
        std::vector<ItemInteractionActionMenu> actions;
    };

    struct ItemInteractionExecuteResult
    {
        bool executed = false;
        bool inventoryChanged = false;
        std::vector<BlockEditResult> blockEdits;
    };

    class ClientGameplayRuntime
    {
    public:
        using BlockSampler = BlockInteractionSystem::BlockSampler;
        using FluidSampler = BlockInteractionSystem::FluidSampler;
        using BlockDefinitionProvider = BlockInteractionSystem::BlockDefinitionProvider;
        using TerrainAabbCollisionPredicate = BlockInteractionSystem::TerrainAabbCollisionPredicate;
        using SetBlockFn = std::function<bool(int, int, int, uint16_t)>;
        using MarkDirtyFn = world::DroppedItemRuntime::MarkDirtyFn;
        using PickupSoundFn = world::DroppedItemRuntime::PickupSoundFn;

        ClientGameplayRuntime() = default;
        ClientGameplayRuntime(world::WorldRuntime* worldRuntime, const std::vector<ItemDefinition>* itemDefinitions);

        void setContext(world::WorldRuntime* worldRuntime, const std::vector<ItemDefinition>* itemDefinitions);

        bool playerColliderIntersectsTerrain(DVec3 playerPosition, double heightScale, const TerrainAabbCollisionPredicate& terrainCellIntersectsPlayer) const;
        bool playerColliderHasSupportBelow(DVec3 playerPosition, const TerrainAabbCollisionPredicate& terrainCellIntersectsPlayer) const;
        bool playerColliderIntersectsWater(DVec3 playerPosition, double heightScale, const FluidSampler& fluidAtWorld) const;
        BlockEditResult editBlockInView(
            DVec3 origin,
            Vec3 direction,
            bool placeBlock,
            uint16_t placeBlockId,
            DVec3 playerPosition,
            double playerHeightScale,
            const BlockSampler& blockAtWorld,
            const BlockDefinitionProvider& blockDefinition,
            const BlockInteractionSystem::PropMeshProvider& propMesh,
            const SetBlockFn& setBlockAtWorld,
            const MarkDirtyFn& markDirty);
        BlockEditResult breakBlockAtHit(
            const BlockRaycastHit& hit,
            uint16_t durabilityCost,
            const BlockSampler& blockAtWorld,
            const BlockDefinitionProvider& blockDefinition,
            const SetBlockFn& setBlockAtWorld,
            const MarkDirtyFn& markDirty);
        BlockBreakingUpdate updateBlockBreaking(
            DVec3 origin,
            Vec3 direction,
            bool breaking,
            float deltaSeconds,
            bool sandboxMode,
            const BlockSampler& blockAtWorld,
            const BlockDefinitionProvider& blockDefinition,
            const BlockInteractionSystem::PropMeshProvider& propMesh = {});
        BlockTickResult tickBlockUpdates(
            uint32_t maxCells,
            const BlockDefinitionProvider& blockDefinition,
            const SetBlockFn& setBlockAtWorld,
            const MarkDirtyFn& markDirty);
        void resetBlockBreaking();
        const BlockBreakingState& blockBreakingState() const;

        bool pickupDroppedItemInView(DVec3 origin, Vec3 direction, const MarkDirtyFn& markDirty);
        bool dropSelectedHotbarItem(bool wholeStack, DVec3 sourcePosition, Vec3 direction, const MarkDirtyFn& markDirty);
        BlockEditResult placeSelectedItemBlockInView(
            DVec3 origin,
            Vec3 direction,
            DVec3 playerPosition,
            double playerHeightScale,
            const BlockSampler& blockAtWorld,
            const BlockDefinitionProvider& blockDefinition,
            const BlockInteractionSystem::PropMeshProvider& propMesh,
            const SetBlockFn& setBlockAtWorld,
            const world::DroppedItemRuntime::TerrainCollisionFn& terrainCellBlocksItem,
            const MarkDirtyFn& markDirty);
        ItemInteractionMenu beginItemInteractionInView(
            DVec3 origin,
            Vec3 direction,
            bool preferHeldItemBlockActions,
            const std::vector<ItemInteractionRecipe>& recipes,
            const BlockSampler& blockAtWorld,
            const BlockDefinitionProvider& blockDefinition,
            const BlockInteractionSystem::PropMeshProvider& propMesh);
        ItemInteractionExecuteResult executePendingItemInteraction(
            std::size_t actionIndex,
            std::size_t candidateIndex,
            bool repeat,
            const SetBlockFn& setBlockAtWorld,
            const MarkDirtyFn& markDirty);
        void cancelPendingItemInteraction();
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
        BlockBreakTool currentBlockBreakTool() const;
        bool damageSelectedHotbarItem(uint16_t damage);

        struct PendingItemInteraction
        {
            bool active = false;
            std::size_t heldSlotIndex = 0;
            WorldEntityHandle targetHandle{};
            uint64_t targetEntityId = 0;
            bool blockInteraction = false;
            int blockX = 0;
            int blockY = 0;
            int blockZ = 0;
            uint16_t blockId = 0;
            bool areaInteraction = false;
            float areaMinX = 0.0f;
            float areaMinY = 0.0f;
            float areaMinZ = 0.0f;
            float areaMaxX = 0.0f;
            float areaMaxY = 0.0f;
            float areaMaxZ = 0.0f;
            Vec3 areaResultPosition{};
            std::vector<ItemInteractionActionMenu> actions;
        };

        const std::vector<ItemDefinition>* itemDefinitions_ = nullptr;
        world::WorldRuntime* worldRuntime_ = nullptr;
        int hotbarSelectedSlot_ = 0;
        BlockBreakingState blockBreaking_;
        PlayerInventory playerInventory_;
        world::DroppedItemRuntime droppedItemRuntime_;
        PendingItemInteraction pendingItemInteraction_;
    };
}
