#pragma once

#include "camera/Camera.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdint>

namespace dolbuto
{
    class CloudRenderPath
    {
    public:
        struct Push
        {
            float data[32]{};
        };

        void draw(
            VkCommandBuffer commandBuffer,
            VkPipeline pipeline,
            VkPipelineLayout pipelineLayout,
            const Camera& camera,
            Vec3 cameraPosition,
            float fovRadians,
            VkExtent2D extent,
            uint64_t worldTicks,
            float cloudCoverage) const;
    };
}
