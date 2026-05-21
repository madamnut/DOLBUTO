#include "renderer/SkyRenderPath.h"

#include <cmath>

namespace dolbuto
{
    namespace
    {
        constexpr uint64_t SkyTicksPerDay = 28800;
        constexpr double TwoPi = 6.283185307179586;
        constexpr double HalfPi = 1.5707963267948966;

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
        const double dayPhase = static_cast<double>(worldTicks % SkyTicksPerDay) / static_cast<double>(SkyTicksPerDay);
        const double skyAngle = HalfPi - dayPhase * TwoPi;
        const Vec3 sunDirection = normalize({
            static_cast<float>(std::cos(skyAngle)),
            static_cast<float>(std::sin(skyAngle)),
            0.0f
        });

        Push push{};
        writeVec3(&push.data[0], camera.right());
        writeVec3(&push.data[4], camera.up());
        writeVec3(&push.data[8], camera.forward());
        writeVec3(&push.data[12], sunDirection);
        writeVec3(&push.data[16], {-sunDirection.x, -sunDirection.y, -sunDirection.z});
        push.data[20] = std::tan(fovRadians * 0.5f);
        push.data[21] = aspect;
        push.data[22] = static_cast<float>(dayPhase);
        push.data[23] = 0.0f;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &push);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
}
