#include "renderer/Renderer.h"

#include "camera/Camera.h"
#include "renderer/RendererGameplayBridge.h"

#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>

namespace dolbuto
{
    namespace
    {
        constexpr float TerrainNearPlane = 0.1f;
        constexpr float TerrainFarPlane = 4000.0f;
        constexpr float ViewmodelFieldOfViewRadians = 1.0471975512f;

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

    void Renderer::updatePlayerMesh(Vec3 playerPosition, float playerYaw, float playerHeadYaw, float playerHeadPitch, float playerWalkPhase, float playerWalkAmount)
    {
        playerMeshRenderPath_.update(playerPosition, playerYaw, playerHeadYaw, playerHeadPitch, playerWalkPhase, playerWalkAmount);
    }

    void Renderer::updateFirstPersonHandMesh(const Camera& camera, Vec3 cameraPosition)
    {
        playerMeshRenderPath_.updateFirstPersonHand(camera, cameraPosition, client_.viewmodelConfig.hand);
    }

    void Renderer::drawTerrain(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, float fovRadians, bool wireframe, bool drawBlocks, bool drawFluids, uint32_t sceneImageIndex)
    {
        if (terrainRenderPath_.empty())
        {
            return;
        }
        (void)sceneImageIndex;

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(vulkan_.swapchainExtent.width);
        viewport.height = static_cast<float>(vulkan_.swapchainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = vulkan_.swapchainExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        const float aspect = static_cast<float>(vulkan_.swapchainExtent.width) / static_cast<float>(vulkan_.swapchainExtent.height);
        const Mat4 projection = perspective(fovRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);
        const Vec3 cameraRight = camera.right();
        const Vec3 terrainRight{-cameraRight.x, -cameraRight.y, -cameraRight.z};
        const Vec3 forward = camera.forward();
        const Vec3 terrainForward{forward.x, -forward.y, forward.z};
        const Vec3 terrainUp = normalize(cross(terrainForward, terrainRight));
        const float tanHalfVertical = std::tan(fovRadians * 0.5f);
        const TerrainRenderPath::View terrainView{
            cameraPosition,
            {},
            terrainRight,
            terrainUp,
            terrainForward,
            tanHalfVertical,
            tanHalfVertical * aspect,
            TerrainNearPlane,
            TerrainFarPlane
        };

        TerrainPush push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = static_cast<float>(glfwGetTime());
        push.fluidWaterParams[0] = client_.renderConfig.fluidWaterAlpha;

        uint32_t visibleDrawCount = 0;
        uint32_t visibleFaceCount = 0;
        uint32_t visibleVertexCount = 0;
        auto addVisibleStats = [&](const TerrainRenderPath::Stats& stats)
        {
            visibleDrawCount += stats.drawCount;
            visibleFaceCount += stats.faceCount;
            visibleVertexCount += stats.vertexCount;
        };

        if (drawBlocks)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe ? vulkan_.terrainWireframePipeline : vulkan_.terrainPipeline);
            vkCmdPushConstants(commandBuffer, vulkan_.terrainPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.terrainPipelineLayout, 0, 1, &rendererAssets_.terrainTextureArray.descriptorSet, 0, nullptr);
            addVisibleStats(terrainRenderPath_.drawSolid(commandBuffer, vulkan_.terrainPipelineLayout, terrainView));
            if (!wireframe)
            {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.terrainBlendPipeline);
                addVisibleStats(terrainRenderPath_.drawBlend(commandBuffer, vulkan_.terrainPipelineLayout, terrainView));
            }
        }

