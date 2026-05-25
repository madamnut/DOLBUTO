#include "renderer/DroppedItemRenderCollector.h"

#include "world/DroppedItemSystem.h"
#include "world/SkyLightSystem.h"

#include <algorithm>
#include <cmath>

namespace dolbuto
{
    namespace
    {
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeY = 512;
        constexpr int ChunkSizeZ = 16;
        constexpr float TerrainNearPlane = 0.1f;
        constexpr float TerrainFarPlane = 4000.0f;
        constexpr std::size_t MaxDroppedItems = world::DroppedItemSystem::MaxDroppedItems;
        constexpr float DroppedItemThickness = world::DroppedItemSystem::DroppedItemThickness;
        constexpr float DroppedItemRenderDistanceSquared = world::DroppedItemSystem::DroppedItemRenderDistanceSquared;
        constexpr std::size_t MaxDroppedItemRenderInstances = world::DroppedItemSystem::MaxDroppedItemRenderInstances;

        struct Frustum
        {
            Vec3 position{};
            Vec3 right{};
            Vec3 up{};
            Vec3 forward{};
            float tanHalfVertical = 1.0f;
            float tanHalfHorizontal = 1.0f;
        };

        Frustum makeFrustum(const Camera& camera, Vec3 position, float aspect, float fovRadians)
        {
            const Vec3 cameraRight = camera.right();
            const Vec3 terrainRight{-cameraRight.x, -cameraRight.y, -cameraRight.z};
            const Vec3 forward = camera.forward();
            const Vec3 terrainForward{forward.x, -forward.y, forward.z};
            const Vec3 terrainUp = normalize(cross(terrainForward, terrainRight));
            const float tanHalfVertical = std::tan(fovRadians * 0.5f);

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

        int blockCoordinateXz(float worldCoordinate)
        {
            return static_cast<int>(std::floor(worldCoordinate + 0.5f));
        }

        int blockCoordinateY(float worldCoordinate)
        {
            return static_cast<int>(std::floor(worldCoordinate));
        }
    }

    std::vector<DroppedItemRenderPath::RenderInstance> DroppedItemRenderCollector::collect(const Input& input)
    {
        std::vector<DroppedItemRenderPath::RenderInstance> renderInstances;
        if (input.loadedItemCount == 0 || !input.findChunk || !input.meshReady)
        {
            return renderInstances;
        }

        const std::size_t itemCount = std::min(input.loadedItemCount, MaxDroppedItems);
        renderInstances.reserve(std::min<std::size_t>(itemCount, MaxDroppedItemRenderInstances));

        const Vec3 cameraPosition = input.cameraPosition;
        const Frustum frustum = makeFrustum(input.camera, {}, input.aspect, input.fovRadians);

        std::size_t renderedItems = 0;
        for (const auto& trackedChunk : input.droppedItemCountsByChunk)
        {
            const RuntimeChunk* chunk = input.findChunk(trackedChunk.first);
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
                    static_cast<std::size_t>(item.droppedItem.stack.itemId) >= input.itemDefinitions.size())
                {
                    continue;
                }

                const ItemDefinition& definition = input.itemDefinitions[item.droppedItem.stack.itemId];
                if (definition.droppedRender != ItemRenderType::ExtrudedSprite)
                {
                    continue;
                }
                if (static_cast<std::size_t>(item.droppedItem.stack.itemId) >= input.itemSpriteMeshes.size() ||
                    input.itemSpriteMeshes[item.droppedItem.stack.itemId].quads.empty())
                {
                    continue;
                }
                if (!input.meshReady(item.droppedItem.stack.itemId))
                {
                    continue;
                }

                const Vec3 interpolatedPosition{
                    item.previousPosition.x + (item.position.x - item.previousPosition.x) * input.renderAlpha,
                    item.previousPosition.y + (item.position.y - item.previousPosition.y) * input.renderAlpha,
                    item.previousPosition.z + (item.position.z - item.previousPosition.z) * input.renderAlpha
                };
                const float distanceX = interpolatedPosition.x - cameraPosition.x;
                const float distanceY = interpolatedPosition.y - cameraPosition.y;
                const float distanceZ = interpolatedPosition.z - cameraPosition.z;
                if (distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ > DroppedItemRenderDistanceSquared)
                {
                    continue;
                }

                const float layer = static_cast<float>(definition.droppedTextureLayer);
                const uint8_t packedLight = input.lightAtWorld
                    ? input.lightAtWorld(
                        blockCoordinateXz(interpolatedPosition.x),
                        blockCoordinateY(interpolatedPosition.y + DroppedItemThickness),
                        blockCoordinateXz(interpolatedPosition.z))
                    : world::packLight(world::MaxSkyLight, 0);
                const float skyLight = static_cast<float>(world::skyLightFromPacked(packedLight)) / static_cast<float>(world::MaxSkyLight);
                const float blockLight = static_cast<float>(world::blockLightFromPacked(packedLight)) / static_cast<float>(world::MaxSkyLight);
                DroppedItemRenderPath::RenderInstance renderInstance{};
                renderInstance.itemId = item.droppedItem.stack.itemId;
                renderInstance.instance.centerX = interpolatedPosition.x;
                renderInstance.instance.centerY = interpolatedPosition.y + DroppedItemThickness * 0.5f;
                renderInstance.instance.centerZ = interpolatedPosition.z;
                renderInstance.instance.rotationX = item.renderRotationX;
                renderInstance.instance.rotationY = item.renderRotation;
                renderInstance.instance.rotationZ = item.renderRotationZ;
                renderInstance.instance.textureLayer = layer;
                renderInstance.instance.mipDistanceScale = 1.0f;
                renderInstance.instance.skyLight = skyLight;
                renderInstance.instance.blockLight = blockLight;
                renderInstances.push_back(renderInstance);
                ++renderedItems;
            }
        }

        return renderInstances;
    }
}
