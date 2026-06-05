#include "renderer/Renderer.h"

#include "camera/Camera.h"
#include "renderer/RendererGameplayBridge.h"
#include "world/BlockVisualShape.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace dolbuto
{
    namespace
    {
        constexpr float TerrainNearPlane = 0.1f;
        constexpr float TerrainFarPlane = 4000.0f;
        constexpr float ViewmodelFieldOfViewRadians = 1.0471975512f;
        constexpr int FireAnimationFrameCount = 14;

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

        void appendLine(std::vector<LineVertex>& vertices, Vec3 a, Vec3 b)
        {
            vertices.push_back(LineVertex{a.x, a.y, a.z});
            vertices.push_back(LineVertex{b.x, b.y, b.z});
        }

        void appendBoxLines(std::vector<LineVertex>& vertices, const std::array<Vec3, 8>& corners)
        {
            appendLine(vertices, corners[0], corners[1]);
            appendLine(vertices, corners[1], corners[3]);
            appendLine(vertices, corners[3], corners[2]);
            appendLine(vertices, corners[2], corners[0]);

            appendLine(vertices, corners[4], corners[5]);
            appendLine(vertices, corners[5], corners[7]);
            appendLine(vertices, corners[7], corners[6]);
            appendLine(vertices, corners[6], corners[4]);

            appendLine(vertices, corners[0], corners[4]);
            appendLine(vertices, corners[1], corners[5]);
            appendLine(vertices, corners[2], corners[6]);
            appendLine(vertices, corners[3], corners[7]);
        }

        float terrainTextureLayerFor(const std::vector<std::string>& textureNames, const char* name)
        {
            const auto it = std::find(textureNames.begin(), textureNames.end(), name);
            return it != textureNames.end()
                ? static_cast<float>(std::distance(textureNames.begin(), it))
                : -1.0f;
        }
    }

    void Renderer::updatePlayerMesh(Vec3 playerPosition, float playerYaw, float playerHeadYaw, float playerHeadPitch, float playerWalkPhase, float playerWalkAmount, bool playerWalkReverse, bool playerCrouching, bool playerSprinting, bool playerProne, float animationSeconds, uint32_t frameIndex, uint8_t packedLight)
    {
        playerMeshRenderPath_.update(playerPosition, playerYaw, playerHeadYaw, playerHeadPitch, playerWalkPhase, playerWalkAmount, playerWalkReverse, playerCrouching, playerSprinting, playerProne, animationSeconds, frameIndex, packedLight);
    }

    void Renderer::updateFirstPersonHandMesh(const Camera& camera, Vec3 cameraPosition, uint32_t frameIndex, uint8_t packedLight)
    {
        playerMeshRenderPath_.updateFirstPersonHand(camera, cameraPosition, client_.viewmodelConfig.hand, frameIndex, packedLight);
    }

    void Renderer::drawTerrain(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, float fovRadians, float skyBrightness, uint16_t heldPortableLightEmission, bool wireframe, bool drawBlocks, bool drawFluids, uint32_t sceneImageIndex)
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
        push.fluidWaterParams[1] = skyBrightness;
        push.fluidWaterParams[2] = terrainTextureLayerFor(client_.content.blockTextureNames(), "fire/fire_00");
        push.fluidWaterParams[3] = static_cast<float>(FireAnimationFrameCount);
        push.dynamicLightParams[0] = static_cast<float>(heldPortableLightEmission);

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

    void Renderer::drawPlayer(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, float fovRadians, float skyBrightness, uint16_t heldPortableLightEmission, uint32_t frameIndex) const
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
        push.fluidWaterParams[1] = skyBrightness;
        push.dynamicLightParams[0] = static_cast<float>(heldPortableLightEmission);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.playerPipeline);
        vkCmdPushConstants(commandBuffer, vulkan_.terrainPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
        playerMeshRenderPath_.draw(commandBuffer, vulkan_.terrainPipelineLayout, rendererAssets_.playerTexture, frameIndex);
    }

    void Renderer::drawFirstPersonHand(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, float skyBrightness, uint16_t heldPortableLightEmission, uint32_t frameIndex) const
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
        push.fluidWaterParams[1] = skyBrightness;
        push.dynamicLightParams[0] = static_cast<float>(heldPortableLightEmission);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.playerViewmodelPipeline);
        vkCmdPushConstants(commandBuffer, vulkan_.terrainPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
        playerMeshRenderPath_.drawFirstPersonHand(commandBuffer, vulkan_.terrainPipelineLayout, rendererAssets_.playerTexture, frameIndex);
    }

    void Renderer::drawBlockBreakParticles(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, float fovRadians, float skyBrightness, uint16_t heldPortableLightEmission)
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
        push.fluidWaterParams[1] = skyBrightness;
        push.dynamicLightParams[0] = static_cast<float>(heldPortableLightEmission);

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
            rendererAssets_.smokeParticleTextureArray,
            push,
            overlay,
            now,
            [this](DVec3 min, DVec3 max)
            {
                return gameplayBridge_->terrainAabbIntersects(min, max);
            },
            [this](int x, int y, int z)
            {
                return client_.worldRuntime.lightAtWorld(x, y, z);
            });
    }

    void Renderer::drawBlockSelection(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, float fovRadians)
    {
        if (!client_.selection.hasSelectedBlock || vulkan_.selectionPipeline == VK_NULL_HANDLE || vulkan_.selectionLineVertexBuffer == VK_NULL_HANDLE)
        {
            return;
        }

        const int selectedX = client_.selection.selectedBlockX;
        const int selectedY = client_.selection.selectedBlockY;
        const int selectedZ = client_.selection.selectedBlockZ;
        const uint16_t selectedBlock = client_.selection.selectedBlockId;
        const BlockDefinition& selectedDefinition = gameplayBridge_->blockDefinition(selectedBlock);

        std::vector<LineVertex> vertices;
        vertices.reserve(24);
        if (selectedDefinition.renderType == BlockRenderType::Cross)
        {
            world::block_visual::forEachCrossQuad(
                selectedDefinition,
                selectedX,
                selectedY,
                selectedZ,
                [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d)
                {
                    appendLine(vertices, a, b);
                    appendLine(vertices, b, c);
                    appendLine(vertices, c, d);
                    appendLine(vertices, d, a);
                });
        }
        else if (selectedDefinition.renderType == BlockRenderType::Prop)
        {
            const assets::PropMesh* mesh = client_.content.propMeshForBlock(selectedBlock);
            if (mesh != nullptr && mesh->hasBounds)
            {
                const Vec3 min = mesh->boundsMin;
                const Vec3 max = mesh->boundsMax;
                std::array<Vec3, 8> corners{{
                    world::block_visual::transformLocalPoint(selectedDefinition, selectedX, selectedY, selectedZ, min.x, min.y, min.z),
                    world::block_visual::transformLocalPoint(selectedDefinition, selectedX, selectedY, selectedZ, max.x, min.y, min.z),
                    world::block_visual::transformLocalPoint(selectedDefinition, selectedX, selectedY, selectedZ, min.x, min.y, max.z),
                    world::block_visual::transformLocalPoint(selectedDefinition, selectedX, selectedY, selectedZ, max.x, min.y, max.z),
                    world::block_visual::transformLocalPoint(selectedDefinition, selectedX, selectedY, selectedZ, min.x, max.y, min.z),
                    world::block_visual::transformLocalPoint(selectedDefinition, selectedX, selectedY, selectedZ, max.x, max.y, min.z),
                    world::block_visual::transformLocalPoint(selectedDefinition, selectedX, selectedY, selectedZ, min.x, max.y, max.z),
                    world::block_visual::transformLocalPoint(selectedDefinition, selectedX, selectedY, selectedZ, max.x, max.y, max.z)
                }};
                appendBoxLines(vertices, corners);
            }
        }
        else if (selectedDefinition.renderType == BlockRenderType::Fire)
        {
            constexpr float Expand = 0.003f;
            const float minX = static_cast<float>(selectedX) - 0.4f - Expand;
            const float maxX = static_cast<float>(selectedX) + 0.4f + Expand;
            const float minY = static_cast<float>(selectedY) - Expand;
            const float maxY = static_cast<float>(selectedY) + 0.1f + Expand;
            const float minZ = static_cast<float>(selectedZ) - 0.4f - Expand;
            const float maxZ = static_cast<float>(selectedZ) + 0.4f + Expand;
            appendBoxLines(vertices, {{
                Vec3{minX, minY, minZ},
                Vec3{maxX, minY, minZ},
                Vec3{minX, minY, maxZ},
                Vec3{maxX, minY, maxZ},
                Vec3{minX, maxY, minZ},
                Vec3{maxX, maxY, minZ},
                Vec3{minX, maxY, maxZ},
                Vec3{maxX, maxY, maxZ}
            }});
        }
        else if (selectedDefinition.renderType == BlockRenderType::Slab)
        {
            constexpr float Expand = 0.003f;
            const world::block_visual::LocalAabb aabb = world::block_visual::slabWorldAabb(
                selectedX,
                selectedY,
                selectedZ,
                client_.worldRuntime.blockStateAtWorld(selectedX, selectedY, selectedZ));
            const float minX = aabb.min.x - Expand;
            const float maxX = aabb.max.x + Expand;
            const float minY = aabb.min.y - Expand;
            const float maxY = aabb.max.y + Expand;
            const float minZ = aabb.min.z - Expand;
            const float maxZ = aabb.max.z + Expand;
            appendBoxLines(vertices, {{
                Vec3{minX, minY, minZ},
                Vec3{maxX, minY, minZ},
                Vec3{minX, minY, maxZ},
                Vec3{maxX, minY, maxZ},
                Vec3{minX, maxY, minZ},
                Vec3{maxX, maxY, minZ},
                Vec3{minX, maxY, maxZ},
                Vec3{maxX, maxY, maxZ}
            }});
        }
        else if (selectedDefinition.renderType == BlockRenderType::HalfSlab)
        {
            constexpr float Expand = 0.003f;
            const world::block_visual::LocalAabb aabb = world::block_visual::halfSlabWorldAabb(
                selectedX,
                selectedY,
                selectedZ,
                client_.worldRuntime.blockStateAtWorld(selectedX, selectedY, selectedZ));
            const float minX = aabb.min.x - Expand;
            const float maxX = aabb.max.x + Expand;
            const float minY = aabb.min.y - Expand;
            const float maxY = aabb.max.y + Expand;
            const float minZ = aabb.min.z - Expand;
            const float maxZ = aabb.max.z + Expand;
            appendBoxLines(vertices, {{
                Vec3{minX, minY, minZ},
                Vec3{maxX, minY, minZ},
                Vec3{minX, minY, maxZ},
                Vec3{maxX, minY, maxZ},
                Vec3{minX, maxY, minZ},
                Vec3{maxX, maxY, minZ},
                Vec3{minX, maxY, maxZ},
                Vec3{maxX, maxY, maxZ}
            }});
        }
        else if (selectedDefinition.renderType == BlockRenderType::Crucible)
        {
            constexpr float Expand = 0.003f;
            world::block_visual::forEachCrucibleWorldAabb(selectedX, selectedY, selectedZ, [&](const world::block_visual::LocalAabb& aabb)
            {
                const float minX = aabb.min.x - Expand;
                const float maxX = aabb.max.x + Expand;
                const float minY = aabb.min.y - Expand;
                const float maxY = aabb.max.y + Expand;
                const float minZ = aabb.min.z - Expand;
                const float maxZ = aabb.max.z + Expand;
                appendBoxLines(vertices, {{
                    Vec3{minX, minY, minZ},
                    Vec3{maxX, minY, minZ},
                    Vec3{minX, minY, maxZ},
                    Vec3{maxX, minY, maxZ},
                    Vec3{minX, maxY, minZ},
                    Vec3{maxX, maxY, minZ},
                    Vec3{minX, maxY, maxZ},
                    Vec3{maxX, maxY, maxZ}
                }});
            });
        }
        if (vertices.empty())
        {
            constexpr float Expand = 0.003f;
            const float minX = static_cast<float>(selectedX) - 0.5f - Expand;
            const float maxX = static_cast<float>(selectedX) + 0.5f + Expand;
            const float minY = static_cast<float>(selectedY) - Expand;
            const float maxY = static_cast<float>(selectedY + 1) + Expand;
            const float minZ = static_cast<float>(selectedZ) - 0.5f - Expand;
            const float maxZ = static_cast<float>(selectedZ) + 0.5f + Expand;
            appendBoxLines(vertices, {{
                Vec3{minX, minY, minZ},
                Vec3{maxX, minY, minZ},
                Vec3{minX, minY, maxZ},
                Vec3{maxX, minY, maxZ},
                Vec3{minX, maxY, minZ},
                Vec3{maxX, maxY, minZ},
                Vec3{minX, maxY, maxZ},
                Vec3{maxX, maxY, maxZ}
            }});
        }

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
