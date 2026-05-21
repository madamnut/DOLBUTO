#pragma once

#include "camera/Camera.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdint>

namespace dolbuto
{
    class SkyRenderPath
    {
    public:
        struct Push
        {
            float data[24]{};
        };

        void draw(
            VkCommandBuffer commandBuffer,
            VkPipeline pipeline,
            VkPipelineLayout pipelineLayout,
            const Camera& camera,
            float fovRadians,
            VkExtent2D extent,
            uint64_t worldTicks) const;
    };
}
