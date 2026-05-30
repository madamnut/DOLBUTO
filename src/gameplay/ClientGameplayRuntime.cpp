#include "gameplay/ClientGameplayRuntime.h"

#include "world/BlockData.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace dolbuto::gameplay
{
    namespace
    {
        constexpr uint16_t BlockAir = 0;

        bool recipeTargetsBlock(const ItemInteractionRecipe& recipe, uint16_t block)
        {
            return recipe.targetAnyBlock || recipe.targetBlockId == block;
        }
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
        DVec3 sourcePosition,
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

        const uint16_t dropCount = wholeStack ? slot.count : 1u;
        ItemStack dropStack{};
        dropStack.itemId = slot.itemId;
        dropStack.count = dropCount;
        dropStack.durability = slot.durability;

        WorldEntity item = droppedItemRuntime_.createManualDropEntity(dropStack, sourcePosition, direction);
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
        bool preferHeldItemBlockActions,
        const std::vector<ItemInteractionRecipe>& recipes,
        const BlockSampler& blockAtWorld,
        const BlockDefinitionProvider& blockDefinition,
        const BlockInteractionSystem::PropMeshProvider& propMesh)
    {
        pendingItemInteraction_ = {};

        const std::size_t slotIndex = static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9));
        const ItemStack& heldStack = playerInventory_.slot(slotIndex);
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        std::vector<std::string> heldUseActions;
        if (heldStack.itemId != 0 &&
            heldStack.count != 0 &&
            static_cast<std::size_t>(heldStack.itemId) < definitions.size())
        {
            heldUseActions = definitions[heldStack.itemId].useActions;
        }

        BlockRaycastHit interactionBlockHit{};
        uint16_t interactionBlock = BlockAir;
        bool hasBlockTarget = false;
        bool hasInteractionBlock = false;
        if (blockAtWorld && blockDefinition &&
            BlockInteractionSystem::raycastBlock(origin, direction, blockAtWorld, blockDefinition, interactionBlockHit, propMesh))
        {
            interactionBlock = blockAtWorld(interactionBlockHit.blockX, interactionBlockHit.blockY, interactionBlockHit.blockZ);
            hasBlockTarget = interactionBlock != BlockAir;
            hasInteractionBlock = hasBlockTarget &&
                !blockDefinition(interactionBlock).interactActions.empty();
        }
        bool heldHasBlockTargetAction = false;
        if (hasBlockTarget)
        {
            for (const std::string& heldAction : heldUseActions)
            {
                for (const ItemInteractionRecipe& recipe : recipes)
                {
                    if (recipe.action == heldAction &&
                        !recipe.candidates.empty() &&
                        recipeTargetsBlock(recipe, interactionBlock))
                    {
                        heldHasBlockTargetAction = true;
                        break;
                    }
                }
                if (heldHasBlockTargetAction)
                {
                    break;
                }
            }
        }

        world::DroppedItemRuntime::Target target{};
        if ((!hasInteractionBlock || preferHeldItemBlockActions) &&
            !heldHasBlockTargetAction &&
            !(preferHeldItemBlockActions && hasBlockTarget) &&
            droppedItemRuntime_.targetInView(origin, direction, target) &&
            target.stack.itemId != 0 &&
            static_cast<std::size_t>(target.stack.itemId) < definitions.size())
        {
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
            if (menu.hasUseTarget)
            {
                struct AvailableAction
                {
                    std::string action;
                    bool consumesHeldDurability = false;
                };
                std::vector<AvailableAction> availableActions{{"handcraft", false}};
                for (const std::string& action : heldUseActions)
                {
                    const auto existing = std::find_if(
                        availableActions.begin(),
                        availableActions.end(),
                        [&](const AvailableAction& availableAction)
                        {
                            return availableAction.action == action;
                        });
                    if (existing == availableActions.end())
                    {
                        availableActions.push_back(AvailableAction{action, true});
                    }
                }

                for (const AvailableAction& availableAction : availableActions)
                {
                    for (const ItemInteractionRecipe& recipe : recipes)
                    {
                        if (recipe.action != availableAction.action ||
                            recipe.targetItemId != target.stack.itemId ||
                            !recipe.ingredients.empty() ||
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

                        ItemInteractionActionMenu actionMenu{};
                        actionMenu.action = availableAction.action;
                        actionMenu.targetCount = recipe.targetCount;
                        actionMenu.candidates = std::move(candidates);
                        actionMenu.actions = {availableAction.action};
                        actionMenu.consumesHeldDurability = availableAction.consumesHeldDurability;
                        menu.actions.push_back(std::move(actionMenu));
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
        }

        if (!hasBlockTarget)
        {
            return {};
        }
        const BlockRaycastHit hit = interactionBlockHit;
        const BlockDefinition& definition = blockDefinition(interactionBlock);

        const float areaMinX = static_cast<float>(hit.blockX) - 0.5f;
        const float areaMinY = static_cast<float>(hit.blockY + 1);
        const float areaMinZ = static_cast<float>(hit.blockZ) - 0.5f;
        const float areaMaxX = static_cast<float>(hit.blockX) + 0.5f;
        const float areaMaxY = static_cast<float>(hit.blockY + 2);
        const float areaMaxZ = static_cast<float>(hit.blockZ) + 0.5f;
        const Vec3 resultPosition{
            static_cast<float>(hit.blockX),
            static_cast<float>(hit.blockY + 1) + 0.05f,
            static_cast<float>(hit.blockZ)
        };

        std::unordered_map<uint16_t, uint32_t> areaCounts;
        for (const world::DroppedItemRuntime::Target& areaTarget : droppedItemRuntime_.targetsInAabb(areaMinX, areaMinY, areaMinZ, areaMaxX, areaMaxY, areaMaxZ))
        {
            areaCounts[areaTarget.stack.itemId] += areaTarget.stack.count;
        }

        auto recipeCandidatesForAction = [&](const std::string& action)
        {
            std::vector<ItemInteractionCandidate> candidates;
            for (const ItemInteractionRecipe& recipe : recipes)
            {
                if (recipe.action != action || recipe.targetItemId == 0 || recipe.candidates.empty())
                {
                    continue;
                }

                std::vector<ItemInteractionIngredient> ingredients;
                ingredients.push_back(ItemInteractionIngredient{recipe.targetItemId, recipe.targetCount});
                ingredients.insert(ingredients.end(), recipe.ingredients.begin(), recipe.ingredients.end());

                if (areaCounts[recipe.targetItemId] == 0)
                {
                    continue;
                }

                bool hasEnoughIngredients = true;
                for (const ItemInteractionIngredient& ingredient : ingredients)
                {
                    if (ingredient.itemId == 0 || ingredient.count == 0 || areaCounts[ingredient.itemId] < ingredient.count)
                    {
                        hasEnoughIngredients = false;
                        break;
                    }
                }

                for (ItemInteractionCandidate candidate : recipe.candidates)
                {
                    candidate.ingredients = ingredients;
                    candidate.enabled = hasEnoughIngredients;
                    candidates.push_back(std::move(candidate));
                }
            }
            return candidates;
        };

        auto blockCandidatesForAction = [&](const std::string& action)
        {
            std::vector<ItemInteractionCandidate> candidates;
            const int placeX = hit.blockX;
            const int placeY = hit.blockY + 1;
            const int placeZ = hit.blockZ;
            const bool hasSolidSupport = blockDefinition(interactionBlock).collision;
            const bool canPlaceAbove = blockAtWorld &&
                blockAtWorld(placeX, placeY, placeZ) == BlockAir &&
                hasSolidSupport;
            for (const ItemInteractionRecipe& recipe : recipes)
            {
                if (recipe.action != action ||
                    recipe.candidates.empty() ||
                    !recipeTargetsBlock(recipe, interactionBlock))
                {
                    continue;
                }

                for (ItemInteractionCandidate candidate : recipe.candidates)
                {
                    if (candidate.placeBlockId == 0)
                    {
                        continue;
                    }
                    candidate.enabled = canPlaceAbove &&
                        (candidate.placeBlockPlacement.empty() || candidate.placeBlockPlacement == "above_target");
                    candidates.push_back(std::move(candidate));
                }
            }
            return candidates;
        };

        ItemInteractionMenu menu{};
        menu.hasUseTarget = true;
        if (hasInteractionBlock && !preferHeldItemBlockActions)
        {
            for (const std::string& blockAction : definition.interactActions)
            {
                std::vector<ItemInteractionCandidate> candidates;
                if (blockAction == "craft")
                {
                    std::vector<ItemInteractionCandidate> handcraftCandidates = recipeCandidatesForAction("handcraft");
                    candidates.insert(
                        candidates.end(),
                        std::make_move_iterator(handcraftCandidates.begin()),
                        std::make_move_iterator(handcraftCandidates.end()));
                    std::vector<ItemInteractionCandidate> craftCandidates = recipeCandidatesForAction("craft");
                    candidates.insert(
                        candidates.end(),
                        std::make_move_iterator(craftCandidates.begin()),
                        std::make_move_iterator(craftCandidates.end()));
                }
                else
                {
                    candidates = recipeCandidatesForAction(blockAction);
                }

                ItemInteractionActionMenu actionMenu{};
                actionMenu.action = blockAction;
                actionMenu.targetCount = 1;
                actionMenu.candidates = std::move(candidates);
                actionMenu.actions = {blockAction};
                actionMenu.areaInteraction = true;
                menu.actions.push_back(std::move(actionMenu));

                for (const std::string& heldAction : heldUseActions)
                {
                    std::vector<ItemInteractionCandidate> toolCandidates = recipeCandidatesForAction(heldAction);
                    if (toolCandidates.empty())
                    {
                        continue;
                    }

                    ItemInteractionActionMenu toolActionMenu{};
                    toolActionMenu.action = blockAction;
                    toolActionMenu.targetCount = 1;
                    toolActionMenu.candidates = std::move(toolCandidates);
                    toolActionMenu.actions = {blockAction, heldAction};
                    toolActionMenu.consumesHeldDurability = true;
                    toolActionMenu.areaInteraction = true;
                    menu.actions.push_back(std::move(toolActionMenu));
                }
            }
        }

        if (!hasInteractionBlock || preferHeldItemBlockActions)
        {
            for (const std::string& heldAction : heldUseActions)
            {
                std::vector<ItemInteractionCandidate> candidates = blockCandidatesForAction(heldAction);
                if (candidates.empty())
                {
                    continue;
                }

                ItemInteractionActionMenu actionMenu{};
                actionMenu.action = heldAction;
                actionMenu.targetCount = 1;
                actionMenu.candidates = std::move(candidates);
                actionMenu.actions = {heldAction};
                actionMenu.consumesHeldDurability = true;
                actionMenu.areaInteraction = false;
                menu.actions.push_back(std::move(actionMenu));
            }
        }

        if (menu.actions.empty())
        {
            if (hasInteractionBlock && !preferHeldItemBlockActions)
            {
                return menu;
            }
            return {};
        }
        menu.available = true;
        pendingItemInteraction_.active = true;
        pendingItemInteraction_.heldSlotIndex = slotIndex;
        pendingItemInteraction_.blockInteraction = true;
        pendingItemInteraction_.blockX = hit.blockX;
        pendingItemInteraction_.blockY = hit.blockY;
        pendingItemInteraction_.blockZ = hit.blockZ;
        pendingItemInteraction_.blockId = interactionBlock;
        pendingItemInteraction_.areaInteraction = true;
        pendingItemInteraction_.areaMinX = areaMinX;
        pendingItemInteraction_.areaMinY = areaMinY;
        pendingItemInteraction_.areaMinZ = areaMinZ;
        pendingItemInteraction_.areaMaxX = areaMaxX;
        pendingItemInteraction_.areaMaxY = areaMaxY;
        pendingItemInteraction_.areaMaxZ = areaMaxZ;
        pendingItemInteraction_.areaResultPosition = resultPosition;
        pendingItemInteraction_.actions = menu.actions;
        return menu;
    }

    ItemInteractionExecuteResult ClientGameplayRuntime::executePendingItemInteraction(
        std::size_t actionIndex,
        std::size_t candidateIndex,
        bool repeat,
        const SetBlockFn& setBlockAtWorld,
        const MarkDirtyFn& markDirty)
    {
        ItemInteractionExecuteResult result{};
        if (!pendingItemInteraction_.active ||
            actionIndex >= pendingItemInteraction_.actions.size() ||
            candidateIndex >= pendingItemInteraction_.actions[actionIndex].candidates.size())
        {
            pendingItemInteraction_ = {};
            return result;
        }

        const ItemInteractionCandidate candidate = pendingItemInteraction_.actions[actionIndex].candidates[candidateIndex];
        const ItemInteractionActionMenu actionMenu = pendingItemInteraction_.actions[actionIndex];
        if ((candidate.outputs.empty() && candidate.placeBlockId == 0) || !candidate.enabled)
        {
            pendingItemInteraction_ = {};
            return result;
        }
        const std::size_t heldSlotIndex = pendingItemInteraction_.heldSlotIndex;
        const WorldEntityHandle targetHandle = pendingItemInteraction_.targetHandle;
        const uint64_t targetEntityId = pendingItemInteraction_.targetEntityId;
        const bool areaInteraction = actionMenu.areaInteraction;
        const bool blockInteraction = pendingItemInteraction_.blockInteraction;
        const int blockX = pendingItemInteraction_.blockX;
        const int blockY = pendingItemInteraction_.blockY;
        const int blockZ = pendingItemInteraction_.blockZ;
        const float areaMinX = pendingItemInteraction_.areaMinX;
        const float areaMinY = pendingItemInteraction_.areaMinY;
        const float areaMinZ = pendingItemInteraction_.areaMinZ;
        const float areaMaxX = pendingItemInteraction_.areaMaxX;
        const float areaMaxY = pendingItemInteraction_.areaMaxY;
        const float areaMaxZ = pendingItemInteraction_.areaMaxZ;
        const Vec3 areaResultPosition = pendingItemInteraction_.areaResultPosition;
        const ItemStack heldStack = playerInventory_.slot(heldSlotIndex);
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        uint16_t maxApplications = 1;
        bool consumesDurability = actionMenu.consumesHeldDurability;
        if (repeat)
        {
            maxApplications = UINT16_MAX;
            if (consumesDurability &&
                heldStack.itemId != 0 &&
                static_cast<std::size_t>(heldStack.itemId) < definitions.size())
            {
                const uint16_t maxDurability = definitions[heldStack.itemId].maxDurability;
                if (maxDurability > 0)
                {
                    maxApplications = heldStack.durability == 0
                        ? maxDurability
                        : std::min(heldStack.durability, maxDurability);
                }
                else
                {
                    consumesDurability = false;
                }
            }
        }
        else if (consumesDurability &&
            (heldStack.itemId == 0 ||
                static_cast<std::size_t>(heldStack.itemId) >= definitions.size() ||
                definitions[heldStack.itemId].maxDurability == 0))
        {
            consumesDurability = false;
        }
        pendingItemInteraction_ = {};
        if (candidate.placeBlockId != 0)
        {
            if (!blockInteraction ||
                !(candidate.placeBlockPlacement.empty() || candidate.placeBlockPlacement == "above_target") ||
                !setBlockAtWorld)
            {
                return result;
            }

            const int placeX = blockX;
            const int placeY = blockY + 1;
            const int placeZ = blockZ;
            if (worldRuntime_ == nullptr || worldRuntime_->blockAtWorld(placeX, placeY, placeZ) != BlockAir)
            {
                return result;
            }
            if (!setBlockAtWorld(placeX, placeY, placeZ, candidate.placeBlockId))
            {
                return result;
            }

            BlockEditResult edit{};
            edit.changed = true;
            edit.type = BlockEditType::Place;
            edit.hit.blockX = blockX;
            edit.hit.blockY = blockY;
            edit.hit.blockZ = blockZ;
            edit.hit.previousBlockX = placeX;
            edit.hit.previousBlockY = placeY;
            edit.hit.previousBlockZ = placeZ;
            edit.block = candidate.placeBlockId;
            result.blockEdits.push_back(edit);
            result.executed = true;
            if (consumesDurability)
            {
                result.inventoryChanged = playerInventory_.damageSlot(heldSlotIndex, 1, itemDefinitions());
            }
            return result;
        }

        const uint16_t applicationCount = areaInteraction
            ? droppedItemRuntime_.replaceAreaItems(
                areaMinX,
                areaMinY,
                areaMinZ,
                areaMaxX,
                areaMaxY,
                areaMaxZ,
                candidate.ingredients,
                candidate.outputs,
                maxApplications,
                areaResultPosition,
                markDirty)
            : droppedItemRuntime_.replaceTargetItems(
                targetHandle,
                targetEntityId,
                candidate.outputs,
                actionMenu.targetCount,
                maxApplications,
                markDirty);
        if (applicationCount == 0)
        {
            return result;
        }

        result.executed = true;
        if (consumesDurability)
        {
            result.inventoryChanged = playerInventory_.damageSlot(heldSlotIndex, applicationCount, itemDefinitions());
        }
        return result;
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
