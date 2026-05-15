#include "renderer/Renderer.h"

#include "platform/Log.h"
#include "platform/RuntimePaths.h"

#include <stb_image.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace dolbuto
{
    namespace
    {
        constexpr size_t MaxUiVertices = 262144;
        constexpr size_t MaxUiIndices = 393216;
    }
    void Renderer::initializeRmlUi()
    {
        ui_.initialize(
            window_,
            assetDirectory(),
            static_cast<int>(swapchainExtent_.width),
            static_cast<int>(swapchainExtent_.height),
            this);
        ui_.setClickCallback([this]()
        {
            audio_.playButtonClick();
        });
        uiBridge_.updateHotbarScopeClass();
        updateInventoryUi();
        uiBridge_.updateInventoryDebugSlots();
    }

    void Renderer::shutdownRmlUi()
    {
        ui_.shutdown();
    }

    bool Renderer::renderRmlUi(VkCommandBuffer commandBuffer, int menuOverlayMode, bool hudVisible)
    {
        if (!ui_.available())
        {
            return false;
        }

        const int effectiveMenuOverlayMode = hudVisible ? menuOverlayMode : (menuOverlayMode == 0 ? -1 : menuOverlayMode);
        if (ui_.activeMenuOverlayMode() == 5 && effectiveMenuOverlayMode != 5)
        {
            closeInventoryInteraction();
        }
        rmlUiVertexOffset_ = 0;
        rmlUiIndexOffset_ = 0;
        rmlCommandBuffer_ = commandBuffer;
        const bool rendered = ui_.render(
            effectiveMenuOverlayMode,
            static_cast<int>(swapchainExtent_.width),
            static_cast<int>(swapchainExtent_.height));
        rmlCommandBuffer_ = VK_NULL_HANDLE;
        return rendered;
    }

    void Renderer::uiMouseMove(double x, double y)
    {
        uiBridge_.processMouseMove(x, y, swapchainExtent_.width, swapchainExtent_.height);
    }

    void Renderer::uiMouseButton(int button, bool pressed, int modifiers)
    {
        uiBridge_.processMouseButton(button, pressed, modifiers, swapchainExtent_.width, swapchainExtent_.height);
    }

    void Renderer::uiMouseWheel(double yOffset)
    {
        uiBridge_.processMouseWheel(yOffset);
    }

    void Renderer::uiTextInput(unsigned int codepoint)
    {
        uiBridge_.processTextInput(codepoint);
    }

    void Renderer::uiKey(int key, bool pressed, int modifiers)
    {
        uiBridge_.processKey(key, pressed, modifiers, swapchainExtent_.width, swapchainExtent_.height);
    }

    std::optional<std::string> Renderer::consumeUiAction()
    {
        return ui_.consumeAction();
    }

    bool Renderer::rmlUiAvailable() const
    {
        return ui_.available();
    }

    void Renderer::setHotbarSelectedSlot(int slot)
    {
        uiBridge_.setHotbarSelectedSlot(slot);
    }

    void Renderer::closeInventoryInteraction()
    {
        uiBridge_.closeInventoryInteraction(swapchainExtent_.width, swapchainExtent_.height);
    }

    void Renderer::updateInventoryUi()
    {
        uiBridge_.updateInventoryUi();
    }

    void Renderer::setWorldList(const std::vector<game::WorldListItem>& worlds)
    {
        uiBridge_.setWorldList(worlds);
    }

    std::string Renderer::uiInputValue(std::string_view id) const
    {
        return ui_.inputValue(id);
    }


    Rml::CompiledGeometryHandle Renderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
    {
        auto* geometry = new UiGeometry();
        geometry->vertices.reserve(vertices.size());
        geometry->indices.reserve(indices.size());

        for (const Rml::Vertex& vertex : vertices)
        {
            UiVertex out{};
            out.x = vertex.position.x;
            out.y = vertex.position.y;
            out.r = static_cast<float>(vertex.colour.red) / 255.0f;
            out.g = static_cast<float>(vertex.colour.green) / 255.0f;
            out.b = static_cast<float>(vertex.colour.blue) / 255.0f;
            out.a = static_cast<float>(vertex.colour.alpha) / 255.0f;
            out.u = vertex.tex_coord.x;
            out.v = vertex.tex_coord.y;
            geometry->vertices.push_back(out);
        }
        for (int index : indices)
        {
            geometry->indices.push_back(static_cast<uint32_t>(index));
        }

        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
    }

    void Renderer::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle textureHandle)
    {
        if (rmlCommandBuffer_ == VK_NULL_HANDLE || uiPipeline_ == VK_NULL_HANDLE || handle == 0)
        {
            return;
        }

        const auto* geometry = reinterpret_cast<const UiGeometry*>(handle);
        if (geometry->vertices.empty() || geometry->indices.empty() ||
            rmlUiVertexOffset_ + geometry->vertices.size() > MaxUiVertices ||
            rmlUiIndexOffset_ + geometry->indices.size() > MaxUiIndices)
        {
            return;
        }

        const VkDeviceSize vertexBytes = sizeof(UiVertex) * geometry->vertices.size();
        const VkDeviceSize vertexBufferOffset = sizeof(UiVertex) * rmlUiVertexOffset_;
        void* vertexData = nullptr;
        vkMapMemory(device_, uiVertexMemory_, vertexBufferOffset, vertexBytes, 0, &vertexData);
        std::memcpy(vertexData, geometry->vertices.data(), static_cast<size_t>(vertexBytes));
        vkUnmapMemory(device_, uiVertexMemory_);

        const VkDeviceSize indexBytes = sizeof(uint32_t) * geometry->indices.size();
        const VkDeviceSize indexBufferOffset = sizeof(uint32_t) * rmlUiIndexOffset_;
        void* indexData = nullptr;
        vkMapMemory(device_, uiIndexMemory_, indexBufferOffset, indexBytes, 0, &indexData);
        std::memcpy(indexData, geometry->indices.data(), static_cast<size_t>(indexBytes));
        vkUnmapMemory(device_, uiIndexMemory_);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapchainExtent_.width);
        viewport.height = static_cast<float>(swapchainExtent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapchainExtent_;
        if (rmlScissorEnabled_)
        {
            scissor = rmlScissor_;
        }

        const Texture* texture = textureHandle == 0 ? &rendererAssets_.white : reinterpret_cast<const Texture*>(textureHandle);
        const UiPush push{
            static_cast<float>(swapchainExtent_.width),
            static_cast<float>(swapchainExtent_.height),
            translation.x,
            translation.y
        };

        vkCmdBindPipeline(rmlCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline_);
        vkCmdSetViewport(rmlCommandBuffer_, 0, 1, &viewport);
        vkCmdSetScissor(rmlCommandBuffer_, 0, 1, &scissor);
        vkCmdBindVertexBuffers(rmlCommandBuffer_, 0, 1, &uiVertexBuffer_, &vertexBufferOffset);
        vkCmdBindIndexBuffer(rmlCommandBuffer_, uiIndexBuffer_, indexBufferOffset, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(rmlCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipelineLayout_, 0, 1, &texture->descriptorSet, 0, nullptr);
        vkCmdPushConstants(rmlCommandBuffer_, uiPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(UiPush), &push);
        vkCmdDrawIndexed(rmlCommandBuffer_, static_cast<uint32_t>(geometry->indices.size()), 1, 0, 0, 0);

        rmlUiVertexOffset_ += geometry->vertices.size();
        rmlUiIndexOffset_ += geometry->indices.size();
    }

    void Renderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
    {
        delete reinterpret_cast<UiGeometry*>(geometry);
    }

    Rml::TextureHandle Renderer::LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source)
    {
        std::filesystem::path texturePath(source);
        if (texturePath.is_relative())
        {
            texturePath = (assetDirectory() / "ui" / texturePath).lexically_normal();
        }
        if (!std::filesystem::exists(texturePath))
        {
            std::string normalized = texturePath.generic_string();
            const std::string marker = "/textures/";
            const size_t markerPosition = normalized.find(marker);
            if (markerPosition != std::string::npos)
            {
                const std::string textureTail = normalized.substr(markerPosition + marker.size());
                const std::filesystem::path remappedPath = assetDirectory() / "textures" / std::filesystem::path(textureTail);
                if (std::filesystem::exists(remappedPath))
                {
                    texturePath = remappedPath;
                }
            }
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(texturePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            log::warn("RmlUi texture load failed: " + texturePath.string());
            return 0;
        }

        const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        std::vector<unsigned char> premultiplied(pixelCount * 4u);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const unsigned char alpha = pixels[i * 4u + 3u];
            premultiplied[i * 4u + 0u] = static_cast<unsigned char>((static_cast<uint32_t>(pixels[i * 4u + 0u]) * alpha) / 255u);
            premultiplied[i * 4u + 1u] = static_cast<unsigned char>((static_cast<uint32_t>(pixels[i * 4u + 1u]) * alpha) / 255u);
            premultiplied[i * 4u + 2u] = static_cast<unsigned char>((static_cast<uint32_t>(pixels[i * 4u + 2u]) * alpha) / 255u);
            premultiplied[i * 4u + 3u] = alpha;
        }
        stbi_image_free(pixels);

        Texture texture = gpuResources_.createTextureFromRgba(premultiplied.data(), width, height, VK_FORMAT_R8G8B8A8_SRGB);
        textureDimensions = Rml::Vector2i(width, height);
        return reinterpret_cast<Rml::TextureHandle>(new Texture(texture));
    }

    Rml::TextureHandle Renderer::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions)
    {
        Texture texture = gpuResources_.createTextureFromRgba(
            reinterpret_cast<const unsigned char*>(source.data()),
            sourceDimensions.x,
            sourceDimensions.y,
            VK_FORMAT_R8G8B8A8_UNORM);
        return reinterpret_cast<Rml::TextureHandle>(new Texture(texture));
    }

    void Renderer::ReleaseTexture(Rml::TextureHandle textureHandle)
    {
        if (textureHandle == 0)
        {
            return;
        }

        auto* texture = reinterpret_cast<Texture*>(textureHandle);
        gpuResources_.destroyTexture(*texture);
        delete texture;
    }

    void Renderer::EnableScissorRegion(bool enable)
    {
        rmlScissorEnabled_ = enable;
    }

    void Renderer::SetScissorRegion(Rml::Rectanglei region)
    {
        const int left = std::max(region.Left(), 0);
        const int top = std::max(region.Top(), 0);
        const int right = std::min(region.Right(), static_cast<int>(swapchainExtent_.width));
        const int bottom = std::min(region.Bottom(), static_cast<int>(swapchainExtent_.height));

        rmlScissor_.offset = {left, top};
        rmlScissor_.extent = {
            static_cast<uint32_t>(std::max(right - left, 0)),
            static_cast<uint32_t>(std::max(bottom - top, 0))
        };
    }

}
