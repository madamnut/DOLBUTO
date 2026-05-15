#pragma once

#include "renderer/RendererGpuResources.h"

#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

namespace dolbuto
{
    class TextRenderPath
    {
    public:
        struct Color
        {
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;
            float a = 1.0f;
        };

        struct TextVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float u = 0.0f;
            float v = 0.0f;
        };

        struct TextBatch
        {
            std::vector<TextVertex> outline;
            std::vector<TextVertex> fill;
        };

        struct TextPush
        {
            float data[12]{};
        };

        TextRenderPath() = default;
        TextRenderPath(VkDevice* device, const VulkanResourceManager* gpuResources);

        void loadFont(const std::filesystem::path& fontPath, Texture& fontTexture);
        void createBuffers();
        void destroy();
        VkBuffer vertexBuffer() const;

        void addText(TextBatch& batch, std::string_view text, float x, float y, bool alignRight, VkExtent2D extent) const;
        float measureText(std::string_view text) const;
        void drawBatch(
            VkCommandBuffer commandBuffer,
            const TextBatch& batch,
            const Texture& fontTexture,
            VkExtent2D extent,
            VkPipelineLayout pipelineLayout);

    private:
        struct FontCharacter
        {
            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            float xOffset = 0.0f;
            float yOffset = 0.0f;
            float advance = 0.0f;
        };

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

        struct Glyph
        {
            Rect rect;
            UvRect uv;
            float advance = 0.0f;
        };

        void addTextPass(std::vector<TextVertex>& vertices, std::string_view text, float x, float y, bool alignRight, float offsetX, float offsetY, VkExtent2D extent) const;
        void appendGlyphQuad(std::vector<TextVertex>& vertices, const Glyph& glyph) const;
        void drawTextVertices(VkCommandBuffer commandBuffer, const Texture& fontTexture, VkPipelineLayout pipelineLayout, const std::vector<TextVertex>& vertices, Color color, VkDeviceSize bufferOffset) const;
        Glyph makeGlyph(char character, float x, float y, VkExtent2D extent) const;

        VkDevice* device_ = nullptr;
        const VulkanResourceManager* gpuResources_ = nullptr;
        VkBuffer textVertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory textVertexMemory_ = VK_NULL_HANDLE;
        std::array<FontCharacter, 95> fontCharacters_{};
    };
}
