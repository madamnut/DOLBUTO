#include "renderer/Renderer.h"

#include "world/DroppedItemSystem.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dolbuto
{
    namespace
    {
        constexpr float FieldOfViewRadians = 1.0471975512f;
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeY = 512;
        constexpr int ChunkSizeZ = 16;
        constexpr float TerrainNearPlane = 0.1f;
        constexpr float TerrainFarPlane = 4000.0f;
        constexpr size_t MaxDroppedItems = world::DroppedItemSystem::MaxDroppedItems;
        constexpr float DroppedItemThickness = world::DroppedItemSystem::DroppedItemThickness;
        constexpr float DroppedItemRenderDistanceSquared = world::DroppedItemSystem::DroppedItemRenderDistanceSquared;
        constexpr size_t MaxDroppedItemRenderInstances = world::DroppedItemSystem::MaxDroppedItemRenderInstances;

        struct Mat4
        {
            float m[16]{};
        };

        struct Frustum
        {
            Vec3 position{};
            Vec3 right{};
            Vec3 up{};
            Vec3 forward{};
            float tanHalfVertical = 1.0f;
            float tanHalfHorizontal = 1.0f;
        };

        Mat4 identity()
        {
            Mat4 matrix{};
            matrix.m[0] = 1.0f;
            matrix.m[5] = 1.0f;
            matrix.m[10] = 1.0f;
            matrix.m[15] = 1.0f;
            return matrix;
        }

        Mat4 multiply(const Mat4& left, const Mat4& right)
        {
            Mat4 result{};
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    result.m[column * 4 + row] =
                        left.m[0 * 4 + row] * right.m[column * 4 + 0] +
                        left.m[1 * 4 + row] * right.m[column * 4 + 1] +
                        left.m[2 * 4 + row] * right.m[column * 4 + 2] +
                        left.m[3 * 4 + row] * right.m[column * 4 + 3];
                }
            }
            return result;
        }

        Mat4 perspective(float fovRadians, float aspect, float nearPlane, float farPlane)
        {
            const float f = 1.0f / std::tan(fovRadians * 0.5f);
            Mat4 matrix{};
            matrix.m[0] = f / aspect;
            matrix.m[5] = -f;
            matrix.m[10] = farPlane / (farPlane - nearPlane);
            matrix.m[11] = 1.0f;
            matrix.m[14] = -(nearPlane * farPlane) / (farPlane - nearPlane);
            return matrix;
        }

        Mat4 viewMatrix(const Camera& camera, Vec3 position)
        {
            const Vec3 cameraRight = camera.right();
            const Vec3 terrainRight{-cameraRight.x, -cameraRight.y, -cameraRight.z};
            const Vec3 forward = camera.forward();
            const Vec3 terrainForward{forward.x, -forward.y, forward.z};
            const Vec3 terrainUp = normalize(cross(terrainForward, terrainRight));

            Mat4 matrix = identity();
            matrix.m[0] = terrainRight.x;
            matrix.m[4] = terrainRight.y;
            matrix.m[8] = terrainRight.z;
            matrix.m[12] = -dot(terrainRight, position);

            matrix.m[1] = terrainUp.x;
            matrix.m[5] = terrainUp.y;
            matrix.m[9] = terrainUp.z;
            matrix.m[13] = -dot(terrainUp, position);

            matrix.m[2] = terrainForward.x;
            matrix.m[6] = terrainForward.y;
            matrix.m[10] = terrainForward.z;
            matrix.m[14] = -dot(terrainForward, position);
            return matrix;
        }

        Frustum makeFrustum(const Camera& camera, Vec3 position, float aspect)
        {
            const Vec3 cameraRight = camera.right();
            const Vec3 terrainRight{-cameraRight.x, -cameraRight.y, -cameraRight.z};
            const Vec3 forward = camera.forward();
            const Vec3 terrainForward{forward.x, -forward.y, forward.z};
            const Vec3 terrainUp = normalize(cross(terrainForward, terrainRight));
            const float tanHalfVertical = std::tan(FieldOfViewRadians * 0.5f);

            return {
                position,
                terrainRight,
                terrainUp,
                terrainForward,
                tanHalfVertical,
                tanHalfVertical * aspect
            };
        }

        bool aabbIntersectsFrustum(const Frustum& frustum, Vec3 minCorner, Vec3 maxCorner)
        {
            const Vec3 center{
                (minCorner.x + maxCorner.x) * 0.5f,
                (minCorner.y + maxCorner.y) * 0.5f,
                (minCorner.z + maxCorner.z) * 0.5f
            };
            const Vec3 extent{
                (maxCorner.x - minCorner.x) * 0.5f,
                (maxCorner.y - minCorner.y) * 0.5f,
                (maxCorner.z - minCorner.z) * 0.5f
            };
            const Vec3 relative{
                center.x - frustum.position.x,
                center.y - frustum.position.y,
                center.z - frustum.position.z
            };

            const float viewX = dot(relative, frustum.right);
            const float viewY = dot(relative, frustum.up);
            const float viewZ = dot(relative, frustum.forward);
            const float radiusX =
                std::abs(frustum.right.x) * extent.x +
                std::abs(frustum.right.y) * extent.y +
                std::abs(frustum.right.z) * extent.z;
            const float radiusY =
                std::abs(frustum.up.x) * extent.x +
                std::abs(frustum.up.y) * extent.y +
                std::abs(frustum.up.z) * extent.z;
            const float radiusZ =
                std::abs(frustum.forward.x) * extent.x +
                std::abs(frustum.forward.y) * extent.y +
                std::abs(frustum.forward.z) * extent.z;

            if (viewZ + radiusZ < TerrainNearPlane || viewZ - radiusZ > TerrainFarPlane)
            {
                return false;
            }
            if (std::abs(viewX) > viewZ * frustum.tanHalfHorizontal + radiusX + radiusZ * frustum.tanHalfHorizontal)
            {
                return false;
            }
            if (std::abs(viewY) > viewZ * frustum.tanHalfVertical + radiusY + radiusZ * frustum.tanHalfVertical)
            {
                return false;
            }

            return true;
        }
    }
    uint64_t Renderer::allocateWorldEntityId()
    {
        if (nextWorldEntityId_ == 0)
        {
            nextWorldEntityId_ = 1;
        }
        return nextWorldEntityId_++;
    }

    uint64_t Renderer::entityChunkKey(const WorldEntity& entity) const
    {
        return world::DroppedItemSystem::entityChunkKey(entity);
    }

    RuntimeChunk* Renderer::runtimeChunkForEntity(const WorldEntity& entity)
    {
        const uint64_t key = entityChunkKey(entity);
        RuntimeChunk* chunk = worldRuntime_.find(key);
        if (chunk == nullptr || !chunk->data)
        {
            return nullptr;
        }
        return chunk;
    }

    const RuntimeChunk* Renderer::runtimeChunkForEntity(const WorldEntity& entity) const
    {
        const uint64_t key = entityChunkKey(entity);
        const RuntimeChunk* chunk = worldRuntime_.find(key);
        if (chunk == nullptr || !chunk->data)
        {
            return nullptr;
        }
        return chunk;
    }

    bool Renderer::addWorldEntity(WorldEntity entity)
    {
        if (entity.entityId == 0)
        {
            entity.entityId = allocateWorldEntityId();
        }

        if (entity.type == WorldEntityType::DroppedItem)
        {
            mergeDroppedItemIntoNearby(entity);
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

        if (chunk->data->entities.size() >= MaxDroppedItems)
        {
            chunk->data->entities.erase(chunk->data->entities.begin());
        }
        chunk->data->entities.push_back(std::move(entity));
        refreshDroppedItemChunkTracking(entityChunkKey(chunk->data->entities.back()));
        markRuntimeChunkDataDirty(*chunk);
        return true;
    }

    size_t Renderer::countDroppedItemsInChunk(const RuntimeChunk& chunk) const
    {
        return world::DroppedItemSystem::countDroppedItemsInChunk(chunk);
    }

    void Renderer::refreshDroppedItemChunkTracking(uint64_t key)
    {
        const auto oldIt = droppedItemCountsByChunk_.find(key);
        const size_t oldCount = oldIt != droppedItemCountsByChunk_.end() ? oldIt->second : 0u;

        size_t newCount = 0;
        const RuntimeChunk* chunk = worldRuntime_.find(key);
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

        loadedDroppedItemCount_ = loadedDroppedItemCount_ - oldCount + newCount;
    }

    void Renderer::removeDroppedItemChunkTracking(uint64_t key)
    {
        const auto oldIt = droppedItemCountsByChunk_.find(key);
        if (oldIt == droppedItemCountsByChunk_.end())
        {
            return;
        }

        loadedDroppedItemCount_ -= oldIt->second;
        droppedItemCountsByChunk_.erase(oldIt);
    }

    void Renderer::resetDroppedItemTracking()
    {
        droppedItemCountsByChunk_.clear();
        loadedDroppedItemCount_ = 0;
    }

    uint16_t Renderer::mergeDroppedItemIntoNearby(WorldEntity& source)
    {
        return world::DroppedItemSystem::mergeIntoNearby(
            source,
            worldRuntime_.chunks(),
            itemDefinitions_,
            [this](RuntimeChunk& chunk)
            {
                markRuntimeChunkDataDirty(chunk);
            },
            [this](uint64_t key)
            {
                refreshDroppedItemChunkTracking(key);
            });
    }

    size_t Renderer::loadedDroppedItemCount() const
    {
        return loadedDroppedItemCount_;
    }

    bool Renderer::worldEntityGrounded(const WorldEntity& entity) const
    {
        return world::DroppedItemSystem::grounded(entity);
    }

    void Renderer::setWorldEntityGrounded(WorldEntity& entity, bool grounded) const
    {
        world::DroppedItemSystem::setGrounded(entity, grounded);
    }

    void Renderer::spawnBlockDrops(int x, int y, int z, uint16_t block)
    {
        const BlockDefinition& definition = blockDefinition(block);
        std::vector<WorldEntity> drops = world::DroppedItemSystem::createBlockDropEntities(
            x,
            y,
            z,
            definition,
            itemDefinitions_,
            [this]()
            {
                return allocateWorldEntityId();
            });
        for (WorldEntity& item : drops)
        {
            addWorldEntity(std::move(item));
        }
    }

    bool Renderer::raycastDroppedItem(DVec3 origin, Vec3 direction, WorldEntityHandle& itemHandle) const
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
            const double halfWidth = static_cast<double>(world::DroppedItemSystem::DroppedItemSize) * 0.5;
            const double minX = static_cast<double>(item.position.x) - halfWidth;
            const double maxX = static_cast<double>(item.position.x) + halfWidth;
            const double minY = static_cast<double>(item.position.y);
            const double maxY = static_cast<double>(item.position.y) + static_cast<double>(world::DroppedItemSystem::DroppedItemThickness);
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
        for (const auto& entry : worldRuntime_.chunks())
        {
            const RuntimeChunk& chunk = entry.second;
            if (!chunk.data)
            {
                continue;
            }
            for (size_t i = 0; i < chunk.data->entities.size(); ++i)
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

    bool Renderer::droppedItemTouchesPlayerCollider(const WorldEntity& item, Vec3 playerPosition) const
    {
        return world::DroppedItemSystem::touchesPlayerCollider(item, playerPosition);
    }

    void Renderer::updateDroppedItems(Vec3 playerPosition)
    {
        const double now = glfwGetTime();
        if (lastDroppedItemUpdateTime_ <= 0.0)
        {
            lastDroppedItemUpdateTime_ = now;
            droppedItemRenderAlpha_ = 0.0f;
            return;
        }

        const float frameDt = static_cast<float>(std::clamp(
            now - lastDroppedItemUpdateTime_,
            0.0,
            static_cast<double>(world::DroppedItemSystem::DroppedItemMaxFrameSeconds)));
        lastDroppedItemUpdateTime_ = now;
        if (frameDt <= 0.0f)
        {
            droppedItemRenderAlpha_ = 0.0f;
            return;
        }
        if (loadedDroppedItemCount() == 0)
        {
            droppedItemTickAccumulator_ = 0.0f;
            droppedItemRenderAlpha_ = 0.0f;
            return;
        }

        for (auto& entry : worldRuntime_.chunks())
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
                if (!worldEntityGrounded(item) || item.collecting)
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

        droppedItemTickAccumulator_ += frameDt;
        while (droppedItemTickAccumulator_ >= world::DroppedItemSystem::DroppedItemTickSeconds)
        {
            updateDroppedItemsTick(playerPosition, world::DroppedItemSystem::DroppedItemTickSeconds);
            droppedItemTickAccumulator_ -= world::DroppedItemSystem::DroppedItemTickSeconds;
        }
        droppedItemRenderAlpha_ = std::clamp(droppedItemTickAccumulator_ / world::DroppedItemSystem::DroppedItemTickSeconds, 0.0f, 1.0f);
    }

    void Renderer::updateDroppedItemsTick(Vec3 playerPosition, float dt)
    {
        if (dt <= 0.0f || loadedDroppedItemCount() == 0)
        {
            return;
        }

        world::DroppedItemSystem::updateTick(
            worldRuntime_.chunks(),
            itemDefinitions_,
            playerPosition,
            dt,
            [this](int x, int y, int z)
            {
                return terrainCellBlocksPlayer(x, y, z);
            },
            [this](ItemStack stack)
            {
                return addItemToPlayerInventory(stack);
            },
            [this]()
            {
                playItemPickupSound();
            },
            [this](RuntimeChunk& chunk)
            {
                markRuntimeChunkDataDirty(chunk);
            },
            [this](uint64_t key)
            {
                refreshDroppedItemChunkTracking(key);
            });
    }

    Renderer::ItemSpriteMesh Renderer::buildItemSpriteMesh(const std::filesystem::path& path) const
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* loadedPixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (loadedPixels == nullptr)
        {
            throw std::runtime_error("Failed to load item sprite mesh texture: " + path.string());
        }

        ItemSpriteMesh mesh{};
        auto alphaAt = [&](int x, int y)
        {
            if (x < 0 || x >= width || y < 0 || y >= height)
            {
                return 0u;
            }
            const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u + 3u;
            return static_cast<unsigned int>(loadedPixels[index]);
        };
        auto opaqueAt = [&](int x, int y)
        {
            return alphaAt(x, y) >= 128u;
        };
        auto addQuad = [&](std::array<Vec3, 4> positions, std::array<std::array<float, 2>, 4> uvs, float ao)
        {
            ItemSpriteQuad quad{};
            quad.positions = positions;
            quad.uvs = uvs;
            quad.ao = ao;
            mesh.quads.push_back(quad);
        };

        addQuad(
            std::array<Vec3, 4>{Vec3{-0.5f, 0.5f, -0.5f}, Vec3{-0.5f, 0.5f, 0.5f}, Vec3{0.5f, 0.5f, 0.5f}, Vec3{0.5f, 0.5f, -0.5f}},
            std::array<std::array<float, 2>, 4>{{{0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}}},
            1.0f);
        addQuad(
            std::array<Vec3, 4>{Vec3{0.5f, -0.5f, -0.5f}, Vec3{0.5f, -0.5f, 0.5f}, Vec3{-0.5f, -0.5f, 0.5f}, Vec3{-0.5f, -0.5f, -0.5f}},
            std::array<std::array<float, 2>, 4>{{{1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}}},
            0.82f);

        const float invWidth = width > 0 ? 1.0f / static_cast<float>(width) : 1.0f;
        const float invHeight = height > 0 ? 1.0f / static_cast<float>(height) : 1.0f;
        auto addLeftSpan = [&](int x, int y0, int y1)
        {
            const float u = (static_cast<float>(x) + 0.5f) * invWidth;
            const float v0 = static_cast<float>(y0) * invHeight;
            const float v1 = static_cast<float>(y1) * invHeight;
            const float localX = static_cast<float>(x) * invWidth - 0.5f;
            const float localZ0 = 0.5f - v0;
            const float localZ1 = 0.5f - v1;
            addQuad(
                std::array<Vec3, 4>{Vec3{localX, 0.5f, localZ1}, Vec3{localX, -0.5f, localZ1}, Vec3{localX, -0.5f, localZ0}, Vec3{localX, 0.5f, localZ0}},
                std::array<std::array<float, 2>, 4>{{{u, v1}, {u, v1}, {u, v0}, {u, v0}}},
                0.72f);
        };
        auto addRightSpan = [&](int x, int y0, int y1)
        {
            const float u = (static_cast<float>(x) + 0.5f) * invWidth;
            const float v0 = static_cast<float>(y0) * invHeight;
            const float v1 = static_cast<float>(y1) * invHeight;
            const float localX = static_cast<float>(x + 1) * invWidth - 0.5f;
            const float localZ0 = 0.5f - v0;
            const float localZ1 = 0.5f - v1;
            addQuad(
                std::array<Vec3, 4>{Vec3{localX, 0.5f, localZ0}, Vec3{localX, -0.5f, localZ0}, Vec3{localX, -0.5f, localZ1}, Vec3{localX, 0.5f, localZ1}},
                std::array<std::array<float, 2>, 4>{{{u, v0}, {u, v0}, {u, v1}, {u, v1}}},
                0.72f);
        };
        auto addTopSpan = [&](int y, int x0, int x1)
        {
            const float u0 = static_cast<float>(x0) * invWidth;
            const float u1 = static_cast<float>(x1) * invWidth;
            const float v = (static_cast<float>(y) + 0.5f) * invHeight;
            const float localX0 = u0 - 0.5f;
            const float localX1 = u1 - 0.5f;
            const float localZ = 0.5f - static_cast<float>(y) * invHeight;
            addQuad(
                std::array<Vec3, 4>{Vec3{localX0, 0.5f, localZ}, Vec3{localX1, 0.5f, localZ}, Vec3{localX1, -0.5f, localZ}, Vec3{localX0, -0.5f, localZ}},
                std::array<std::array<float, 2>, 4>{{{u0, v}, {u1, v}, {u1, v}, {u0, v}}},
                0.76f);
        };
        auto addBottomSpan = [&](int y, int x0, int x1)
        {
            const float u0 = static_cast<float>(x0) * invWidth;
            const float u1 = static_cast<float>(x1) * invWidth;
            const float v = (static_cast<float>(y) + 0.5f) * invHeight;
            const float localX0 = u0 - 0.5f;
            const float localX1 = u1 - 0.5f;
            const float localZ = 0.5f - static_cast<float>(y + 1) * invHeight;
            addQuad(
                std::array<Vec3, 4>{Vec3{localX1, 0.5f, localZ}, Vec3{localX0, 0.5f, localZ}, Vec3{localX0, -0.5f, localZ}, Vec3{localX1, -0.5f, localZ}},
                std::array<std::array<float, 2>, 4>{{{u1, v}, {u0, v}, {u0, v}, {u1, v}}},
                0.70f);
        };

        for (int x = 0; x < width; ++x)
        {
            int leftRunStart = -1;
            int rightRunStart = -1;
            for (int y = 0; y <= height; ++y)
            {
                const bool leftEdge = y < height && opaqueAt(x, y) && !opaqueAt(x - 1, y);
                const bool rightEdge = y < height && opaqueAt(x, y) && !opaqueAt(x + 1, y);
                if (leftEdge && leftRunStart < 0)
                {
                    leftRunStart = y;
                }
                else if (!leftEdge && leftRunStart >= 0)
                {
                    addLeftSpan(x, leftRunStart, y);
                    leftRunStart = -1;
                }
                if (rightEdge && rightRunStart < 0)
                {
                    rightRunStart = y;
                }
                else if (!rightEdge && rightRunStart >= 0)
                {
                    addRightSpan(x, rightRunStart, y);
                    rightRunStart = -1;
                }
            }
        }

        for (int y = 0; y < height; ++y)
        {
            int topRunStart = -1;
            int bottomRunStart = -1;
            for (int x = 0; x <= width; ++x)
            {
                const bool topEdge = x < width && opaqueAt(x, y) && !opaqueAt(x, y - 1);
                const bool bottomEdge = x < width && opaqueAt(x, y) && !opaqueAt(x, y + 1);
                if (topEdge && topRunStart < 0)
                {
                    topRunStart = x;
                }
                else if (!topEdge && topRunStart >= 0)
                {
                    addTopSpan(y, topRunStart, x);
                    topRunStart = -1;
                }
                if (bottomEdge && bottomRunStart < 0)
                {
                    bottomRunStart = x;
                }
                else if (!bottomEdge && bottomRunStart >= 0)
                {
                    addBottomSpan(y, bottomRunStart, x);
                    bottomRunStart = -1;
                }
            }
        }

        stbi_image_free(loadedPixels);
        return mesh;
    }

    void Renderer::drawDroppedItems(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, Vec3 playerPosition)
    {
        updateDroppedItems(playerPosition);
        if (loadedDroppedItemCount() == 0 ||
            itemPipeline_ == VK_NULL_HANDLE ||
            droppedItemVertexBuffer_ == VK_NULL_HANDLE ||
            droppedItemIndexBuffer_ == VK_NULL_HANDLE ||
            droppedItemInstanceBuffer_ == VK_NULL_HANDLE ||
            droppedItemInstanceMapped_ == nullptr ||
            itemTextureArray_.descriptorSet == VK_NULL_HANDLE)
        {
            return;
        }

        const size_t itemCount = std::min(loadedDroppedItemCount(), MaxDroppedItems);
        std::vector<DroppedItemRenderInstance> renderInstances;
        renderInstances.reserve(std::min<size_t>(itemCount * 4u, MaxDroppedItemRenderInstances));
        const float aspect = static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);
        const Frustum frustum = makeFrustum(camera, {}, aspect);
        auto visualCopyCount = [](uint16_t count) -> size_t
        {
            if (count >= 49)
            {
                return 4;
            }
            if (count >= 17)
            {
                return 3;
            }
            if (count >= 2)
            {
                return 2;
            }
            return 1;
        };
        const std::array<Vec3, 4> visualOffsets{{
            {0.0f, 0.0f, 0.0f},
            {-0.08f, 0.0f, -0.04f},
            {0.08f, 0.0f, 0.04f},
            {-0.02f, 0.0f, 0.10f}
        }};

        size_t renderedItems = 0;
        for (const auto& trackedChunk : droppedItemCountsByChunk_)
        {
            const RuntimeChunk* chunk = worldRuntime_.find(trackedChunk.first);
            if (chunk == nullptr || !chunk->data)
            {
                continue;
            }

            const int chunkX = static_cast<int32_t>(static_cast<uint32_t>(trackedChunk.first >> 32u));
            const int chunkZ = static_cast<int32_t>(static_cast<uint32_t>(trackedChunk.first));
            const float minX = static_cast<float>(chunkX * ChunkSizeX) - 0.5f - cameraPosition.x;
            const float maxX = static_cast<float>(chunkX * ChunkSizeX + ChunkSizeX) - 0.5f - cameraPosition.x;
            const float minY = -cameraPosition.y;
            const float maxY = static_cast<float>(ChunkSizeY) - cameraPosition.y;
            const float minZ = static_cast<float>(chunkZ * ChunkSizeZ) - 0.5f - cameraPosition.z;
            const float maxZ = static_cast<float>(chunkZ * ChunkSizeZ + ChunkSizeZ) - 0.5f - cameraPosition.z;
            if (!aabbIntersectsFrustum(frustum, {minX, minY, minZ}, {maxX, maxY, maxZ}))
            {
                continue;
            }

            for (const WorldEntity& item : chunk->data->entities)
            {
                if (renderedItems >= itemCount || renderInstances.size() >= MaxDroppedItemRenderInstances)
                {
                    break;
                }
                if (item.type != WorldEntityType::DroppedItem ||
                    item.droppedItem.stack.itemId == 0 ||
                    item.droppedItem.stack.count == 0 ||
                    static_cast<size_t>(item.droppedItem.stack.itemId) >= itemDefinitions_.size())
                {
                    continue;
                }

                const ItemDefinition& definition = itemDefinitions_[item.droppedItem.stack.itemId];
                if (definition.droppedRender != ItemRenderType::ExtrudedSprite)
                {
                    continue;
                }
                if (static_cast<size_t>(item.droppedItem.stack.itemId) >= itemSpriteMeshes_.size() ||
                    itemSpriteMeshes_[item.droppedItem.stack.itemId].quads.empty())
                {
                    continue;
                }
                if (static_cast<size_t>(item.droppedItem.stack.itemId) >= itemSpriteGpuMeshes_.size() ||
                    itemSpriteGpuMeshes_[item.droppedItem.stack.itemId].indexCount == 0)
                {
                    continue;
                }

                const Vec3 interpolatedPosition{
                    item.previousPosition.x + (item.position.x - item.previousPosition.x) * droppedItemRenderAlpha_,
                    item.previousPosition.y + (item.position.y - item.previousPosition.y) * droppedItemRenderAlpha_,
                    item.previousPosition.z + (item.position.z - item.previousPosition.z) * droppedItemRenderAlpha_
                };
                const float distanceX = interpolatedPosition.x - cameraPosition.x;
                const float distanceY = interpolatedPosition.y - cameraPosition.y;
                const float distanceZ = interpolatedPosition.z - cameraPosition.z;
                if (distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ > DroppedItemRenderDistanceSquared)
                {
                    continue;
                }

                const float layer = static_cast<float>(definition.droppedTextureLayer);
                const size_t copies = visualCopyCount(item.droppedItem.stack.count);
                for (size_t copy = 0; copy < copies && renderInstances.size() < MaxDroppedItemRenderInstances; ++copy)
                {
                    const Vec3& offset = visualOffsets[copy];
                    const float copyRotationY = item.renderRotation + static_cast<float>(copy) * 1.5707963268f;
                    DroppedItemRenderInstance renderInstance{};
                    renderInstance.itemId = item.droppedItem.stack.itemId;
                    renderInstance.instance.centerX = interpolatedPosition.x + offset.x;
                    renderInstance.instance.centerY = interpolatedPosition.y + DroppedItemThickness * 0.5f + static_cast<float>(copy) * DroppedItemThickness;
                    renderInstance.instance.centerZ = interpolatedPosition.z + offset.z;
                    renderInstance.instance.rotationX = item.renderRotationX;
                    renderInstance.instance.rotationY = copyRotationY;
                    renderInstance.instance.rotationZ = item.renderRotationZ;
                    renderInstance.instance.textureLayer = layer;
                    renderInstance.instance.mipDistanceScale = 1.0f;
                    renderInstances.push_back(renderInstance);
                }
                ++renderedItems;
            }
        }

        if (renderInstances.empty())
        {
            return;
        }

        std::sort(renderInstances.begin(), renderInstances.end(), [](const DroppedItemRenderInstance& lhs, const DroppedItemRenderInstance& rhs)
        {
            return lhs.itemId < rhs.itemId;
        });

        auto* mappedInstances = static_cast<DroppedItemInstance*>(droppedItemInstanceMapped_);
        for (size_t i = 0; i < renderInstances.size(); ++i)
        {
            mappedInstances[i] = renderInstances[i].instance;
        }

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapchainExtent_.width);
        viewport.height = static_cast<float>(swapchainExtent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapchainExtent_;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        const Mat4 projection = perspective(FieldOfViewRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);

        TerrainPush push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = static_cast<float>(glfwGetTime());

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, itemPipeline_);
        vkCmdPushConstants(commandBuffer, particlePipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipelineLayout_, 0, 1, &itemTextureArray_.descriptorSet, 0, nullptr);
        const std::array<VkBuffer, 2> vertexBuffers = {droppedItemVertexBuffer_, droppedItemInstanceBuffer_};
        const std::array<VkDeviceSize, 2> vertexOffsets = {0, 0};
        vkCmdBindVertexBuffers(commandBuffer, 0, static_cast<uint32_t>(vertexBuffers.size()), vertexBuffers.data(), vertexOffsets.data());
        vkCmdBindIndexBuffer(commandBuffer, droppedItemIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);
        size_t batchStart = 0;
        while (batchStart < renderInstances.size())
        {
            const uint16_t itemId = renderInstances[batchStart].itemId;
            size_t batchEnd = batchStart + 1u;
            while (batchEnd < renderInstances.size() && renderInstances[batchEnd].itemId == itemId)
            {
                ++batchEnd;
            }

            const ItemSpriteGpuMesh& mesh = itemSpriteGpuMeshes_[itemId];
            vkCmdDrawIndexed(
                commandBuffer,
                mesh.indexCount,
                static_cast<uint32_t>(batchEnd - batchStart),
                mesh.firstIndex,
                0,
                static_cast<uint32_t>(batchStart));
            batchStart = batchEnd;
        }
    }

}
