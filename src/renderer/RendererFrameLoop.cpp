#include "renderer/Renderer.h"

#include "platform/RuntimePaths.h"
#include "renderer/RendererAudioBridge.h"
#include "renderer/RendererDiagnosticsBridge.h"
#include "renderer/RendererTerrainRuntimeBridge.h"
#include "renderer/RendererUiRuntimeBridge.h"

#include <array>
#include <chrono>
#include <cstring>
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

        Vec3 toVec3(DVec3 value)
        {
            return {
                static_cast<float>(value.x),
                static_cast<float>(value.y),
                static_cast<float>(value.z)
            };
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
    }

    void Renderer::drawFrame(const RendererFrame& frame)
    {
        const Vec3 cameraPositionFloat = toVec3(frame.cameraPosition);
        const Vec3 playerPositionFloat = toVec3(frame.playerPosition);

        audioBridge_->updateListener(frame.camera, cameraPositionFloat);
        audioBridge_->updateMusicPlayback(frame.menuOverlayMode, frame.gameSceneRenderEnabled);
        diagnosticsBridge_->updateTerrainDebugText();

        vkWaitForFences(vulkan_.device, 1, &vulkan_.inFlightFences[vulkan_.currentFrame], VK_TRUE, UINT64_MAX);
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
        VkResult result = vkAcquireNextImageKHR(vulkan_.device, vulkan_.swapchain, UINT64_MAX, vulkan_.imageAvailableSemaphores[vulkan_.currentFrame], VK_NULL_HANDLE, &imageIndex);
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
            updatePlayerMesh(playerPositionFloat, frame.playerYaw);
        }
        ensureClimateOverlayTexture(frame.climateOverlayMode);

        recordCommandBuffer(
            vulkan_.commandBuffers[vulkan_.currentFrame],
            imageIndex,
            frame.camera,
            cameraPositionFloat,
            playerPositionFloat,
            frame.fpsText,
            frame.debugTextVisible,
            screenshotBuffer,
            frame.showPlayer,
            frame.terrainWireframe,
            frame.climateOverlayMode,
            frame.menuOverlayMode,
            frame.hudVisible,
            frame.gameSceneRenderEnabled,
            frame.worldTicks);

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

        if (vkQueueSubmit(vulkan_.graphicsQueue, 1, &submitInfo, vulkan_.inFlightFences[vulkan_.currentFrame]) != VK_SUCCESS)
        {
            if (screenshotBuffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(vulkan_.device, screenshotBuffer, nullptr);
                vkFreeMemory(vulkan_.device, screenshotMemory, nullptr);
            }
            throw std::runtime_error("Failed to submit draw command buffer.");
        }
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

        result = vkQueuePresentKHR(vulkan_.presentQueue, &presentInfo);
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
        diagnosticsBridge_->updatePerformanceText(std::chrono::duration<double, std::milli>(cpuEnd - cpuStart).count());
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

    void Renderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera, Vec3 cameraPosition, Vec3 playerPosition, std::string_view fpsText, bool debugTextVisible, VkBuffer screenshotBuffer, bool showPlayer, bool terrainWireframe, int climateOverlayMode, int menuOverlayMode, bool hudVisible, bool gameSceneRenderEnabled, uint64_t worldTicks)
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
        clearColor.color = {{0.45f, 0.68f, 0.95f, 1.0f}};
        VkClearValue clearDepth{};
        clearDepth.depthStencil = {1.0f, 0};
        std::array<VkClearValue, 2> clearValues = {clearColor, clearDepth};

        VkRenderPassBeginInfo scenePassInfo{};
        scenePassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        scenePassInfo.renderPass = vulkan_.sceneRenderPass;
        scenePassInfo.framebuffer = vulkan_.sceneFramebuffers[imageIndex];
        scenePassInfo.renderArea.offset = {0, 0};
        scenePassInfo.renderArea.extent = vulkan_.swapchainExtent;
        scenePassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        scenePassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &scenePassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.pipeline);

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
            screenPresentation_.drawSkySprites(
                commandBuffer,
                camera,
                vulkan_.swapchainExtent,
                worldTicks,
                rendererAssets_,
                spriteRenderPath_,
                vulkan_.pipelineLayout,
                textRenderPath_.vertexBuffer());

            drawTerrain(commandBuffer, camera, cameraPosition, terrainWireframe, true, false, imageIndex);
            drawTerrain(commandBuffer, camera, cameraPosition, terrainWireframe, false, true, imageIndex);
            if (showPlayer && menuOverlayMode == 0)
            {
                drawPlayer(commandBuffer, camera, cameraPosition);
            }
            drawBlockBreakParticles(commandBuffer, camera, cameraPosition);
            drawDroppedItems(commandBuffer, camera, cameraPosition, playerPosition);
            if (menuOverlayMode == 0)
            {
                drawBlockSelection(commandBuffer, camera, cameraPosition);
            }
        }
        vkCmdEndRenderPass(commandBuffer);

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = vulkan_.renderPass;
        renderPassInfo.framebuffer = vulkan_.framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = vulkan_.swapchainExtent;
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.pipeline);
        if (gameSceneRenderEnabled)
        {
            screenPresentation_.drawSceneComposite(
                commandBuffer,
                sceneColorTargets_[imageIndex],
                vulkan_.swapchainExtent,
                rendererAssets_,
                spriteRenderPath_,
                vulkan_.pipelineLayout,
                textRenderPath_.vertexBuffer(),
                climateOverlayMode);
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
            diagnosticsBridge_->updateDebugTextBatch(fpsText);
            screenPresentation_.drawDebugText(
                commandBuffer,
                debugOverlayText_.batch(),
                rendererAssets_,
                textRenderPath_,
                vulkan_.swapchainExtent,
                vulkan_.pipelineLayout);
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
