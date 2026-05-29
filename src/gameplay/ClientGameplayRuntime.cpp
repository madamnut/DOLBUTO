#include "gameplay/ClientGameplayRuntime.h"

#include "world/BlockData.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace dolbuto::gameplay
{
    namespace
    {
        constexpr uint16_t BlockAir = 0;
    }

    ClientGameplayRuntime::ClientGameplayRuntime(world::WorldRuntime* worldRuntime, const std::vector<ItemDefinition>* itemDefinitions)
        : itemDefinitions_(itemDefinitions),
        worldRuntime_(worldRuntime),
        droppedItemRuntime_(worldRuntime, itemDefinitions)
    {
    }

    void ClientGameplayRuntime::setContext(world::WorldRuntime* worldRuntime, const std::vector<ItemDefinition>* itemDefinitions)
    {
        itemDefinitions_ = itemDefinitions;
        worldRuntime_ = worldRuntime;
        droppedItemRuntime_.setContext(worldRuntime, itemDefinitions);
    }

    const std::vector<ItemDefinition>& ClientGameplayRuntime::itemDefinitions() const
    {
        if (itemDefinitions_ == nullptr)
        {
            throw std::runtime_error("ClientGameplayRuntime item definitions are not initialized.");
        }
        return *itemDefinitions_;
    }

    bool ClientGameplayRuntime::playerColliderIntersectsTerrain(
        DVec3 playerPosition,
        double heightScale,
        const TerrainCollisionPredicate& terrainCellBlocksPlayer) const
    {
        return BlockInteractionSystem::playerColliderIntersectsTerrain(playerPosition, heightScale, terrainCellBlocksPlayer);
    }

    bool ClientGameplayRuntime::playerColliderHasSupportBelow(
        DVec3 playerPosition,
        const TerrainCollisionPredicate& terrainCellBlocksPlayer) const
    {
        return BlockInteractionSystem::playerColliderHasSupportBelow(playerPosition, terrainCellBlocksPlayer);
    }

    bool ClientGameplayRuntime::playerColliderIntersectsWater(
        DVec3 playerPosition,
        double heightScale,
        const FluidSampler& fluidAtWorld) const
    {
        return BlockInteractionSystem::playerColliderIntersectsWater(playerPosition, heightScale, fluidAtWorld);
    }

    BlockEditResult ClientGameplayRuntime::editBlockInView(
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
        const MarkDirtyFn& markDirty)
    {
        BlockRaycastHit hit{};
        if (!BlockInteractionSystem::raycastBlock(origin, direction, blockAtWorld, blockDefinition, hit, propMesh))
        {
            return {};
        }

        if (!placeBlock)
        {
            return breakBlockAtHit(hit, 0, blockAtWorld, blockDefinition, setBlockAtWorld, markDirty);
        }

        if (BlockInteractionSystem::blockIntersectsPlayerCollider(
                hit.previousBlockX,
                hit.previousBlockY,
                hit.previousBlockZ,
                blockDefinition(placeBlockId),
                playerPosition,
                playerHeightScale))
        {
            return {};
        }

        if (!setBlockAtWorld || !setBlockAtWorld(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ, placeBlockId))
        {
            return {};
        }

        BlockEditResult result{};
        result.changed = true;
        result.type = BlockEditType::Place;
        result.hit = hit;
        result.block = placeBlockId;
        return result;
    }

    BlockEditResult ClientGameplayRuntime::breakBlockAtHit(
        const BlockRaycastHit& hit,
        uint16_t durabilityCost,
        const BlockSampler& blockAtWorld,
        const BlockDefinitionProvider& blockDefinition,
        const SetBlockFn& setBlockAtWorld,
        const MarkDirtyFn& markDirty)
    {
        const uint16_t destroyedBlock = blockAtWorld ? blockAtWorld(hit.blockX, hit.blockY, hit.blockZ) : BlockAir;
        if (destroyedBlock == BlockAir || blockDefinition(destroyedBlock).hardness < 0.0f)
        {
            return {};
        }

        if (!setBlockAtWorld || !setBlockAtWorld(hit.blockX, hit.blockY, hit.blockZ, BlockAir))
        {
            return {};
        }

        droppedItemRuntime_.spawnBlockDrops(
            hit.blockX,
            hit.blockY,
            hit.blockZ,
            blockDefinition(destroyedBlock),
            markDirty);

        BlockEditResult result{};
        result.changed = true;
        result.type = BlockEditType::Break;
        result.hit = hit;
        result.block = destroyedBlock;
        result.inventoryChanged = damageSelectedHotbarItem(durabilityCost);
        return result;
    }

    BlockBreakingUpdate ClientGameplayRuntime::updateBlockBreaking(
        DVec3 origin,
        Vec3 direction,
        bool breaking,
        float deltaSeconds,
        bool sandboxMode,
        const BlockSampler& blockAtWorld,
        const BlockDefinitionProvider& blockDefinition,
        const BlockInteractionSystem::PropMeshProvider& propMesh)
    {
        return BlockInteractionSystem::updateBreaking(
            blockBreaking_,
            origin,
            direction,
            breaking,
            deltaSeconds,
            sandboxMode,
            currentBlockBreakTool(),
            blockAtWorld,
            blockDefinition,
            propMesh);
    }

    BlockTickResult ClientGameplayRuntime::tickBlockUpdates(
        uint32_t maxCells,
        const BlockDefinitionProvider& blockDefinition,
        const SetBlockFn& setBlockAtWorld,
        const MarkDirtyFn& markDirty)
    {
        BlockTickResult result{};
        if (worldRuntime_ == nullptr || !setBlockAtWorld)
        {
            return result;
        }

        const std::vector<world::WorldRuntime::BlockTickCell> cells = worldRuntime_->takeScheduledBlockTicks(maxCells);
        for (const world::WorldRuntime::BlockTickCell& cell : cells)
        {
            const uint16_t block = worldRuntime_->blockAtWorld(cell.x, cell.y, cell.z);
            if (block == BlockAir)
            {
                continue;
            }

            const BlockDefinition& definition = blockDefinition(block);
            if (definition.attachmentFace == BlockAttachmentFace::None)
            {
                continue;
            }

            bool attached = true;
            if (definition.attachmentFace == BlockAttachmentFace::Bottom)
            {
                const uint16_t support = worldRuntime_->blockAtWorld(cell.x, cell.y - 1, cell.z);
                attached = support != BlockAir && blockDefinition(support).collision;
            }
            if (attached)
            {
                continue;
            }

            if (!setBlockAtWorld(cell.x, cell.y, cell.z, BlockAir))
            {
                continue;
            }

            droppedItemRuntime_.spawnBlockDrops(cell.x, cell.y, cell.z, definition, markDirty);
            result.brokenBlocks.push_back(BlockBreakEvent{
                cell.x,
                cell.y,
                cell.z,
                block
            });
        }

        return result;
    }

    void ClientGameplayRuntime::resetBlockBreaking()
    {
        BlockInteractionSystem::resetBreaking(blockBreaking_);
    }

    const BlockBreakingState& ClientGameplayRuntime::blockBreakingState() const
    {
        return blockBreaking_;
    }

    bool ClientGameplayRuntime::pickupDroppedItemInView(DVec3 origin, Vec3 direction, const MarkDirtyFn& markDirty)
    {
        return droppedItemRuntime_.pickupInView(origin, direction, markDirty);
    }

    bool ClientGameplayRuntime::dropSelectedHotbarItem(
        bool wholeStack,
        DVec3 playerPosition,
        Vec3 direction,
        const MarkDirtyFn& markDirty)
    {
        const std::size_t slotIndex = static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9));
        const ItemStack& slot = playerInventory_.slot(slotIndex);
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        if (slot.itemId == 0 || slot.count == 0 || static_cast<std::size_t>(slot.itemId) >= definitions.size())
        {
            return false;
        }

        (void)wholeStack;
        const uint16_t dropCount = 1u;
        ItemStack dropStack{};
        dropStack.itemId = slot.itemId;
        dropStack.count = dropCount;
        dropStack.durability = slot.durability;

        WorldEntity item = droppedItemRuntime_.createManualDropEntity(dropStack, playerPosition, direction);
        if (!droppedItemRuntime_.addWorldEntity(std::move(item), markDirty))
        {
            return false;
        }

        return playerInventory_.removeFromSlot(slotIndex, dropCount);
    }

    BlockEditResult ClientGameplayRuntime::placeSelectedItemBlockInView(
        DVec3 origin,
        Vec3 direction,
        DVec3 playerPosition,
        double playerHeightScale,
        const BlockSampler& blockAtWorld,
        const BlockDefinitionProvider& blockDefinition,
        const BlockInteractionSystem::PropMeshProvider& propMesh,
        const SetBlockFn& setBlockAtWorld,
        const TerrainCollisionPredicate& terrainCellBlocksItem,
        const MarkDirtyFn& markDirty)
    {
        const std::size_t slotIndex = static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9));
        const ItemStack& heldStack = playerInventory_.slot(slotIndex);
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        if (heldStack.itemId == 0 ||
            heldStack.count == 0 ||
            static_cast<std::size_t>(heldStack.itemId) >= definitions.size())
        {
            return {};
        }

        const ItemDefinition& heldDefinition = definitions[heldStack.itemId];
        if (heldDefinition.placeBlockId == BlockAir ||
            std::find(heldDefinition.placeActions.begin(), heldDefinition.placeActions.end(), "place") == heldDefinition.placeActions.end())
        {
            return {};
        }

        BlockRaycastHit hit{};
        if (!BlockInteractionSystem::raycastBlock(origin, direction, blockAtWorld, blockDefinition, hit, propMesh))
        {
            return {};
        }
        if (blockAtWorld && blockAtWorld(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ) != BlockAir)
        {
            return {};
        }
        if (BlockInteractionSystem::blockIntersectsPlayerCollider(
                hit.previousBlockX,
                hit.previousBlockY,
                hit.previousBlockZ,
                blockDefinition(heldDefinition.placeBlockId),
                playerPosition,
                playerHeightScale))
        {
            return {};
        }
        if (!setBlockAtWorld || !setBlockAtWorld(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ, heldDefinition.placeBlockId))
        {
            return {};
        }

        droppedItemRuntime_.pushItemsOutOfBlock(
            hit.previousBlockX,
            hit.previousBlockY,
            hit.previousBlockZ,
            terrainCellBlocksItem,
            markDirty);

        BlockEditResult result{};
        result.changed = true;
        result.type = BlockEditType::Place;
        result.hit = hit;
        result.block = heldDefinition.placeBlockId;
        result.inventoryChanged = playerInventory_.removeFromSlot(slotIndex, 1);
        return result;
    }

    ItemInteractionMenu ClientGameplayRuntime::beginItemInteractionInView(
        DVec3 origin,
        Vec3 direction,
        const std::vector<ItemInteractionRecipe>& recipes)
    {
        pendingItemInteraction_ = {};

        const std::size_t slotIndex = static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9));
        const ItemStack& heldStack = playerInventory_.slot(slotIndex);
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        world::DroppedItemRuntime::Target target{};
        if (!droppedItemRuntime_.targetInView(origin, direction, target) ||
            target.stack.itemId == 0 ||
            static_cast<std::size_t>(target.stack.itemId) >= definitions.size())
        {
            return {};
        }

        ItemInteractionMenu menu{};
        menu.targetItemId = target.stack.itemId;
        for (const ItemInteractionRecipe& recipe : recipes)
        {
            if (recipe.targetItemId == target.stack.itemId)
            {
                menu.hasUseTarget = true;
                break;
            }
        }
        if (!menu.hasUseTarget)
        {
            return {};
        }

        std::vector<std::string> availableActions{"handcraft"};
        if (heldStack.itemId != 0 &&
            heldStack.count != 0 &&
            static_cast<std::size_t>(heldStack.itemId) < definitions.size())
        {
            for (const std::string& action : definitions[heldStack.itemId].useActions)
            {
                if (std::find(availableActions.begin(), availableActions.end(), action) == availableActions.end())
                {
                    availableActions.push_back(action);
                }
            }
        }
        for (const std::string& action : availableActions)
        {
            for (const ItemInteractionRecipe& recipe : recipes)
            {
                if (recipe.action != action ||
                    recipe.targetItemId != target.stack.itemId ||
                    recipe.candidates.empty())
                {
                    continue;
                }

                std::vector<ItemInteractionCandidate> candidates = recipe.candidates;
                const bool hasEnoughTargetItems = target.stack.count >= recipe.targetCount;
                for (ItemInteractionCandidate& candidate : candidates)
                {
                    candidate.enabled = hasEnoughTargetItems;
                }
                menu.actions.push_back(ItemInteractionActionMenu{
                    action,
                    recipe.targetCount,
                    std::move(candidates)
                });
                break;
            }
        }

        if (menu.actions.empty())
        {
            return menu;
        }

        menu.available = true;
        pendingItemInteraction_.active = true;
        pendingItemInteraction_.heldSlotIndex = slotIndex;
        pendingItemInteraction_.targetHandle = target.handle;
        pendingItemInteraction_.targetEntityId = target.entityId;
        pendingItemInteraction_.actions = menu.actions;
        return menu;
    }

    bool ClientGameplayRuntime::executePendingItemInteraction(std::size_t actionIndex, std::size_t candidateIndex, bool repeat, const MarkDirtyFn& markDirty)
    {
        if (!pendingItemInteraction_.active ||
            actionIndex >= pendingItemInteraction_.actions.size() ||
            candidateIndex >= pendingItemInteraction_.actions[actionIndex].candidates.size())
        {
            pendingItemInteraction_ = {};
            return false;
        }

        const ItemInteractionCandidate candidate = pendingItemInteraction_.actions[actionIndex].candidates[candidateIndex];
        const uint16_t targetCount = pendingItemInteraction_.actions[actionIndex].targetCount;
        if (candidate.outputs.empty())
        {
            pendingItemInteraction_ = {};
            return false;
        }
        const std::size_t heldSlotIndex = pendingItemInteraction_.heldSlotIndex;
        const WorldEntityHandle targetHandle = pendingItemInteraction_.targetHandle;
        const uint64_t targetEntityId = pendingItemInteraction_.targetEntityId;
        const ItemStack heldStack = playerInventory_.slot(heldSlotIndex);
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        uint16_t maxApplications = 1;
        bool consumesDurability = false;
        if (repeat)
        {
            maxApplications = UINT16_MAX;
            if (heldStack.itemId != 0 && static_cast<std::size_t>(heldStack.itemId) < definitions.size())
            {
                const uint16_t maxDurability = definitions[heldStack.itemId].maxDurability;
                if (maxDurability > 0)
                {
                    consumesDurability = true;
                    maxApplications = heldStack.durability == 0
                        ? maxDurability
                        : std::min(heldStack.durability, maxDurability);
                }
            }
        }
        else if (heldStack.itemId != 0 && static_cast<std::size_t>(heldStack.itemId) < definitions.size())
        {
            consumesDurability = definitions[heldStack.itemId].maxDurability > 0;
        }
        pendingItemInteraction_ = {};
        const uint16_t applicationCount = droppedItemRuntime_.replaceTargetItems(
            targetHandle,
            targetEntityId,
            candidate.outputs,
            targetCount,
            maxApplications,
            markDirty);
        if (applicationCount == 0)
        {
            return false;
        }

        if (consumesDurability)
        {
            playerInventory_.damageSlot(heldSlotIndex, applicationCount, itemDefinitions());
        }
        return true;
    }

    void ClientGameplayRuntime::cancelPendingItemInteraction()
    {
        pendingItemInteraction_ = {};
    }

    bool ClientGameplayRuntime::updateDroppedItems(
        Vec3 playerPosition,
        double now,
        const world::DroppedItemRuntime::TerrainCollisionFn& terrainCellBlocksPlayer,
        const PickupSoundFn& playPickupSound,
        const MarkDirtyFn& markDirty)
    {
        bool inventoryChanged = false;
        droppedItemRuntime_.update(
            playerPosition,
            now,
            terrainCellBlocksPlayer,
            [this, &inventoryChanged](ItemStack stack)
            {
                const uint16_t originalCount = stack.count;
                const uint16_t remaining = addItemToPlayerInventory(stack);
                inventoryChanged = inventoryChanged || remaining != originalCount;
                return remaining;
            },
            playPickupSound,
            markDirty);
        return inventoryChanged;
    }

    void ClientGameplayRuntime::reserveDroppedItemTracking(std::size_t capacity)
    {
        droppedItemRuntime_.reserveTracking(capacity);
    }

    void ClientGameplayRuntime::refreshDroppedItemChunkTracking(uint64_t key)
    {
        droppedItemRuntime_.refreshChunkTracking(key);
    }

    void ClientGameplayRuntime::removeDroppedItemChunkTracking(uint64_t key)
    {
        droppedItemRuntime_.removeChunkTracking(key);
    }

    void ClientGameplayRuntime::resetDroppedItemTracking()
    {
        droppedItemRuntime_.resetTracking();
    }

    void ClientGameplayRuntime::resetForScene(double timestamp)
    {
        resetBlockBreaking();
        droppedItemRuntime_.resetForScene(timestamp);
        clearInventory();
    }

    void ClientGameplayRuntime::resetForUnload()
    {
        resetBlockBreaking();
        droppedItemRuntime_.resetForUnload();
        clearInventory();
    }

    void ClientGameplayRuntime::normalizeLoadedEntity(WorldEntity& entity)
    {
        droppedItemRuntime_.normalizeLoadedEntity(entity);
    }

    std::size_t ClientGameplayRuntime::loadedDroppedItemCount() const
    {
        return droppedItemRuntime_.loadedItemCount();
    }

    float ClientGameplayRuntime::droppedItemRenderAlpha() const
    {
        return droppedItemRuntime_.renderAlpha();
    }

    const std::unordered_map<uint64_t, std::size_t>& ClientGameplayRuntime::droppedItemTrackedChunkCounts() const
    {
        return droppedItemRuntime_.trackedChunkCounts();
    }

    void ClientGameplayRuntime::setHotbarSelectedSlot(int slot)
    {
        hotbarSelectedSlot_ = std::clamp(slot, 0, 9);
    }

    int ClientGameplayRuntime::hotbarSelectedSlot() const
    {
        return hotbarSelectedSlot_;
    }

    std::size_t ClientGameplayRuntime::inventorySlotCount() const
    {
        return playerInventory_.slotCount();
    }

    const ItemStack& ClientGameplayRuntime::inventorySlot(std::size_t index) const
    {
        return playerInventory_.slot(index);
    }

    const ItemStack& ClientGameplayRuntime::inventoryCursorStack() const
    {
        return playerInventory_.cursorStack();
    }

    void ClientGameplayRuntime::clearInventory()
    {
        playerInventory_.clear();
    }

    std::array<ItemStack, PlayerInventory::SlotCount> ClientGameplayRuntime::inventorySnapshot() const
    {
        return playerInventory_.snapshot();
    }

    void ClientGameplayRuntime::setInventorySnapshot(const std::array<ItemStack, PlayerInventory::SlotCount>& slots)
    {
        playerInventory_.setSlots(slots, itemDefinitions());
    }

    uint16_t ClientGameplayRuntime::addItemToPlayerInventory(ItemStack stack)
    {
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        if (stack.itemId == 0 || stack.count == 0 || static_cast<std::size_t>(stack.itemId) >= definitions.size())
        {
            return stack.count;
        }

        const uint16_t maxStack = definitions[stack.itemId].stackSize;
        if (maxStack == 0)
        {
            return stack.count;
        }

        return playerInventory_.add(stack, definitions);
    }

    bool ClientGameplayRuntime::handleInventorySlotClick(std::size_t slotIndex, InventoryClickButton button, bool shift)
    {
        return playerInventory_.handleSlotClick(slotIndex, button, shift, itemDefinitions());
    }

    bool ClientGameplayRuntime::swapHotbarWithSlot(std::size_t slotIndex, std::size_t hotbarSlot)
    {
        return playerInventory_.swapHotbarWithSlot(slotIndex, hotbarSlot);
    }

    bool ClientGameplayRuntime::closeInventoryCursor()
    {
        return playerInventory_.closeCursor(itemDefinitions());
    }

    BlockBreakTool ClientGameplayRuntime::currentBlockBreakTool() const
    {
        BlockBreakTool tool{};
        const std::size_t slotIndex = static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9));
        const ItemStack& heldStack = playerInventory_.slot(slotIndex);
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        if (heldStack.itemId == 0 ||
            heldStack.count == 0 ||
            static_cast<std::size_t>(heldStack.itemId) >= definitions.size())
        {
            return tool;
        }

        const ItemDefinition& definition = definitions[heldStack.itemId];
        if (definition.breakActions.empty() && definition.breakLevel == 0)
        {
            return tool;
        }

        tool.level = definition.breakLevel;
        tool.actions = definition.breakActions;
        tool.durable = definition.maxDurability > 0;
        return tool;
    }

    bool ClientGameplayRuntime::damageSelectedHotbarItem(uint16_t damage)
    {
        if (damage == 0)
        {
            return false;
        }
        const std::size_t slotIndex = static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9));
        return playerInventory_.damageSlot(slotIndex, damage, itemDefinitions());
    }
}
