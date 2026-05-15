#include "renderer/Renderer.h"

#include "renderer/DroppedItemRenderCollector.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace dolbuto
{
    namespace
    {
        constexpr float FieldOfViewRadians = 1.0471975512f;
        constexpr float TerrainNearPlane = 0.1f;
        constexpr float TerrainFarPlane = 4000.0f;

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

    void Renderer::drawDroppedItems(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, Vec3 playerPosition)
    {
        const bool inventoryChanged = gameplayRuntime_.updateDroppedItems(
            playerPosition,
            glfwGetTime(),
            [this](int x, int y, int z)
            {
                return terrainCellBlocksPlayer(x, y, z);
            },
            [this]()
            {
                playItemPickupSound();
            },
            [this](RuntimeChunk& chunk)
            {
                markRuntimeChunkDataDirty(chunk);
            });
        if (inventoryChanged)
        {
            updateInventoryUi();
        }

        if (gameplayRuntime_.loadedDroppedItemCount() == 0 ||
            itemPipeline_ == VK_NULL_HANDLE ||
            !droppedItemRenderPath_.ready() ||
            rendererAssets_.itemTextureArray.descriptorSet == VK_NULL_HANDLE)
        {
            return;
        }

        const float aspect = static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);
        std::vector<DroppedItemRenderPath::RenderInstance> renderInstances = DroppedItemRenderCollector::collect(
            DroppedItemRenderCollector::Input{
                camera,
                cameraPosition,
                gameplayRuntime_.loadedDroppedItemCount(),
                gameplayRuntime_.droppedItemTrackedChunkCounts(),
                content_.itemDefinitions(),
                rendererAssets_.itemSpriteMeshes,
                aspect,
                gameplayRuntime_.droppedItemRenderAlpha(),
                [this](uint64_t key)
                {
                    return worldRuntime_.find(key);
                },
                [this](uint16_t itemId)
                {
                    return droppedItemRenderPath_.meshReady(itemId);
                }
            });

        if (renderInstances.empty())
        {
            return;
        }

        const Mat4 projection = perspective(FieldOfViewRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);

        DroppedItemRenderPath::PushConstants push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = static_cast<float>(glfwGetTime());

        droppedItemRenderPath_.draw(
            commandBuffer,
            swapchainExtent_,
            itemPipeline_,
            particlePipelineLayout_,
            rendererAssets_.itemTextureArray,
            push,
            renderInstances);
    }
}
