#include "renderer/RendererGpuResources.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace dolbuto
{
    namespace
    {
        void writePngRgba(const std::filesystem::path& path, const std::vector<unsigned char>& rgba, uint32_t width, uint32_t height)
        {
            std::filesystem::create_directories(path.parent_path());
            const int strideBytes = static_cast<int>(width * 4u);
            if (stbi_write_png(path.string().c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba.data(), strideBytes) == 0)
            {
                throw std::runtime_error("Failed to write generated mip texture: " + path.string());
            }
        }

        std::vector<unsigned char> downsampleRgba2x(
            const std::vector<unsigned char>& source,
            uint32_t sourceWidth,
            uint32_t sourceHeight,
            uint32_t targetWidth,
            uint32_t targetHeight)
        {
            std::vector<unsigned char> result(static_cast<size_t>(targetWidth) * targetHeight * 4u);
            for (uint32_t y = 0; y < targetHeight; ++y)
            {
                for (uint32_t x = 0; x < targetWidth; ++x)
                {
                    uint32_t sum[4] = {};
                    uint32_t count = 0;
                    for (uint32_t oy = 0; oy < 2; ++oy)
                    {
                        const uint32_t sourceY = std::min(sourceHeight - 1u, y * 2u + oy);
                        for (uint32_t ox = 0; ox < 2; ++ox)
                        {
                            const uint32_t sourceX = std::min(sourceWidth - 1u, x * 2u + ox);
                            const unsigned char* pixel = source.data() + (static_cast<size_t>(sourceY) * sourceWidth + sourceX) * 4u;
                            for (int channel = 0; channel < 4; ++channel)
                            {
                                sum[channel] += pixel[channel];
                            }
                            ++count;
                        }
                    }

                    unsigned char* target = result.data() + (static_cast<size_t>(y) * targetWidth + x) * 4u;
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        target[channel] = static_cast<unsigned char>((sum[channel] + count / 2u) / count);
                    }
                }
            }
            return result;
        }

        bool isBlockTexturePath(const std::string& basePath)
        {
            std::filesystem::path path(basePath);
            return path.parent_path().filename() == "block";
        }

        std::filesystem::path manualMipPath(const std::string& basePath, uint32_t mipLevel)
        {
            std::filesystem::path path(basePath);
            return path.parent_path() / "mip" / (path.stem().string() + "_mip" + std::to_string(mipLevel) + ".png");
        }
    }

    VulkanResourceManager::VulkanResourceManager(
        const VkPhysicalDevice* physicalDevice,
        const VkDevice* device,
        const VkQueue* graphicsQueue,
        const VkCommandPool* commandPool,
        const VkDescriptorPool* descriptorPool,
        const VkDescriptorSetLayout* textureDescriptorSetLayout,
        const VkSampler* sampler)
    {
        setHandles(physicalDevice, device, graphicsQueue, commandPool, descriptorPool, textureDescriptorSetLayout, sampler);
    }

    void VulkanResourceManager::setHandles(
        const VkPhysicalDevice* physicalDevice,
        const VkDevice* device,
        const VkQueue* graphicsQueue,
        const VkCommandPool* commandPool,
        const VkDescriptorPool* descriptorPool,
        const VkDescriptorSetLayout* textureDescriptorSetLayout,
        const VkSampler* sampler)
    {
        physicalDevice_ = physicalDevice;
        device_ = device;
        graphicsQueue_ = graphicsQueue;
        commandPool_ = commandPool;
        descriptorPool_ = descriptorPool;
        textureDescriptorSetLayout_ = textureDescriptorSetLayout;
        sampler_ = sampler;
    }

    VkDevice VulkanResourceManager::device() const
    {
        return device_ != nullptr ? *device_ : VK_NULL_HANDLE;
    }

    VkPhysicalDevice VulkanResourceManager::physicalDevice() const
    {
        return physicalDevice_ != nullptr ? *physicalDevice_ : VK_NULL_HANDLE;
    }

    VkQueue VulkanResourceManager::graphicsQueue() const
    {
        return graphicsQueue_ != nullptr ? *graphicsQueue_ : VK_NULL_HANDLE;
    }

    VkCommandPool VulkanResourceManager::commandPool() const
    {
        return commandPool_ != nullptr ? *commandPool_ : VK_NULL_HANDLE;
    }

    VkDescriptorPool VulkanResourceManager::descriptorPool() const
    {
        return descriptorPool_ != nullptr ? *descriptorPool_ : VK_NULL_HANDLE;
    }

    VkDescriptorSetLayout VulkanResourceManager::textureDescriptorSetLayout() const
    {
        return textureDescriptorSetLayout_ != nullptr ? *textureDescriptorSetLayout_ : VK_NULL_HANDLE;
    }

    VkSampler VulkanResourceManager::sampler() const
    {
        return sampler_ != nullptr ? *sampler_ : VK_NULL_HANDLE;
    }

    Texture VulkanResourceManager::createTexture(const std::string& path, VkFormat format, VkSampler descriptorSampler) const
    {
        Texture texture;
        int channels = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &texture.width, &texture.height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            throw std::runtime_error("Failed to load texture: " + path);
        }

        Texture result = createTextureFromRgba(pixels, texture.width, texture.height, format, descriptorSampler);
        stbi_image_free(pixels);
        return result;
    }

    Texture VulkanResourceManager::createTextureFromRgba(const unsigned char* pixels, int width, int height, VkFormat format, VkSampler descriptorSampler) const
    {
        Texture texture;
        texture.width = width;
        texture.height = height;
        texture.mipLevels = calculateMipLevels(width, height);

        const VkDevice logicalDevice = device();
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(texture.width) * static_cast<VkDeviceSize>(texture.height) * 4;
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory);

        void* data = nullptr;
        vkMapMemory(logicalDevice, stagingMemory, 0, imageSize, 0, &data);
        std::memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(logicalDevice, stagingMemory);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height), 1};
        imageInfo.mipLevels = texture.mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(logicalDevice, &imageInfo, nullptr, &texture.image) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create texture image.");
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(logicalDevice, texture.image, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &texture.memory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate texture memory.");
        }

        vkBindImageMemory(logicalDevice, texture.image, texture.memory, 0);
        transitionImageLayout(texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mipLevels);
        copyBufferToImage(stagingBuffer, texture.image, static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height));
        generateMipmaps(texture.image, texture.width, texture.height, texture.mipLevels);

        vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
        vkFreeMemory(logicalDevice, stagingMemory, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = texture.mipLevels;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(logicalDevice, &viewInfo, nullptr, &texture.view) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create texture image view.");
        }

        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool();
        setInfo.descriptorSetCount = 1;
        const VkDescriptorSetLayout layout = textureDescriptorSetLayout();
        setInfo.pSetLayouts = &layout;

        if (vkAllocateDescriptorSets(logicalDevice, &setInfo, &texture.descriptorSet) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate texture descriptor set.");
        }

        VkDescriptorImageInfo imageDescriptor{};
        imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageDescriptor.imageView = texture.view;
        imageDescriptor.sampler = descriptorSampler != VK_NULL_HANDLE ? descriptorSampler : sampler();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = texture.descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(logicalDevice, 1, &write, 0, nullptr);

        return texture;
    }

    Texture VulkanResourceManager::createRenderTargetTexture(
        VkExtent2D extent,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspectMask,
        VkImageLayout descriptorLayout,
        VkSampler descriptorSampler) const
    {
        Texture texture;
        texture.width = static_cast<int>(extent.width);
        texture.height = static_cast<int>(extent.height);
        texture.mipLevels = 1;
        texture.layers = 1;

        const VkDevice logicalDevice = device();
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {extent.width, extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(logicalDevice, &imageInfo, nullptr, &texture.image) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create render target image.");
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(logicalDevice, texture.image, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &texture.memory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate render target memory.");
        }

        vkBindImageMemory(logicalDevice, texture.image, texture.memory, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectMask;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(logicalDevice, &viewInfo, nullptr, &texture.view) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create render target image view.");
        }

        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool();
        setInfo.descriptorSetCount = 1;
        const VkDescriptorSetLayout layout = textureDescriptorSetLayout();
        setInfo.pSetLayouts = &layout;

        if (vkAllocateDescriptorSets(logicalDevice, &setInfo, &texture.descriptorSet) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate render target descriptor set.");
        }

        VkDescriptorImageInfo imageDescriptor{};
        imageDescriptor.imageLayout = descriptorLayout;
        imageDescriptor.imageView = texture.view;
        imageDescriptor.sampler = descriptorSampler != VK_NULL_HANDLE ? descriptorSampler : sampler();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = texture.descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(logicalDevice, 1, &write, 0, nullptr);

        return texture;
    }

    Texture VulkanResourceManager::createTextureArray(const std::vector<std::string>& paths, float alphaMultiplier) const
    {
        if (paths.empty())
        {
            throw std::runtime_error("Cannot create an empty texture array.");
        }

        Texture texture;
        texture.layers = static_cast<uint32_t>(paths.size());

        std::vector<unsigned char> pixels;
        std::vector<unsigned char> mipOverridePixels;
        std::vector<TextureMipOverride> mipOverrides;
        for (size_t layer = 0; layer < paths.size(); ++layer)
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_uc* loadedPixels = stbi_load(paths[layer].c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (loadedPixels == nullptr)
            {
                throw std::runtime_error("Failed to load texture: " + paths[layer]);
            }

            if (layer == 0)
            {
                texture.width = width;
                texture.height = height;
                texture.mipLevels = calculateMipLevels(width, height);
                pixels.resize(static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height) * 4u * paths.size());
            }
            else if (width != texture.width || height != texture.height)
            {
                stbi_image_free(loadedPixels);
                throw std::runtime_error("Texture array images must have the same size: " + paths[layer]);
            }

            const size_t layerSize = static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height) * 4u;
            std::memcpy(pixels.data() + layer * layerSize, loadedPixels, layerSize);
            if (alphaMultiplier < 1.0f)
            {
                unsigned char* layerPixels = pixels.data() + layer * layerSize;
                for (size_t pixel = 0; pixel < layerSize / 4u; ++pixel)
                {
                    unsigned char& alpha = layerPixels[pixel * 4u + 3u];
                    alpha = static_cast<unsigned char>(std::clamp(
                        static_cast<int>(std::lround(static_cast<float>(alpha) * alphaMultiplier)),
                        0,
                        255));
                }
            }
            stbi_image_free(loadedPixels);
        }

        for (size_t layer = 0; layer < paths.size(); ++layer)
        {
            if (!isBlockTexturePath(paths[layer]))
            {
                continue;
            }

            const size_t layerSize = static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height) * 4u;
            std::vector<unsigned char> previousMip(layerSize);
            std::memcpy(previousMip.data(), pixels.data() + layer * layerSize, layerSize);
            uint32_t previousWidth = static_cast<uint32_t>(texture.width);
            uint32_t previousHeight = static_cast<uint32_t>(texture.height);

            for (uint32_t mip = 1; mip < texture.mipLevels; ++mip)
            {
                const std::filesystem::path path = manualMipPath(paths[layer], mip);
                const uint32_t expectedWidth = std::max(1u, static_cast<uint32_t>(texture.width) >> mip);
                const uint32_t expectedHeight = std::max(1u, static_cast<uint32_t>(texture.height) >> mip);
                std::vector<unsigned char> mipPixels;

                if (std::filesystem::exists(path))
                {
                    int width = 0;
                    int height = 0;
                    int channels = 0;
                    stbi_uc* loadedPixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
                    if (loadedPixels == nullptr)
                    {
                        throw std::runtime_error("Failed to load manual mip texture: " + path.string());
                    }
                    if (static_cast<uint32_t>(width) != expectedWidth || static_cast<uint32_t>(height) != expectedHeight)
                    {
                        stbi_image_free(loadedPixels);
                        throw std::runtime_error("Manual mip texture has wrong size: " + path.string());
                    }

                    const size_t mipSize = static_cast<size_t>(expectedWidth) * static_cast<size_t>(expectedHeight) * 4u;
                    mipPixels.resize(mipSize);
                    std::memcpy(mipPixels.data(), loadedPixels, mipSize);
                    stbi_image_free(loadedPixels);
                }
                else
                {
                    mipPixels = downsampleRgba2x(previousMip, previousWidth, previousHeight, expectedWidth, expectedHeight);
                    writePngRgba(path, mipPixels, expectedWidth, expectedHeight);
                }

                const VkDeviceSize offset = static_cast<VkDeviceSize>(mipOverridePixels.size());
                const size_t mipSize = static_cast<size_t>(expectedWidth) * static_cast<size_t>(expectedHeight) * 4u;
                mipOverridePixels.resize(mipOverridePixels.size() + mipSize);
                std::memcpy(mipOverridePixels.data() + static_cast<size_t>(offset), mipPixels.data(), mipSize);

                mipOverrides.push_back({
                    static_cast<uint32_t>(layer),
                    mip,
                    expectedWidth,
                    expectedHeight,
                    offset
                });

                previousMip = std::move(mipPixels);
                previousWidth = expectedWidth;
                previousHeight = expectedHeight;
            }
        }

        const VkDevice logicalDevice = device();
        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(pixels.size());
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory);

        void* data = nullptr;
        vkMapMemory(logicalDevice, stagingMemory, 0, imageSize, 0, &data);
        std::memcpy(data, pixels.data(), pixels.size());
        vkUnmapMemory(logicalDevice, stagingMemory);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height), 1};
        imageInfo.mipLevels = texture.mipLevels;
        imageInfo.arrayLayers = texture.layers;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(logicalDevice, &imageInfo, nullptr, &texture.image) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create texture array image.");
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(logicalDevice, texture.image, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &texture.memory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate texture array memory.");
        }

        vkBindImageMemory(logicalDevice, texture.image, texture.memory, 0);
        transitionImageLayout(texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mipLevels, texture.layers);
        copyBufferToImage(stagingBuffer, texture.image, static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height), texture.layers);

        if (mipOverrides.empty())
        {
            generateMipmaps(texture.image, texture.width, texture.height, texture.mipLevels, texture.layers);
        }
        else
        {
            VkBuffer mipOverrideBuffer = VK_NULL_HANDLE;
            VkDeviceMemory mipOverrideMemory = VK_NULL_HANDLE;
            const VkDeviceSize mipOverrideSize = static_cast<VkDeviceSize>(mipOverridePixels.size());
            createBuffer(mipOverrideSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mipOverrideBuffer, mipOverrideMemory);

            void* mipData = nullptr;
            vkMapMemory(logicalDevice, mipOverrideMemory, 0, mipOverrideSize, 0, &mipData);
            std::memcpy(mipData, mipOverridePixels.data(), mipOverridePixels.size());
            vkUnmapMemory(logicalDevice, mipOverrideMemory);

            generateTextureArrayMipmaps(texture.image, texture.width, texture.height, texture.mipLevels, texture.layers, mipOverrides, mipOverrideBuffer);

            vkDestroyBuffer(logicalDevice, mipOverrideBuffer, nullptr);
            vkFreeMemory(logicalDevice, mipOverrideMemory, nullptr);
        }

        vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
        vkFreeMemory(logicalDevice, stagingMemory, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = texture.mipLevels;
        viewInfo.subresourceRange.layerCount = texture.layers;

        if (vkCreateImageView(logicalDevice, &viewInfo, nullptr, &texture.view) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create texture array image view.");
        }

        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool();
        setInfo.descriptorSetCount = 1;
        const VkDescriptorSetLayout layout = textureDescriptorSetLayout();
        setInfo.pSetLayouts = &layout;

        if (vkAllocateDescriptorSets(logicalDevice, &setInfo, &texture.descriptorSet) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate texture array descriptor set.");
        }

        VkDescriptorImageInfo imageDescriptor{};
        imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageDescriptor.imageView = texture.view;
        imageDescriptor.sampler = sampler();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = texture.descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(logicalDevice, 1, &write, 0, nullptr);

        return texture;
    }

    void VulkanResourceManager::destroyTexture(Texture& texture) const
    {
        const VkDevice logicalDevice = device();
        if (texture.descriptorSet != VK_NULL_HANDLE && descriptorPool() != VK_NULL_HANDLE)
        {
            vkFreeDescriptorSets(logicalDevice, descriptorPool(), 1, &texture.descriptorSet);
        }
        if (texture.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(logicalDevice, texture.view, nullptr);
        }
        if (texture.image != VK_NULL_HANDLE)
        {
            vkDestroyImage(logicalDevice, texture.image, nullptr);
        }
        if (texture.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(logicalDevice, texture.memory, nullptr);
        }
        texture = {};
    }

    uint32_t VulkanResourceManager::calculateMipLevels(int width, int height) const
    {
        const int maxDimension = std::max(width, height);
        return static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(maxDimension)))) + 1;
    }

    uint32_t VulkanResourceManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
    {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice(), &memoryProperties);

        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
        {
            if ((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        throw std::runtime_error("Failed to find suitable memory type.");
    }

    void VulkanResourceManager::createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& memory,
        uint32_t* memoryTypeIndex) const
    {
        const VkDevice logicalDevice = device();
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create buffer.");
        }

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(logicalDevice, buffer, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);
        if (memoryTypeIndex != nullptr)
        {
            *memoryTypeIndex = allocInfo.memoryTypeIndex;
        }

        if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &memory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate buffer memory.");
        }

        vkBindBufferMemory(logicalDevice, buffer, memory, 0);
    }

    void VulkanResourceManager::createDeviceLocalBuffer(
        const void* source,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkBuffer& buffer,
        VkDeviceMemory& memory,
        uint32_t* memoryTypeIndex) const
    {
        const VkDevice logicalDevice = device();
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingMemory);

        void* data = nullptr;
        vkMapMemory(logicalDevice, stagingMemory, 0, size, 0, &data);
        std::memcpy(data, source, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice, stagingMemory);

        createBuffer(
            size,
            usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            buffer,
            memory,
            memoryTypeIndex);

        copyBuffer(stagingBuffer, buffer, size);
        vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
        vkFreeMemory(logicalDevice, stagingMemory, nullptr);
    }

    void VulkanResourceManager::copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) const
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferCopy region{};
        region.size = size;
        vkCmdCopyBuffer(commandBuffer, source, destination, 1, &region);

        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = destination;
        barrier.size = size;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            1,
            &barrier,
            0,
            nullptr);

        endSingleTimeCommands(commandBuffer);
    }

    void VulkanResourceManager::uploadBufferRegions(VkBuffer destination, const std::vector<BufferUploadRegion>& regions) const
    {
        if (regions.empty())
        {
            return;
        }

        VkDeviceSize stagingSize = 0;
        for (const BufferUploadRegion& region : regions)
        {
            stagingSize += region.size;
        }

        const VkDevice logicalDevice = device();
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(
            stagingSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingMemory);

        std::vector<VkBufferCopy> copies;
        copies.reserve(regions.size());

        void* data = nullptr;
        vkMapMemory(logicalDevice, stagingMemory, 0, stagingSize, 0, &data);
        VkDeviceSize sourceOffset = 0;
        for (const BufferUploadRegion& region : regions)
        {
            std::memcpy(static_cast<char*>(data) + sourceOffset, region.source, static_cast<size_t>(region.size));

            VkBufferCopy copy{};
            copy.srcOffset = sourceOffset;
            copy.dstOffset = region.destinationOffset;
            copy.size = region.size;
            copies.push_back(copy);

            sourceOffset += region.size;
        }
        vkUnmapMemory(logicalDevice, stagingMemory);

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        vkCmdCopyBuffer(commandBuffer, stagingBuffer, destination, static_cast<uint32_t>(copies.size()), copies.data());

        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = destination;
        barrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            1,
            &barrier,
            0,
            nullptr);

        endSingleTimeCommands(commandBuffer);
        vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
        vkFreeMemory(logicalDevice, stagingMemory, nullptr);
    }

    void VulkanResourceManager::uploadBufferData(VkBuffer destination, const void* source, VkDeviceSize size, VkDeviceSize destinationOffset) const
    {
        const BufferUploadRegion region{source, size, destinationOffset};
        uploadBufferRegions(destination, std::vector<BufferUploadRegion>{region});
    }

    void VulkanResourceManager::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount) const
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        std::vector<VkBufferImageCopy> regions(layerCount);
        const VkDeviceSize layerSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
        for (uint32_t layer = 0; layer < layerCount; ++layer)
        {
            VkBufferImageCopy& region = regions[layer];
            region.bufferOffset = layerSize * layer;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.baseArrayLayer = layer;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {width, height, 1};
        }

        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), regions.data());
        endSingleTimeCommands(commandBuffer);
    }

    void VulkanResourceManager::transitionImageLayout(
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        uint32_t mipLevels,
        uint32_t layerCount) const
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.layerCount = layerCount;

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else
        {
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        }

        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        endSingleTimeCommands(commandBuffer);
    }

    VkCommandBuffer VulkanResourceManager::beginSingleTimeCommands() const
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(device(), &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void VulkanResourceManager::endSingleTimeCommands(VkCommandBuffer commandBuffer) const
    {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue());
        vkFreeCommandBuffers(device(), commandPool(), 1, &commandBuffer);
    }

    void VulkanResourceManager::generateMipmaps(VkImage image, int32_t width, int32_t height, uint32_t mipLevels, uint32_t layerCount) const
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = layerCount;
        barrier.subresourceRange.levelCount = 1;

        int32_t mipWidth = width;
        int32_t mipHeight = height;
        for (uint32_t mip = 1; mip < mipLevels; ++mip)
        {
            barrier.subresourceRange.baseMipLevel = mip - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);

            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = mip - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = layerCount;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = mip;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = layerCount;

            vkCmdBlitImage(
                commandBuffer,
                image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &blit,
                VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);

            mipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
            mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
        }

        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);

        endSingleTimeCommands(commandBuffer);
    }

    void VulkanResourceManager::generateTextureArrayMipmaps(
        VkImage image,
        int32_t width,
        int32_t height,
        uint32_t mipLevels,
        uint32_t layerCount,
        const std::vector<TextureMipOverride>& mipOverrides,
        VkBuffer mipOverrideBuffer) const
    {
        std::vector<const TextureMipOverride*> overrideBySubresource(static_cast<size_t>(layerCount) * mipLevels, nullptr);
        for (const TextureMipOverride& mipOverride : mipOverrides)
        {
            if (mipOverride.layer >= layerCount || mipOverride.mipLevel == 0 || mipOverride.mipLevel >= mipLevels)
            {
                continue;
            }
            overrideBySubresource[static_cast<size_t>(mipOverride.layer) * mipLevels + mipOverride.mipLevel] = &mipOverride;
        }

        std::vector<VkImageLayout> layouts(static_cast<size_t>(layerCount) * mipLevels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        auto subresourceIndex = [mipLevels](uint32_t layer, uint32_t mip)
        {
            return static_cast<size_t>(layer) * mipLevels + mip;
        };

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        auto transitionSubresource = [&](uint32_t layer, uint32_t mip, VkImageLayout newLayout)
        {
            VkImageLayout& oldLayout = layouts[subresourceIndex(layer, mip)];
            if (oldLayout == newLayout)
            {
                return;
            }

            VkAccessFlags srcAccess = 0;
            VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            {
                srcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            {
                srcAccess = VK_ACCESS_TRANSFER_READ_BIT;
            }

            VkAccessFlags dstAccess = 0;
            VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            {
                dstAccess = VK_ACCESS_TRANSFER_READ_BIT;
            }
            else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                dstAccess = VK_ACCESS_SHADER_READ_BIT;
                dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            {
                dstAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            }

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = dstAccess;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = mip;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = layer;
            barrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(
                commandBuffer,
                srcStage,
                dstStage,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);

            oldLayout = newLayout;
        };

        for (uint32_t layer = 0; layer < layerCount; ++layer)
        {
            for (uint32_t mip = 1; mip < mipLevels; ++mip)
            {
                if (const TextureMipOverride* mipOverride = overrideBySubresource[subresourceIndex(layer, mip)])
                {
                    VkBufferImageCopy region{};
                    region.bufferOffset = mipOverride->bufferOffset;
                    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    region.imageSubresource.mipLevel = mip;
                    region.imageSubresource.baseArrayLayer = layer;
                    region.imageSubresource.layerCount = 1;
                    region.imageExtent = {mipOverride->width, mipOverride->height, 1};

                    vkCmdCopyBufferToImage(commandBuffer, mipOverrideBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                    continue;
                }

                transitionSubresource(layer, mip - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

                const int32_t sourceWidth = std::max(1, width >> (mip - 1));
                const int32_t sourceHeight = std::max(1, height >> (mip - 1));
                const int32_t destinationWidth = std::max(1, width >> mip);
                const int32_t destinationHeight = std::max(1, height >> mip);

                VkImageBlit blit{};
                blit.srcOffsets[0] = {0, 0, 0};
                blit.srcOffsets[1] = {sourceWidth, sourceHeight, 1};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = mip - 1;
                blit.srcSubresource.baseArrayLayer = layer;
                blit.srcSubresource.layerCount = 1;
                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {destinationWidth, destinationHeight, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = mip;
                blit.dstSubresource.baseArrayLayer = layer;
                blit.dstSubresource.layerCount = 1;

                vkCmdBlitImage(
                    commandBuffer,
                    image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &blit,
                    VK_FILTER_LINEAR);

                transitionSubresource(layer, mip - 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }

        for (uint32_t layer = 0; layer < layerCount; ++layer)
        {
            for (uint32_t mip = 0; mip < mipLevels; ++mip)
            {
                transitionSubresource(layer, mip, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }

        endSingleTimeCommands(commandBuffer);
    }
}
