#include "renderer/Renderer.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

namespace dolbuto
{
    namespace
    {
        constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;

        VkFormat chooseSceneColorFormat(VkPhysicalDevice physicalDevice, VkFormat fallback)
        {
            constexpr VkFormat preferred = VK_FORMAT_R16G16B16A16_SFLOAT;
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice, preferred, &properties);
            constexpr VkFormatFeatureFlags required =
                VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
            if ((properties.optimalTilingFeatures & required) == required)
            {
                return preferred;
            }
            return fallback;
        }
    }

    void Renderer::createSwapchain()
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan_.physicalDevice, vulkan_.surface, &capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan_.physicalDevice, vulkan_.surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan_.physicalDevice, vulkan_.surface, &formatCount, formats.data());

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan_.physicalDevice, vulkan_.surface, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan_.physicalDevice, vulkan_.surface, &presentModeCount, presentModes.data());

        VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
        VkPresentModeKHR presentMode = choosePresentMode(presentModes);
        VkExtent2D extent = chooseExtent(capabilities);

        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
        {
            imageCount = capabilities.maxImageCount;
        }

        QueueFamilyIndices indices = findQueueFamilies(vulkan_.physicalDevice);
        uint32_t queueFamilyIndices[] = {indices.graphics, indices.present};

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = vulkan_.surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0)
        {
            throw std::runtime_error("Swapchain does not support screenshot transfer.");
        }
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        if (indices.graphics != indices.present)
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(vulkan_.device, &createInfo, nullptr, &vulkan_.swapchain) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create swapchain.");
        }

        vkGetSwapchainImagesKHR(vulkan_.device, vulkan_.swapchain, &imageCount, nullptr);
        vulkan_.swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(vulkan_.device, vulkan_.swapchain, &imageCount, vulkan_.swapchainImages.data());

        vulkan_.swapchainImageFormat = surfaceFormat.format;
        vulkan_.sceneColorFormat = chooseSceneColorFormat(vulkan_.physicalDevice, vulkan_.swapchainImageFormat);
        vulkan_.swapchainExtent = extent;
    }



    void Renderer::createImageViews()
    {
        vulkan_.swapchainImageViews.resize(vulkan_.swapchainImages.size());
        for (size_t i = 0; i < vulkan_.swapchainImages.size(); ++i)
        {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = vulkan_.swapchainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = vulkan_.swapchainImageFormat;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(vulkan_.device, &createInfo, nullptr, &vulkan_.swapchainImageViews[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create swapchain image view.");
            }
        }
    }



    void Renderer::createRenderPass()
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = vulkan_.swapchainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = DepthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = 1;
        createInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(vulkan_.device, &createInfo, nullptr, &vulkan_.renderPass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create render pass.");
        }

    }



    void Renderer::createSceneRenderPass()
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = vulkan_.sceneColorFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription bloomAttachment = colorAttachment;

        VkAttachmentReference bloomRef{};
        bloomRef.attachment = 1;
        bloomRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = DepthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 2;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        std::array<VkAttachmentReference, 2> colorRefs = {colorRef, bloomRef};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
        subpass.pColorAttachments = colorRefs.data();
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkSubpassDependency readDependency{};
        readDependency.srcSubpass = 0;
        readDependency.dstSubpass = VK_SUBPASS_EXTERNAL;
        readDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        readDependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        readDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        readDependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        std::array<VkAttachmentDescription, 3> attachments = {colorAttachment, bloomAttachment, depthAttachment};
        std::array<VkSubpassDependency, 2> dependencies = {dependency, readDependency};

        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        createInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(vulkan_.device, &createInfo, nullptr, &vulkan_.sceneRenderPass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create scene render pass.");
        }
    }



    void Renderer::createWaterBlurRenderPass()
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = vulkan_.sceneColorFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkSubpassDependency readDependency{};
        readDependency.srcSubpass = 0;
        readDependency.dstSubpass = VK_SUBPASS_EXTERNAL;
        readDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        readDependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        readDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        readDependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        std::array<VkSubpassDependency, 2> dependencies = {dependency, readDependency};

        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &colorAttachment;
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        createInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(vulkan_.device, &createInfo, nullptr, &vulkan_.waterBlurRenderPass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create water blur render pass.");
        }

        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (vkCreateRenderPass(vulkan_.device, &createInfo, nullptr, &vulkan_.postProcessLoadRenderPass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create post-process load render pass.");
        }
    }



    void Renderer::createDepthResources()
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = DepthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(vulkan_.device, &imageInfo, nullptr, &vulkan_.depthImage) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create depth image.");
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(vulkan_.device, vulkan_.depthImage, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = gpuResources_.findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(vulkan_.device, &allocInfo, nullptr, &vulkan_.depthMemory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate depth image memory.");
        }

        vkBindImageMemory(vulkan_.device, vulkan_.depthImage, vulkan_.depthMemory, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = vulkan_.depthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = DepthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(vulkan_.device, &viewInfo, nullptr, &vulkan_.depthImageView) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create depth image view.");
        }
    }



    void Renderer::createSceneTargets()
    {
        sceneColorTargets_.clear();
        bloomSourceTargets_.clear();
        sceneDepthTargets_.clear();
        waterBlurTargetsA_.clear();
        waterBlurTargetsB_.clear();
        for (std::vector<Texture>& targets : bloomTargets_)
        {
            targets.clear();
        }
        sceneColorTargets_.reserve(vulkan_.swapchainImageViews.size());
        bloomSourceTargets_.reserve(vulkan_.swapchainImageViews.size());
        sceneDepthTargets_.reserve(vulkan_.swapchainImageViews.size());
        waterBlurTargetsA_.reserve(vulkan_.swapchainImageViews.size());
        waterBlurTargetsB_.reserve(vulkan_.swapchainImageViews.size());
        for (std::vector<Texture>& targets : bloomTargets_)
        {
            targets.reserve(vulkan_.swapchainImageViews.size());
        }
        const VkExtent2D waterBlurExtent{
            std::max(1u, vulkan_.swapchainExtent.width / 4u),
            std::max(1u, vulkan_.swapchainExtent.height / 4u)
        };
        for (size_t i = 0; i < vulkan_.swapchainImageViews.size(); ++i)
        {
            sceneColorTargets_.push_back(gpuResources_.createRenderTargetTexture(
                vulkan_.swapchainExtent,
                vulkan_.sceneColorFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                vulkan_.linearSampler));
            bloomSourceTargets_.push_back(gpuResources_.createRenderTargetTexture(
                vulkan_.swapchainExtent,
                vulkan_.sceneColorFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                vulkan_.linearSampler));
            sceneDepthTargets_.push_back(gpuResources_.createRenderTargetTexture(
                vulkan_.swapchainExtent,
                DepthFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL));
            waterBlurTargetsA_.push_back(gpuResources_.createRenderTargetTexture(
                waterBlurExtent,
                vulkan_.sceneColorFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                vulkan_.linearSampler));
            waterBlurTargetsB_.push_back(gpuResources_.createRenderTargetTexture(
                waterBlurExtent,
                vulkan_.sceneColorFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                vulkan_.linearSampler));
            for (size_t mip = 0; mip < bloomTargets_.size(); ++mip)
            {
                const uint32_t divisor = 4u << static_cast<uint32_t>(mip);
                const VkExtent2D bloomExtent{
                    std::max(1u, vulkan_.swapchainExtent.width / divisor),
                    std::max(1u, vulkan_.swapchainExtent.height / divisor)
                };
                bloomTargets_[mip].push_back(gpuResources_.createRenderTargetTexture(
                    bloomExtent,
                    vulkan_.sceneColorFormat,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    vulkan_.linearSampler));
            }
        }
    }



    void Renderer::createFramebuffers()
    {
        vulkan_.sceneFramebuffers.resize(vulkan_.swapchainImageViews.size());
        for (size_t i = 0; i < vulkan_.sceneFramebuffers.size(); ++i)
        {
            std::array<VkImageView, 3> sceneAttachments = {sceneColorTargets_[i].view, bloomSourceTargets_[i].view, sceneDepthTargets_[i].view};

            VkFramebufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            createInfo.renderPass = vulkan_.sceneRenderPass;
            createInfo.attachmentCount = static_cast<uint32_t>(sceneAttachments.size());
            createInfo.pAttachments = sceneAttachments.data();
            createInfo.width = vulkan_.swapchainExtent.width;
            createInfo.height = vulkan_.swapchainExtent.height;
            createInfo.layers = 1;

            if (vkCreateFramebuffer(vulkan_.device, &createInfo, nullptr, &vulkan_.sceneFramebuffers[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create scene framebuffer.");
            }
        }

        vulkan_.waterBlurFramebuffersA.resize(waterBlurTargetsA_.size());
        vulkan_.waterBlurFramebuffersB.resize(waterBlurTargetsB_.size());
        for (size_t mip = 0; mip < vulkan_.bloomFramebuffers.size(); ++mip)
        {
            vulkan_.bloomFramebuffers[mip].resize(bloomTargets_[mip].size());
        }
        auto createPostProcessFramebuffer = [this](const Texture& target, VkFramebuffer& framebuffer)
        {
            VkFramebufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            createInfo.renderPass = vulkan_.waterBlurRenderPass;
            createInfo.attachmentCount = 1;
            createInfo.pAttachments = &target.view;
            createInfo.width = static_cast<uint32_t>(target.width);
            createInfo.height = static_cast<uint32_t>(target.height);
            createInfo.layers = 1;

            if (vkCreateFramebuffer(vulkan_.device, &createInfo, nullptr, &framebuffer) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create post-process framebuffer.");
            }
        };
        for (size_t i = 0; i < waterBlurTargetsA_.size(); ++i)
        {
            createPostProcessFramebuffer(waterBlurTargetsA_[i], vulkan_.waterBlurFramebuffersA[i]);
            createPostProcessFramebuffer(waterBlurTargetsB_[i], vulkan_.waterBlurFramebuffersB[i]);
        }
        for (size_t mip = 0; mip < bloomTargets_.size(); ++mip)
        {
            for (size_t i = 0; i < bloomTargets_[mip].size(); ++i)
            {
                createPostProcessFramebuffer(bloomTargets_[mip][i], vulkan_.bloomFramebuffers[mip][i]);
            }
        }

        vulkan_.framebuffers.resize(vulkan_.swapchainImageViews.size());
        for (size_t i = 0; i < vulkan_.swapchainImageViews.size(); ++i)
        {
            std::array<VkImageView, 2> attachments = {vulkan_.swapchainImageViews[i], vulkan_.depthImageView};

            VkFramebufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            createInfo.renderPass = vulkan_.renderPass;
            createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            createInfo.pAttachments = attachments.data();
            createInfo.width = vulkan_.swapchainExtent.width;
            createInfo.height = vulkan_.swapchainExtent.height;
            createInfo.layers = 1;

            if (vkCreateFramebuffer(vulkan_.device, &createInfo, nullptr, &vulkan_.framebuffers[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create framebuffer.");
            }
        }
    }



    void Renderer::cleanupSwapchain()
    {
        for (VkFramebuffer framebuffer : vulkan_.sceneFramebuffers)
        {
            vkDestroyFramebuffer(vulkan_.device, framebuffer, nullptr);
        }
        vulkan_.sceneFramebuffers.clear();

        for (VkFramebuffer framebuffer : vulkan_.waterBlurFramebuffersA)
        {
            vkDestroyFramebuffer(vulkan_.device, framebuffer, nullptr);
        }
        vulkan_.waterBlurFramebuffersA.clear();
        for (VkFramebuffer framebuffer : vulkan_.waterBlurFramebuffersB)
        {
            vkDestroyFramebuffer(vulkan_.device, framebuffer, nullptr);
        }
        vulkan_.waterBlurFramebuffersB.clear();
        for (std::vector<VkFramebuffer>& framebuffers : vulkan_.bloomFramebuffers)
        {
            for (VkFramebuffer framebuffer : framebuffers)
            {
                vkDestroyFramebuffer(vulkan_.device, framebuffer, nullptr);
            }
            framebuffers.clear();
        }

        for (VkFramebuffer framebuffer : vulkan_.framebuffers)
        {
            vkDestroyFramebuffer(vulkan_.device, framebuffer, nullptr);
        }
        vulkan_.framebuffers.clear();

        for (Texture& texture : sceneColorTargets_)
        {
            gpuResources_.destroyTexture(texture);
        }
        sceneColorTargets_.clear();
        for (Texture& texture : bloomSourceTargets_)
        {
            gpuResources_.destroyTexture(texture);
        }
        bloomSourceTargets_.clear();
        for (Texture& texture : sceneDepthTargets_)
        {
            gpuResources_.destroyTexture(texture);
        }
        sceneDepthTargets_.clear();
        for (Texture& texture : waterBlurTargetsA_)
        {
            gpuResources_.destroyTexture(texture);
        }
        waterBlurTargetsA_.clear();
        for (Texture& texture : waterBlurTargetsB_)
        {
            gpuResources_.destroyTexture(texture);
        }
        waterBlurTargetsB_.clear();
        for (std::vector<Texture>& targets : bloomTargets_)
        {
            for (Texture& texture : targets)
            {
                gpuResources_.destroyTexture(texture);
            }
            targets.clear();
        }

        if (vulkan_.depthImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(vulkan_.device, vulkan_.depthImageView, nullptr);
            vulkan_.depthImageView = VK_NULL_HANDLE;
        }
        if (vulkan_.depthImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(vulkan_.device, vulkan_.depthImage, nullptr);
            vulkan_.depthImage = VK_NULL_HANDLE;
        }
        if (vulkan_.depthMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(vulkan_.device, vulkan_.depthMemory, nullptr);
            vulkan_.depthMemory = VK_NULL_HANDLE;
        }

        for (VkImageView view : vulkan_.swapchainImageViews)
        {
            vkDestroyImageView(vulkan_.device, view, nullptr);
        }
        vulkan_.swapchainImageViews.clear();

        if (vulkan_.swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(vulkan_.device, vulkan_.swapchain, nullptr);
            vulkan_.swapchain = VK_NULL_HANDLE;
        }
    }



    void Renderer::recreateSwapchain()
    {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        while (width == 0 || height == 0)
        {
            glfwWaitEvents();
            glfwGetFramebufferSize(window_, &width, &height);
        }

        vkDeviceWaitIdle(vulkan_.device);
        cleanupSwapchain();
        createSwapchain();
        createImageViews();
        createDepthResources();
        createSceneTargets();
        createFramebuffers();
    }



    VkSurfaceFormatKHR Renderer::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
    {
        for (const auto& format : formats)
        {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return format;
            }
        }
        return formats.front();
    }



    VkPresentModeKHR Renderer::choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const
    {
        for (VkPresentModeKHR mode : modes)
        {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return mode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }



    VkExtent2D Renderer::chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);

        VkExtent2D extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return extent;
    }


}
