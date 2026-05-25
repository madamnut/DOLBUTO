#include "world/DroppedItemSystem.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>
#include <utility>

namespace dolbuto::world
{
    namespace
    {
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeZ = 16;
        constexpr float DroppedItemGravity = 32.0f;
        constexpr float DroppedItemDrag = 0.94f;
        constexpr float DroppedItemCollisionRadius = DroppedItemSystem::DroppedItemSize * 0.5f;
        constexpr float DroppedItemWallBounce = 0.25f;
        constexpr float DroppedItemWallFriction = 0.65f;
        constexpr float DroppedItemPickupBaseSpeed = 7.0f;
        constexpr float DroppedItemPickupAcceleration = 256.0f;
        constexpr float DroppedItemPickupMaxSpeed = 52.0f;
        constexpr float DroppedItemManualDropForwardOffset = 0.75f;
        constexpr float DroppedItemManualDropForwardVelocity = 7.0f;
        constexpr float DroppedItemManualDropUpVelocity = 1.5f;
        constexpr uint8_t WorldEntityFlagGrounded = 1u << 0u;
        constexpr uint32_t BlockDropSalt = 0xD90210A5u;

        uint32_t worldRandomHash(int x, int y, int z, uint32_t salt)
        {
            uint32_t hash = static_cast<uint32_t>(x) * 0x8da6b343u;
            hash ^= static_cast<uint32_t>(y) * 0xd8163841u;
            hash ^= static_cast<uint32_t>(z) * 0xcb1ab31fu;
            hash ^= salt;
            hash ^= hash >> 16u;
            hash *= 0x7feb352du;
            hash ^= hash >> 15u;
            hash *= 0x846ca68bu;
            hash ^= hash >> 16u;
            return hash;
        }

        float unitRandom(uint32_t hash)
        {
            return static_cast<float>(hash) / static_cast<float>(std::numeric_limits<uint32_t>::max());
        }

        float horizontalSpeedSquared(const WorldEntity& item)
        {
            return item.velocity.x * item.velocity.x + item.velocity.z * item.velocity.z;
        }

        WorldEntity* horizontalCorrectionTarget(WorldEntity& a, WorldEntity& b)
        {
            const bool aGrounded = DroppedItemSystem::grounded(a);
            const bool bGrounded = DroppedItemSystem::grounded(b);
            if (aGrounded != bGrounded)
            {
                return aGrounded ? &b : &a;
            }

            constexpr float SpeedBias = 0.0001f;
            const float aSpeed = horizontalSpeedSquared(a);
            const float bSpeed = horizontalSpeedSquared(b);
            if (aSpeed > bSpeed + SpeedBias)
            {
                return &a;
            }
            if (bSpeed > aSpeed + SpeedBias)
            {
                return &b;
            }

            constexpr float AgeBiasSeconds = 0.05f;
            if (a.age + AgeBiasSeconds < b.age)
            {
                return &a;
            }
            if (b.age + AgeBiasSeconds < a.age)
            {
                return &b;
            }

            return nullptr;
        }

        void dampHorizontalVelocity(WorldEntity& item)
        {
            item.velocity.x *= 0.15f;
            item.velocity.z *= 0.15f;
            if (std::abs(item.velocity.x) < 0.01f)
            {
                item.velocity.x = 0.0f;
            }
            if (std::abs(item.velocity.z) < 0.01f)
            {
                item.velocity.z = 0.0f;
            }
        }

        void dampLandingHorizontalVelocity(WorldEntity& item)
        {
            item.velocity.x *= 0.35f;
            item.velocity.z *= 0.35f;
            if (std::abs(item.velocity.x) < 0.01f)
            {
                item.velocity.x = 0.0f;
            }
            if (std::abs(item.velocity.z) < 0.01f)
            {
                item.velocity.z = 0.0f;
            }
        }

