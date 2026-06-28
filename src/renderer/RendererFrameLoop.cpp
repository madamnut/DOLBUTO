#include "renderer/Renderer.h"

#include "platform/RuntimePaths.h"
#include "renderer/RendererAudioBridge.h"
#include "renderer/RendererDiagnosticsBridge.h"
#include "renderer/RendererTerrainRuntimeBridge.h"
#include "renderer/RendererUiRuntimeBridge.h"
#include "world/WorldRuntime.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace dolbuto
{
    namespace
    {
        constexpr int MaxFramesInFlight = 2;
        constexpr int FluidAmountBits = 7;
        constexpr uint16_t FluidAmountMask = (1u << FluidAmountBits) - 1u;
        constexpr uint16_t FluidWater = 1;
        constexpr uint16_t FluidFullAmount = 100;
        constexpr uint16_t FluidHeightStepAmount = 10;
        constexpr uint16_t FluidHeightLevels = 10;
        constexpr float FluidSurfaceMaxHeight = 0.8f;
        constexpr float WaterSurfaceSplitMargin = 0.05f;

        Vec3 toVec3(DVec3 value)
        {
            return {
                static_cast<float>(value.x),
                static_cast<float>(value.y),
                static_cast<float>(value.z)
            };
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
            if (clampedAmount == 0)
            {
                return 0.0f;
            }

            const uint16_t level = static_cast<uint16_t>((clampedAmount + FluidHeightStepAmount - 1u) / FluidHeightStepAmount);
            return (static_cast<float>(level) / static_cast<float>(FluidHeightLevels)) * FluidSurfaceMaxHeight;
        }

        std::filesystem::path screenshotPath()
        {
            const std::filesystem::path directory = screenshotDirectory();
            std::filesystem::create_directories(directory);

            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

            std::tm localTime{};
#ifdef _WIN32
            localtime_s(&localTime, &time);
#else
            localtime_r(&time, &localTime);
#endif

            std::ostringstream name;
            name << "DOLBUTO_"
                << std::put_time(&localTime, "%Y%m%d_%H%M%S")
                << "_" << std::setw(3) << std::setfill('0') << milliseconds
                << ".bmp";
            return directory / name.str();
        }

        void writeBmp(const std::filesystem::path& path, const unsigned char* pixels, uint32_t width, uint32_t height, VkFormat format)
        {
            const bool bgra = format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM;
            const bool rgba = format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_R8G8B8A8_UNORM;
            if (!bgra && !rgba)
            {
                throw std::runtime_error("Unsupported screenshot swapchain format.");
            }

            const uint32_t rowStride = ((width * 3u) + 3u) & ~3u;
            const uint32_t imageSize = rowStride * height;
            const uint32_t fileSize = 54u + imageSize;
            std::vector<unsigned char> file(fileSize);

            file[0] = 'B';
            file[1] = 'M';
            std::memcpy(file.data() + 2, &fileSize, sizeof(fileSize));
            const uint32_t pixelOffset = 54;
            std::memcpy(file.data() + 10, &pixelOffset, sizeof(pixelOffset));
            const uint32_t dibSize = 40;
            const int32_t bmpWidth = static_cast<int32_t>(width);
            const int32_t bmpHeight = static_cast<int32_t>(height);
            const uint16_t planes = 1;
            const uint16_t bitsPerPixel = 24;
            std::memcpy(file.data() + 14, &dibSize, sizeof(dibSize));
            std::memcpy(file.data() + 18, &bmpWidth, sizeof(bmpWidth));
            std::memcpy(file.data() + 22, &bmpHeight, sizeof(bmpHeight));
            std::memcpy(file.data() + 26, &planes, sizeof(planes));
            std::memcpy(file.data() + 28, &bitsPerPixel, sizeof(bitsPerPixel));
            std::memcpy(file.data() + 34, &imageSize, sizeof(imageSize));

            unsigned char* out = file.data() + pixelOffset;
            for (uint32_t y = 0; y < height; ++y)
            {
                const uint32_t sourceY = height - 1u - y;
                const unsigned char* source = pixels + static_cast<size_t>(sourceY) * width * 4u;
                unsigned char* row = out + static_cast<size_t>(y) * rowStride;
                for (uint32_t x = 0; x < width; ++x)
                {
                    const unsigned char* pixel = source + static_cast<size_t>(x) * 4u;
                    row[x * 3u + 0u] = bgra ? pixel[0] : pixel[2];
                    row[x * 3u + 1u] = pixel[1];
                    row[x * 3u + 2u] = bgra ? pixel[2] : pixel[0];
                }
            }

            std::ofstream stream(path, std::ios::binary);
            if (!stream.is_open())
            {
                throw std::runtime_error("Failed to open screenshot file.");
            }
            stream.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        }

        void clearDepthAttachment(VkCommandBuffer commandBuffer, VkExtent2D extent)
        {
            VkClearAttachment depthClear{};
            depthClear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            depthClear.colorAttachment = VK_ATTACHMENT_UNUSED;
            depthClear.clearValue.depthStencil = {1.0f, 0};

            VkClearRect clearRect{};
            clearRect.rect.offset = {0, 0};
            clearRect.rect.extent = extent;
            clearRect.baseArrayLayer = 0;
            clearRect.layerCount = 1;

            vkCmdClearAttachments(commandBuffer, 1, &depthClear, 1, &clearRect);
        }

        int blockCoordinateXz(float worldCoordinate)
        {
            return static_cast<int>(std::floor(worldCoordinate + 0.5f));
        }

        int blockCoordinateY(float worldCoordinate)
        {
            return static_cast<int>(std::floor(worldCoordinate));
        }

        ScreenPresentation::WaterOverlay waterOverlayForCamera(
            const world::WorldRuntime& worldRuntime,
            const Camera& camera,
            Vec3 cameraPosition,
            float fovRadians)
        {
            const int x = blockCoordinateXz(cameraPosition.x);
            const int y = blockCoordinateY(cameraPosition.y);
            const int z = blockCoordinateXz(cameraPosition.z);
            const uint16_t cameraFluid = worldRuntime.fluidAtWorld(x, y, z);
            if (!isWater(cameraFluid))
            {
                return {};
            }

            int surfaceY = y;
            uint16_t surfaceFluid = cameraFluid;
            while (surfaceY + 1 < world::WorldRuntime::ChunkSizeY)
            {
                const uint16_t aboveFluid = worldRuntime.fluidAtWorld(x, surfaceY + 1, z);
                if (!isWater(aboveFluid))
                {
                    break;
                }
                ++surfaceY;
                surfaceFluid = aboveFluid;
            }

            const float surfaceHeight = static_cast<float>(surfaceY) + fluidSurfaceHeight(fluidAmount(surfaceFluid));
            if (cameraPosition.y > surfaceHeight)
            {
                return {};
            }

            ScreenPresentation::WaterOverlay overlay{};
            overlay.active = true;
            if (cameraPosition.y < surfaceHeight - WaterSurfaceSplitMargin)
            {
                overlay.waterLineY = -1.0f;
                return overlay;
            }

            const Vec3 forward = camera.forward();
            const Vec3 up = camera.up();
            const float tanHalfFov = std::max(std::tan(fovRadians * 0.5f), 0.001f);
            const float upY = std::max(up.y, 0.001f);
            const float horizonLine = -forward.y / (tanHalfFov * upY);
            const float waterLine = std::clamp(horizonLine, -1.0f, 1.0f);

            overlay.waterLineY = waterLine;
            return overlay;
        }
    }

    void Renderer::drawFrame(const RendererFrame& frame)
    {
        const Vec3 cameraPositionFloat = toVec3(frame.cameraPosition);
        const Vec3 playerPositionFloat = toVec3(frame.playerPosition);
        const uint8_t playerPackedLight = client_.worldRuntime.lightAtWorld(
            blockCoordinateXz(playerPositionFloat.x),
            blockCoordinateY(playerPositionFloat.y + 1.0f),
            blockCoordinateXz(playerPositionFloat.z));
        auto recordMax = [this](game::ClientPerfCounter counter, const std::chrono::steady_clock::time_point& start)
        {
            game::recordPerfMax(
                client_.diagnostics.perfMax,
                counter,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        };

        audioBridge_->updateListener(frame.camera, cameraPositionFloat);
        audioBridge_->updateMusicPlayback(frame.menuOverlayMode, frame.gameSceneRenderEnabled);
        diagnosticsBridge_->updateTerrainDebugText();

        auto sectionStart = std::chrono::steady_clock::now();
        vkWaitForFences(vulkan_.device, 1, &vulkan_.inFlightFences[vulkan_.currentFrame], VK_TRUE, UINT64_MAX);
        recordMax(game::ClientPerfCounter::RenderFenceWait, sectionStart);
        if (vulkan_.timestampSupported && vulkan_.timestampQueryPool != VK_NULL_HANDLE && vulkan_.timestampQueryReady[vulkan_.currentFrame])
        {
            std::array<uint64_t, 2> timestamps{};
            const VkResult queryResult = vkGetQueryPoolResults(
                vulkan_.device,
                vulkan_.timestampQueryPool,
                vulkan_.currentFrame * 2,
                2,
                sizeof(uint64_t) * timestamps.size(),
                timestamps.data(),
                sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT);
            if (queryResult == VK_SUCCESS && timestamps[1] >= timestamps[0])
            {
                vulkan_.lastGpuFrameMs = static_cast<double>(timestamps[1] - timestamps[0]) * static_cast<double>(vulkan_.timestampPeriod) / 1000000.0;
            }
        }
        if (frame.worldUpdateEnabled)
        {
            terrainRuntimeBridge_->updateLoadedChunks(frame.playerPosition);
            terrainRuntimeBridge_->processCompletedTerrainJobs();
        }
        const auto cpuStart = std::chrono::steady_clock::now();

        uint32_t imageIndex = 0;
        sectionStart = std::chrono::steady_clock::now();
        VkResult result = vkAcquireNextImageKHR(vulkan_.device, vulkan_.swapchain, UINT64_MAX, vulkan_.imageAvailableSemaphores[vulkan_.currentFrame], VK_NULL_HANDLE, &imageIndex);
        recordMax(game::ClientPerfCounter::RenderAcquire, sectionStart);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapchain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            throw std::runtime_error("Failed to acquire swapchain image.");
        }

        vkResetFences(vulkan_.device, 1, &vulkan_.inFlightFences[vulkan_.currentFrame]);
        vkResetCommandBuffer(vulkan_.commandBuffers[vulkan_.currentFrame], 0);

        VkBuffer screenshotBuffer = VK_NULL_HANDLE;
        VkDeviceMemory screenshotMemory = VK_NULL_HANDLE;
        const VkDeviceSize screenshotSize = static_cast<VkDeviceSize>(vulkan_.swapchainExtent.width) * static_cast<VkDeviceSize>(vulkan_.swapchainExtent.height) * 4u;
        if (frame.screenshotRequested)
        {
            gpuResources_.createBuffer(
                screenshotSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                screenshotBuffer,
                screenshotMemory);
        }

        if (frame.showPlayer)
        {
            updatePlayerMesh(
                playerPositionFloat,
                frame.playerYaw,
                frame.playerHeadYaw,
                frame.playerHeadPitch,
                frame.playerWalkPhase,
                frame.playerWalkAmount,
                frame.playerWalkReverse,
                frame.playerCrouching,
                frame.playerSprinting,
                frame.playerProne,
                static_cast<float>(glfwGetTime()),
                vulkan_.currentFrame,
                playerPackedLight);
        }
        if (frame.showFirstPersonHand && frame.heldItemId == 0)
        {
            updateFirstPersonHandMesh(frame.camera, cameraPositionFloat, vulkan_.currentFrame, playerPackedLight);
        }
        ensureClimateOverlayTexture(frame.climateOverlayMode);
        ScreenPresentation::WaterOverlay waterOverlay = frame.gameSceneRenderEnabled
            ? waterOverlayForCamera(client_.worldRuntime, frame.camera, cameraPositionFloat, frame.fovRadians)
            : ScreenPresentation::WaterOverlay{};
        if (waterOverlay.active)
        {
            waterOverlay.active = client_.renderConfig.fluidWaterScreenBlurEnabled;
            waterOverlay.blurSpread = client_.renderConfig.fluidWaterScreenBlurSpread;
            waterOverlay.blurIntensity = client_.renderConfig.fluidWaterScreenBlurIntensity;
            waterOverlay.tint = client_.renderConfig.fluidWaterScreenBlurTint;
        }
        sectionStart = std::chrono::steady_clock::now();
        recordCommandBuffer(
            vulkan_.commandBuffers[vulkan_.currentFrame],
            imageIndex,
            frame.camera,
            cameraPositionFloat,
            frame.fovRadians,
            frame.skyBrightness,
            waterOverlay,
            playerPositionFloat,
            playerPackedLight,
            frame.heldPortableLightEmission,
            frame.fpsText,
            frame.perfText,
            frame.debugTextVisible,
            screenshotBuffer,
            frame.showPlayer,
            frame.terrainWireframe,
            frame.climateOverlayMode,
            frame.menuOverlayMode,
            frame.hudVisible,
            frame.gameSceneRenderEnabled,
            frame.showFirstPersonHand,
            frame.heldItemId,
            frame.offhandItemId,
            frame.heldItemStack,
            frame.offhandItemStack,
            frame.worldTicks,
            frame.radialMenu);
        recordMax(game::ClientPerfCounter::RenderRecord, sectionStart);

        VkSemaphore waitSemaphores[] = {vulkan_.imageAvailableSemaphores[vulkan_.currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore signalSemaphores[] = {vulkan_.renderFinishedSemaphores[vulkan_.currentFrame]};

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &vulkan_.commandBuffers[vulkan_.currentFrame];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        sectionStart = std::chrono::steady_clock::now();
        if (vkQueueSubmit(vulkan_.graphicsQueue, 1, &submitInfo, vulkan_.inFlightFences[vulkan_.currentFrame]) != VK_SUCCESS)
        {
            if (screenshotBuffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(vulkan_.device, screenshotBuffer, nullptr);
                vkFreeMemory(vulkan_.device, screenshotMemory, nullptr);
            }
            throw std::runtime_error("Failed to submit draw command buffer.");
        }
        recordMax(game::ClientPerfCounter::RenderSubmit, sectionStart);
        if (vulkan_.timestampSupported && vulkan_.timestampQueryPool != VK_NULL_HANDLE)
        {
            vulkan_.timestampQueryReady[vulkan_.currentFrame] = true;
        }

        if (screenshotBuffer != VK_NULL_HANDLE)
        {
            vkWaitForFences(vulkan_.device, 1, &vulkan_.inFlightFences[vulkan_.currentFrame], VK_TRUE, UINT64_MAX);
            saveScreenshot(screenshotMemory, screenshotSize);
            vkDestroyBuffer(vulkan_.device, screenshotBuffer, nullptr);
            vkFreeMemory(vulkan_.device, screenshotMemory, nullptr);
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &vulkan_.swapchain;
        presentInfo.pImageIndices = &imageIndex;

        sectionStart = std::chrono::steady_clock::now();
        result = vkQueuePresentKHR(vulkan_.presentQueue, &presentInfo);
        recordMax(game::ClientPerfCounter::RenderPresent, sectionStart);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || vulkan_.framebufferResized)
        {
            vulkan_.framebufferResized = false;
            recreateSwapchain();
        }
        else if (result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to present swapchain image.");
        }

        const auto cpuEnd = std::chrono::steady_clock::now();
        const double renderCpuMs = std::chrono::duration<double, std::milli>(cpuEnd - cpuStart).count();
        game::recordPerfMax(client_.diagnostics.perfMax, game::ClientPerfCounter::RenderCpu, renderCpuMs);
        diagnosticsBridge_->updatePerformanceText(renderCpuMs);
        vulkan_.currentFrame = (vulkan_.currentFrame + 1) % MaxFramesInFlight;
    }

    void Renderer::setFramebufferResized()
    {
        vulkan_.framebufferResized = true;
    }

    void Renderer::createCommandBuffers()
    {
        vulkan_.commandBuffers.resize(MaxFramesInFlight);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = vulkan_.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(vulkan_.commandBuffers.size());

        if (vkAllocateCommandBuffers(vulkan_.device, &allocInfo, vulkan_.commandBuffers.data()) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate command buffers.");
        }
    }

    void Renderer::createSyncObjects()
    {
        vulkan_.imageAvailableSemaphores.resize(MaxFramesInFlight);
        vulkan_.renderFinishedSemaphores.resize(MaxFramesInFlight);
        vulkan_.inFlightFences.resize(MaxFramesInFlight);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MaxFramesInFlight; ++i)
        {
            if (vkCreateSemaphore(vulkan_.device, &semaphoreInfo, nullptr, &vulkan_.imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(vulkan_.device, &semaphoreInfo, nullptr, &vulkan_.renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(vulkan_.device, &fenceInfo, nullptr, &vulkan_.inFlightFences[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create sync objects.");
            }
        }
    }

    void Renderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera, Vec3 cameraPosition, float fovRadians, float skyBrightness, ScreenPresentation::WaterOverlay waterOverlay, Vec3 playerPosition, uint8_t playerPackedLight, uint16_t heldPortableLightEmission, std::string_view fpsText, std::string_view perfText, bool debugTextVisible, VkBuffer screenshotBuffer, bool showPlayer, bool terrainWireframe, int climateOverlayMode, int menuOverlayMode, bool hudVisible, bool gameSceneRenderEnabled, bool showFirstPersonHand, uint16_t heldItemId, uint16_t offhandItemId, const ItemStack& heldItemStack, const ItemStack& offhandItemStack, uint64_t worldTicks, const game::RadialMenuRenderFrame& radialMenu)
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to begin command buffer.");
        }
        if (vulkan_.timestampSupported && vulkan_.timestampQueryPool != VK_NULL_HANDLE)
        {
            const uint32_t firstQuery = vulkan_.currentFrame * 2;
            vkCmdResetQueryPool(commandBuffer, vulkan_.timestampQueryPool, firstQuery, 2);
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, vulkan_.timestampQueryPool, firstQuery);
        }

        VkClearValue clearColor{};
        clearColor.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkClearValue clearBloom{};
        clearBloom.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkClearValue clearDepth{};
        clearDepth.depthStencil = {1.0f, 0};
        std::array<VkClearValue, 3> sceneClearValues = {clearColor, clearBloom, clearDepth};
        std::array<VkClearValue, 2> clearValues = {clearColor, clearDepth};

        VkRenderPassBeginInfo scenePassInfo{};
        scenePassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        scenePassInfo.renderPass = vulkan_.sceneRenderPass;
        scenePassInfo.framebuffer = vulkan_.sceneFramebuffers[imageIndex];
        scenePassInfo.renderArea.offset = {0, 0};
        scenePassInfo.renderArea.extent = vulkan_.swapchainExtent;
        scenePassInfo.clearValueCount = static_cast<uint32_t>(sceneClearValues.size());
        scenePassInfo.pClearValues = sceneClearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &scenePassInfo, VK_SUBPASS_CONTENTS_INLINE);

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

        if (gameSceneRenderEnabled)
        {
            skyRenderPath_.draw(
                commandBuffer,
                vulkan_.skyPipeline,
                vulkan_.skyPipelineLayout,
                camera,
                fovRadians,
                vulkan_.swapchainExtent,
                worldTicks);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.sceneSpritePipeline);
            screenPresentation_.drawSkySprites(
                commandBuffer,
                camera,
                fovRadians,
                vulkan_.swapchainExtent,
                worldTicks,
                rendererAssets_,
                spriteRenderPath_,
                vulkan_.pipelineLayout,
                textRenderPath_.vertexBuffer());

            drawTerrain(commandBuffer, camera, cameraPosition, fovRadians, skyBrightness, heldPortableLightEmission, terrainWireframe, true, false, imageIndex);
            drawTerrain(commandBuffer, camera, cameraPosition, fovRadians, skyBrightness, heldPortableLightEmission, terrainWireframe, false, true, imageIndex);
            drawCrucibleMoltenSurfaces(commandBuffer, camera, cameraPosition, fovRadians, skyBrightness, heldPortableLightEmission);
            if (showPlayer && menuOverlayMode == 0)
            {
                drawPlayer(commandBuffer, camera, cameraPosition, fovRadians, skyBrightness, heldPortableLightEmission, vulkan_.currentFrame);
                drawThirdPersonHeldItems(commandBuffer, camera, cameraPosition, fovRadians, skyBrightness, heldPortableLightEmission, heldItemStack, offhandItemStack, playerPackedLight);
            }
            drawBlockBreakParticles(commandBuffer, camera, cameraPosition, fovRadians, skyBrightness, heldPortableLightEmission);
            drawDroppedItems(commandBuffer, camera, cameraPosition, fovRadians, skyBrightness, heldPortableLightEmission, playerPosition);
            if (menuOverlayMode == 0)
            {
                drawBlockSelection(commandBuffer, camera, cameraPosition, fovRadians);
            }
            if (showFirstPersonHand && menuOverlayMode == 0)
            {
                clearDepthAttachment(commandBuffer, vulkan_.swapchainExtent);
                if (heldItemId == 0)
                {
                    drawFirstPersonHand(commandBuffer, camera, cameraPosition, skyBrightness, heldPortableLightEmission, vulkan_.currentFrame);
                }
                if (heldItemId != 0 || offhandItemId != 0)
                {
                    drawHeldItem(commandBuffer, camera, cameraPosition, heldItemStack, offhandItemStack, skyBrightness, heldPortableLightEmission, playerPackedLight);
                }
            }
        }
        vkCmdEndRenderPass(commandBuffer);

        if (gameSceneRenderEnabled && waterOverlay.active)
        {
            drawWaterBlurTargets(commandBuffer, imageIndex, waterOverlay);
        }
        if (gameSceneRenderEnabled && client_.renderConfig.bloomEnabled)
        {
            drawBloomTargets(commandBuffer, imageIndex);
        }

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = vulkan_.renderPass;
        renderPassInfo.framebuffer = vulkan_.framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = vulkan_.swapchainExtent;
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport presentationViewport{};
        presentationViewport.x = 0.0f;
        presentationViewport.y = 0.0f;
        presentationViewport.width = static_cast<float>(vulkan_.swapchainExtent.width);
        presentationViewport.height = static_cast<float>(vulkan_.swapchainExtent.height);
        presentationViewport.minDepth = 0.0f;
        presentationViewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &presentationViewport);

        VkRect2D presentationScissor{};
        presentationScissor.offset = {0, 0};
        presentationScissor.extent = vulkan_.swapchainExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &presentationScissor);

        if (gameSceneRenderEnabled)
        {
            screenPresentation_.drawSceneComposite(
                commandBuffer,
                sceneColorTargets_[imageIndex],
                vulkan_.swapchainExtent,
                rendererAssets_,
                spriteRenderPath_,
                vulkan_.pipeline,
                vulkan_.additiveSpritePipeline,
                vulkan_.pipelineLayout,
                textRenderPath_.vertexBuffer(),
                ScreenPresentation::BloomOverlay{
                    client_.renderConfig.bloomEnabled && imageIndex < bloomTargets_[0].size(),
                    client_.renderConfig.bloomIntensity
                },
                (client_.renderConfig.bloomEnabled && imageIndex < bloomTargets_[0].size()) ? bloomTargets_[0][imageIndex] : sceneColorTargets_[imageIndex],
                waterOverlay,
                (waterOverlay.active && imageIndex < waterBlurTargetsB_.size()) ? waterBlurTargetsB_[imageIndex] : sceneColorTargets_[imageIndex],
                climateOverlayMode);
        }
        else
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.pipeline);
        }

        if (gameSceneRenderEnabled && menuOverlayMode == 0 && hudVisible)
        {
            screenPresentation_.drawCrosshair(
                commandBuffer,
                vulkan_.swapchainExtent,
                rendererAssets_,
                spriteRenderPath_,
                vulkan_.pipelineLayout,
                textRenderPath_.vertexBuffer());
        }

        if (debugTextVisible)
        {
            diagnosticsBridge_->updateDebugTextBatch(fpsText, perfText);
            screenPresentation_.drawDebugText(
                commandBuffer,
                debugOverlayText_.batch(),
                rendererAssets_,
                textRenderPath_,
                vulkan_.swapchainExtent,
                vulkan_.pipelineLayout);
        }
        if (gameSceneRenderEnabled && menuOverlayMode == 0 && radialMenu.visible)
        {
            radialMenuRenderPath_.draw(
                commandBuffer,
                vulkan_.pipelineLayout,
                rendererAssets_.white,
                vulkan_.swapchainExtent,
                radialMenu);
        }
        if (!uiRuntimeBridge_->render(commandBuffer, menuOverlayMode, hudVisible))
        {
            screenPresentation_.drawMenuOverlay(
                commandBuffer,
                menuOverlayMode,
                rendererAssets_,
                spriteRenderPath_,
                textRenderPath_,
                vulkan_.swapchainExtent,
                vulkan_.pipelineLayout,
                textRenderPath_.vertexBuffer());
        }

        vkCmdEndRenderPass(commandBuffer);

        if (screenshotBuffer != VK_NULL_HANDLE)
        {
            copySwapchainImageToBuffer(commandBuffer, imageIndex, screenshotBuffer);
        }
        if (vulkan_.timestampSupported && vulkan_.timestampQueryPool != VK_NULL_HANDLE)
        {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vulkan_.timestampQueryPool, vulkan_.currentFrame * 2 + 1);
        }

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to record command buffer.");
        }
    }

    void Renderer::drawWaterBlurTargets(VkCommandBuffer commandBuffer, uint32_t imageIndex, const ScreenPresentation::WaterOverlay& waterOverlay)
    {
        if (imageIndex >= waterBlurTargetsA_.size() ||
            imageIndex >= waterBlurTargetsB_.size() ||
            imageIndex >= vulkan_.waterBlurFramebuffersA.size() ||
            imageIndex >= vulkan_.waterBlurFramebuffersB.size())
        {
            return;
        }

        auto drawBlurPass = [this, commandBuffer](const Texture& source, VkFramebuffer framebuffer, const Texture& target, float offsetScale)
        {
            VkClearValue clearColor{};
            clearColor.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

            VkRenderPassBeginInfo passInfo{};
            passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            passInfo.renderPass = vulkan_.waterBlurRenderPass;
            passInfo.framebuffer = framebuffer;
            passInfo.renderArea.offset = {0, 0};
            passInfo.renderArea.extent = {static_cast<uint32_t>(target.width), static_cast<uint32_t>(target.height)};
            passInfo.clearValueCount = 1;
            passInfo.pClearValues = &clearColor;

            vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(target.width);
            viewport.height = static_cast<float>(target.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = {static_cast<uint32_t>(target.width), static_cast<uint32_t>(target.height)};
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

            SpriteRenderPath::Push push{};
            push.data[2] = 1.0f;
            push.data[3] = 1.0f;
            push.data[4] = 0.0f;
            push.data[5] = 1.0f;
            push.data[6] = 1.0f;
            push.data[7] = -1.0f;
            push.data[8] = 1.0f / std::max(static_cast<float>(source.width), 1.0f);
            push.data[9] = 1.0f / std::max(static_cast<float>(source.height), 1.0f);
            push.data[10] = offsetScale;

            const VkDeviceSize vertexOffset = 0;
            VkBuffer vertexBuffer = textRenderPath_.vertexBuffer();
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.waterBlurPipeline);
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.waterBlurPipelineLayout, 0, 1, &source.descriptorSet, 0, nullptr);
            vkCmdPushConstants(commandBuffer, vulkan_.waterBlurPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SpriteRenderPath::Push), &push);
            vkCmdDraw(commandBuffer, 6, 1, 0, 0);

            vkCmdEndRenderPass(commandBuffer);
        };

        const float spread = std::max(waterOverlay.blurSpread, 0.0f);
        drawBlurPass(sceneColorTargets_[imageIndex], vulkan_.waterBlurFramebuffersA[imageIndex], waterBlurTargetsA_[imageIndex], 1.5f * spread);
        drawBlurPass(waterBlurTargetsA_[imageIndex], vulkan_.waterBlurFramebuffersB[imageIndex], waterBlurTargetsB_[imageIndex], 2.5f * spread);
    }

    void Renderer::drawBloomTargets(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        for (size_t mip = 0; mip < bloomTargets_.size(); ++mip)
        {
            if (imageIndex >= bloomTargets_[mip].size() ||
                imageIndex >= vulkan_.bloomFramebuffers[mip].size())
            {
                return;
            }
        }

        if (vulkan_.bloomDownsamplePipeline == VK_NULL_HANDLE ||
            vulkan_.bloomUpsamplePipeline == VK_NULL_HANDLE)
        {
            return;
        }

        auto beginPostPass = [this, commandBuffer](VkRenderPass renderPass, VkFramebuffer framebuffer, const Texture& target)
        {
            VkClearValue clearColor{};
            clearColor.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

            VkRenderPassBeginInfo passInfo{};
            passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            passInfo.renderPass = renderPass;
            passInfo.framebuffer = framebuffer;
            passInfo.renderArea.offset = {0, 0};
            passInfo.renderArea.extent = {static_cast<uint32_t>(target.width), static_cast<uint32_t>(target.height)};
            passInfo.clearValueCount = 1;
            passInfo.pClearValues = &clearColor;

            vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(target.width);
            viewport.height = static_cast<float>(target.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = {static_cast<uint32_t>(target.width), static_cast<uint32_t>(target.height)};
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        };

        auto fullscreenPush = [](const Texture& source, float radius)
        {
            SpriteRenderPath::Push push{};
            push.data[2] = 1.0f;
            push.data[3] = 1.0f;
            push.data[4] = 0.0f;
            push.data[5] = 1.0f;
            push.data[6] = 1.0f;
            push.data[7] = -1.0f;
            push.data[8] = 1.0f / std::max(static_cast<float>(source.width), 1.0f);
            push.data[9] = 1.0f / std::max(static_cast<float>(source.height), 1.0f);
            push.data[10] = radius;
            return push;
        };

        auto bindFullscreenSource = [this, commandBuffer](VkPipeline pipeline, const Texture& source, const SpriteRenderPath::Push& push)
        {
            const VkDeviceSize vertexOffset = 0;
            VkBuffer vertexBuffer = textRenderPath_.vertexBuffer();
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.waterBlurPipelineLayout, 0, 1, &source.descriptorSet, 0, nullptr);
            vkCmdPushConstants(commandBuffer, vulkan_.waterBlurPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SpriteRenderPath::Push), &push);
            vkCmdDraw(commandBuffer, 6, 1, 0, 0);
        };

        const float radius = std::max(client_.renderConfig.bloomRadius, 0.0f);
        const float downsampleRadius = std::max(radius, 0.5f);
        const float upsampleRadius = std::max(radius * 0.75f, 0.5f);

        if (imageIndex >= bloomSourceTargets_.size())
        {
            return;
        }

        const Texture* source = &bloomSourceTargets_[imageIndex];
        for (size_t mip = 0; mip < bloomTargets_.size(); ++mip)
        {
            Texture& target = bloomTargets_[mip][imageIndex];
            beginPostPass(vulkan_.waterBlurRenderPass, vulkan_.bloomFramebuffers[mip][imageIndex], target);

            SpriteRenderPath::Push push = fullscreenPush(*source, downsampleRadius);
            push.data[11] = -1.0f;
            bindFullscreenSource(vulkan_.bloomDownsamplePipeline, *source, push);
            vkCmdEndRenderPass(commandBuffer);

            source = &target;
        }

        for (size_t mip = bloomTargets_.size() - 1; mip > 0; --mip)
        {
            const Texture& smaller = bloomTargets_[mip][imageIndex];
            Texture& larger = bloomTargets_[mip - 1][imageIndex];
            beginPostPass(vulkan_.postProcessLoadRenderPass, vulkan_.bloomFramebuffers[mip - 1][imageIndex], larger);

            SpriteRenderPath::Push push = fullscreenPush(smaller, upsampleRadius);
            bindFullscreenSource(vulkan_.bloomUpsamplePipeline, smaller, push);
            vkCmdEndRenderPass(commandBuffer);
        }
    }

    void Renderer::copySwapchainImageToBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, VkBuffer buffer) const
    {
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = vulkan_.swapchainImages[imageIndex];
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toTransfer);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height, 1};
        vkCmdCopyImageToBuffer(commandBuffer, vulkan_.swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);

        VkImageMemoryBarrier toPresent{};
        toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = vulkan_.swapchainImages[imageIndex];
        toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toPresent.subresourceRange.levelCount = 1;
        toPresent.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toPresent);
    }

    void Renderer::saveScreenshot(VkDeviceMemory memory, VkDeviceSize size) const
    {
        void* data = nullptr;
        vkMapMemory(vulkan_.device, memory, 0, size, 0, &data);
        writeBmp(screenshotPath(), static_cast<const unsigned char*>(data), vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height, vulkan_.swapchainImageFormat);
        vkUnmapMemory(vulkan_.device, memory);
    }
}
