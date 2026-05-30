#pragma once

#include "camera/Camera.h"
#include "items/ItemData.h"
#include "world/BlockData.h"
#include "world/WorldTypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace dolbuto::world
{
    class DroppedItemSystem
    {
    public:
        using RuntimeChunkMap = std::unordered_map<uint64_t, RuntimeChunk>;
        using EntityIdProvider = std::function<uint64_t()>;
        using TerrainCollisionPredicate = std::function<bool(int, int, int)>;
        using InventoryInsertCallback = std::function<uint16_t(ItemStack)>;
        using DirtyChunkCallback = std::function<void(RuntimeChunk&)>;
        using ChunkTrackingCallback = std::function<void(uint64_t)>;
        using PickupSoundCallback = std::function<void()>;

        struct Bounds
        {
            float halfWidth = 0.34f;
            float height = 0.05f;
        };

        static constexpr size_t MaxDroppedItems = 1024;
        static constexpr float DroppedItemSize = 0.68f;
        static constexpr float DroppedItemThickness = 0.05f;
        static constexpr float BlockModelDroppedItemSize = 0.2f;
        static constexpr float DroppedItemTickSeconds = 1.0f / 20.0f;
        static constexpr float DroppedItemMaxFrameSeconds = 0.25f;
        static constexpr float DroppedItemRenderDistance = 48.0f;
        static constexpr float DroppedItemRenderDistanceSquared = DroppedItemRenderDistance * DroppedItemRenderDistance;
        static constexpr size_t MaxDroppedItemRenderInstances = MaxDroppedItems * 4u;

        static int blockCoordinateXz(double worldCoordinate);
        static int blockCoordinateY(double worldCoordinate);
        static uint64_t chunkKey(int chunkX, int chunkZ);
        static uint64_t entityChunkKey(const WorldEntity& entity);

        static size_t countDroppedItemsInChunk(const RuntimeChunk& chunk);
        static size_t visualCopyCount(uint16_t count);
        static Bounds boundsForStack(const ItemStack& stack, const std::vector<ItemDefinition>& itemDefinitions);
        static bool grounded(const WorldEntity& entity);
        static void setGrounded(WorldEntity& entity, bool grounded);
        static bool touchesPlayerCollider(const WorldEntity& item, Vec3 playerPosition, const std::vector<ItemDefinition>& itemDefinitions);

        static std::vector<WorldEntity> createBlockDropEntities(
            int x,
            int y,
            int z,
            const BlockDefinition& definition,
            const std::vector<ItemDefinition>& itemDefinitions,
            const EntityIdProvider& allocateEntityId);

        static WorldEntity createManualDropEntity(
            ItemStack stack,
            DVec3 sourcePosition,
            Vec3 direction,
            const EntityIdProvider& allocateEntityId);

        static void updateTick(
            RuntimeChunkMap& runtimeChunks,
            const std::vector<ItemDefinition>& itemDefinitions,
            Vec3 playerPosition,
            float dt,
            const TerrainCollisionPredicate& terrainCellBlocksPlayer,
            const InventoryInsertCallback& addToPlayerInventory,
            const PickupSoundCallback& playPickupSound,
            const DirtyChunkCallback& markDirty,
            const ChunkTrackingCallback& refreshTracking);

    private:
        static int floorDiv(int value, int divisor);
    };
}
