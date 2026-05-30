#include "world/DroppedItemRuntime.h"

#include "world/DroppedItemSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace dolbuto::world
{
    namespace
    {
        uint16_t interactionResultDurability(
            const ItemStack& source,
            const ItemDefinition& sourceDefinition,
            const ItemDefinition& resultDefinition)
        {
            const uint16_t resultMaxDurability = resultDefinition.maxDurability;
            if (resultMaxDurability == 0)
            {
                return 0;
            }

            const uint16_t sourceMaxDurability = sourceDefinition.maxDurability;
            if (sourceMaxDurability == 0)
            {
                return resultMaxDurability;
            }

            const uint32_t sourceDurability = source.durability == 0
                ? sourceMaxDurability
                : std::min<uint16_t>(source.durability, sourceMaxDurability);
            const uint32_t numerator = sourceDurability * static_cast<uint32_t>(resultMaxDurability);
            return static_cast<uint16_t>(std::max<uint32_t>(1u, (numerator + sourceMaxDurability - 1u) / sourceMaxDurability));
        }

        bool droppedItemOverlapsAabb(
            const WorldEntity& item,
            const std::vector<ItemDefinition>& itemDefinitions,
            float minX,
            float minY,
            float minZ,
            float maxX,
            float maxY,
            float maxZ)
        {
            const DroppedItemSystem::Bounds bounds = DroppedItemSystem::boundsForStack(item.droppedItem.stack, itemDefinitions);
            const float itemMinX = item.position.x - bounds.halfWidth;
            const float itemMaxX = item.position.x + bounds.halfWidth;
            const float itemMinY = item.position.y;
            const float itemMaxY = item.position.y + bounds.height;
            const float itemMinZ = item.position.z - bounds.halfWidth;
            const float itemMaxZ = item.position.z + bounds.halfWidth;
            return itemMinX < maxX &&
                itemMaxX > minX &&
                itemMinY < maxY &&
                itemMaxY > minY &&
                itemMinZ < maxZ &&
                itemMaxZ > minZ;
        }
    }

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
        if (entity.type == WorldEntityType::DroppedItem &&
            entity.droppedItem.stack.itemId != 0 &&
            static_cast<std::size_t>(entity.droppedItem.stack.itemId) < itemDefinitions().size())
        {
            const ItemDefinition& definition = itemDefinitions()[entity.droppedItem.stack.itemId];
            entity.droppedItem.stack.count = std::min(entity.droppedItem.stack.count, definition.stackSize);
            const uint16_t maxDurability = definition.maxDurability;
            entity.droppedItem.stack.durability = maxDurability == 0
                ? 0
                : (entity.droppedItem.stack.durability == 0 ? maxDurability : std::min(entity.droppedItem.stack.durability, maxDurability));
        }
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

    WorldEntity DroppedItemRuntime::createManualDropEntity(ItemStack stack, DVec3 sourcePosition, Vec3 direction)
    {
        return DroppedItemSystem::createManualDropEntity(
            stack,
            sourcePosition,
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
            const DroppedItemSystem::Bounds bounds = DroppedItemSystem::boundsForStack(item.droppedItem.stack, itemDefinitions());
            const double halfWidth = static_cast<double>(bounds.halfWidth);
            const double minX = static_cast<double>(item.position.x) - halfWidth;
            const double maxX = static_cast<double>(item.position.x) + halfWidth;
            const double minY = static_cast<double>(item.position.y);
            const double maxY = static_cast<double>(item.position.y) + static_cast<double>(bounds.height);
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

    bool DroppedItemRuntime::targetInView(DVec3 origin, Vec3 direction, Target& target) const
    {
        WorldEntityHandle itemHandle{};
        if (!raycast(origin, direction, itemHandle))
        {
            return false;
        }

        const RuntimeChunk* chunk = worldRuntime().find(itemHandle.chunkKey);
        if (chunk == nullptr || !chunk->data ||
            itemHandle.index >= chunk->data->entities.size())
        {
            return false;
        }

        const WorldEntity& item = chunk->data->entities[itemHandle.index];
        if (item.type != WorldEntityType::DroppedItem ||
            item.droppedItem.stack.itemId == 0 ||
            item.droppedItem.stack.count == 0 ||
            item.collecting)
        {
            return false;
        }

        target.handle = itemHandle;
        target.entityId = item.entityId;
        target.stack = item.droppedItem.stack;
        target.position = item.position;
        return true;
    }

    std::vector<DroppedItemRuntime::Target> DroppedItemRuntime::targetsInAabb(
        float minX,
        float minY,
        float minZ,
        float maxX,
        float maxY,
        float maxZ) const
    {
        std::vector<Target> targets;
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
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
                    item.collecting ||
                    static_cast<std::size_t>(item.droppedItem.stack.itemId) >= definitions.size() ||
                    !droppedItemOverlapsAabb(item, definitions, minX, minY, minZ, maxX, maxY, maxZ))
                {
                    continue;
                }

                targets.push_back(Target{
                    WorldEntityHandle{entry.first, i},
                    item.entityId,
                    item.droppedItem.stack,
                    item.position
                });
            }
        }
        return targets;
    }

    uint16_t DroppedItemRuntime::replaceTargetItems(
        const WorldEntityHandle& itemHandle,
        uint64_t entityId,
        const std::vector<ItemInteractionOutput>& outputs,
        uint16_t targetCount,
        uint16_t maxApplications,
        const MarkDirtyFn& markDirty)
    {
        if (outputs.empty() || maxApplications == 0)
        {
            return 0;
        }

        uint64_t chunkKey = itemHandle.chunkKey;
        RuntimeChunk* chunk = worldRuntime().find(chunkKey);
        std::size_t targetIndex = itemHandle.index;
        if (chunk == nullptr ||
            !chunk->data ||
            targetIndex >= chunk->data->entities.size() ||
            chunk->data->entities[targetIndex].entityId != entityId)
        {
            chunk = nullptr;
            for (auto& entry : worldRuntime().chunks())
            {
                RuntimeChunk& candidateChunk = entry.second;
                if (!candidateChunk.data)
                {
                    continue;
                }

                for (std::size_t i = 0; i < candidateChunk.data->entities.size(); ++i)
                {
                    if (candidateChunk.data->entities[i].entityId == entityId)
                    {
                        chunk = &candidateChunk;
                        chunkKey = entry.first;
                        targetIndex = i;
                        break;
                    }
                }
                if (chunk != nullptr)
                {
                    break;
                }
            }
            if (chunk == nullptr || !chunk->data)
            {
                return 0;
            }
        }

        WorldEntity& target = chunk->data->entities[targetIndex];
        if (target.entityId != entityId ||
            target.type != WorldEntityType::DroppedItem ||
            target.droppedItem.stack.itemId == 0 ||
            static_cast<std::size_t>(target.droppedItem.stack.itemId) >= itemDefinitions().size() ||
            target.droppedItem.stack.count == 0 ||
            target.collecting)
        {
            return 0;
        }

        const ItemDefinition& sourceDefinition = itemDefinitions()[target.droppedItem.stack.itemId];
        targetCount = std::max<uint16_t>(targetCount, 1);
        const uint16_t availableApplications = static_cast<uint16_t>(target.droppedItem.stack.count / targetCount);
        const uint16_t applicationCount = std::min(availableApplications, maxApplications);
        if (applicationCount == 0)
        {
            return 0;
        }

        constexpr float InteractionBounceVelocity = 2.4f;
        constexpr float InteractionSpin = 7.0f;
        static thread_local std::mt19937 random{std::random_device{}()};
        const Vec3 resultPosition = target.position;
        std::uniform_real_distribution<float> offsetDistribution(-0.12f, 0.12f);
        std::uniform_real_distribution<float> velocityDistribution(-0.45f, 0.45f);
        auto validOutputItem = [&](uint16_t itemId)
        {
            return itemId != 0 &&
                static_cast<std::size_t>(itemId) < itemDefinitions().size() &&
                itemDefinitions()[itemId].stackSize != 0;
        };
        auto spawnExtraStack = [&](uint16_t itemId, uint16_t count, uint16_t durability)
        {
            if (!validOutputItem(itemId) || count == 0)
            {
                return;
            }

            WorldEntity extra{};
            extra.entityId = allocateEntityId();
            extra.type = WorldEntityType::DroppedItem;
            extra.position = {
                resultPosition.x + offsetDistribution(random),
                resultPosition.y + 0.02f,
                resultPosition.z + offsetDistribution(random)
            };
            extra.previousPosition = extra.position;
            extra.velocity = {
                velocityDistribution(random),
                InteractionBounceVelocity + velocityDistribution(random) * 0.5f,
                velocityDistribution(random)
            };
            extra.droppedItem.stack = ItemStack{itemId, count, durability};
            extra.renderSpinX = InteractionSpin;
            extra.renderSpin = InteractionSpin;
            extra.renderSpinZ = InteractionSpin;
            DroppedItemSystem::setGrounded(extra, false);
            addWorldEntity(std::move(extra), markDirty);
        };

        struct ResolvedOutput
        {
            uint16_t itemId = 0;
            uint16_t count = 0;
            uint16_t durability = 0;
        };

        auto spawnStackedOutput = [&](uint16_t itemId, uint32_t count, uint16_t durability)
        {
            if (!validOutputItem(itemId) || count == 0)
            {
                return;
            }

            const uint16_t maxStack = itemDefinitions()[itemId].stackSize;
            while (count > 0)
            {
                const uint16_t stackCount = static_cast<uint16_t>(std::min<uint32_t>(count, maxStack));
                spawnExtraStack(itemId, stackCount, durability);
                count -= stackCount;
            }
        };

        std::vector<ResolvedOutput> resolvedOutputs;
        resolvedOutputs.reserve(outputs.size());
        for (ItemInteractionOutput output : outputs)
        {
            if (!validOutputItem(output.itemId))
            {
                continue;
            }
            if (output.max < output.min)
            {
                output.max = output.min;
            }
            uint32_t totalCount = 0;
            std::uniform_int_distribution<int> countDistribution(output.min, output.max);
            for (uint16_t application = 0; application < applicationCount; ++application)
            {
                totalCount += static_cast<uint32_t>(countDistribution(random));
            }
            if (totalCount == 0)
            {
                continue;
            }

            const ItemDefinition& outputDefinition = itemDefinitions()[output.itemId];
            while (totalCount > 0)
            {
                const uint16_t stackCount = static_cast<uint16_t>(std::min<uint32_t>(totalCount, outputDefinition.stackSize));
                resolvedOutputs.push_back(ResolvedOutput{
                    output.itemId,
                    stackCount,
                    interactionResultDurability(target.droppedItem.stack, sourceDefinition, outputDefinition)
                });
                totalCount -= stackCount;
            }
        }
        if (resolvedOutputs.empty())
        {
            return 0;
        }

        const uint16_t consumedTargetCount = static_cast<uint16_t>(applicationCount * targetCount);
        const bool consumedTarget = consumedTargetCount >= target.droppedItem.stack.count;
        std::size_t firstExtraOutput = 0;
        if (consumedTarget)
        {
            const ResolvedOutput& primaryOutput = resolvedOutputs.front();
            target.previousPosition = target.position;
            target.position = resultPosition;
            target.velocity.y = std::max(target.velocity.y, InteractionBounceVelocity);
            target.droppedItem.stack = ItemStack{primaryOutput.itemId, primaryOutput.count, primaryOutput.durability};
            target.renderSpinX = InteractionSpin;
            target.renderSpin = InteractionSpin;
            target.renderSpinZ = InteractionSpin;
            DroppedItemSystem::setGrounded(target, false);
            firstExtraOutput = 1;
        }
        else
        {
            target.droppedItem.stack.count = static_cast<uint16_t>(target.droppedItem.stack.count - consumedTargetCount);
        }
        refreshChunkTracking(chunkKey);
        if (markDirty)
        {
            markDirty(*chunk);
        }
        for (std::size_t outputIndex = firstExtraOutput; outputIndex < resolvedOutputs.size(); ++outputIndex)
        {
            const ResolvedOutput& output = resolvedOutputs[outputIndex];
            spawnStackedOutput(output.itemId, output.count, output.durability);
        }
        return applicationCount;
    }

    uint16_t DroppedItemRuntime::replaceAreaItems(
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
        const MarkDirtyFn& markDirty)
    {
        if (ingredients.empty() || outputs.empty() || maxApplications == 0)
        {
            return 0;
        }

        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        auto validItem = [&](uint16_t itemId)
        {
            return itemId != 0 &&
                static_cast<std::size_t>(itemId) < definitions.size() &&
                definitions[itemId].stackSize != 0;
        };

        std::unordered_map<uint16_t, uint32_t> availableCounts;
        for (const Target& target : targetsInAabb(minX, minY, minZ, maxX, maxY, maxZ))
        {
            availableCounts[target.stack.itemId] += target.stack.count;
        }

        uint16_t applicationCount = maxApplications;
        for (const ItemInteractionIngredient& ingredient : ingredients)
        {
            if (!validItem(ingredient.itemId) || ingredient.count == 0)
            {
                return 0;
            }

            const uint32_t available = availableCounts[ingredient.itemId];
            applicationCount = std::min<uint16_t>(
                applicationCount,
                static_cast<uint16_t>(available / ingredient.count));
        }
        if (applicationCount == 0)
        {
            return 0;
        }

        struct ResolvedOutput
        {
            uint16_t itemId = 0;
            uint16_t count = 0;
            uint16_t durability = 0;
        };

        constexpr float InteractionBounceVelocity = 2.4f;
        constexpr float InteractionSpin = 7.0f;
        static thread_local std::mt19937 random{std::random_device{}()};
        std::vector<ResolvedOutput> resolvedOutputs;
        for (ItemInteractionOutput output : outputs)
        {
            if (!validItem(output.itemId))
            {
                continue;
            }
            if (output.max < output.min)
            {
                output.max = output.min;
            }

            uint32_t totalCount = 0;
            std::uniform_int_distribution<int> countDistribution(output.min, output.max);
            for (uint16_t application = 0; application < applicationCount; ++application)
            {
                totalCount += static_cast<uint32_t>(countDistribution(random));
            }
            while (totalCount > 0)
            {
                const uint16_t stackCount = static_cast<uint16_t>(std::min<uint32_t>(totalCount, definitions[output.itemId].stackSize));
                resolvedOutputs.push_back(ResolvedOutput{
                    output.itemId,
                    stackCount,
                    definitions[output.itemId].maxDurability
                });
                totalCount -= stackCount;
            }
        }
        if (resolvedOutputs.empty())
        {
            return 0;
        }

        std::unordered_map<uint16_t, uint32_t> remainingToConsume;
        for (const ItemInteractionIngredient& ingredient : ingredients)
        {
            remainingToConsume[ingredient.itemId] += static_cast<uint32_t>(ingredient.count) * applicationCount;
        }

        std::vector<uint64_t> changedChunkKeys;
        for (auto& entry : worldRuntime().chunks())
        {
            RuntimeChunk& chunk = entry.second;
            if (!chunk.data)
            {
                continue;
            }

            bool chunkChanged = false;
            for (std::size_t i = 0; i < chunk.data->entities.size();)
            {
                WorldEntity& item = chunk.data->entities[i];
                if (item.type != WorldEntityType::DroppedItem ||
                    item.droppedItem.stack.itemId == 0 ||
                    item.droppedItem.stack.count == 0 ||
                    item.collecting ||
                    static_cast<std::size_t>(item.droppedItem.stack.itemId) >= definitions.size() ||
                    !droppedItemOverlapsAabb(item, definitions, minX, minY, minZ, maxX, maxY, maxZ))
                {
                    ++i;
                    continue;
                }

                auto consumeIt = remainingToConsume.find(item.droppedItem.stack.itemId);
                if (consumeIt == remainingToConsume.end() || consumeIt->second == 0)
                {
                    ++i;
                    continue;
                }

                const uint16_t consumed = static_cast<uint16_t>(std::min<uint32_t>(item.droppedItem.stack.count, consumeIt->second));
                consumeIt->second -= consumed;
                if (consumed >= item.droppedItem.stack.count)
                {
                    chunk.data->entities.erase(chunk.data->entities.begin() + static_cast<std::ptrdiff_t>(i));
                }
                else
                {
                    item.droppedItem.stack.count = static_cast<uint16_t>(item.droppedItem.stack.count - consumed);
                    ++i;
                }
                chunkChanged = true;
            }

            if (chunkChanged)
            {
                changedChunkKeys.push_back(entry.first);
                if (markDirty)
                {
                    markDirty(chunk);
                }
            }
        }

        for (const uint64_t key : changedChunkKeys)
        {
            refreshChunkTracking(key);
        }

        std::uniform_real_distribution<float> offsetDistribution(-0.12f, 0.12f);
        std::uniform_real_distribution<float> velocityDistribution(-0.45f, 0.45f);
        for (const ResolvedOutput& output : resolvedOutputs)
        {
            WorldEntity extra{};
            extra.entityId = allocateEntityId();
            extra.type = WorldEntityType::DroppedItem;
            extra.position = {
                resultPosition.x + offsetDistribution(random),
                resultPosition.y + 0.02f,
                resultPosition.z + offsetDistribution(random)
            };
            extra.previousPosition = extra.position;
            extra.velocity = {
                velocityDistribution(random),
                InteractionBounceVelocity + velocityDistribution(random) * 0.5f,
                velocityDistribution(random)
            };
            extra.droppedItem.stack = ItemStack{output.itemId, output.count, output.durability};
            extra.renderSpinX = InteractionSpin;
            extra.renderSpin = InteractionSpin;
            extra.renderSpinZ = InteractionSpin;
            DroppedItemSystem::setGrounded(extra, false);
            addWorldEntity(std::move(extra), markDirty);
        }

        return applicationCount;
    }

    void DroppedItemRuntime::pushItemsOutOfBlock(
        int blockX,
        int blockY,
        int blockZ,
        const TerrainCollisionFn& terrainCellBlocksItem,
        const MarkDirtyFn& markDirty)
    {
        constexpr float Epsilon = 0.001f;
        constexpr float HorizontalPushSpeed = 2.2f;
        constexpr float VerticalPushSpeed = 1.4f;
        constexpr float Spin = 6.0f;

        const float blockMinX = static_cast<float>(blockX) - 0.5f;
        const float blockMaxX = static_cast<float>(blockX) + 0.5f;
        const float blockMinY = static_cast<float>(blockY);
        const float blockMaxY = static_cast<float>(blockY + 1);
        const float blockMinZ = static_cast<float>(blockZ) - 0.5f;
        const float blockMaxZ = static_cast<float>(blockZ) + 0.5f;

        auto overlapsPlacedBlock = [&](const WorldEntity& item)
        {
            const DroppedItemSystem::Bounds bounds = DroppedItemSystem::boundsForStack(item.droppedItem.stack, itemDefinitions());
            const float itemMinX = item.position.x - bounds.halfWidth;
            const float itemMaxX = item.position.x + bounds.halfWidth;
            const float itemMinY = item.position.y;
            const float itemMaxY = item.position.y + bounds.height;
            const float itemMinZ = item.position.z - bounds.halfWidth;
            const float itemMaxZ = item.position.z + bounds.halfWidth;
            return itemMinX < blockMaxX &&
                itemMaxX > blockMinX &&
                itemMinY < blockMaxY &&
                itemMaxY > blockMinY &&
                itemMinZ < blockMaxZ &&
                itemMaxZ > blockMinZ;
        };

        auto itemAabbBlocked = [&](Vec3 position, DroppedItemSystem::Bounds bounds)
        {
            if (!terrainCellBlocksItem)
            {
                return false;
            }

            const int minX = DroppedItemSystem::blockCoordinateXz(position.x - bounds.halfWidth);
            const int maxX = DroppedItemSystem::blockCoordinateXz(position.x + bounds.halfWidth - Epsilon);
            const int minY = DroppedItemSystem::blockCoordinateY(position.y);
            const int maxY = DroppedItemSystem::blockCoordinateY(position.y + bounds.height - Epsilon);
            const int minZ = DroppedItemSystem::blockCoordinateXz(position.z - bounds.halfWidth);
            const int maxZ = DroppedItemSystem::blockCoordinateXz(position.z + bounds.halfWidth - Epsilon);
            for (int y = minY; y <= maxY; ++y)
            {
                for (int z = minZ; z <= maxZ; ++z)
                {
                    for (int x = minX; x <= maxX; ++x)
                    {
                        if (terrainCellBlocksItem(x, y, z))
                        {
                            return true;
                        }
                    }
                }
            }
            return false;
        };

        struct Candidate
        {
            Vec3 position{};
            Vec3 velocity{};
            float distance = 0.0f;
        };

        struct EntityMove
        {
            uint64_t targetKey = 0;
            WorldEntity entity;
        };
        std::vector<EntityMove> moves;

        for (auto& entry : worldRuntime().chunks())
        {
            RuntimeChunk& chunk = entry.second;
            if (!chunk.data)
            {
                continue;
            }

            for (std::size_t i = 0; i < chunk.data->entities.size();)
            {
                WorldEntity& item = chunk.data->entities[i];
                if (item.type != WorldEntityType::DroppedItem ||
                    item.droppedItem.stack.itemId == 0 ||
                    item.droppedItem.stack.count == 0 ||
                    item.collecting ||
                    !overlapsPlacedBlock(item))
                {
                    ++i;
                    continue;
                }

                const Vec3 originalPosition = item.position;
                const DroppedItemSystem::Bounds bounds = DroppedItemSystem::boundsForStack(item.droppedItem.stack, itemDefinitions());
                std::array<Candidate, 4> horizontalCandidates{{
                    Candidate{{blockMinX - bounds.halfWidth - Epsilon, originalPosition.y, originalPosition.z}, {-HorizontalPushSpeed, VerticalPushSpeed * 0.5f, 0.0f}, std::abs(originalPosition.x - (blockMinX - bounds.halfWidth - Epsilon))},
                    Candidate{{blockMaxX + bounds.halfWidth + Epsilon, originalPosition.y, originalPosition.z}, {HorizontalPushSpeed, VerticalPushSpeed * 0.5f, 0.0f}, std::abs(originalPosition.x - (blockMaxX + bounds.halfWidth + Epsilon))},
                    Candidate{{originalPosition.x, originalPosition.y, blockMinZ - bounds.halfWidth - Epsilon}, {0.0f, VerticalPushSpeed * 0.5f, -HorizontalPushSpeed}, std::abs(originalPosition.z - (blockMinZ - bounds.halfWidth - Epsilon))},
                    Candidate{{originalPosition.x, originalPosition.y, blockMaxZ + bounds.halfWidth + Epsilon}, {0.0f, VerticalPushSpeed * 0.5f, HorizontalPushSpeed}, std::abs(originalPosition.z - (blockMaxZ + bounds.halfWidth + Epsilon))}
                }};
                std::sort(horizontalCandidates.begin(), horizontalCandidates.end(), [](const Candidate& a, const Candidate& b)
                {
                    return a.distance < b.distance;
                });

                Candidate selected{};
                bool hasSelected = false;
                for (const Candidate& candidate : horizontalCandidates)
                {
                    if (!itemAabbBlocked(candidate.position, bounds))
                    {
                        selected = candidate;
                        hasSelected = true;
                        break;
                    }
                }

                if (!hasSelected)
                {
                    selected = Candidate{
                        {originalPosition.x, blockMaxY + Epsilon, originalPosition.z},
                        {0.0f, VerticalPushSpeed, 0.0f},
                        std::abs(originalPosition.y - (blockMaxY + Epsilon))
                    };
                    hasSelected = true;
                }

                item.position = selected.position;
                item.velocity.x = selected.velocity.x;
                item.velocity.y = std::max(item.velocity.y, selected.velocity.y);
                item.velocity.z = selected.velocity.z;
                item.renderSpinX = Spin;
                item.renderSpin = Spin;
                item.renderSpinZ = Spin;
                DroppedItemSystem::setGrounded(item, false);

                const uint64_t originalOwnerKey = entry.first;
                const uint64_t targetOwnerKey = entityChunkKey(item);
                if (markDirty)
                {
                    markDirty(chunk);
                }
                if (targetOwnerKey != originalOwnerKey)
                {
                    auto targetIt = worldRuntime().chunks().find(targetOwnerKey);
                    if (targetIt != worldRuntime().chunks().end() && targetIt->second.data)
                    {
                        moves.push_back(EntityMove{targetOwnerKey, item});
                        chunk.data->entities.erase(chunk.data->entities.begin() + static_cast<std::ptrdiff_t>(i));
                        refreshChunkTracking(originalOwnerKey);
                        continue;
                    }
                }

                ++i;
            }
        }

        for (EntityMove& move : moves)
        {
            auto targetIt = worldRuntime().chunks().find(move.targetKey);
            if (targetIt == worldRuntime().chunks().end() || !targetIt->second.data)
            {
                continue;
            }

            targetIt->second.data->entities.push_back(std::move(move.entity));
            refreshChunkTracking(move.targetKey);
            if (markDirty)
            {
                markDirty(targetIt->second);
            }
        }
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
