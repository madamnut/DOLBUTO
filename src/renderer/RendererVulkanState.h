#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dolbuto
{
    struct RendererVulkanState
    {
        static constexpr size_t BloomMipCount = 4;
        static constexpr size_t FrameInFlightCount = 2;
        static constexpr size_t ShadowCascadeCount = 1;
        static constexpr size_t ShadowHistoryCount = FrameInFlightCount;
        static constexpr uint32_t ShadowMapSize = 2048;

        VkInstance instance = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;

        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;
        std::vector<VkFramebuffer> framebuffers;
        VkFormat swapchainImageFormat = VK_FORMAT_UNDEFINED;
        VkFormat sceneColorFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D swapchainExtent{};
        VkImage depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView depthImageView = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkRenderPass sceneRenderPass = VK_NULL_HANDLE;
        VkRenderPass sceneLoadRenderPass = VK_NULL_HANDLE;
        VkRenderPass waterBlurRenderPass = VK_NULL_HANDLE;
        VkRenderPass postProcessLoadRenderPass = VK_NULL_HANDLE;
        VkRenderPass shadowRenderPass = VK_NULL_HANDLE;

        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout terrainVertexDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout shadowDescriptorSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout skyPipelineLayout = VK_NULL_HANDLE;
        VkPipeline skyPipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipeline sceneSpritePipeline = VK_NULL_HANDLE;
        VkPipeline additiveSpritePipeline = VK_NULL_HANDLE;
        VkPipelineLayout waterBlurPipelineLayout = VK_NULL_HANDLE;
        VkPipeline waterBlurPipeline = VK_NULL_HANDLE;
        VkPipeline bloomDownsamplePipeline = VK_NULL_HANDLE;
        VkPipeline bloomUpsamplePipeline = VK_NULL_HANDLE;
        VkPipelineLayout uiPipelineLayout = VK_NULL_HANDLE;
        VkPipeline uiPipeline = VK_NULL_HANDLE;
        VkPipelineLayout terrainPipelineLayout = VK_NULL_HANDLE;
        VkPipeline terrainPipeline = VK_NULL_HANDLE;
        VkPipeline terrainFadePipeline = VK_NULL_HANDLE;
        VkPipeline terrainBlendPipeline = VK_NULL_HANDLE;
        VkPipeline terrainWireframePipeline = VK_NULL_HANDLE;
        VkPipeline fluidPipeline = VK_NULL_HANDLE;
        VkPipeline playerPipeline = VK_NULL_HANDLE;
        VkPipeline playerViewmodelPipeline = VK_NULL_HANDLE;
        VkPipeline terrainShadowPipeline = VK_NULL_HANDLE;
        VkPipeline playerShadowPipeline = VK_NULL_HANDLE;
        VkPipelineLayout particlePipelineLayout = VK_NULL_HANDLE;
        VkPipeline particlePipeline = VK_NULL_HANDLE;
        VkPipeline itemPipeline = VK_NULL_HANDLE;
        VkPipeline itemViewmodelPipeline = VK_NULL_HANDLE;
        VkPipeline crucibleMoltenPipeline = VK_NULL_HANDLE;
        VkPipelineLayout selectionPipelineLayout = VK_NULL_HANDLE;
        VkPipeline selectionPipeline = VK_NULL_HANDLE;

        VkCommandPool commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers;
        VkQueryPool timestampQueryPool = VK_NULL_HANDLE;
        std::array<bool, 2> timestampQueryReady{};
        bool timestampSupported = false;
        float timestampPeriod = 0.0f;
        double lastGpuFrameMs = 0.0;

        VkSampler sampler = VK_NULL_HANDLE;
        VkSampler linearSampler = VK_NULL_HANDLE;
        VkSampler shadowSampler = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::array<VkImage, ShadowHistoryCount> shadowImages{};
        std::array<VkDeviceMemory, ShadowHistoryCount> shadowMemories{};
        std::array<VkImageView, ShadowHistoryCount> shadowImageViews{};
        std::array<std::array<VkImageView, ShadowCascadeCount>, ShadowHistoryCount> shadowLayerViews{};
        std::array<std::array<VkFramebuffer, ShadowCascadeCount>, ShadowHistoryCount> shadowFramebuffers{};
        std::array<VkBuffer, FrameInFlightCount> shadowUniformBuffers{};
        std::array<VkDeviceMemory, FrameInFlightCount> shadowUniformMemories{};
        std::array<VkDescriptorSet, FrameInFlightCount> shadowDescriptorSets{};
        VkBuffer uiVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory uiVertexMemory = VK_NULL_HANDLE;
        VkBuffer uiIndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory uiIndexMemory = VK_NULL_HANDLE;
        VkBuffer selectionLineVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory selectionLineVertexMemory = VK_NULL_HANDLE;

        VkCommandBuffer rmlCommandBuffer = VK_NULL_HANDLE;
        bool rmlScissorEnabled = false;
        VkRect2D rmlScissor{};
        size_t rmlUiVertexOffset = 0;
        size_t rmlUiIndexOffset = 0;
        std::vector<VkFramebuffer> sceneFramebuffers;
        std::vector<VkFramebuffer> waterBlurFramebuffersA;
        std::vector<VkFramebuffer> waterBlurFramebuffersB;
        std::array<std::vector<VkFramebuffer>, BloomMipCount> bloomFramebuffers;

        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> inFlightFences;
        uint32_t currentFrame = 0;
        bool framebufferResized = false;
    };
}
