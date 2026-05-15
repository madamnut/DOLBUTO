#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace dolbuto
{
    struct Texture
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        int width = 0;
        int height = 0;
        uint32_t mipLevels = 1;
        uint32_t layers = 1;
    };

    struct BufferUploadRegion
    {
        const void* source = nullptr;
        VkDeviceSize size = 0;
        VkDeviceSize destinationOffset = 0;
    };

    class VulkanResourceManager
    {
    public:
        VulkanResourceManager() = default;
        VulkanResourceManager(
            const VkPhysicalDevice* physicalDevice,
            const VkDevice* device,
            const VkQueue* graphicsQueue,
            const VkCommandPool* commandPool,
            const VkDescriptorPool* descriptorPool,
            const VkDescriptorSetLayout* textureDescriptorSetLayout,
            const VkSampler* sampler);

        void setHandles(
            const VkPhysicalDevice* physicalDevice,
            const VkDevice* device,
            const VkQueue* graphicsQueue,
            const VkCommandPool* commandPool,
            const VkDescriptorPool* descriptorPool,
            const VkDescriptorSetLayout* textureDescriptorSetLayout,
            const VkSampler* sampler);

        Texture createTexture(const std::string& path, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB) const;
        Texture createTextureFromRgba(const unsigned char* pixels, int width, int height, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB) const;
        Texture createTextureArray(const std::vector<std::string>& paths, float alphaMultiplier = 1.0f) const;
        Texture createRenderTargetTexture(
            VkExtent2D extent,
            VkFormat format,
            VkImageUsageFlags usage,
            VkImageAspectFlags aspectMask,
            VkImageLayout descriptorLayout) const;
        void destroyTexture(Texture& texture) const;

        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
        void createBuffer(
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VkMemoryPropertyFlags properties,
            VkBuffer& buffer,
            VkDeviceMemory& memory,
            uint32_t* memoryTypeIndex = nullptr) const;
        void createDeviceLocalBuffer(
            const void* source,
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VkBuffer& buffer,
            VkDeviceMemory& memory,
            uint32_t* memoryTypeIndex = nullptr) const;
        void copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) const;
        void uploadBufferRegions(VkBuffer destination, const std::vector<BufferUploadRegion>& regions) const;
        void uploadBufferData(VkBuffer destination, const void* source, VkDeviceSize size, VkDeviceSize destinationOffset = 0) const;
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount = 1) const;
        void transitionImageLayout(
            VkImage image,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            uint32_t mipLevels = 1,
            uint32_t layerCount = 1) const;
        VkCommandBuffer beginSingleTimeCommands() const;
        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

    private:
        struct TextureMipOverride
        {
            uint32_t layer = 0;
            uint32_t mipLevel = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            VkDeviceSize bufferOffset = 0;
        };

        VkDevice device() const;
        VkPhysicalDevice physicalDevice() const;
        VkQueue graphicsQueue() const;
        VkCommandPool commandPool() const;
        VkDescriptorPool descriptorPool() const;
        VkDescriptorSetLayout textureDescriptorSetLayout() const;
        VkSampler sampler() const;

        uint32_t calculateMipLevels(int width, int height) const;
        void generateMipmaps(VkImage image, int32_t width, int32_t height, uint32_t mipLevels, uint32_t layerCount = 1) const;
        void generateTextureArrayMipmaps(
            VkImage image,
            int32_t width,
            int32_t height,
            uint32_t mipLevels,
            uint32_t layerCount,
            const std::vector<TextureMipOverride>& mipOverrides,
            VkBuffer mipOverrideBuffer) const;

        const VkPhysicalDevice* physicalDevice_ = nullptr;
        const VkDevice* device_ = nullptr;
        const VkQueue* graphicsQueue_ = nullptr;
        const VkCommandPool* commandPool_ = nullptr;
        const VkDescriptorPool* descriptorPool_ = nullptr;
        const VkDescriptorSetLayout* textureDescriptorSetLayout_ = nullptr;
        const VkSampler* sampler_ = nullptr;
    };
}
