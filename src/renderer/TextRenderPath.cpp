#include "renderer/TextRenderPath.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace dolbuto
{
    namespace
    {
        constexpr int FontAtlasSize = 512;
        constexpr float FontPixelHeight = 18.0f;
        constexpr size_t MaxTextVertices = 65536;

        std::vector<char> readFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open())
            {
                throw std::runtime_error("Failed to open file: " + path.string());
            }

            const auto size = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(size);
            file.seekg(0);
            file.read(buffer.data(), static_cast<std::streamsize>(size));
            return buffer;
        }
    }

    TextRenderPath::TextRenderPath(VkDevice* device, const VulkanResourceManager* gpuResources) :
        device_(device),
        gpuResources_(gpuResources)
    {
    }

    void TextRenderPath::loadFont(const std::filesystem::path& fontPath, Texture& fontTexture)
    {
        if (gpuResources_ == nullptr)
        {
            throw std::runtime_error("Text render path GPU resources are not initialized.");
        }

        std::vector<char> fontData = readFile(fontPath);

        FT_Library library = nullptr;
        if (FT_Init_FreeType(&library) != 0)
        {
            throw std::runtime_error("Failed to initialize FreeType.");
        }

        FT_Face face = nullptr;
        if (FT_New_Memory_Face(
                library,
                reinterpret_cast<const FT_Byte*>(fontData.data()),
                static_cast<FT_Long>(fontData.size()),
                0,
                &face) != 0)
        {
            FT_Done_FreeType(library);
            throw std::runtime_error("Failed to load debug font face.");
        }

        if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(FontPixelHeight)) != 0)
        {
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            throw std::runtime_error("Failed to set debug font pixel size.");
        }

        std::vector<unsigned char> alpha(FontAtlasSize * FontAtlasSize);
        int penX = 1;
        int penY = 1;
        int rowHeight = 0;

        for (char character = 32; character <= 126; ++character)
        {
            if (FT_Load_Char(face, static_cast<FT_ULong>(character), FT_LOAD_RENDER) != 0)
            {
                FT_Done_Face(face);
                FT_Done_FreeType(library);
                throw std::runtime_error("Failed to render debug font glyph.");
            }

            const FT_GlyphSlot glyph = face->glyph;
            const FT_Bitmap& bitmap = glyph->bitmap;
            const int glyphWidth = static_cast<int>(bitmap.width);
            const int glyphHeight = static_cast<int>(bitmap.rows);
            if (penX + glyphWidth + 1 >= FontAtlasSize)
            {
                penX = 1;
                penY += rowHeight + 1;
                rowHeight = 0;
            }
            if (penY + glyphHeight + 1 >= FontAtlasSize)
            {
                FT_Done_Face(face);
                FT_Done_FreeType(library);
                throw std::runtime_error("Debug font atlas is too small.");
            }

            FontCharacter& fontCharacter = fontCharacters_[static_cast<size_t>(character - 32)];
            fontCharacter.x0 = penX;
            fontCharacter.y0 = penY;
            fontCharacter.x1 = penX + glyphWidth;
            fontCharacter.y1 = penY + glyphHeight;
            fontCharacter.xOffset = static_cast<float>(glyph->bitmap_left);
            fontCharacter.yOffset = -static_cast<float>(glyph->bitmap_top);
            fontCharacter.advance = static_cast<float>(glyph->advance.x) / 64.0f;

            for (int row = 0; row < glyphHeight; ++row)
            {
                const unsigned char* source = bitmap.buffer + static_cast<size_t>(row) * static_cast<size_t>(std::abs(bitmap.pitch));
                unsigned char* destination = alpha.data() + static_cast<size_t>(penY + row) * FontAtlasSize + static_cast<size_t>(penX);
                std::memcpy(destination, source, static_cast<size_t>(glyphWidth));
            }

            penX += glyphWidth + 1;
            rowHeight = std::max(rowHeight, glyphHeight);
        }

        FT_Done_Face(face);
        FT_Done_FreeType(library);

        std::vector<unsigned char> rgba(FontAtlasSize * FontAtlasSize * 4);
        for (int i = 0; i < FontAtlasSize * FontAtlasSize; ++i)
        {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = alpha[i];
        }

        fontTexture = gpuResources_->createTextureFromRgba(rgba.data(), FontAtlasSize, FontAtlasSize);
    }

    void TextRenderPath::createBuffers()
    {
        if (gpuResources_ == nullptr)
        {
            throw std::runtime_error("Text render path GPU resources are not initialized.");
        }

        constexpr VkDeviceSize BufferSize = sizeof(TextVertex) * MaxTextVertices;
        gpuResources_->createBuffer(
            BufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            textVertexBuffer_,
            textVertexMemory_);
    }

    void TextRenderPath::destroy()
    {
        if (device_ == nullptr || *device_ == VK_NULL_HANDLE)
        {
            return;
        }

        if (textVertexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(*device_, textVertexBuffer_, nullptr);
            textVertexBuffer_ = VK_NULL_HANDLE;
        }
        if (textVertexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(*device_, textVertexMemory_, nullptr);
            textVertexMemory_ = VK_NULL_HANDLE;
        }
    }

    VkBuffer TextRenderPath::vertexBuffer() const
    {
        return textVertexBuffer_;
    }

    void TextRenderPath::addText(TextBatch& batch, std::string_view text, float x, float y, bool alignRight, VkExtent2D extent) const
    {
        constexpr float LineHeight = 22.0f;

        float lineY = y;
        size_t lineStart = 0;
        while (lineStart <= text.size())
        {
            const size_t lineEnd = text.find('\n', lineStart);
            const std::string_view line = lineEnd == std::string_view::npos
                ? text.substr(lineStart)
                : text.substr(lineStart, lineEnd - lineStart);

            addTextPass(batch.outline, line, x, lineY, alignRight, -1.0f, 0.0f, extent);
            addTextPass(batch.outline, line, x, lineY, alignRight, 1.0f, 0.0f, extent);
            addTextPass(batch.outline, line, x, lineY, alignRight, 0.0f, -1.0f, extent);
            addTextPass(batch.outline, line, x, lineY, alignRight, 0.0f, 1.0f, extent);
            addTextPass(batch.fill, line, x, lineY, alignRight, 0.0f, 0.0f, extent);

            if (lineEnd == std::string_view::npos)
            {
                break;
            }

            lineStart = lineEnd + 1;
            lineY += LineHeight;
        }
    }

    void TextRenderPath::addTextPass(std::vector<TextVertex>& vertices, std::string_view text, float x, float y, bool alignRight, float offsetX, float offsetY, VkExtent2D extent) const
    {
        float cursorX = alignRight ? x - measureText(text) : x;
        const float cursorY = y;

        for (char character : text)
        {
            if (character < 32 || character > 126)
            {
                continue;
            }

            Glyph glyph = makeGlyph(character, cursorX + offsetX, cursorY + offsetY, extent);
            cursorX += glyph.advance;
            appendGlyphQuad(vertices, glyph);
        }
    }

    void TextRenderPath::appendGlyphQuad(std::vector<TextVertex>& vertices, const Glyph& glyph) const
    {
        const float left = glyph.rect.centerX - glyph.rect.halfWidth;
        const float right = glyph.rect.centerX + glyph.rect.halfWidth;
        const float top = glyph.rect.centerY - glyph.rect.halfHeight;
        const float bottom = glyph.rect.centerY + glyph.rect.halfHeight;
        const float u0 = glyph.uv.x;
        const float v0 = glyph.uv.y;
        const float u1 = glyph.uv.x + glyph.uv.width;
        const float v1 = glyph.uv.y + glyph.uv.height;

        vertices.push_back({left, top, u0, v0});
        vertices.push_back({right, top, u1, v0});
        vertices.push_back({right, bottom, u1, v1});
        vertices.push_back({left, top, u0, v0});
        vertices.push_back({right, bottom, u1, v1});
        vertices.push_back({left, bottom, u0, v1});
    }

    void TextRenderPath::drawBatch(
        VkCommandBuffer commandBuffer,
        const TextBatch& batch,
        const Texture& fontTexture,
        VkExtent2D,
        VkPipelineLayout pipelineLayout)
    {
        const size_t totalVertices = batch.outline.size() + batch.fill.size();
        if (totalVertices == 0 || totalVertices > MaxTextVertices)
        {
            return;
        }
        if (device_ == nullptr || *device_ == VK_NULL_HANDLE || textVertexMemory_ == VK_NULL_HANDLE)
        {
            return;
        }

        const VkDeviceSize outlineSize = sizeof(TextVertex) * batch.outline.size();
        const VkDeviceSize fillSize = sizeof(TextVertex) * batch.fill.size();
        const VkDeviceSize fillOffset = outlineSize;

        void* data = nullptr;
        vkMapMemory(*device_, textVertexMemory_, 0, outlineSize + fillSize, 0, &data);
        if (outlineSize > 0)
        {
            std::memcpy(data, batch.outline.data(), static_cast<size_t>(outlineSize));
        }
        if (fillSize > 0)
        {
            std::memcpy(static_cast<char*>(data) + fillOffset, batch.fill.data(), static_cast<size_t>(fillSize));
        }
        vkUnmapMemory(*device_, textVertexMemory_);

        drawTextVertices(commandBuffer, fontTexture, pipelineLayout, batch.outline, {0.0f, 0.0f, 0.0f, 1.0f}, 0);
        drawTextVertices(commandBuffer, fontTexture, pipelineLayout, batch.fill, {1.0f, 1.0f, 1.0f, 1.0f}, fillOffset);
    }

    void TextRenderPath::drawTextVertices(VkCommandBuffer commandBuffer, const Texture& fontTexture, VkPipelineLayout pipelineLayout, const std::vector<TextVertex>& vertices, Color color, VkDeviceSize bufferOffset) const
    {
        if (vertices.empty())
        {
            return;
        }

        TextPush push{};
        push.data[0] = 0.0f;
        push.data[1] = 0.0f;
        push.data[2] = -1.0f;
        push.data[3] = 1.0f;
        push.data[4] = 0.0f;
        push.data[5] = 0.0f;
        push.data[6] = 1.0f;
        push.data[7] = 1.0f;
        push.data[8] = color.r;
        push.data[9] = color.g;
        push.data[10] = color.b;
        push.data[11] = color.a;

        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &textVertexBuffer_, &bufferOffset);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &fontTexture.descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TextPush), &push);
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    }

    TextRenderPath::Glyph TextRenderPath::makeGlyph(char character, float x, float y, VkExtent2D extent) const
    {
        const FontCharacter& fontCharacter = fontCharacters_[static_cast<size_t>(character - 32)];

        const float x0 = x + fontCharacter.xOffset;
        const float y0 = y + fontCharacter.yOffset;
        const float x1 = x0 + static_cast<float>(fontCharacter.x1 - fontCharacter.x0);
        const float y1 = y0 + static_cast<float>(fontCharacter.y1 - fontCharacter.y0);

        Glyph glyph{};
        glyph.rect.centerX = ((x0 + x1) * 0.5f / static_cast<float>(extent.width)) * 2.0f - 1.0f;
        glyph.rect.centerY = ((y0 + y1) * 0.5f / static_cast<float>(extent.height)) * 2.0f - 1.0f;
        glyph.rect.halfWidth = (x1 - x0) / static_cast<float>(extent.width);
        glyph.rect.halfHeight = (y1 - y0) / static_cast<float>(extent.height);
        glyph.uv.x = static_cast<float>(fontCharacter.x0) / static_cast<float>(FontAtlasSize);
        glyph.uv.y = static_cast<float>(fontCharacter.y0) / static_cast<float>(FontAtlasSize);
        glyph.uv.width = static_cast<float>(fontCharacter.x1 - fontCharacter.x0) / static_cast<float>(FontAtlasSize);
        glyph.uv.height = static_cast<float>(fontCharacter.y1 - fontCharacter.y0) / static_cast<float>(FontAtlasSize);
        glyph.advance = fontCharacter.advance;
        return glyph;
    }

    float TextRenderPath::measureText(std::string_view text) const
    {
        float width = 0.0f;
        for (char character : text)
        {
            if (character < 32 || character > 126)
            {
                continue;
            }

            width += fontCharacters_[static_cast<size_t>(character - 32)].advance;
        }
        return width;
    }
}
