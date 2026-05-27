#pragma once

#include "game/RadialMenuRenderFrame.h"
#include "renderer/RendererGpuResources.h"
#include "renderer/SpriteRenderPath.h"

#include <cstdint>
#include <vector>

namespace dolbuto
{
    class RadialMenuRenderPath
    {
    public:
        struct Vertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float u = 0.5f;
            float v = 0.5f;
        };

        RadialMenuRenderPath() = default;
        RadialMenuRenderPath(VkDevice* device, const VulkanResourceManager* gpuResources);

        void createBuffers();
        void destroy();
        void draw(
            VkCommandBuffer commandBuffer,
            VkPipelineLayout pipelineLayout,
            const Texture& whiteTexture,
            VkExtent2D extent,
            const game::RadialMenuRenderFrame& frame);

    private:
        struct DrawCommand
        {
            uint32_t firstVertex = 0;
            uint32_t vertexCount = 0;
            SpriteRenderPath::Color color;
        };

        void appendRingSector(
            std::vector<Vertex>& vertices,
            VkExtent2D extent,
            float centerX,
            float centerY,
            float innerRadius,
            float outerRadius,
            double startAngle,
            double endAngle) const;
        void appendTriangle(
            std::vector<Vertex>& vertices,
            VkExtent2D extent,
            float screenX0,
            float screenY0,
            float screenX1,
            float screenY1,
            float screenX2,
            float screenY2) const;
        Vertex vertexFromPixels(VkExtent2D extent, float x, float y) const;
        void uploadVertices(const std::vector<Vertex>& vertices) const;
        void drawUploadedVertices(
            VkCommandBuffer commandBuffer,
            VkPipelineLayout pipelineLayout,
            const Texture& whiteTexture,
            const DrawCommand& drawCommand,
            SpriteRenderPath::Color color) const;

        VkDevice* device_ = nullptr;
        const VulkanResourceManager* gpuResources_ = nullptr;
        VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    };
}
