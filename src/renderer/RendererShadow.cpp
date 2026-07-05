#include "renderer/Renderer.h"

#include "renderer/CelestialDirections.h"
#include "renderer/RendererAssetStore.h"

#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace dolbuto
{
    namespace
    {
        constexpr VkFormat ShadowDepthFormat = VK_FORMAT_D32_SFLOAT;
        constexpr uint64_t ShadowSunTickStep = 5;
        constexpr float ShadowDistance = 192.0f;
        constexpr float ShadowReceiverMargin = 16.0f;
        constexpr float ShadowCasterDepthPadding = 256.0f;

        static_assert(RendererVulkanState::ShadowHistoryCount == RendererVulkanState::FrameInFlightCount);

        uint32_t shadowHistoryIndexForFrame(uint32_t frameIndex)
        {
            return frameIndex;
        }

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

        Mat4 orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane)
        {
            Mat4 matrix = identity();
            matrix.m[0] = 2.0f / (right - left);
            matrix.m[5] = 2.0f / (bottom - top);
            matrix.m[10] = 1.0f / (farPlane - nearPlane);
            matrix.m[12] = -(right + left) / (right - left);
            matrix.m[13] = -(bottom + top) / (bottom - top);
            matrix.m[14] = -nearPlane / (farPlane - nearPlane);
            return matrix;
        }

        Mat4 lightViewMatrix(Vec3 right, Vec3 up, Vec3 forward)
        {
            Mat4 matrix = identity();
            matrix.m[0] = right.x;
            matrix.m[4] = right.y;
            matrix.m[8] = right.z;
            matrix.m[1] = up.x;
            matrix.m[5] = up.y;
            matrix.m[9] = up.z;
            matrix.m[2] = forward.x;
            matrix.m[6] = forward.y;
            matrix.m[10] = forward.z;
            return matrix;
        }

        Vec3 transformPoint(const Mat4& matrix, Vec3 point)
        {
            return {
                matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12],
                matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13],
                matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14]
            };
        }

        uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
        {
            VkPhysicalDeviceMemoryProperties memoryProperties{};
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
            for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
            {
                if ((typeFilter & (1u << i)) != 0u && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
                {
                    return i;
                }
            }
            throw std::runtime_error("Failed to find suitable memory type.");
        }

        void transitionShadowImageToShaderRead(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkImage image)
        {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = commandPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to allocate shadow transition command buffer.");
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(commandBuffer, &beginInfo);

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = static_cast<uint32_t>(RendererVulkanState::ShadowCascadeCount);
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);

            vkEndCommandBuffer(commandBuffer);
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;
            vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(queue);
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        }
    }

    void Renderer::createShadowRenderPass()
    {
        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = ShadowDepthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 0;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &depthAttachment;
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        createInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(vulkan_.device, &createInfo, nullptr, &vulkan_.shadowRenderPass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create shadow render pass.");
        }
    }

    void Renderer::createShadowResources()
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.maxLod = 0.0f;
        if (vkCreateSampler(vulkan_.device, &samplerInfo, nullptr, &vulkan_.shadowSampler) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create shadow sampler.");
        }

        for (size_t history = 0; history < RendererVulkanState::ShadowHistoryCount; ++history)
        {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent = {RendererVulkanState::ShadowMapSize, RendererVulkanState::ShadowMapSize, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = static_cast<uint32_t>(RendererVulkanState::ShadowCascadeCount);
            imageInfo.format = ShadowDepthFormat;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateImage(vulkan_.device, &imageInfo, nullptr, &vulkan_.shadowImages[history]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create shadow image.");
            }

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(vulkan_.device, vulkan_.shadowImages[history], &requirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = requirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(vulkan_.physicalDevice, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(vulkan_.device, &allocInfo, nullptr, &vulkan_.shadowMemories[history]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to allocate shadow image memory.");
            }
            vkBindImageMemory(vulkan_.device, vulkan_.shadowImages[history], vulkan_.shadowMemories[history], 0);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = vulkan_.shadowImages[history];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            viewInfo.format = ShadowDepthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = static_cast<uint32_t>(RendererVulkanState::ShadowCascadeCount);
            if (vkCreateImageView(vulkan_.device, &viewInfo, nullptr, &vulkan_.shadowImageViews[history]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create shadow image view.");
            }

            for (uint32_t cascade = 0; cascade < RendererVulkanState::ShadowCascadeCount; ++cascade)
            {
                VkImageViewCreateInfo layerViewInfo = viewInfo;
                layerViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                layerViewInfo.subresourceRange.baseArrayLayer = cascade;
                layerViewInfo.subresourceRange.layerCount = 1;
                if (vkCreateImageView(vulkan_.device, &layerViewInfo, nullptr, &vulkan_.shadowLayerViews[history][cascade]) != VK_SUCCESS)
                {
                    throw std::runtime_error("Failed to create shadow cascade image view.");
                }

                VkFramebufferCreateInfo framebufferInfo{};
                framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferInfo.renderPass = vulkan_.shadowRenderPass;
                framebufferInfo.attachmentCount = 1;
                framebufferInfo.pAttachments = &vulkan_.shadowLayerViews[history][cascade];
                framebufferInfo.width = RendererVulkanState::ShadowMapSize;
                framebufferInfo.height = RendererVulkanState::ShadowMapSize;
                framebufferInfo.layers = 1;
                if (vkCreateFramebuffer(vulkan_.device, &framebufferInfo, nullptr, &vulkan_.shadowFramebuffers[history][cascade]) != VK_SUCCESS)
                {
                    throw std::runtime_error("Failed to create shadow framebuffer.");
                }
            }

            transitionShadowImageToShaderRead(vulkan_.device, vulkan_.commandPool, vulkan_.graphicsQueue, vulkan_.shadowImages[history]);
        }

        for (size_t frame = 0; frame < RendererVulkanState::FrameInFlightCount; ++frame)
        {
            gpuResources_.createBuffer(
                sizeof(ShadowUniformData),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                vulkan_.shadowUniformBuffers[frame],
                vulkan_.shadowUniformMemories[frame]);

            VkDescriptorSetAllocateInfo setInfo{};
            setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            setInfo.descriptorPool = vulkan_.descriptorPool;
            setInfo.descriptorSetCount = 1;
            setInfo.pSetLayouts = &vulkan_.shadowDescriptorSetLayout;
            if (vkAllocateDescriptorSets(vulkan_.device, &setInfo, &vulkan_.shadowDescriptorSets[frame]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to allocate shadow descriptor set.");
            }

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = vulkan_.shadowUniformBuffers[frame];
            bufferInfo.range = sizeof(ShadowUniformData);

            VkDescriptorImageInfo imageDescriptor{};
            imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageDescriptor.imageView = vulkan_.shadowImageViews[shadowHistoryIndexForFrame(static_cast<uint32_t>(frame))];
            imageDescriptor.sampler = vulkan_.shadowSampler;

            VkDescriptorImageInfo previousImageDescriptor{};
            previousImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            previousImageDescriptor.imageView = vulkan_.shadowImageViews[shadowHistoryIndexForFrame(static_cast<uint32_t>(frame))];
            previousImageDescriptor.sampler = vulkan_.shadowSampler;

            std::array<VkWriteDescriptorSet, 3> writes{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = vulkan_.shadowDescriptorSets[frame];
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].pBufferInfo = &bufferInfo;
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = vulkan_.shadowDescriptorSets[frame];
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].pImageInfo = &imageDescriptor;
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = vulkan_.shadowDescriptorSets[frame];
            writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[2].pImageInfo = &previousImageDescriptor;
            vkUpdateDescriptorSets(vulkan_.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    void Renderer::destroyShadowResources()
    {
        for (VkBuffer& buffer : vulkan_.shadowUniformBuffers)
        {
            if (buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(vulkan_.device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
            }
        }
        for (VkDeviceMemory& memory : vulkan_.shadowUniformMemories)
        {
            if (memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(vulkan_.device, memory, nullptr);
                memory = VK_NULL_HANDLE;
            }
        }
        for (auto& framebuffers : vulkan_.shadowFramebuffers)
        {
            for (VkFramebuffer& framebuffer : framebuffers)
            {
                if (framebuffer != VK_NULL_HANDLE)
                {
                    vkDestroyFramebuffer(vulkan_.device, framebuffer, nullptr);
                    framebuffer = VK_NULL_HANDLE;
                }
            }
        }
        for (auto& layerViews : vulkan_.shadowLayerViews)
        {
            for (VkImageView& view : layerViews)
            {
                if (view != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(vulkan_.device, view, nullptr);
                    view = VK_NULL_HANDLE;
                }
            }
        }
        for (VkImageView& view : vulkan_.shadowImageViews)
        {
            if (view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(vulkan_.device, view, nullptr);
                view = VK_NULL_HANDLE;
            }
        }
        for (VkImage& image : vulkan_.shadowImages)
        {
            if (image != VK_NULL_HANDLE)
            {
                vkDestroyImage(vulkan_.device, image, nullptr);
                image = VK_NULL_HANDLE;
            }
        }
        for (VkDeviceMemory& memory : vulkan_.shadowMemories)
        {
            if (memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(vulkan_.device, memory, nullptr);
                memory = VK_NULL_HANDLE;
            }
        }
    }

    void Renderer::updateShadowData(const Camera& camera, Vec3 cameraPosition, float fovRadians, uint64_t worldTicks)
    {
        (void)camera;
        (void)fovRadians;

        ShadowUniformData shadowData{};
        shadowData.params[0] = 0.0f;
        shadowData.params[1] = static_cast<float>(RendererVulkanState::ShadowMapSize);
        shadowData.params[2] = 0.00012f;
        shadowData.params[3] = 0.60f;
        shadowData.cascadeSplits[0] = ShadowDistance;

        const uint64_t shadowWorldTicks = (worldTicks / ShadowSunTickStep) * ShadowSunTickStep;
        const Vec3 sunPositionDirection = celestial::sunPositionDirection(shadowWorldTicks);
        shadowData.sunPositionDirection[0] = sunPositionDirection.x;
        shadowData.sunPositionDirection[1] = sunPositionDirection.y;
        shadowData.sunPositionDirection[2] = sunPositionDirection.z;
        shadowData.sunPositionDirection[3] = 0.0f;
        const uint32_t frameResourceIndex = vulkan_.currentFrame;
        if (frameResourceIndex >= vulkan_.shadowUniformMemories.size() ||
            vulkan_.shadowUniformMemories[frameResourceIndex] == VK_NULL_HANDLE)
        {
            return;
        }
        if (sunPositionDirection.y < -0.02f)
        {
            void* mapped = nullptr;
            vkMapMemory(vulkan_.device, vulkan_.shadowUniformMemories[frameResourceIndex], 0, sizeof(shadowData), 0, &mapped);
            std::memcpy(mapped, &shadowData, sizeof(shadowData));
            vkUnmapMemory(vulkan_.device, vulkan_.shadowUniformMemories[frameResourceIndex]);
            return;
        }
        const Vec3 lightForward = celestial::sunlightTravelDirection(sunPositionDirection);
        const Vec3 upCandidate = std::abs(lightForward.y) > 0.95f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
        const Vec3 lightRight = normalize(cross(upCandidate, lightForward));
        const Vec3 lightUp = normalize(cross(lightForward, lightRight));
        const Mat4 view = lightViewMatrix(lightRight, lightUp, lightForward);

        const Vec3 center = cameraPosition;
        const float radius = std::ceil((ShadowDistance + ShadowReceiverMargin) * 16.0f) / 16.0f;

        Vec3 lightCenter = transformPoint(view, center);
        const float diameter = radius * 2.0f;
        const float texelSize = diameter / static_cast<float>(RendererVulkanState::ShadowMapSize);
        shadowData.cascadeTexelSizes[0] = texelSize;
        lightCenter.x = std::floor(lightCenter.x / texelSize) * texelSize;
        lightCenter.y = std::floor(lightCenter.y / texelSize) * texelSize;

        const Mat4 projection = orthographic(
            lightCenter.x - radius,
            lightCenter.x + radius,
            lightCenter.y - radius,
            lightCenter.y + radius,
            lightCenter.z - radius - ShadowCasterDepthPadding,
            lightCenter.z + radius + ShadowCasterDepthPadding);
        const Mat4 lightViewProjection = multiply(projection, view);
        std::memcpy(shadowData.lightViewProjection[0], lightViewProjection.m, sizeof(lightViewProjection.m));
        shadowData.params[0] = 1.0f;
        std::memcpy(shadowData.previousLightViewProjection, shadowData.lightViewProjection, sizeof(shadowData.previousLightViewProjection));
        std::memcpy(shadowData.previousCascadeTexelSizes, shadowData.cascadeTexelSizes, sizeof(shadowData.previousCascadeTexelSizes));
        shadowData.historyParams[0] = 1.0f;
        shadowData.historyParams[1] = 0.0f;

        if (vulkan_.shadowDescriptorSets[frameResourceIndex] != VK_NULL_HANDLE)
        {
            VkDescriptorImageInfo currentImageDescriptor{};
            currentImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            const uint32_t currentImageIndex = shadowHistoryIndexForFrame(frameResourceIndex);
            const uint32_t previousImageIndex = currentImageIndex;

            currentImageDescriptor.imageView = vulkan_.shadowImageViews[currentImageIndex];
            currentImageDescriptor.sampler = vulkan_.shadowSampler;

            VkDescriptorImageInfo previousImageDescriptor{};
            previousImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            previousImageDescriptor.imageView = vulkan_.shadowImageViews[previousImageIndex];
            previousImageDescriptor.sampler = vulkan_.shadowSampler;

            std::array<VkWriteDescriptorSet, 2> imageWrites{};
            imageWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            imageWrites[0].dstSet = vulkan_.shadowDescriptorSets[frameResourceIndex];
            imageWrites[0].dstBinding = 1;
            imageWrites[0].descriptorCount = 1;
            imageWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            imageWrites[0].pImageInfo = &currentImageDescriptor;
            imageWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            imageWrites[1].dstSet = vulkan_.shadowDescriptorSets[frameResourceIndex];
            imageWrites[1].dstBinding = 2;
            imageWrites[1].descriptorCount = 1;
            imageWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            imageWrites[1].pImageInfo = &previousImageDescriptor;
            vkUpdateDescriptorSets(vulkan_.device, static_cast<uint32_t>(imageWrites.size()), imageWrites.data(), 0, nullptr);
        }

        void* mapped = nullptr;
        vkMapMemory(vulkan_.device, vulkan_.shadowUniformMemories[frameResourceIndex], 0, sizeof(shadowData), 0, &mapped);
        std::memcpy(mapped, &shadowData, sizeof(shadowData));
        vkUnmapMemory(vulkan_.device, vulkan_.shadowUniformMemories[frameResourceIndex]);
    }

    void Renderer::drawShadowPass(VkCommandBuffer commandBuffer, bool drawPlayer, uint32_t frameIndex)
    {
        if (vulkan_.shadowRenderPass == VK_NULL_HANDLE || vulkan_.terrainShadowPipeline == VK_NULL_HANDLE ||
            frameIndex >= vulkan_.shadowDescriptorSets.size() ||
            vulkan_.shadowDescriptorSets[frameIndex] == VK_NULL_HANDLE)
        {
            return;
        }

        ShadowUniformData shadowData{};
        if (frameIndex >= vulkan_.shadowUniformMemories.size() ||
            vulkan_.shadowUniformMemories[frameIndex] == VK_NULL_HANDLE)
        {
            return;
        }
        void* mapped = nullptr;
        vkMapMemory(vulkan_.device, vulkan_.shadowUniformMemories[frameIndex], 0, sizeof(shadowData), 0, &mapped);
        std::memcpy(&shadowData, mapped, sizeof(shadowData));
        vkUnmapMemory(vulkan_.device, vulkan_.shadowUniformMemories[frameIndex]);
        if (shadowData.params[0] <= 0.5f)
        {
            return;
        }

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(RendererVulkanState::ShadowMapSize);
        viewport.height = static_cast<float>(RendererVulkanState::ShadowMapSize);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {RendererVulkanState::ShadowMapSize, RendererVulkanState::ShadowMapSize};

        for (uint32_t cascade = 0; cascade < RendererVulkanState::ShadowCascadeCount; ++cascade)
        {
            VkClearValue clearDepth{};
            clearDepth.depthStencil = {1.0f, 0};
            VkRenderPassBeginInfo passInfo{};
            passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            passInfo.renderPass = vulkan_.shadowRenderPass;
            passInfo.framebuffer = vulkan_.shadowFramebuffers[shadowHistoryIndexForFrame(frameIndex)][cascade];
            passInfo.renderArea.offset = {0, 0};
            passInfo.renderArea.extent = scissor.extent;
            passInfo.clearValueCount = 1;
            passInfo.pClearValues = &clearDepth;

            vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

            TerrainPush push{};
            std::memcpy(push.mvp, shadowData.lightViewProjection[cascade], sizeof(push.mvp));
            push.cameraPosition[3] = static_cast<float>(glfwGetTime());
            push.dynamicLightParams[1] = 1.0f;

            vkCmdPushConstants(commandBuffer, vulkan_.terrainPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.terrainPipelineLayout, 0, 1, &rendererAssets_.terrainTextureArray.descriptorSet, 0, nullptr);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.terrainShadowPipeline);
            terrainRenderPath_.drawShadow(commandBuffer, vulkan_.terrainPipelineLayout);

            if (drawPlayer && vulkan_.playerShadowPipeline != VK_NULL_HANDLE)
            {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.playerShadowPipeline);
                playerMeshRenderPath_.draw(commandBuffer, vulkan_.terrainPipelineLayout, rendererAssets_.playerTexture, frameIndex);
            }

            vkCmdEndRenderPass(commandBuffer);
        }
    }
}
