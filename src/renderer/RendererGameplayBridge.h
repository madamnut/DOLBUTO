#pragma once

#include "gameplay/ClientGameplayRuntime.h"
#include "gameplay/PlayerInventory.h"
#include "items/ItemData.h"
#include "world/BlockData.h"
#include "world/WorldTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace dolbuto
{
    class ParticleRenderPath;
    namespace game
    {
        struct ClientRuntimeState;
    }
    struct RendererVulkanState;

    class RendererGameplayBridge
    {
    public:
        struct Hooks
        {
            std::function<void()> updateInventoryUi;
            std::function<void(RuntimeChunk&)> markRuntimeChunkDataDirty;
            std::function<bool(int, int, int, uint16_t)> setBlockAtWorld;
            std::function<void(int, int, int)> rebuildEditedChunkMeshes;
            std::function<void(int, int, int)> playBlockBreakSound;
            std::function<void(int, int, int)> playBlockPlaceSound;
        };

        RendererGameplayBridge(
            game::ClientRuntimeState& client,
            const RendererVulkanState& vulkan,
            ParticleRenderPath& particleRenderPath,
            Hooks hooks);

        void updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, float deltaSeconds, bool sandboxMode);
        bool editBlockInView(DVec3 origin, Vec3 direction, bool placeBlock, uint16_t placeBlockId, DVec3 playerPosition, double playerHeightScale);
        bool placeSelectedItemBlockInView(DVec3 origin, Vec3 direction, DVec3 playerPosition, double playerHeightScale);
        bool pickupDroppedItemInView(DVec3 origin, Vec3 direction);
        bool dropSelectedHotbarItem(bool wholeStack, DVec3 sourcePosition, Vec3 direction);
        gameplay::ItemInteractionMenu beginItemInteractionInView(DVec3 origin, Vec3 direction, bool preferHeldItemBlockActions);
        bool executePendingItemInteraction(std::size_t actionIndex, std::size_t candidateIndex, bool repeat);
        void cancelPendingItemInteraction();
        void setInventorySnapshot(const std::array<ItemStack, gameplay::PlayerInventory::SlotCount>& slots);
        void tickBlockUpdates();

        const BlockDefinition& blockDefinition(uint16_t block) const;
        uint16_t blockAtWorld(int x, int y, int z) const;
        bool terrainCellBlocksPlayer(int x, int y, int z) const;
        bool terrainAabbIntersects(DVec3 min, DVec3 max) const;
        uint32_t blockFaceTextureLayer(uint16_t block, int face) const;
        void resetBlockBreaking();

    private:
        using BlockRaycastHit = gameplay::BlockRaycastHit;

        bool applyBlockEditResult(const gameplay::BlockEditResult& result);
        bool breakBlockAtHit(const BlockRaycastHit& hit, uint16_t durabilityCost);
        uint32_t blockFaceTextureLayerForHit(uint16_t block, const BlockRaycastHit& hit) const;
        void spawnBlockBreakParticles(int x, int y, int z, uint16_t block);
        void spawnBlockMiningParticle(const BlockRaycastHit& hit, uint16_t block);

        game::ClientRuntimeState& client_;
        const RendererVulkanState& vulkan_;
        ParticleRenderPath& particleRenderPath_;
        Hooks hooks_;
    };
}
