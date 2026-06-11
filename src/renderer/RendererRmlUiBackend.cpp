#include "renderer/RendererRmlUiBackend.h"

#include "platform/Log.h"
#include "renderer/RendererTypes.h"

#include <stb_image.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace dolbuto
{
    namespace
    {
        constexpr size_t MaxUiVertices = 262144;
        constexpr size_t MaxUiIndices = 393216;

        bool isItemTexturePath(const std::filesystem::path& path)
        {
            const std::string normalized = path.generic_string();
            return normalized.find("/textures/item/") != std::string::npos;
        }

        UiVertex transformUiVertex(const UiVertex& vertex, const Rml::Matrix4f& transform, Rml::Vector2f translation)
        {
            UiVertex transformed = vertex;
            const Rml::Vector4f position(vertex.x + translation.x, vertex.y + translation.y, 0.0f, 1.0f);
            const Rml::Vector4f transformedPosition = transform * position;
            transformed.x = transformedPosition.x;
            transformed.y = transformedPosition.y;
            return transformed;
        }
    }

    RendererRmlUiBackend::RendererRmlUiBackend(
        RendererVulkanState& vulkan,
        VulkanResourceManager& gpuResources,
        RendererAssetStore& assets,
        std::filesystem::path assetDirectory) :
        vulkan_(vulkan),
        gpuResources_(gpuResources),
        assets_(assets),
        assetDirectory_(std::move(assetDirectory))
    {
    }

    Rml::RenderInterface* RendererRmlUiBackend::renderInterface()
    {
        return this;
    }

    void RendererRmlUiBackend::beginFrame(VkCommandBuffer commandBuffer)
    {
        vulkan_.rmlUiVertexOffset = 0;
        vulkan_.rmlUiIndexOffset = 0;
        vulkan_.rmlCommandBuffer = commandBuffer;
    }

    void RendererRmlUiBackend::endFrame()
    {
        vulkan_.rmlCommandBuffer = VK_NULL_HANDLE;
    }

    Rml::CompiledGeometryHandle RendererRmlUiBackend::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
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

    void RendererRmlUiBackend::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle textureHandle)
    {
        if (vulkan_.rmlCommandBuffer == VK_NULL_HANDLE || vulkan_.uiPipeline == VK_NULL_HANDLE || handle == 0)
        {
            return;
        }

        const auto* geometry = reinterpret_cast<const UiGeometry*>(handle);
        if (geometry->vertices.empty() || geometry->indices.empty() ||
            vulkan_.rmlUiVertexOffset + geometry->vertices.size() > MaxUiVertices ||
            vulkan_.rmlUiIndexOffset + geometry->indices.size() > MaxUiIndices)
        {
            return;
        }

        std::vector<UiVertex> transformedVertices;
        const std::vector<UiVertex>* vertexSource = &geometry->vertices;
        Rml::Vector2f pushTranslation = translation;
        if (transformEnabled_)
        {
            transformedVertices.reserve(geometry->vertices.size());
            for (const UiVertex& vertex : geometry->vertices)
            {
                transformedVertices.push_back(transformUiVertex(vertex, transform_, translation));
            }
            vertexSource = &transformedVertices;
            pushTranslation = Rml::Vector2f(0.0f, 0.0f);
        }

        const VkDeviceSize vertexBytes = sizeof(UiVertex) * vertexSource->size();
        const VkDeviceSize vertexBufferOffset = sizeof(UiVertex) * vulkan_.rmlUiVertexOffset;
        void* vertexData = nullptr;
        vkMapMemory(vulkan_.device, vulkan_.uiVertexMemory, vertexBufferOffset, vertexBytes, 0, &vertexData);
        std::memcpy(vertexData, vertexSource->data(), static_cast<size_t>(vertexBytes));
        vkUnmapMemory(vulkan_.device, vulkan_.uiVertexMemory);

        const VkDeviceSize indexBytes = sizeof(uint32_t) * geometry->indices.size();
        const VkDeviceSize indexBufferOffset = sizeof(uint32_t) * vulkan_.rmlUiIndexOffset;
        void* indexData = nullptr;
        vkMapMemory(vulkan_.device, vulkan_.uiIndexMemory, indexBufferOffset, indexBytes, 0, &indexData);
        std::memcpy(indexData, geometry->indices.data(), static_cast<size_t>(indexBytes));
        vkUnmapMemory(vulkan_.device, vulkan_.uiIndexMemory);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(vulkan_.swapchainExtent.width);
        viewport.height = static_cast<float>(vulkan_.swapchainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = vulkan_.swapchainExtent;
        if (vulkan_.rmlScissorEnabled)
        {
            scissor = vulkan_.rmlScissor;
        }

        const Texture* texture = textureHandle == 0 ? &assets_.white : reinterpret_cast<const Texture*>(textureHandle);
        const UiPush push{
            static_cast<float>(vulkan_.swapchainExtent.width),
            static_cast<float>(vulkan_.swapchainExtent.height),
            pushTranslation.x,
            pushTranslation.y
        };

        vkCmdBindPipeline(vulkan_.rmlCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.uiPipeline);
        vkCmdSetViewport(vulkan_.rmlCommandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(vulkan_.rmlCommandBuffer, 0, 1, &scissor);
        vkCmdBindVertexBuffers(vulkan_.rmlCommandBuffer, 0, 1, &vulkan_.uiVertexBuffer, &vertexBufferOffset);
        vkCmdBindIndexBuffer(vulkan_.rmlCommandBuffer, vulkan_.uiIndexBuffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(vulkan_.rmlCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_.uiPipelineLayout, 0, 1, &texture->descriptorSet, 0, nullptr);
        vkCmdPushConstants(vulkan_.rmlCommandBuffer, vulkan_.uiPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(UiPush), &push);
        vkCmdDrawIndexed(vulkan_.rmlCommandBuffer, static_cast<uint32_t>(geometry->indices.size()), 1, 0, 0, 0);

        vulkan_.rmlUiVertexOffset += geometry->vertices.size();
        vulkan_.rmlUiIndexOffset += geometry->indices.size();
    }

    void RendererRmlUiBackend::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
    {
        delete reinterpret_cast<UiGeometry*>(geometry);
    }

    Rml::TextureHandle RendererRmlUiBackend::LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source)
    {
        std::filesystem::path texturePath(source);
        if (texturePath.is_relative())
        {
            texturePath = (assetDirectory_ / "ui" / texturePath).lexically_normal();
        }
        if (!std::filesystem::exists(texturePath))
        {
            std::string normalized = texturePath.generic_string();
            const std::string marker = "/textures/";
            const size_t markerPosition = normalized.find(marker);
            if (markerPosition != std::string::npos)
            {
                const std::string textureTail = normalized.substr(markerPosition + marker.size());
                const std::filesystem::path remappedPath = assetDirectory_ / "textures" / std::filesystem::path(textureTail);
                if (std::filesystem::exists(remappedPath))
                {
                    texturePath = remappedPath;
                }
            }
        }
        if (!std::filesystem::exists(texturePath) && isItemTexturePath(texturePath))
        {
            const std::filesystem::path defaultItemTexturePath = assetDirectory_ / "textures" / "item" / "default.png";
            if (std::filesystem::exists(defaultItemTexturePath))
            {
                texturePath = defaultItemTexturePath;
            }
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(texturePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr && isItemTexturePath(texturePath))
        {
            const std::filesystem::path defaultItemTexturePath = assetDirectory_ / "textures" / "item" / "default.png";
            if (texturePath != defaultItemTexturePath && std::filesystem::exists(defaultItemTexturePath))
            {
                texturePath = defaultItemTexturePath;
                pixels = stbi_load(texturePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
            }
        }
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

    Rml::TextureHandle RendererRmlUiBackend::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions)
    {
        Texture texture = gpuResources_.createTextureFromRgba(
            reinterpret_cast<const unsigned char*>(source.data()),
            sourceDimensions.x,
            sourceDimensions.y,
            VK_FORMAT_R8G8B8A8_UNORM);
        return reinterpret_cast<Rml::TextureHandle>(new Texture(texture));
    }

    void RendererRmlUiBackend::ReleaseTexture(Rml::TextureHandle textureHandle)
    {
        if (textureHandle == 0)
        {
            return;
        }

        auto* texture = reinterpret_cast<Texture*>(textureHandle);
        gpuResources_.destroyTexture(*texture);
        delete texture;
    }

    void RendererRmlUiBackend::EnableScissorRegion(bool enable)
    {
        vulkan_.rmlScissorEnabled = enable;
    }

    void RendererRmlUiBackend::SetScissorRegion(Rml::Rectanglei region)
    {
        const int left = std::max(region.Left(), 0);
        const int top = std::max(region.Top(), 0);
        const int right = std::min(region.Right(), static_cast<int>(vulkan_.swapchainExtent.width));
        const int bottom = std::min(region.Bottom(), static_cast<int>(vulkan_.swapchainExtent.height));

        vulkan_.rmlScissor.offset = {left, top};
        vulkan_.rmlScissor.extent = {
            static_cast<uint32_t>(std::max(right - left, 0)),
            static_cast<uint32_t>(std::max(bottom - top, 0))
        };
    }

    void RendererRmlUiBackend::SetTransform(const Rml::Matrix4f* transform)
    {
        const Rml::Matrix4f& identity = Rml::Matrix4f::Identity();
        if (transform == nullptr || *transform == identity)
        {
            transform_ = identity;
            transformEnabled_ = false;
            return;
        }

        transform_ = *transform;
        transformEnabled_ = true;
    }
}
