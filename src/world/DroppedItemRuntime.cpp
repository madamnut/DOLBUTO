#include "world/DroppedItemRuntime.h"

#include "world/DroppedItemSystem.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace dolbuto::world
{
    DroppedItemRuntime::DroppedItemRuntime(WorldRuntime* worldRuntime, const std::vector<ItemDefinition>* itemDefinitions)
        : worldRuntime_(worldRuntime),
        itemDefinitions_(itemDefinitions)
    {
    }

    void DroppedItemRuntime::setContext(WorldRuntime* worldRuntime, const std::vector<ItemDefinition>* itemDefinitions)
    {
        worldRuntime_ = worldRuntime;
        itemDefinitions_ = itemDefinitions;
    }

    WorldRuntime& DroppedItemRuntime::worldRuntime()
    {
        if (worldRuntime_ == nullptr)
        {
            throw std::runtime_error("DroppedItemRuntime world runtime is not initialized.");
        }
        return *worldRuntime_;
    }

    const WorldRuntime& DroppedItemRuntime::worldRuntime() const
    {
        if (worldRuntime_ == nullptr)
        {
            throw std::runtime_error("DroppedItemRuntime world runtime is not initialized.");
        }
        return *worldRuntime_;
    }

    const std::vector<ItemDefinition>& DroppedItemRuntime::itemDefinitions() const
    {
        if (itemDefinitions_ == nullptr)
        {
            throw std::runtime_error("DroppedItemRuntime item definitions are not initialized.");
        }
        return *itemDefinitions_;
    }

    uint64_t DroppedItemRuntime::allocateEntityId()
    {
        if (nextEntityId_ == 0)
        {
            nextEntityId_ = 1;
        }
        return nextEntityId_++;
    }

    void DroppedItemRuntime::resetEntityIds()
    {
        nextEntityId_ = 1;
    }

    void DroppedItemRuntime::reserveTracking(std::size_t capacity)
    {
        droppedItemCountsByChunk_.reserve(capacity);
    }

    void DroppedItemRuntime::resetTracking()
    {
        droppedItemCountsByChunk_.clear();
        loadedItemCount_ = 0;
    }

    void DroppedItemRuntime::resetFrameClock(double timestamp)
    {
        lastUpdateTime_ = timestamp;
        tickAccumulator_ = 0.0f;
        renderAlpha_ = 0.0f;
    }

    void DroppedItemRuntime::resetForScene(double timestamp)
    {
        resetEntityIds();
        resetTracking();
        resetFrameClock(timestamp);
    }

    void DroppedItemRuntime::resetForUnload()
    {
        resetEntityIds();
        resetTracking();
        resetFrameClock();
    }

    void DroppedItemRuntime::normalizeLoadedEntity(WorldEntity& entity)
    {
        if (entity.entityId == 0)
        {
            entity.entityId = allocateEntityId();
        }
        nextEntityId_ = std::max(nextEntityId_, entity.entityId + 1u);
        entity.previousPosition = entity.position;
        entity.collecting = false;
        entity.collectAge = 0.0f;
        if (DroppedItemSystem::grounded(entity))
        {
            entity.renderRotationX = 0.0f;
            entity.renderRotation = std::fmod(entity.renderRotation, 6.2831853f);
            entity.renderRotationZ = 0.0f;
            entity.renderSpinX = 0.0f;
            entity.renderSpin = 0.0f;
            entity.renderSpinZ = 0.0f;
        }
        else
        {
            entity.renderSpinX = entity.renderSpinX == 0.0f ? 5.0f : entity.renderSpinX;
            entity.renderSpin = entity.renderSpin == 0.0f ? 5.0f : entity.renderSpin;
            entity.renderSpinZ = entity.renderSpinZ == 0.0f ? 5.0f : entity.renderSpinZ;
        }
    }

    uint64_t DroppedItemRuntime::entityChunkKey(const WorldEntity& entity) const
    {
        return DroppedItemSystem::entityChunkKey(entity);
    }

    RuntimeChunk* DroppedItemRuntime::runtimeChunkForEntity(const WorldEntity& entity)
    {
        RuntimeChunk* chunk = worldRuntime().find(entityChunkKey(entity));
        if (chunk == nullptr || !chunk->data)
        {
            return nullptr;
        }
        return chunk;
    }

    bool DroppedItemRuntime::addWorldEntity(WorldEntity entity, const MarkDirtyFn& markDirty)
    {
        if (entity.entityId == 0)
        {
            entity.entityId = allocateEntityId();
        }

        if (entity.type == WorldEntityType::DroppedItem)
        {
            mergeIntoNearby(entity, markDirty);
            if (entity.droppedItem.stack.count == 0)
            {
                return true;
            }
        }

        RuntimeChunk* chunk = runtimeChunkForEntity(entity);
        if (chunk == nullptr || !chunk->data)
        {
            return false;
        }

        if (chunk->data->entities.size() >= DroppedItemSystem::MaxDroppedItems)
        {
            chunk->data->entities.erase(chunk->data->entities.begin());
        }
        chunk->data->entities.push_back(std::move(entity));
        refreshChunkTracking(entityChunkKey(chunk->data->entities.back()));
        if (markDirty)
        {
            markDirty(*chunk);
        }
        return true;
    }

    std::size_t DroppedItemRuntime::countDroppedItemsInChunk(const RuntimeChunk& chunk) const
    {
        return DroppedItemSystem::countDroppedItemsInChunk(chunk);
    }

    void DroppedItemRuntime::refreshChunkTracking(uint64_t key)
    {
        const auto oldIt = droppedItemCountsByChunk_.find(key);
        const std::size_t oldCount = oldIt != droppedItemCountsByChunk_.end() ? oldIt->second : 0u;

        std::size_t newCount = 0;
        const RuntimeChunk* chunk = worldRuntime().find(key);
        if (chunk != nullptr)
        {
            newCount = countDroppedItemsInChunk(*chunk);
        }

        if (newCount > 0)
        {
            droppedItemCountsByChunk_[key] = newCount;
        }
        else if (oldIt != droppedItemCountsByChunk_.end())
        {
            droppedItemCountsByChunk_.erase(oldIt);
        }

        loadedItemCount_ = loadedItemCount_ - oldCount + newCount;
    }

    void DroppedItemRuntime::removeChunkTracking(uint64_t key)
    {
        const auto oldIt = droppedItemCountsByChunk_.find(key);
        if (oldIt == droppedItemCountsByChunk_.end())
        {
            return;
        }

        loadedItemCount_ -= oldIt->second;
        droppedItemCountsByChunk_.erase(oldIt);
    }

    uint16_t DroppedItemRuntime::mergeIntoNearby(WorldEntity& source, const MarkDirtyFn& markDirty)
    {
        return DroppedItemSystem::mergeIntoNearby(
            source,
            worldRuntime().chunks(),
            itemDefinitions(),
            markDirty,
            [this](uint64_t key)
            {
                refreshChunkTracking(key);
            });
    }

    void DroppedItemRuntime::spawnBlockDrops(int x, int y, int z, const BlockDefinition& block, const MarkDirtyFn& markDirty)
    {
        std::vector<WorldEntity> drops = DroppedItemSystem::createBlockDropEntities(
            x,
            y,
            z,
            block,
            itemDefinitions(),
            [this]()
            {
                return allocateEntityId();
            });
        for (WorldEntity& item : drops)
        {
            addWorldEntity(std::move(item), markDirty);
        }
    }

    WorldEntity DroppedItemRuntime::createManualDropEntity(ItemStack stack, DVec3 playerPosition, Vec3 direction)
    {
        return DroppedItemSystem::createManualDropEntity(
            stack,
            playerPosition,
            direction,
            [this]()
            {
                return allocateEntityId();
            });
    }

    bool DroppedItemRuntime::raycast(DVec3 origin, Vec3 direction, WorldEntityHandle& itemHandle) const
    {
        constexpr double MaxInteractionDistance = 8.0;
        constexpr double Epsilon = 0.000001;

        const Vec3 normalizedDirection = normalize(direction);
        if (normalizedDirection.x == 0.0f && normalizedDirection.y == 0.0f && normalizedDirection.z == 0.0f)
        {
            return false;
        }

        auto rayIntersectsAabb = [&](const WorldEntity& item, double& hitDistance)
        {
            const double halfWidth = static_cast<double>(DroppedItemSystem::DroppedItemSize) * 0.5;
            const double minX = static_cast<double>(item.position.x) - halfWidth;
            const double maxX = static_cast<double>(item.position.x) + halfWidth;
            const double minY = static_cast<double>(item.position.y);
            const double maxY = static_cast<double>(item.position.y) + static_cast<double>(DroppedItemSystem::DroppedItemThickness);
            const double minZ = static_cast<double>(item.position.z) - halfWidth;
            const double maxZ = static_cast<double>(item.position.z) + halfWidth;

            double tMin = 0.0;
            double tMax = MaxInteractionDistance;
            auto testAxis = [&](double axisOrigin, double axisDirection, double axisMin, double axisMax)
            {
                if (std::abs(axisDirection) < Epsilon)
                {
                    return axisOrigin >= axisMin && axisOrigin <= axisMax;
                }

                double t0 = (axisMin - axisOrigin) / axisDirection;
                double t1 = (axisMax - axisOrigin) / axisDirection;
                if (t0 > t1)
                {
                    std::swap(t0, t1);
                }
                tMin = std::max(tMin, t0);
                tMax = std::min(tMax, t1);
                return tMin <= tMax;
            };

            if (!testAxis(origin.x, normalizedDirection.x, minX, maxX) ||
                !testAxis(origin.y, normalizedDirection.y, minY, maxY) ||
                !testAxis(origin.z, normalizedDirection.z, minZ, maxZ))
            {
                return false;
            }

            hitDistance = tMin;
            return hitDistance >= 0.0 && hitDistance <= MaxInteractionDistance;
        };

        bool found = false;
        double bestDistance = MaxInteractionDistance;
        for (const auto& entry : worldRuntime().chunks())
        {
            const RuntimeChunk& chunk = entry.second;
            if (!chunk.data)
            {
                continue;
            }
            for (std::size_t i = 0; i < chunk.data->entities.size(); ++i)
            {
                const WorldEntity& item = chunk.data->entities[i];
                if (item.type != WorldEntityType::DroppedItem ||
                    item.droppedItem.stack.itemId == 0 ||
                    item.droppedItem.stack.count == 0 ||
                    item.collecting)
                {
                    continue;
                }

                double hitDistance = 0.0;
                if (rayIntersectsAabb(item, hitDistance) && hitDistance <= bestDistance)
                {
                    bestDistance = hitDistance;
                    itemHandle.chunkKey = entry.first;
                    itemHandle.index = i;
                    found = true;
                }
            }
        }

        return found;
    }

    bool DroppedItemRuntime::pickupInView(DVec3 origin, Vec3 direction, const MarkDirtyFn& markDirty)
    {
        WorldEntityHandle itemHandle{};
        if (!raycast(origin, direction, itemHandle))
        {
            return false;
        }

        RuntimeChunk* chunk = worldRuntime().find(itemHandle.chunkKey);
        if (chunk == nullptr || !chunk->data ||
            itemHandle.index >= chunk->data->entities.size())
        {
            return false;
        }

        WorldEntity& item = chunk->data->entities[itemHandle.index];
        item.collecting = true;
        item.collectAge = 0.0f;
        DroppedItemSystem::setGrounded(item, false);
        item.velocity = {};
        item.previousPosition = item.position;
        item.renderSpinX = 8.0f;
        item.renderSpin = 8.0f;
        item.renderSpinZ = 8.0f;
        if (markDirty)
        {
            markDirty(*chunk);
        }
        return true;
    }

    void DroppedItemRuntime::update(
        Vec3 playerPosition,
        double now,
        const TerrainCollisionFn& terrainCellBlocksPlayer,
        const AddInventoryFn& addToPlayerInventory,
        const PickupSoundFn& playPickupSound,
        const MarkDirtyFn& markDirty)
    {
        if (lastUpdateTime_ <= 0.0)
        {
            lastUpdateTime_ = now;
            renderAlpha_ = 0.0f;
            return;
        }

        const float frameDt = static_cast<float>(std::clamp(
            now - lastUpdateTime_,
            0.0,
            static_cast<double>(DroppedItemSystem::DroppedItemMaxFrameSeconds)));
        lastUpdateTime_ = now;
        if (frameDt <= 0.0f)
        {
            renderAlpha_ = 0.0f;
            return;
        }
        if (loadedItemCount() == 0)
        {
            tickAccumulator_ = 0.0f;
            renderAlpha_ = 0.0f;
            return;
        }

        for (auto& entry : worldRuntime().chunks())
        {
            RuntimeChunk& chunk = entry.second;
            if (!chunk.data)
            {
                continue;
            }
            for (WorldEntity& item : chunk.data->entities)
            {
                if (item.type != WorldEntityType::DroppedItem)
                {
                    continue;
                }
                if (!DroppedItemSystem::grounded(item) || item.collecting)
                {
                    item.renderRotationX += item.renderSpinX * frameDt;
                    item.renderRotation += item.renderSpin * frameDt;
                    item.renderRotationZ += item.renderSpinZ * frameDt;
                }
                else
                {
                    item.renderRotationX = 0.0f;
                    item.renderRotationZ = 0.0f;
                }
            }
        }

        tickAccumulator_ += frameDt;
        while (tickAccumulator_ >= DroppedItemSystem::DroppedItemTickSeconds)
        {
            updateTick(
                playerPosition,
                DroppedItemSystem::DroppedItemTickSeconds,
                terrainCellBlocksPlayer,
                addToPlayerInventory,
                playPickupSound,
                markDirty);
            tickAccumulator_ -= DroppedItemSystem::DroppedItemTickSeconds;
        }
        renderAlpha_ = std::clamp(tickAccumulator_ / DroppedItemSystem::DroppedItemTickSeconds, 0.0f, 1.0f);
    }

    void DroppedItemRuntime::updateTick(
        Vec3 playerPosition,
        float dt,
        const TerrainCollisionFn& terrainCellBlocksPlayer,
        const AddInventoryFn& addToPlayerInventory,
        const PickupSoundFn& playPickupSound,
        const MarkDirtyFn& markDirty)
    {
        if (dt <= 0.0f || loadedItemCount() == 0)
        {
            return;
        }

        DroppedItemSystem::updateTick(
            worldRuntime().chunks(),
            itemDefinitions(),
            playerPosition,
            dt,
            terrainCellBlocksPlayer,
            addToPlayerInventory,
            playPickupSound,
            markDirty,
            [this](uint64_t key)
            {
                refreshChunkTracking(key);
            });
    }

    std::size_t DroppedItemRuntime::loadedItemCount() const
    {
        return loadedItemCount_;
    }

    float DroppedItemRuntime::renderAlpha() const
    {
        return renderAlpha_;
    }

    const std::unordered_map<uint64_t, std::size_t>& DroppedItemRuntime::trackedChunkCounts() const
    {
        return droppedItemCountsByChunk_;
    }
}
