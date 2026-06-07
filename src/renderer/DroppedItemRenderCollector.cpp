#include "renderer/DroppedItemRenderCollector.h"

#include "world/DroppedItemSystem.h"
#include "world/SkyLightSystem.h"

#include <algorithm>
#include <array>
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
        constexpr int FluidAmountBits = 7;
        constexpr uint16_t FluidAmountMask = (1u << FluidAmountBits) - 1u;
        constexpr uint16_t FluidWater = 1;
        constexpr uint16_t FluidFullAmount = 100;
        constexpr uint16_t FluidHeightStepAmount = 10;
        constexpr uint16_t FluidHeightLevels = 10;
        constexpr float FluidSurfaceMaxHeight = 0.8f;
        constexpr std::size_t MaxDroppedItems = world::DroppedItemSystem::MaxDroppedItems;
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

        uint16_t fluidId(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid >> FluidAmountBits);
        }

        uint16_t fluidAmount(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid & FluidAmountMask);
        }

        bool isWater(uint16_t fluid)
        {
            return fluidId(fluid) == FluidWater && fluidAmount(fluid) != 0;
        }

        float fluidSurfaceHeight(uint16_t amount)
        {
            const uint16_t clampedAmount = amount > FluidFullAmount ? FluidFullAmount : amount;
            const uint16_t level = static_cast<uint16_t>((clampedAmount + FluidHeightStepAmount - 1u) / FluidHeightStepAmount);
            return (static_cast<float>(level) / static_cast<float>(FluidHeightLevels)) * FluidSurfaceMaxHeight;
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
        renderInstances.reserve(std::min<std::size_t>(itemCount * 4u, MaxDroppedItemRenderInstances));

        const Vec3 cameraPosition = input.cameraPosition;
        const Frustum frustum = makeFrustum(input.camera, {}, input.aspect, input.fovRadians);
        const std::array<Vec3, 4> visualOffsets{{
            {0.0f, 0.0f, 0.0f},
            {-0.08f, 0.0f, -0.04f},
            {0.08f, 0.0f, 0.04f},
            {-0.02f, 0.0f, 0.10f}
        }};

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
                if (definition.droppedRender != input.renderType)
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

                ItemStack baseRenderStack = item.droppedItem.stack;
                baseRenderStack.count = 1;
                const world::DroppedItemSystem::Bounds renderBounds =
                    world::DroppedItemSystem::renderBoundsForStack(baseRenderStack, input.itemDefinitions);
                const float itemHeight = renderBounds.height;
                const float itemWidth = renderBounds.halfWidth * 2.0f;
                const float layer = static_cast<float>(definition.droppedTextureLayer);
                const uint8_t packedLight = input.lightAtWorld
                    ? input.lightAtWorld(
                        blockCoordinateXz(interpolatedPosition.x),
                        blockCoordinateY(interpolatedPosition.y + itemHeight),
                        blockCoordinateXz(interpolatedPosition.z))
                    : world::packLight(world::MaxSkyLight, 0);
                const float skyLight = static_cast<float>(world::skyLightFromPacked(packedLight)) / static_cast<float>(world::MaxSkyLight);
                const float blockLight = static_cast<float>(world::blockLightFromPacked(packedLight)) / static_cast<float>(world::MaxSkyLight);
                const std::size_t copies = world::DroppedItemSystem::visualCopyCount(item.droppedItem.stack.count);
                for (std::size_t copy = 0; copy < copies && renderInstances.size() < MaxDroppedItemRenderInstances; ++copy)
                {
                    const Vec3& offset = visualOffsets[copy];
                    const float copyCenterY = interpolatedPosition.y + itemHeight * 0.5f + static_cast<float>(copy) * itemHeight;
                    float waterTint = 0.0f;
                    if (input.fluidAtWorld)
                    {
                        const int fluidX = blockCoordinateXz(interpolatedPosition.x + offset.x);
                        const int fluidY = blockCoordinateY(copyCenterY);
                        const int fluidZ = blockCoordinateXz(interpolatedPosition.z + offset.z);
                        const uint16_t fluid = input.fluidAtWorld(fluidX, fluidY, fluidZ);
                        if (isWater(fluid))
                        {
                            const bool hasWaterAbove = isWater(input.fluidAtWorld(fluidX, fluidY + 1, fluidZ));
                            const float waterTop = static_cast<float>(fluidY) + (hasWaterAbove ? 1.0f : fluidSurfaceHeight(fluidAmount(fluid)));
                            if (copyCenterY < waterTop)
                            {
                                waterTint = hasWaterAbove ? 1.0f : std::clamp((waterTop - copyCenterY) / std::max(itemHeight, 0.01f), 0.0f, 1.0f);
                            }
                        }
                    }
                    DroppedItemRenderPath::RenderInstance renderInstance{};
                    renderInstance.itemId = item.droppedItem.stack.itemId;
                    renderInstance.instance.centerX = interpolatedPosition.x + offset.x;
                    renderInstance.instance.centerY = copyCenterY;
                    renderInstance.instance.centerZ = interpolatedPosition.z + offset.z;
                    renderInstance.instance.rotationX = item.renderRotationX;
                    renderInstance.instance.rotationY = item.renderRotation + static_cast<float>(copy) * 1.5707963268f;
                    renderInstance.instance.rotationZ = item.renderRotationZ;
                    renderInstance.instance.textureLayer = layer;
                    renderInstance.instance.mipDistanceScale = 1.0f;
                    renderInstance.instance.scaleX = itemWidth;
                    renderInstance.instance.scaleY = itemHeight;
                    renderInstance.instance.scaleZ = itemWidth;
                    renderInstance.instance.skyLight = skyLight;
                    renderInstance.instance.blockLight = blockLight;
                    renderInstance.instance.waterTint = waterTint;
                    renderInstances.push_back(renderInstance);
                }
                ++renderedItems;
            }
        }

        return renderInstances;
    }
}
