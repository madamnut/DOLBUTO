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
        VkExtent2D swapchainExtent{};
        VkImage depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView depthImageView = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkRenderPass sceneRenderPass = VK_NULL_HANDLE;

        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout terrainVertexDescriptorSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout uiPipelineLayout = VK_NULL_HANDLE;
        VkPipeline uiPipeline = VK_NULL_HANDLE;
        VkPipelineLayout terrainPipelineLayout = VK_NULL_HANDLE;
        VkPipeline terrainPipeline = VK_NULL_HANDLE;
        VkPipeline terrainWireframePipeline = VK_NULL_HANDLE;
        VkPipeline fluidPipeline = VK_NULL_HANDLE;
        VkPipeline playerPipeline = VK_NULL_HANDLE;
        VkPipelineLayout particlePipelineLayout = VK_NULL_HANDLE;
        VkPipeline particlePipeline = VK_NULL_HANDLE;
        VkPipeline itemPipeline = VK_NULL_HANDLE;
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
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
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

        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> inFlightFences;
        uint32_t currentFrame = 0;
        bool framebufferResized = false;
    };
}
