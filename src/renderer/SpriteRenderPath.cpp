#include "renderer/SpriteRenderPath.h"

namespace dolbuto
{
    void SpriteRenderPath::draw(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout pipelineLayout,
        VkBuffer vertexBuffer,
        const Texture& texture,
        Rect rect,
        UvRect uv,
        Color color) const
    {
        drawDescriptor(commandBuffer, pipelineLayout, vertexBuffer, texture.descriptorSet, rect, uv, color);
    }

    void SpriteRenderPath::drawDescriptor(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout pipelineLayout,
        VkBuffer vertexBuffer,
        VkDescriptorSet descriptorSet,
        Rect rect,
        UvRect uv,
        Color color) const
    {
        Push push{};
        push.data[0] = rect.centerX;
        push.data[1] = rect.centerY;
        push.data[2] = rect.halfWidth;
        push.data[3] = rect.halfHeight;
        push.data[4] = uv.x;
        push.data[5] = uv.y;
        push.data[6] = uv.width;
        push.data[7] = uv.height;
        push.data[8] = color.r;
        push.data[9] = color.g;
        push.data[10] = color.b;
        push.data[11] = color.a;

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &push);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    }
}
