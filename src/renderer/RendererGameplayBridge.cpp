#include "renderer/RendererGameplayBridge.h"

#include "gameplay/BlockInteractionSystem.h"
#include "renderer/ParticleRenderPath.h"
#include "game/ClientRuntimeState.h"
#include "renderer/RendererVulkanState.h"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace dolbuto
{
    RendererGameplayBridge::RendererGameplayBridge(
        game::ClientRuntimeState& client,
        const RendererVulkanState& vulkan,
        ParticleRenderPath& particleRenderPath,
        Hooks hooks) :
        client_(client),
        vulkan_(vulkan),
        particleRenderPath_(particleRenderPath),
        hooks_(std::move(hooks))
    {
    }

    bool RendererGameplayBridge::editBlockInView(DVec3 origin, Vec3 direction, bool placeBlock, uint16_t placeBlockId, DVec3 playerPosition, double playerHeightScale)
    {
        return applyBlockEditResult(
            client_.gameplayRuntime.editBlockInView(
                origin,
                direction,
                placeBlock,
                placeBlockId,
                playerPosition,
                playerHeightScale,
                [this](int x, int y, int z) { return blockAtWorld(x, y, z); },
                [this](uint16_t block) -> const BlockDefinition& { return blockDefinition(block); },
                [this](uint16_t block) { return client_.content.propMeshForBlock(block); },
                [this](int x, int y, int z, uint16_t block)
                {
                    return hooks_.setBlockAtWorld && hooks_.setBlockAtWorld(x, y, z, block);
                },
                [this](RuntimeChunk& chunk)
                {
                    if (hooks_.markRuntimeChunkDataDirty)
                    {
                        hooks_.markRuntimeChunkDataDirty(chunk);
                    }
                }));
    }

    bool RendererGameplayBridge::placeSelectedItemBlockInView(DVec3 origin, Vec3 direction, DVec3 playerPosition, double playerHeightScale)
    {
        return applyBlockEditResult(
            client_.gameplayRuntime.placeSelectedItemBlockInView(
                origin,
                direction,
                playerPosition,
                playerHeightScale,
                [this](int x, int y, int z) { return blockAtWorld(x, y, z); },
                [this](uint16_t block) -> const BlockDefinition& { return blockDefinition(block); },
                [this](uint16_t block) { return client_.content.propMeshForBlock(block); },
                [this](int x, int y, int z, uint16_t block)
                {
                    return hooks_.setBlockAtWorld && hooks_.setBlockAtWorld(x, y, z, block);
                },
                [this](DVec3 min, DVec3 max)
                {
                    return terrainAabbIntersects(min, max);
                },
                [this](RuntimeChunk& chunk)
                {
                    if (hooks_.markRuntimeChunkDataDirty)
                    {
                        hooks_.markRuntimeChunkDataDirty(chunk);
                    }
                }));
    }

    void RendererGameplayBridge::updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, float deltaSeconds, bool sandboxMode)
    {
        const gameplay::BlockBreakingUpdate update = client_.gameplayRuntime.updateBlockBreaking(
            origin,
            direction,
            breaking,
            deltaSeconds,
            sandboxMode,
            [this](int x, int y, int z) { return blockAtWorld(x, y, z); },
            [this](uint16_t block) -> const BlockDefinition& { return blockDefinition(block); },
            [this](uint16_t block) { return client_.content.propMeshForBlock(block); });

        if (update.spawnMiningParticle)
        {
            spawnBlockMiningParticle(update.hit, update.block);
        }
        if (update.breakBlock)
        {
            breakBlockAtHit(update.hit, update.durabilityCost);
        }
    }

    bool RendererGameplayBridge::pickupDroppedItemInView(DVec3 origin, Vec3 direction)
    {
        return client_.gameplayRuntime.pickupDroppedItemInView(
            origin,
            direction,
            [this](RuntimeChunk& chunk)
            {
                if (hooks_.markRuntimeChunkDataDirty)
                {
                    hooks_.markRuntimeChunkDataDirty(chunk);
                }
            });
    }

    bool RendererGameplayBridge::dropSelectedHotbarItem(bool wholeStack, DVec3 sourcePosition, Vec3 direction)
    {
        const bool dropped = client_.gameplayRuntime.dropSelectedHotbarItem(
            wholeStack,
            sourcePosition,
            direction,
            [this](RuntimeChunk& chunk)
            {
                if (hooks_.markRuntimeChunkDataDirty)
                {
                    hooks_.markRuntimeChunkDataDirty(chunk);
                }
            });
        if (!dropped)
        {
            return false;
        }
        if (hooks_.updateInventoryUi)
        {
            hooks_.updateInventoryUi();
        }
        return true;
    }

    gameplay::ItemInteractionMenu RendererGameplayBridge::beginItemInteractionInView(
        DVec3 origin,
        Vec3 direction,
        bool preferHeldItemBlockActions)
    {
        return client_.gameplayRuntime.beginItemInteractionInView(
            origin,
            direction,
            preferHeldItemBlockActions,
            client_.content.itemInteractionRecipes(),
            [this](int x, int y, int z) { return blockAtWorld(x, y, z); },
            [this](uint16_t block) -> const BlockDefinition& { return blockDefinition(block); },
            [this](uint16_t block) { return client_.content.propMeshForBlock(block); });
    }

    bool RendererGameplayBridge::executePendingItemInteraction(std::size_t actionIndex, std::size_t candidateIndex, bool repeat)
    {
        const gameplay::ItemInteractionExecuteResult result = client_.gameplayRuntime.executePendingItemInteraction(
            actionIndex,
            candidateIndex,
            repeat,
            [this](int x, int y, int z, uint16_t block)
            {
                return hooks_.setBlockAtWorld && hooks_.setBlockAtWorld(x, y, z, block);
            },
            [this](RuntimeChunk& chunk)
            {
                if (hooks_.markRuntimeChunkDataDirty)
                {
                    hooks_.markRuntimeChunkDataDirty(chunk);
                }
            });
        for (const gameplay::BlockEditResult& edit : result.blockEdits)
        {
            applyBlockEditResult(edit);
        }
        if (result.executed && hooks_.updateInventoryUi)
        {
            hooks_.updateInventoryUi();
            client_.uiBridge.updateItemTooltipUi(vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height);
        }
        return result.executed;
    }

    void RendererGameplayBridge::cancelPendingItemInteraction()
    {
        client_.gameplayRuntime.cancelPendingItemInteraction();
    }

    bool RendererGameplayBridge::tickHeldBurningItems(bool extinguishHeldBurnableLights)
    {
        const bool changed = client_.gameplayRuntime.tickHeldBurningItems(extinguishHeldBurnableLights);
        if (changed && hooks_.updateInventoryUi)
        {
            hooks_.updateInventoryUi();
            client_.uiBridge.updateItemTooltipUi(vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height);
        }
        return changed;
    }

    void RendererGameplayBridge::tickBlockUpdates()
    {
        constexpr uint32_t MaxBlockTickCells = 256;
        const gameplay::BlockTickResult result = client_.gameplayRuntime.tickBlockUpdates(
            MaxBlockTickCells,
            [this](uint16_t block) -> const BlockDefinition& { return blockDefinition(block); },
            client_.content.itemProcessingRecipes(),
            [this](int x, int y, int z, uint16_t block)
            {
                return hooks_.setBlockAtWorld && hooks_.setBlockAtWorld(x, y, z, block);
            },
            [this](RuntimeChunk& chunk)
            {
                if (hooks_.markRuntimeChunkDataDirty)
                {
                    hooks_.markRuntimeChunkDataDirty(chunk);
                }
            });

        for (const gameplay::FireSmokeRateUpdate& update : result.fireSmokeRateUpdates)
        {
            particleRenderPath_.setFireEmitterSmokeStyle(update.x, update.y, update.z, update.multiplier, update.textureSet);
        }

        std::vector<std::array<int, 3>> rebuildBlocks;
        rebuildBlocks.reserve(result.brokenBlocks.size());
        for (const gameplay::BlockBreakEvent& broken : result.brokenBlocks)
        {
            spawnBlockBreakParticles(broken.x, broken.y, broken.z, broken.block);
            if (broken.playSound && hooks_.playBlockBreakSound)
            {
                hooks_.playBlockBreakSound(broken.x, broken.y, broken.z);
            }
            rebuildBlocks.push_back({broken.x, broken.y, broken.z});
        }
        if (!rebuildBlocks.empty())
        {
            if (hooks_.rebuildEditedChunkMeshesBatch)
            {
                hooks_.rebuildEditedChunkMeshesBatch(rebuildBlocks);
            }
            else if (hooks_.rebuildEditedChunkMeshes)
            {
                for (const std::array<int, 3>& block : rebuildBlocks)
                {
                    hooks_.rebuildEditedChunkMeshes(block[0], block[1], block[2]);
                }
            }
        }
    }

    void RendererGameplayBridge::setInventorySnapshot(const std::array<ItemStack, gameplay::PlayerInventory::SlotCount>& slots)
    {
        client_.gameplayRuntime.setInventorySnapshot(slots);
        if (hooks_.updateInventoryUi)
        {
            hooks_.updateInventoryUi();
        }
        client_.uiBridge.updateInventoryDebugSlots();
        client_.uiBridge.updateInventoryCursorUi();
        client_.uiBridge.updateItemTooltipUi(vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height);
    }

    void RendererGameplayBridge::setOffhandSlot(ItemStack stack)
    {
        client_.gameplayRuntime.setOffhandSlot(stack);
        if (hooks_.updateInventoryUi)
        {
            hooks_.updateInventoryUi();
        }
        client_.uiBridge.updateInventoryDebugSlots();
        client_.uiBridge.updateInventoryCursorUi();
        client_.uiBridge.updateItemTooltipUi(vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height);
    }

    bool RendererGameplayBridge::swapSelectedHotbarWithOffhand()
    {
        if (!client_.gameplayRuntime.swapSelectedHotbarWithOffhand())
        {
            return false;
        }
        if (hooks_.updateInventoryUi)
        {
            hooks_.updateInventoryUi();
        }
        client_.uiBridge.updateInventoryDebugSlots();
        client_.uiBridge.updateInventoryCursorUi();
        client_.uiBridge.updateItemTooltipUi(vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height);
        return true;
    }

    const BlockDefinition& RendererGameplayBridge::blockDefinition(uint16_t block) const
    {
        static const BlockDefinition fallback{};
        if (static_cast<size_t>(block) >= client_.content.blockDefinitions().size())
        {
            return fallback;
        }
        return client_.content.blockDefinitions()[block];
    }

    uint16_t RendererGameplayBridge::blockAtWorld(int x, int y, int z) const
    {
        return client_.worldRuntime.blockAtWorld(x, y, z);
    }

    bool RendererGameplayBridge::applyBlockEditResult(const gameplay::BlockEditResult& result)
    {
        if (!result.changed)
        {
            return false;
        }

        if (result.type == gameplay::BlockEditType::Break)
        {
            spawnBlockBreakParticles(result.hit.blockX, result.hit.blockY, result.hit.blockZ, result.block);
            if (hooks_.playBlockBreakSound)
            {
                hooks_.playBlockBreakSound(result.hit.blockX, result.hit.blockY, result.hit.blockZ);
            }
            if (hooks_.rebuildEditedChunkMeshes)
            {
                hooks_.rebuildEditedChunkMeshes(result.hit.blockX, result.hit.blockY, result.hit.blockZ);
            }
            if (result.inventoryChanged && hooks_.updateInventoryUi)
            {
                hooks_.updateInventoryUi();
                client_.uiBridge.updateItemTooltipUi(vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height);
            }
            return true;
        }

        if (result.type == gameplay::BlockEditType::Place)
        {
            if (hooks_.rebuildEditedChunkMeshes)
            {
                hooks_.rebuildEditedChunkMeshes(result.hit.previousBlockX, result.hit.previousBlockY, result.hit.previousBlockZ);
            }
            if (hooks_.playBlockPlaceSound)
            {
                hooks_.playBlockPlaceSound(result.hit.previousBlockX, result.hit.previousBlockY, result.hit.previousBlockZ);
            }
            if (result.inventoryChanged && hooks_.updateInventoryUi)
            {
                hooks_.updateInventoryUi();
                client_.uiBridge.updateItemTooltipUi(vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height);
            }
            return true;
        }

        return false;
    }

    bool RendererGameplayBridge::breakBlockAtHit(const BlockRaycastHit& hit, uint16_t durabilityCost)
    {
        return applyBlockEditResult(
            client_.gameplayRuntime.breakBlockAtHit(
                hit,
                durabilityCost,
                [this](int x, int y, int z) { return blockAtWorld(x, y, z); },
                [this](uint16_t block) -> const BlockDefinition& { return blockDefinition(block); },
                [this](int x, int y, int z, uint16_t block)
                {
                    return hooks_.setBlockAtWorld && hooks_.setBlockAtWorld(x, y, z, block);
                },
                [this](RuntimeChunk& chunk)
                {
                    if (hooks_.markRuntimeChunkDataDirty)
                    {
                        hooks_.markRuntimeChunkDataDirty(chunk);
                    }
                }));
    }

    void RendererGameplayBridge::resetBlockBreaking()
    {
        client_.gameplayRuntime.resetBlockBreaking();
    }

    bool RendererGameplayBridge::terrainCellBlocksPlayer(int x, int y, int z) const
    {
        return client_.worldRuntime.terrainCellBlocksPlayer(
            x,
            y,
            z,
            [this](uint16_t block) -> const BlockDefinition& { return blockDefinition(block); });
    }

    bool RendererGameplayBridge::terrainAabbIntersects(DVec3 min, DVec3 max) const
    {
        constexpr double Epsilon = 0.000001;
        const int minX = world::WorldRuntime::blockCoordinateXz(min.x);
        const int maxX = world::WorldRuntime::blockCoordinateXz(max.x - Epsilon);
        const int minY = world::WorldRuntime::blockCoordinateY(min.y);
        const int maxY = world::WorldRuntime::blockCoordinateY(max.y - Epsilon);
        const int minZ = world::WorldRuntime::blockCoordinateXz(min.z);
        const int maxZ = world::WorldRuntime::blockCoordinateXz(max.z - Epsilon);

        for (int y = minY; y <= maxY; ++y)
        {
            for (int z = minZ; z <= maxZ; ++z)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    if (client_.worldRuntime.terrainCellIntersectsAabb(
                            x,
                            y,
                            z,
                            min,
                            max,
                            [this](uint16_t block) -> const BlockDefinition&
                            {
                                return blockDefinition(block);
                            }))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    uint32_t RendererGameplayBridge::blockFaceTextureLayer(uint16_t block, int face) const
    {
        if (face < 0 || face >= 6 || static_cast<size_t>(block) >= client_.content.blockTextureLayers().size())
        {
            return 0;
        }

        return client_.content.blockTextureLayers()[block].faces[static_cast<size_t>(face)];
    }

    uint32_t RendererGameplayBridge::blockFaceTextureLayerForHit(uint16_t block, const BlockRaycastHit& hit) const
    {
        const int dx = hit.previousBlockX - hit.blockX;
        const int dy = hit.previousBlockY - hit.blockY;
        const int dz = hit.previousBlockZ - hit.blockZ;
        if (dy > 0)
        {
            return blockFaceTextureLayer(block, 0);
        }
        if (dy < 0)
        {
            return blockFaceTextureLayer(block, 1);
        }
        if (dx != 0 || dz != 0)
        {
            return blockFaceTextureLayer(block, 2);
        }
        return blockFaceTextureLayer(block, 0);
    }

    void RendererGameplayBridge::spawnBlockBreakParticles(int x, int y, int z, uint16_t block)
    {
        const BlockDefinition& definition = blockDefinition(block);
        if (definition.renderType == BlockRenderType::None || !definition.breakEffectParticles)
        {
            return;
        }

        particleRenderPath_.spawnBlockBreak(x, y, z, block, blockFaceTextureLayer(block, 0));
    }

    void RendererGameplayBridge::spawnBlockMiningParticle(const BlockRaycastHit& hit, uint16_t block)
    {
        const BlockDefinition& definition = blockDefinition(block);
        if (definition.renderType == BlockRenderType::None)
        {
            return;
        }

        particleRenderPath_.spawnMiningParticle(
            ParticleRenderPath::MiningHit{
                hit.blockX,
                hit.blockY,
                hit.blockZ,
                hit.previousBlockX,
                hit.previousBlockY,
                hit.previousBlockZ
            },
            blockFaceTextureLayerForHit(block, hit));
    }

}
