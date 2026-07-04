#include "renderer/SkyRenderPath.h"

#include "renderer/CelestialDirections.h"

#include <cmath>

namespace dolbuto
{
    namespace
    {
        void writeVec3(float* target, Vec3 value)
        {
            target[0] = value.x;
            target[1] = value.y;
            target[2] = value.z;
            target[3] = 0.0f;
        }
    }

    void SkyRenderPath::draw(
        VkCommandBuffer commandBuffer,
        VkPipeline pipeline,
        VkPipelineLayout pipelineLayout,
        const Camera& camera,
        float fovRadians,
        VkExtent2D extent,
        uint64_t worldTicks) const
    {
        if (pipeline == VK_NULL_HANDLE || pipelineLayout == VK_NULL_HANDLE || extent.height == 0)
        {
            return;
        }

        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const double dayPhase = celestial::dayPhase(worldTicks);
        const Vec3 sunPositionDirection = celestial::sunPositionDirection(worldTicks);
        const Vec3 cameraRight = camera.right();
        const Vec3 terrainRight{-cameraRight.x, -cameraRight.y, -cameraRight.z};
        const Vec3 forward = camera.forward();
        const Vec3 terrainForward{forward.x, -forward.y, forward.z};
        const Vec3 terrainUp = normalize(cross(terrainForward, terrainRight));

        Push push{};
        writeVec3(&push.data[0], terrainRight);
        writeVec3(&push.data[4], terrainUp);
        writeVec3(&push.data[8], terrainForward);
        writeVec3(&push.data[12], sunPositionDirection);
        writeVec3(&push.data[16], sunPositionDirection);
        push.data[20] = std::tan(fovRadians * 0.5f);
        push.data[21] = aspect;
        push.data[22] = static_cast<float>(dayPhase);
        push.data[23] = 0.0f;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &push);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
}
