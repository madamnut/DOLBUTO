#include "renderer/Renderer.h"

#include "renderer/DroppedItemRenderCollector.h"
#include "renderer/RendererAudioBridge.h"
#include "renderer/RendererGameplayBridge.h"
#include "renderer/RendererTerrainRuntimeBridge.h"
#include "renderer/RendererUiRuntimeBridge.h"
#include "world/DroppedItemSystem.h"
#include "world/SkyLightSystem.h"
#include "world/WorldRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace dolbuto
{
    namespace
    {
        constexpr float TerrainNearPlane = 0.1f;
        constexpr float TerrainFarPlane = 4000.0f;
        constexpr float ViewmodelFieldOfViewRadians = 1.0471975512f;
        constexpr std::size_t ItemInstanceFrameStride = world::DroppedItemSystem::MaxDroppedItemRenderInstances + 4u;
        constexpr float ExtrudedSpriteDroppedItemSize = world::DroppedItemSystem::DroppedItemRenderSize;
        constexpr float ExtrudedSpriteDroppedItemThickness = world::DroppedItemSystem::DroppedItemThickness;
        constexpr float BlockModelDroppedItemSize = world::DroppedItemSystem::BlockModelDroppedItemSize;
        constexpr float HeldExtrudedSpriteBaseWidth = 0.34f;
        constexpr float HeldExtrudedSpriteBaseThickness = 0.035f;

        struct Mat4
        {
            float m[16]{};
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
    }

    void Renderer::drawHeldItem(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, uint16_t heldItemId, uint16_t offhandItemId, float skyBrightness, uint16_t heldPortableLightEmission, uint8_t playerPackedLight)
    {
        if (vulkan_.itemViewmodelPipeline == VK_NULL_HANDLE ||
            !droppedItemRenderPath_.ready() ||
            (heldItemId == 0 && offhandItemId == 0))
        {
            return;
        }

        (void)camera;
        (void)cameraPosition;

        std::vector<DroppedItemRenderPath::RenderInstance> spriteInstances;
        std::vector<DroppedItemRenderPath::RenderInstance> blockInstances;
        auto appendHeldItem = [&](uint16_t itemId, bool mirrored)
        {
            if (itemId == 0 ||
                static_cast<std::size_t>(itemId) >= client_.content.itemDefinitions().size() ||
                !droppedItemRenderPath_.meshReady(itemId))
            {
                return;
            }

            const ItemDefinition& definition = client_.content.itemDefinitions()[itemId];
            if (definition.heldRender != ItemRenderType::ExtrudedSprite &&
                definition.heldRender != ItemRenderType::BlockModel)
            {
                return;
            }

            const bool blockModel = definition.heldRender == ItemRenderType::BlockModel;
            const auto& heldConfig = blockModel
                ? client_.viewmodelConfig.heldBlockModelItem
                : client_.viewmodelConfig.heldItem;
            DroppedItemRenderPath::RenderInstance heldItem{};
            heldItem.itemId = itemId;
            heldItem.instance.centerX = mirrored ? -heldConfig.x : heldConfig.x;
            heldItem.instance.centerY = heldConfig.y;
            heldItem.instance.centerZ = heldConfig.z;
            heldItem.instance.rotationX = heldConfig.rotationX;
            heldItem.instance.rotationY = mirrored ? -heldConfig.rotationY : heldConfig.rotationY;
            heldItem.instance.rotationZ = mirrored ? -heldConfig.rotationZ : heldConfig.rotationZ;
            heldItem.instance.textureLayer = static_cast<float>(definition.heldTextureLayer);
            heldItem.instance.uvMirrorX = mirrored && blockModel ? 1.0f : 0.0f;
            heldItem.instance.geometryMirrorX = mirrored && !blockModel ? 1.0f : 0.0f;
            heldItem.instance.mipDistanceScale = heldConfig.scale;
            const float heldScale = std::max(heldConfig.scale, 0.001f);
            if (blockModel)
            {
                heldItem.instance.scaleX = BlockModelDroppedItemSize * heldScale;
                heldItem.instance.scaleY = BlockModelDroppedItemSize * heldScale;
                heldItem.instance.scaleZ = BlockModelDroppedItemSize * heldScale;
            }
            else
            {
                heldItem.instance.scaleX = HeldExtrudedSpriteBaseWidth * heldScale;
                heldItem.instance.scaleY = HeldExtrudedSpriteBaseThickness * heldScale;
                heldItem.instance.scaleZ = HeldExtrudedSpriteBaseWidth * heldScale;
            }
            heldItem.instance.skyLight = static_cast<float>(world::skyLightFromPacked(playerPackedLight)) / static_cast<float>(world::MaxSkyLight);
            heldItem.instance.blockLight = static_cast<float>(world::blockLightFromPacked(playerPackedLight)) / static_cast<float>(world::MaxSkyLight);

            if (blockModel)
            {
                blockInstances.push_back(heldItem);
            }
            else
            {
                spriteInstances.push_back(heldItem);
            }
        };

        appendHeldItem(heldItemId, false);
        appendHeldItem(offhandItemId, true);
        if (spriteInstances.empty() && blockInstances.empty())
        {
            return;
        }

        const float aspect = static_cast<float>(vulkan_.swapchainExtent.width) / static_cast<float>(vulkan_.swapchainExtent.height);
        const Mat4 projection = perspective(ViewmodelFieldOfViewRadians, aspect, TerrainNearPlane, TerrainFarPlane);

        DroppedItemRenderPath::PushConstants push{};
        std::memcpy(push.mvp, projection.m, sizeof(push.mvp));
        push.cameraPosition[0] = 0.0f;
        push.cameraPosition[1] = 0.0f;
        push.cameraPosition[2] = 0.0f;
        push.cameraPosition[3] = static_cast<float>(glfwGetTime());
        push.fluidWaterParams[1] = skyBrightness;
        push.dynamicLightParams[0] = static_cast<float>(heldPortableLightEmission);

        const std::size_t frameInstanceOffset = static_cast<std::size_t>(vulkan_.currentFrame) * ItemInstanceFrameStride +
            world::DroppedItemSystem::MaxDroppedItemRenderInstances + 2u;
        if (!spriteInstances.empty() && rendererAssets_.itemTextureArray.descriptorSet != VK_NULL_HANDLE)
        {
            droppedItemRenderPath_.draw(
                commandBuffer,
                vulkan_.swapchainExtent,
                vulkan_.itemViewmodelPipeline,
                vulkan_.particlePipelineLayout,
                rendererAssets_.itemTextureArray,
                push,
                spriteInstances,
                frameInstanceOffset);
        }
        if (!blockInstances.empty() && rendererAssets_.terrainTextureArray.descriptorSet != VK_NULL_HANDLE)
        {
            droppedItemRenderPath_.draw(
                commandBuffer,
                vulkan_.swapchainExtent,
                vulkan_.itemViewmodelPipeline,
                vulkan_.particlePipelineLayout,
                rendererAssets_.terrainTextureArray,
                push,
                blockInstances,
                frameInstanceOffset + spriteInstances.size());
        }
    }

    void Renderer::drawThirdPersonHeldItems(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, float fovRadians, float skyBrightness, uint16_t heldPortableLightEmission, uint16_t heldItemId, uint16_t offhandItemId, uint8_t playerPackedLight)
    {
        if (vulkan_.itemPipeline == VK_NULL_HANDLE ||
            !droppedItemRenderPath_.ready() ||
            !playerMeshRenderPath_.ready() ||
            (heldItemId == 0 && offhandItemId == 0))
        {
            return;
        }

        std::vector<DroppedItemRenderPath::RenderInstance> spriteInstances;
        std::vector<DroppedItemRenderPath::RenderInstance> blockInstances;
        auto appendAttachedItem = [&](uint16_t itemId, const PlayerMeshRenderPath::ItemAttachment& attachment, bool mirrored)
        {
            if (itemId == 0 ||
                !attachment.valid ||
                static_cast<std::size_t>(itemId) >= client_.content.itemDefinitions().size() ||
                !droppedItemRenderPath_.meshReady(itemId))
            {
                return;
            }

            const ItemDefinition& definition = client_.content.itemDefinitions()[itemId];
            if (definition.heldRender != ItemRenderType::ExtrudedSprite &&
                definition.heldRender != ItemRenderType::BlockModel)
            {
                return;
            }

            const bool blockModel = definition.heldRender == ItemRenderType::BlockModel;
            DroppedItemRenderPath::RenderInstance attachedItem{};
            attachedItem.itemId = itemId;
            attachedItem.instance.centerX = attachment.center.x;
            attachedItem.instance.centerY = attachment.center.y;
            attachedItem.instance.centerZ = attachment.center.z;
            attachedItem.instance.textureLayer = static_cast<float>(definition.heldTextureLayer);
            attachedItem.instance.uvMirrorX = mirrored && blockModel ? 1.0f : 0.0f;
            attachedItem.instance.geometryMirrorX = mirrored && !blockModel ? 1.0f : 0.0f;
            attachedItem.instance.mipDistanceScale = 1.0f;
            if (blockModel)
            {
                attachedItem.instance.scaleX = BlockModelDroppedItemSize;
                attachedItem.instance.scaleY = BlockModelDroppedItemSize;
                attachedItem.instance.scaleZ = BlockModelDroppedItemSize;
            }
            else
            {
                attachedItem.instance.scaleX = HeldExtrudedSpriteBaseWidth;
                attachedItem.instance.scaleY = HeldExtrudedSpriteBaseThickness;
                attachedItem.instance.scaleZ = HeldExtrudedSpriteBaseWidth;
            }
            attachedItem.instance.skyLight = static_cast<float>(world::skyLightFromPacked(playerPackedLight)) / static_cast<float>(world::MaxSkyLight);
            attachedItem.instance.blockLight = static_cast<float>(world::blockLightFromPacked(playerPackedLight)) / static_cast<float>(world::MaxSkyLight);
            attachedItem.instance.basisXX = attachment.xAxis.x;
            attachedItem.instance.basisXY = attachment.xAxis.y;
            attachedItem.instance.basisXZ = attachment.xAxis.z;
            attachedItem.instance.basisYX = -attachment.yAxis.x;
            attachedItem.instance.basisYY = -attachment.yAxis.y;
            attachedItem.instance.basisYZ = -attachment.yAxis.z;
            attachedItem.instance.basisZX = -attachment.zAxis.x;
            attachedItem.instance.basisZY = -attachment.zAxis.y;
            attachedItem.instance.basisZZ = -attachment.zAxis.z;

            if (blockModel)
            {
                blockInstances.push_back(attachedItem);
            }
            else
            {
                spriteInstances.push_back(attachedItem);
            }
        };

        appendAttachedItem(heldItemId, playerMeshRenderPath_.rightItemAttachment(), false);
        appendAttachedItem(offhandItemId, playerMeshRenderPath_.leftItemAttachment(), true);
        if (spriteInstances.empty() && blockInstances.empty())
        {
            return;
        }

        const float aspect = static_cast<float>(vulkan_.swapchainExtent.width) / static_cast<float>(vulkan_.swapchainExtent.height);
        const Mat4 projection = perspective(fovRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);

        DroppedItemRenderPath::PushConstants push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = static_cast<float>(glfwGetTime());
        push.fluidWaterParams[1] = skyBrightness;
        push.dynamicLightParams[0] = static_cast<float>(heldPortableLightEmission);

        const std::size_t frameInstanceOffset = static_cast<std::size_t>(vulkan_.currentFrame) * ItemInstanceFrameStride +
            world::DroppedItemSystem::MaxDroppedItemRenderInstances;
        if (!spriteInstances.empty() && rendererAssets_.itemTextureArray.descriptorSet != VK_NULL_HANDLE)
        {
            droppedItemRenderPath_.draw(
                commandBuffer,
                vulkan_.swapchainExtent,
                vulkan_.itemPipeline,
                vulkan_.particlePipelineLayout,
                rendererAssets_.itemTextureArray,
                push,
                spriteInstances,
                frameInstanceOffset);
        }
        if (!blockInstances.empty() && rendererAssets_.terrainTextureArray.descriptorSet != VK_NULL_HANDLE)
        {
            droppedItemRenderPath_.draw(
                commandBuffer,
                vulkan_.swapchainExtent,
                vulkan_.itemPipeline,
                vulkan_.particlePipelineLayout,
                rendererAssets_.terrainTextureArray,
                push,
                blockInstances,
                frameInstanceOffset + spriteInstances.size());
        }
    }

    void Renderer::drawDroppedItems(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, float fovRadians, float skyBrightness, uint16_t heldPortableLightEmission, Vec3 playerPosition)
    {
        const bool inventoryChanged = client_.gameplayRuntime.updateDroppedItems(
            playerPosition,
            glfwGetTime(),
            [this](DVec3 min, DVec3 max)
            {
                return gameplayBridge_->terrainAabbIntersects(min, max);
            },
            [this]()
            {
                audioBridge_->playItemPickup();
            },
            [this](RuntimeChunk& chunk)
            {
                terrainRuntimeBridge_->markRuntimeChunkDataDirty(chunk);
            });
        if (inventoryChanged)
        {
            uiRuntimeBridge_->updateInventoryUi();
        }

        if (client_.gameplayRuntime.loadedDroppedItemCount() == 0 ||
            vulkan_.itemPipeline == VK_NULL_HANDLE ||
            !droppedItemRenderPath_.ready())
        {
            return;
        }

        const float aspect = static_cast<float>(vulkan_.swapchainExtent.width) / static_cast<float>(vulkan_.swapchainExtent.height);
        auto collectDroppedItems = [&](ItemRenderType renderType)
        {
            return DroppedItemRenderCollector::collect(
            DroppedItemRenderCollector::Input{
                camera,
                cameraPosition,
                client_.gameplayRuntime.loadedDroppedItemCount(),
                client_.gameplayRuntime.droppedItemTrackedChunkCounts(),
                client_.content.itemDefinitions(),
                rendererAssets_.itemSpriteMeshes,
                renderType,
                aspect,
                fovRadians,
                client_.gameplayRuntime.droppedItemRenderAlpha(),
                [this](uint64_t key)
                {
                    return client_.worldRuntime.find(key);
                },
                [this](uint16_t itemId)
                {
                    return droppedItemRenderPath_.meshReady(itemId);
                },
                [this](int x, int y, int z)
                {
                    return client_.worldRuntime.lightAtWorld(x, y, z);
                },
                [this](int x, int y, int z)
                {
                    return client_.worldRuntime.fluidAtWorld(x, y, z);
                }
            });
        };

        std::vector<DroppedItemRenderPath::RenderInstance> spriteInstances = collectDroppedItems(ItemRenderType::ExtrudedSprite);
        std::vector<DroppedItemRenderPath::RenderInstance> blockInstances = collectDroppedItems(ItemRenderType::BlockModel);

        const Mat4 projection = perspective(fovRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);

        DroppedItemRenderPath::PushConstants push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = static_cast<float>(glfwGetTime());
        push.fluidWaterParams[0] = client_.renderConfig.fluidWaterAlpha;
        push.fluidWaterParams[1] = skyBrightness;
        push.dynamicLightParams[0] = static_cast<float>(heldPortableLightEmission);

        const std::size_t frameInstanceOffset = static_cast<std::size_t>(vulkan_.currentFrame) * ItemInstanceFrameStride;
        if (!spriteInstances.empty() && rendererAssets_.itemTextureArray.descriptorSet != VK_NULL_HANDLE)
        {
            droppedItemRenderPath_.draw(
                commandBuffer,
                vulkan_.swapchainExtent,
                vulkan_.itemPipeline,
                vulkan_.particlePipelineLayout,
                rendererAssets_.itemTextureArray,
                push,
                spriteInstances,
                frameInstanceOffset);
        }
        if (!blockInstances.empty() && rendererAssets_.terrainTextureArray.descriptorSet != VK_NULL_HANDLE)
        {
            droppedItemRenderPath_.draw(
                commandBuffer,
                vulkan_.swapchainExtent,
                vulkan_.itemPipeline,
                vulkan_.particlePipelineLayout,
                rendererAssets_.terrainTextureArray,
                push,
                blockInstances,
                frameInstanceOffset + spriteInstances.size());
        }
    }

    void Renderer::drawCrucibleMoltenSurfaces(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, float fovRadians, float skyBrightness, uint16_t heldPortableLightEmission)
    {
        if (vulkan_.crucibleMoltenPipeline == VK_NULL_HANDLE ||
            !crucibleMoltenRenderPath_.ready() ||
            !crucibleMoltenRenderPath_.meshReady(1) ||
            rendererAssets_.fluidTextureArray.descriptorSet == VK_NULL_HANDLE)
        {
            return;
        }

        constexpr float InnerSize = 0.60f;
        constexpr float InnerBottomY = 0.20f;
        constexpr float InnerTopY = 0.80f;
        constexpr float Capacity = 100.0f;
        std::vector<DroppedItemRenderPath::RenderInstance> instances;

        const std::vector<FluidDefinition>& fluids = client_.content.fluidDefinitions();
        for (const auto& entry : client_.worldRuntime.chunks())
        {
            const RuntimeChunk& chunk = entry.second;
            if (!chunk.data)
            {
                continue;
            }

            for (const BlockEntity& entity : chunk.data->blockEntities)
            {
                if (entity.type != BlockEntityType::Crucible ||
                    entity.moltenFluidId == 0 ||
                    entity.moltenAmount == 0 ||
                    static_cast<std::size_t>(entity.moltenFluidId) >= fluids.size())
                {
                    continue;
                }

                const FluidDefinition& fluid = fluids[entity.moltenFluidId];
                const float fill = std::clamp(static_cast<float>(entity.moltenAmount) / Capacity, 0.0f, 1.0f);
                const int worldX = chunk.chunkX * world::WorldRuntime::ChunkSizeX + entity.localX;
                const int worldZ = chunk.chunkZ * world::WorldRuntime::ChunkSizeZ + entity.localZ;
                const uint8_t packedLight = client_.worldRuntime.lightAtWorld(worldX, entity.y, worldZ);

                DroppedItemRenderPath::RenderInstance renderInstance{};
                renderInstance.itemId = 1;
                renderInstance.instance.centerX = static_cast<float>(worldX);
                renderInstance.instance.centerY = static_cast<float>(entity.y) + InnerBottomY + (InnerTopY - InnerBottomY) * fill;
                renderInstance.instance.centerZ = static_cast<float>(worldZ);
                renderInstance.instance.textureLayer = static_cast<float>(fluid.textureLayer);
                renderInstance.instance.mipDistanceScale = 1.0f;
                renderInstance.instance.scaleX = InnerSize;
                renderInstance.instance.scaleY = 1.0f;
                renderInstance.instance.scaleZ = InnerSize;
                renderInstance.instance.skyLight = static_cast<float>(world::skyLightFromPacked(packedLight)) / 15.0f;
                renderInstance.instance.blockLight = static_cast<float>(std::max<uint8_t>(world::blockLightFromPacked(packedLight), 10)) / 15.0f;
                instances.push_back(renderInstance);
            }
        }

        if (instances.empty())
        {
            return;
        }

        const float aspect = static_cast<float>(vulkan_.swapchainExtent.width) / static_cast<float>(vulkan_.swapchainExtent.height);
        const Mat4 projection = perspective(fovRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);

        DroppedItemRenderPath::PushConstants push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = static_cast<float>(glfwGetTime());
        push.fluidWaterParams[1] = skyBrightness;
        push.dynamicLightParams[0] = static_cast<float>(heldPortableLightEmission);

        crucibleMoltenRenderPath_.draw(
            commandBuffer,
            vulkan_.swapchainExtent,
            vulkan_.crucibleMoltenPipeline,
            vulkan_.particlePipelineLayout,
            rendererAssets_.fluidTextureArray,
            push,
            instances,
            0);
    }
}
