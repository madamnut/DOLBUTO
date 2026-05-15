#pragma once

#include "renderer/RendererGpuResources.h"

namespace dolbuto
{
    class SpriteRenderPath
    {
    public:
        struct Rect
        {
            float centerX = 0.0f;
            float centerY = 0.0f;
            float halfWidth = 0.0f;
            float halfHeight = 0.0f;
        };

        struct UvRect
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 1.0f;
            float height = 1.0f;
        };

        struct Color
        {
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;
            float a = 1.0f;
        };

        struct Push
        {
            float data[12]{};
        };

        void draw(
            VkCommandBuffer commandBuffer,
            VkPipelineLayout pipelineLayout,
            VkBuffer vertexBuffer,
            const Texture& texture,
            Rect rect,
            UvRect uv = {},
            Color color = {}) const;

        void drawDescriptor(
            VkCommandBuffer commandBuffer,
            VkPipelineLayout pipelineLayout,
            VkBuffer vertexBuffer,
            VkDescriptorSet descriptorSet,
            Rect rect,
            UvRect uv = {},
            Color color = {}) const;
    };
}