        if (drawFluids)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.fluidPipeline);
            vkCmdPushConstants(commandBuffer, vulkan_.terrainPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.terrainPipelineLayout, 0, 1, &rendererAssets_.fluidTextureArray.descriptorSet, 0, nullptr);
            addVisibleStats(terrainRenderPath_.drawFluids(commandBuffer, vulkan_.terrainPipelineLayout, terrainView));
        }

        if (drawBlocks && !drawFluids && (visibleDrawCount != client_.diagnostics.terrainDrawCount ||
            visibleFaceCount != client_.diagnostics.terrainFaceCount ||
            visibleVertexCount != client_.diagnostics.terrainVertexCount))
        {
            terrainRenderPath_.setVisibleStats(visibleDrawCount, visibleFaceCount, visibleVertexCount);
            client_.diagnostics.terrainDrawCount = visibleDrawCount;
            client_.diagnostics.terrainFaceCount = visibleFaceCount;
            client_.diagnostics.terrainVertexCount = visibleVertexCount;
            debugOverlayText_.setTerrainStats(client_.diagnostics.terrainDrawCount, client_.diagnostics.terrainFaceCount, client_.diagnostics.terrainVertexCount);
        }
    }

    void Renderer::drawPlayer(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, float fovRadians) const
    {
        const float aspect = static_cast<float>(vulkan_.swapchainExtent.width) / static_cast<float>(vulkan_.swapchainExtent.height);
        const Mat4 projection = perspective(fovRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);

        TerrainPush push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = 0.0f;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.playerPipeline);
        vkCmdPushConstants(commandBuffer, vulkan_.terrainPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
        playerMeshRenderPath_.draw(commandBuffer, vulkan_.terrainPipelineLayout, rendererAssets_.playerTexture);
    }

    void Renderer::drawFirstPersonHand(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition) const
    {
        if (vulkan_.playerViewmodelPipeline == VK_NULL_HANDLE)
        {
            return;
        }

        const float aspect = static_cast<float>(vulkan_.swapchainExtent.width) / static_cast<float>(vulkan_.swapchainExtent.height);
        const Mat4 projection = perspective(ViewmodelFieldOfViewRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);

        TerrainPush push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = 0.0f;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.playerViewmodelPipeline);
        vkCmdPushConstants(commandBuffer, vulkan_.terrainPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
        playerMeshRenderPath_.drawFirstPersonHand(commandBuffer, vulkan_.terrainPipelineLayout, rendererAssets_.playerTexture);
    }

    void Renderer::drawBlockBreakParticles(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, float fovRadians)
    {
        const gameplay::BlockBreakingState& blockBreaking = client_.gameplayRuntime.blockBreakingState();
        const bool drawBreakingOverlay =
            blockBreaking.active &&
            blockBreaking.progress > 0.0f &&
            blockBreaking.progress < 1.0f &&
            gameplayBridge_->blockDefinition(blockBreaking.block).renderType == BlockRenderType::Cube;

        const float aspect = static_cast<float>(vulkan_.swapchainExtent.width) / static_cast<float>(vulkan_.swapchainExtent.height);
        const Mat4 projection = perspective(fovRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);

        ParticleRenderPath::PushConstants push{};
        const double now = glfwGetTime();
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = static_cast<float>(now);

        ParticleRenderPath::BreakingOverlay overlay{};
        if (drawBreakingOverlay)
        {
            overlay.active = true;
            overlay.x = blockBreaking.x;
            overlay.y = blockBreaking.y;
            overlay.z = blockBreaking.z;
            overlay.progress = blockBreaking.progress;
            overlay.textureLayers = client_.content.blockBreakingTextureLayers().data();
            overlay.textureLayerCount = client_.content.blockBreakingTextureLayers().size();
        }

        particleRenderPath_.draw(
            commandBuffer,
            camera,
            vulkan_.swapchainExtent,
            vulkan_.particlePipeline,
            vulkan_.particlePipelineLayout,
            rendererAssets_.terrainTextureArray,
            push,
            overlay,
            now,
            [this](int x, int y, int z)
            {
                return gameplayBridge_->terrainCellBlocksPlayer(x, y, z);
            });
    }

    void Renderer::drawBlockSelection(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, float fovRadians)
    {
        if (!client_.selection.hasSelectedBlock || vulkan_.selectionPipeline == VK_NULL_HANDLE || vulkan_.selectionLineVertexBuffer == VK_NULL_HANDLE)
        {
            return;
        }

        constexpr float Expand = 0.003f;
        const float minX = static_cast<float>(client_.selection.selectedBlockX) - 0.5f - Expand;
        const float maxX = static_cast<float>(client_.selection.selectedBlockX) + 0.5f + Expand;
        const float minY = static_cast<float>(client_.selection.selectedBlockY) - Expand;
        const float maxY = static_cast<float>(client_.selection.selectedBlockY + 1) + Expand;
        const float minZ = static_cast<float>(client_.selection.selectedBlockZ) - 0.5f - Expand;
        const float maxZ = static_cast<float>(client_.selection.selectedBlockZ) + 0.5f + Expand;

        const std::array<LineVertex, 24> vertices = {
            LineVertex{minX, minY, minZ}, LineVertex{maxX, minY, minZ},
            LineVertex{maxX, minY, minZ}, LineVertex{maxX, minY, maxZ},
            LineVertex{maxX, minY, maxZ}, LineVertex{minX, minY, maxZ},
            LineVertex{minX, minY, maxZ}, LineVertex{minX, minY, minZ},

            LineVertex{minX, maxY, minZ}, LineVertex{maxX, maxY, minZ},
            LineVertex{maxX, maxY, minZ}, LineVertex{maxX, maxY, maxZ},
            LineVertex{maxX, maxY, maxZ}, LineVertex{minX, maxY, maxZ},
            LineVertex{minX, maxY, maxZ}, LineVertex{minX, maxY, minZ},

            LineVertex{minX, minY, minZ}, LineVertex{minX, maxY, minZ},
            LineVertex{maxX, minY, minZ}, LineVertex{maxX, maxY, minZ},
            LineVertex{maxX, minY, maxZ}, LineVertex{maxX, maxY, maxZ},
            LineVertex{minX, minY, maxZ}, LineVertex{minX, maxY, maxZ}
        };

        void* data = nullptr;
        vkMapMemory(vulkan_.device, vulkan_.selectionLineVertexMemory, 0, sizeof(LineVertex) * vertices.size(), 0, &data);
        std::memcpy(data, vertices.data(), sizeof(LineVertex) * vertices.size());
        vkUnmapMemory(vulkan_.device, vulkan_.selectionLineVertexMemory);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(vulkan_.swapchainExtent.width);
        viewport.height = static_cast<float>(vulkan_.swapchainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = vulkan_.swapchainExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        const float aspect = static_cast<float>(vulkan_.swapchainExtent.width) / static_cast<float>(vulkan_.swapchainExtent.height);
        const Mat4 projection = perspective(fovRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);

        TerrainPush push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = 0.0f;

        const VkDeviceSize offset = 0;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.selectionPipeline);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vulkan_.selectionLineVertexBuffer, &offset);
        vkCmdPushConstants(commandBuffer, vulkan_.selectionPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    }


}