        bool tryLandDroppedItemOnItem(WorldEntity& upper, const WorldEntity& lower)
        {
            constexpr float ItemHeight = DroppedItemSystem::DroppedItemThickness;
            constexpr float LandingEpsilon = 0.001f;
            const float lowerTop = lower.position.y + ItemHeight;
            if (upper.previousPosition.y + LandingEpsilon < lowerTop ||
                upper.position.y > lowerTop + LandingEpsilon ||
                upper.position.y > upper.previousPosition.y + LandingEpsilon)
            {
                return false;
            }

            upper.position.y = lowerTop + LandingEpsilon;
            upper.velocity.y = 0.0f;
            dampLandingHorizontalVelocity(upper);
            DroppedItemSystem::setGrounded(upper, true);
            return true;
        }

        void resolveDroppedItemCollisions(
            DroppedItemSystem::RuntimeChunkMap& runtimeChunks,
            const DroppedItemSystem::DirtyChunkCallback& markDirty)
        {
            struct ItemRef
            {
                RuntimeChunk* chunk = nullptr;
                std::size_t index = 0;
            };

            std::vector<ItemRef> items;
            for (auto& entry : runtimeChunks)
            {
                RuntimeChunk& chunk = entry.second;
                if (!chunk.data)
                {
                    continue;
                }

                for (std::size_t i = 0; i < chunk.data->entities.size(); ++i)
                {
                    const WorldEntity& item = chunk.data->entities[i];
                    if (item.type == WorldEntityType::DroppedItem &&
                        item.droppedItem.stack.itemId != 0 &&
                        item.droppedItem.stack.count != 0 &&
                        !item.collecting)
                    {
                        items.push_back(ItemRef{&chunk, i});
                    }
                }
            }

            constexpr float ItemSize = DroppedItemSystem::DroppedItemSize;
            constexpr float ItemHeight = DroppedItemSystem::DroppedItemThickness;
            constexpr float HalfHeight = ItemHeight * 0.5f;
            constexpr float SeparationEpsilon = 0.001f;
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                for (std::size_t j = i + 1; j < items.size(); ++j)
                {
                    WorldEntity& a = items[i].chunk->data->entities[items[i].index];
                    WorldEntity& b = items[j].chunk->data->entities[items[j].index];
                    const float dx = b.position.x - a.position.x;
                    const float dy = (b.position.y + HalfHeight) - (a.position.y + HalfHeight);
                    const float dz = b.position.z - a.position.z;
                    const float overlapX = ItemSize - std::abs(dx);
                    const float overlapY = ItemHeight - std::abs(dy);
                    const float overlapZ = ItemSize - std::abs(dz);
                    if (overlapX <= 0.0f || overlapZ <= 0.0f)
                    {
                        continue;
                    }

                    bool changedA = false;
                    bool changedB = false;
                    if (tryLandDroppedItemOnItem(a, b))
                    {
                        changedA = true;
                    }
                    else if (tryLandDroppedItemOnItem(b, a))
                    {
                        changedB = true;
                    }
                    if (changedA || changedB)
                    {
                        if (changedA && markDirty)
                        {
                            markDirty(*items[i].chunk);
                        }
                        if (changedB && markDirty && (!changedA || items[j].chunk != items[i].chunk))
                        {
                            markDirty(*items[j].chunk);
                        }
                        continue;
                    }

                    if (overlapY <= 0.0f)
                    {
                        continue;
                    }

                    const bool resolveVertical = overlapY < overlapX &&
                        overlapY < overlapZ &&
                        std::abs(dy) > ItemHeight * 0.35f;
                    if (resolveVertical)
                    {
                        const float correction = overlapY + SeparationEpsilon;
                        if (dy >= 0.0f)
                        {
                            b.position.y += correction;
                            b.velocity.y = std::max(b.velocity.y, 0.0f);
                            DroppedItemSystem::setGrounded(b, true);
                            changedB = true;
                        }
                        else
                        {
                            a.position.y += correction;
                            a.velocity.y = std::max(a.velocity.y, 0.0f);
                            DroppedItemSystem::setGrounded(a, true);
                            changedA = true;
                        }
                    }
                    else if (overlapX <= overlapZ)
                    {
                        const float normalX = dx == 0.0f ? (a.entityId < b.entityId ? 1.0f : -1.0f) : (dx > 0.0f ? 1.0f : -1.0f);
                        const float correction = overlapX + SeparationEpsilon;
                        WorldEntity* target = horizontalCorrectionTarget(a, b);
                        if (target == &a)
                        {
                            a.position.x -= normalX * correction;
                            dampHorizontalVelocity(a);
                            changedA = true;
                        }
                        else if (target == &b)
                        {
                            b.position.x += normalX * correction;
                            dampHorizontalVelocity(b);
                            changedB = true;
                        }
                    }
                    else
                    {
                        const float normalZ = dz == 0.0f ? (a.entityId < b.entityId ? 1.0f : -1.0f) : (dz > 0.0f ? 1.0f : -1.0f);
                        const float correction = overlapZ + SeparationEpsilon;
                        WorldEntity* target = horizontalCorrectionTarget(a, b);
                        if (target == &a)
                        {
                            a.position.z -= normalZ * correction;
                            dampHorizontalVelocity(a);
                            changedA = true;
                        }
                        else if (target == &b)
                        {
                            b.position.z += normalZ * correction;
                            dampHorizontalVelocity(b);
                            changedB = true;
                        }
                    }

                    if (changedA && markDirty)
                    {
                        markDirty(*items[i].chunk);
                    }
                    if (changedB && markDirty && (!changedA || items[j].chunk != items[i].chunk))
                    {
                        markDirty(*items[j].chunk);
                    }
                }
            }
        }
    }

    int DroppedItemSystem::floorDiv(int value, int divisor)
    {
        int result = value / divisor;
        const int remainder = value % divisor;
        if (remainder != 0 && ((remainder < 0) != (divisor < 0)))
        {
            --result;
        }
        return result;
    }

    int DroppedItemSystem::blockCoordinateXz(double worldCoordinate)
    {
        return static_cast<int>(std::floor(worldCoordinate + 0.5));
    }

    int DroppedItemSystem::blockCoordinateY(double worldCoordinate)
    {
        return static_cast<int>(std::floor(worldCoordinate));
    }

    uint64_t DroppedItemSystem::chunkKey(int chunkX, int chunkZ)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32u) |
            static_cast<uint64_t>(static_cast<uint32_t>(chunkZ));
    }

    uint64_t DroppedItemSystem::entityChunkKey(const WorldEntity& entity)
    {
        const int chunkX = floorDiv(blockCoordinateXz(entity.position.x), ChunkSizeX);
        const int chunkZ = floorDiv(blockCoordinateXz(entity.position.z), ChunkSizeZ);
        return chunkKey(chunkX, chunkZ);
    }

    size_t DroppedItemSystem::countDroppedItemsInChunk(const RuntimeChunk& chunk)
    {
        size_t count = 0;
        if (!chunk.data)
        {
            return count;
        }

        for (const WorldEntity& entity : chunk.data->entities)
        {
            if (entity.type == WorldEntityType::DroppedItem &&
                entity.droppedItem.stack.itemId != 0 &&
                entity.droppedItem.stack.count != 0)
            {
                ++count;
            }
        }
        return count;
    }

    bool DroppedItemSystem::grounded(const WorldEntity& entity)
    {
        return (entity.flags & WorldEntityFlagGrounded) != 0;
    }

    void DroppedItemSystem::setGrounded(WorldEntity& entity, bool grounded)
    {
        if (grounded)
        {
            entity.flags = static_cast<uint8_t>(entity.flags | WorldEntityFlagGrounded);
        }
        else
        {
            entity.flags = static_cast<uint8_t>(entity.flags & ~WorldEntityFlagGrounded);
        }
    }

    bool DroppedItemSystem::touchesPlayerCollider(const WorldEntity& item, Vec3 playerPosition)
    {
        constexpr float PlayerHalfWidth = 0.3f;
        constexpr float PlayerHeight = 1.75f;
        const float itemHalfWidth = DroppedItemSize * 0.5f;

        return item.position.x + itemHalfWidth >= playerPosition.x - PlayerHalfWidth &&
            item.position.x - itemHalfWidth <= playerPosition.x + PlayerHalfWidth &&
            item.position.y + DroppedItemThickness >= playerPosition.y &&
            item.position.y <= playerPosition.y + PlayerHeight &&
            item.position.z + itemHalfWidth >= playerPosition.z - PlayerHalfWidth &&
            item.position.z - itemHalfWidth <= playerPosition.z + PlayerHalfWidth;
    }

    std::vector<WorldEntity> DroppedItemSystem::createBlockDropEntities(
        int x,
        int y,
        int z,
        const BlockDefinition& definition,
        const std::vector<ItemDefinition>& itemDefinitions,
        const EntityIdProvider& allocateEntityId)
    {
        std::vector<WorldEntity> result;
        if (definition.drops.empty())
        {
            return result;
        }

        static thread_local std::mt19937 runtimeDropRandom{std::random_device{}()};
        auto randomRange = [&](float minValue, float maxValue)
        {
            std::uniform_real_distribution<float> distribution(minValue, maxValue);
            return distribution(runtimeDropRandom);
        };

        for (size_t dropIndex = 0; dropIndex < definition.drops.size(); ++dropIndex)
        {
            const BlockDrop& drop = definition.drops[dropIndex];
            const uint32_t hash = worldRandomHash(x, y, z, BlockDropSalt + static_cast<uint32_t>(dropIndex) * 0x9E3779B9u);
            if (unitRandom(hash) > drop.chance)
            {
                continue;
            }

            const uint16_t range = static_cast<uint16_t>(drop.max - drop.min + 1u);
            const uint16_t count = static_cast<uint16_t>(drop.min + (range > 0 ? hash % range : 0u));
            if (drop.itemId == 0 || count == 0 || static_cast<size_t>(drop.itemId) >= itemDefinitions.size())
            {
                continue;
            }

            for (uint16_t copy = 0; copy < count; ++copy)
            {
                WorldEntity item{};
                item.entityId = allocateEntityId ? allocateEntityId() : 0;
                item.type = WorldEntityType::DroppedItem;
                item.position = {
                    static_cast<float>(x) + randomRange(-0.18f, 0.18f),
                    static_cast<float>(y) + 0.5f + randomRange(-0.08f, 0.12f),
                    static_cast<float>(z) + randomRange(-0.18f, 0.18f)
                };
                item.previousPosition = item.position;
                item.velocity = {
                    randomRange(-1.5f, 1.5f),
                    randomRange(2.0f, 3.5f),
                    randomRange(-1.5f, 1.5f)
                };
                item.droppedItem.stack.itemId = drop.itemId;
                item.droppedItem.stack.count = 1;
                item.renderRotationX = randomRange(0.0f, 6.2831853f);
                item.renderRotation = randomRange(0.0f, 6.2831853f);
                item.renderRotationZ = randomRange(0.0f, 6.2831853f);
                item.renderSpinX = randomRange(-8.0f, 8.0f);
                item.renderSpin = randomRange(-8.0f, 8.0f);
                item.renderSpinZ = randomRange(-8.0f, 8.0f);
                if (std::abs(item.renderSpinX) < 2.0f)
                {
                    item.renderSpinX = item.renderSpinX < 0.0f ? -2.0f : 2.0f;
                }
                if (std::abs(item.renderSpin) < 2.0f)
                {
                    item.renderSpin = item.renderSpin < 0.0f ? -2.0f : 2.0f;
                }
                if (std::abs(item.renderSpinZ) < 2.0f)
                {
                    item.renderSpinZ = item.renderSpinZ < 0.0f ? -2.0f : 2.0f;
                }
                result.push_back(std::move(item));
            }
        }

        return result;
    }

    WorldEntity DroppedItemSystem::createManualDropEntity(
        ItemStack stack,
        DVec3 playerPosition,
        Vec3 direction,
        const EntityIdProvider& allocateEntityId)
    {
        Vec3 dropDirection = normalize(direction);
        if (dropDirection.x == 0.0f && dropDirection.y == 0.0f && dropDirection.z == 0.0f)
        {
            dropDirection = {0.0f, 0.0f, 1.0f};
        }

        WorldEntity item{};
        item.entityId = allocateEntityId ? allocateEntityId() : 0;
        item.type = WorldEntityType::DroppedItem;
        item.position = {
            static_cast<float>(playerPosition.x) + dropDirection.x * DroppedItemManualDropForwardOffset,
            static_cast<float>(playerPosition.y) + 0.875f + dropDirection.y * DroppedItemManualDropForwardOffset,
            static_cast<float>(playerPosition.z) + dropDirection.z * DroppedItemManualDropForwardOffset
        };
        item.previousPosition = item.position;
        item.velocity = {
            dropDirection.x * DroppedItemManualDropForwardVelocity,
            dropDirection.y * DroppedItemManualDropForwardVelocity + DroppedItemManualDropUpVelocity,
            dropDirection.z * DroppedItemManualDropForwardVelocity
        };
        item.droppedItem.stack = ItemStack{stack.itemId, 1};

        static thread_local std::mt19937 manualDropRandom{std::random_device{}()};
        std::uniform_real_distribution<float> angleDistribution(0.0f, 6.2831853f);
        std::uniform_real_distribution<float> spinDistribution(-8.0f, 8.0f);
        item.renderRotationX = angleDistribution(manualDropRandom);
        item.renderRotation = angleDistribution(manualDropRandom);
        item.renderRotationZ = angleDistribution(manualDropRandom);
        item.renderSpinX = spinDistribution(manualDropRandom);
        item.renderSpin = spinDistribution(manualDropRandom);
        item.renderSpinZ = spinDistribution(manualDropRandom);
        return item;
    }

    void DroppedItemSystem::updateTick(
        RuntimeChunkMap& runtimeChunks,
        const std::vector<ItemDefinition>& itemDefinitions,
        Vec3 playerPosition,
        float dt,
        const TerrainCollisionPredicate& terrainCellBlocksPlayer,
        const InventoryInsertCallback& addToPlayerInventory,
        const PickupSoundCallback& playPickupSound,
        const DirtyChunkCallback& markDirty,
        const ChunkTrackingCallback& refreshTracking)
    {
        if (dt <= 0.0f)
        {
            return;
        }
        (void)itemDefinitions;

        constexpr float GroundProbeEpsilon = 0.01f;
        constexpr float WallProbeHeight = 0.08f;
        const float drag = std::pow(DroppedItemDrag, dt * 60.0f);

        auto solidAt = [&](float x, float y, float z)
        {
            return terrainCellBlocksPlayer &&
                terrainCellBlocksPlayer(blockCoordinateXz(x), blockCoordinateY(y), blockCoordinateXz(z));
        };
        auto supportedByGround = [&](const WorldEntity& item)
        {
            return solidAt(item.position.x, item.position.y - GroundProbeEpsilon, item.position.z);
        };
        auto supportedByDroppedItem = [&](const WorldEntity& item)
        {
            constexpr float ItemSupportEpsilon = 0.02f;
            constexpr float ItemSupportMinOverlap = 0.04f;
            for (const auto& entry : runtimeChunks)
            {
                const RuntimeChunk& chunk = entry.second;
                if (!chunk.data)
                {
                    continue;
                }

                for (const WorldEntity& support : chunk.data->entities)
                {
                    if (support.entityId == item.entityId ||
                        support.type != WorldEntityType::DroppedItem ||
                        support.droppedItem.stack.itemId == 0 ||
                        support.droppedItem.stack.count == 0 ||
                        support.collecting)
                    {
                        continue;
                    }

                    const float supportTop = support.position.y + DroppedItemThickness;
                    const float overlapX = DroppedItemSize - std::abs(item.position.x - support.position.x);
                    const float overlapZ = DroppedItemSize - std::abs(item.position.z - support.position.z);
                    if (std::abs(item.position.y - supportTop) <= ItemSupportEpsilon &&
                        overlapX > ItemSupportMinOverlap &&
                        overlapZ > ItemSupportMinOverlap)
                    {
                        return true;
                    }
                }
            }
            return false;
        };
        auto sideBlocked = [&](float x, float y, float z)
        {
            return solidAt(x, y + WallProbeHeight, z);
        };

        struct EntityMove
        {
            uint64_t targetKey = 0;
            WorldEntity entity;
        };
        std::vector<EntityMove> moves;

        for (auto& entry : runtimeChunks)
        {
            RuntimeChunk& chunk = entry.second;
            if (!chunk.data)
            {
                continue;
            }

            for (size_t i = 0; i < chunk.data->entities.size();)
            {
                WorldEntity& item = chunk.data->entities[i];
                if (item.type != WorldEntityType::DroppedItem)
                {
                    ++i;
                    continue;
                }

                const uint64_t originalOwnerKey = entry.first;
                item.previousPosition = item.position;
                item.age += dt;
                if (item.collecting)
                {
                    item.collectAge += dt;
                    const Vec3 target{
                        playerPosition.x,
                        playerPosition.y + 0.875f,
                        playerPosition.z
                    };
                    const Vec3 toTarget{
                        target.x - item.position.x,
                        target.y - item.position.y,
                        target.z - item.position.z
                    };
                    const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z);
                    const float speed = std::min(
                        DroppedItemPickupMaxSpeed,
                        DroppedItemPickupBaseSpeed + item.collectAge * DroppedItemPickupAcceleration);
                    const float travel = speed * dt;
                    if (distance <= travel || touchesPlayerCollider(item, playerPosition))
                    {
                        const uint16_t countBeforePickup = item.droppedItem.stack.count;
                        const uint16_t remaining = addToPlayerInventory ? addToPlayerInventory(item.droppedItem.stack) : item.droppedItem.stack.count;
                        if (remaining < countBeforePickup && playPickupSound)
                        {
                            playPickupSound();
                        }
                        if (remaining == 0)
                        {
                            chunk.data->entities.erase(chunk.data->entities.begin() + static_cast<std::ptrdiff_t>(i));
                            if (refreshTracking)
                            {
                                refreshTracking(entry.first);
                            }
                            if (markDirty)
                            {
                                markDirty(chunk);
                            }
                            continue;
                        }

                        item.droppedItem.stack.count = remaining;
                        item.collecting = false;
                        item.collectAge = 0.0f;
                        item.velocity = {};
                        item.renderSpinX = 5.0f;
                        item.renderSpin = 5.0f;
                        item.renderSpinZ = 5.0f;
                    }
                    else
                    {
                        const float scale = travel / std::max(distance, 0.0001f);
                        item.position.x += toTarget.x * scale;
                        item.position.y += toTarget.y * scale;
                        item.position.z += toTarget.z * scale;
                        item.velocity = {};
                    }
                    if (markDirty)
                    {
                        markDirty(chunk);
                    }
                }
                else
                {
                    if (grounded(item))
                    {
                        if (supportedByGround(item) || supportedByDroppedItem(item))
                        {
                            item.velocity = {};
                            item.renderSpinX = 0.0f;
                            item.renderSpin = 0.0f;
                            item.renderSpinZ = 0.0f;
                        }
                        else
                        {
                            setGrounded(item, false);
                            item.velocity.y = std::min(item.velocity.y, 0.0f);
                            if (item.renderSpinX == 0.0f)
                            {
                                item.renderSpinX = 5.0f;
                            }
                            if (item.renderSpin == 0.0f)
                            {
                                item.renderSpin = 5.0f;
                            }
                            if (item.renderSpinZ == 0.0f)
                            {
                                item.renderSpinZ = 5.0f;
                            }
                            if (markDirty)
                            {
                                markDirty(chunk);
                            }
                        }
                    }

                    if (!grounded(item))
                    {
                        item.velocity.x *= drag;
                        item.velocity.z *= drag;
                        item.velocity.y -= DroppedItemGravity * dt;

                        if (item.velocity.x != 0.0f)
                        {
                            const float nextX = item.position.x + item.velocity.x * dt;
                            const float probeX = nextX + (item.velocity.x > 0.0f ? DroppedItemCollisionRadius : -DroppedItemCollisionRadius);
                            if (sideBlocked(probeX, item.position.y, item.position.z))
                            {
                                item.velocity.x = -item.velocity.x * DroppedItemWallBounce;
                                item.velocity.z *= DroppedItemWallFriction;
                            }
                            else
                            {
                                item.position.x = nextX;
                            }
                        }

                        if (item.velocity.z != 0.0f)
                        {
                            const float nextZ = item.position.z + item.velocity.z * dt;
                            const float probeZ = nextZ + (item.velocity.z > 0.0f ? DroppedItemCollisionRadius : -DroppedItemCollisionRadius);
                            if (sideBlocked(item.position.x, item.position.y, probeZ))
                            {
                                item.velocity.z = -item.velocity.z * DroppedItemWallBounce;
                                item.velocity.x *= DroppedItemWallFriction;
                            }
                            else
                            {
                                item.position.z = nextZ;
                            }
                        }

                        const float currentY = item.position.y;
                        const float nextY = currentY + item.velocity.y * dt;
                        bool landed = false;
                        if (item.velocity.y < 0.0f)
                        {
                            const int startY = blockCoordinateY(currentY - GroundProbeEpsilon);
                            const int endY = blockCoordinateY(nextY - GroundProbeEpsilon);
                            for (int groundY = startY; groundY >= endY; --groundY)
                            {
                                if (!terrainCellBlocksPlayer ||
                                    !terrainCellBlocksPlayer(blockCoordinateXz(item.position.x), groundY, blockCoordinateXz(item.position.z)))
                                {
                                    continue;
                                }

                                item.position.y = static_cast<float>(groundY + 1);
                                item.velocity = {};
                                item.renderRotationX = 0.0f;
                                item.renderRotation = std::fmod(item.renderRotation, 6.2831853f);
                                if (item.renderRotation < 0.0f)
                                {
                                    item.renderRotation += 6.2831853f;
                                }
                                item.renderRotationZ = 0.0f;
                                item.renderSpinX = 0.0f;
                                item.renderSpin = 0.0f;
                                item.renderSpinZ = 0.0f;
                                setGrounded(item, true);
                                landed = true;
                                break;
                            }
                        }

                        if (!landed)
                        {
                            item.position.y = nextY;
                        }
                        if (markDirty)
                        {
                            markDirty(chunk);
                        }
                    }
                }

                const uint64_t targetOwnerKey = entityChunkKey(item);
                if (targetOwnerKey != originalOwnerKey)
                {
                    auto targetIt = runtimeChunks.find(targetOwnerKey);
                    if (targetIt != runtimeChunks.end() && targetIt->second.data)
                    {
                        moves.push_back(EntityMove{targetOwnerKey, item});
                        chunk.data->entities.erase(chunk.data->entities.begin() + static_cast<std::ptrdiff_t>(i));
                        if (refreshTracking)
                        {
                            refreshTracking(entry.first);
                        }
                        if (markDirty)
                        {
                            markDirty(chunk);
                        }
                        continue;
                    }
                }
                ++i;
            }
        }

        for (EntityMove& move : moves)
        {
            auto targetIt = runtimeChunks.find(move.targetKey);
            if (targetIt == runtimeChunks.end() || !targetIt->second.data)
            {
                continue;
            }
            targetIt->second.data->entities.push_back(std::move(move.entity));
            if (refreshTracking)
            {
                refreshTracking(move.targetKey);
            }
            if (markDirty)
            {
                markDirty(targetIt->second);
            }
        }

        resolveDroppedItemCollisions(runtimeChunks, markDirty);
    }
}
