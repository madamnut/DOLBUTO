#include "renderer/RendererGameplayBridge.h"

#include "gameplay/BlockInteractionSystem.h"
#include "renderer/ParticleRenderPath.h"
#include "game/ClientRuntimeState.h"
#include "renderer/RendererVulkanState.h"

#include <cstddef>
#include <utility>

namespace dolbuto
{
    namespace
    {
        constexpr uint16_t BlockRock = 1;
    }

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

    bool RendererGameplayBridge::editBlockInView(DVec3 origin, Vec3 direction, bool placeRock, DVec3 playerPosition)
    {
        return applyBlockEditResult(
            client_.gameplayRuntime.editBlockInView(
                origin,
                direction,
                placeRock,
                BlockRock,
                playerPosition,
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

    void RendererGameplayBridge::updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, float deltaSeconds)
    {
        const gameplay::BlockBreakingUpdate update = client_.gameplayRuntime.updateBlockBreaking(
            origin,
            direction,
            breaking,
            deltaSeconds,
            [this](int x, int y, int z) { return blockAtWorld(x, y, z); },
            [this](uint16_t block) -> const BlockDefinition& { return blockDefinition(block); });

        if (update.spawnMiningParticle)
        {
            spawnBlockMiningParticle(update.hit, update.block);
        }
        if (update.breakBlock)
        {
            breakBlockAtHit(update.hit);
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

    bool RendererGameplayBridge::dropSelectedHotbarItem(bool wholeStack, DVec3 playerPosition, Vec3 direction)
    {
        const bool dropped = client_.gameplayRuntime.dropSelectedHotbarItem(
            wholeStack,
            playerPosition,
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
            return true;
        }

        return false;
    }

    bool RendererGameplayBridge::breakBlockAtHit(const BlockRaycastHit& hit)
    {
        return applyBlockEditResult(
            client_.gameplayRuntime.breakBlockAtHit(
                hit,
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
        if (definition.renderType == BlockRenderType::None)
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
