#include "renderer/Renderer.h"

#include "platform/RuntimePaths.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace dolbuto
{
    namespace
    {
        constexpr size_t MaxUiVertices = 262144;
        constexpr size_t MaxUiIndices = 393216;
    }

    void Renderer::createTextRenderPath()
    {
        textRenderPath_.loadFont(assetDirectory() / "fonts" / "VCR_OSD_MONO.ttf", rendererAssets_.font);
        textRenderPath_.createBuffers();
    }

    void Renderer::createUiBuffers()
    {
        gpuResources_.createBuffer(
            sizeof(UiVertex) * MaxUiVertices,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vulkan_.uiVertexBuffer,
            vulkan_.uiVertexMemory);
        gpuResources_.createBuffer(
            sizeof(uint32_t) * MaxUiIndices,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vulkan_.uiIndexBuffer,
            vulkan_.uiIndexMemory);
    }

    void Renderer::createRenderPathBuffers()
    {
        particleRenderPath_.createBuffers();
        droppedItemRenderPath_.createBuffers(rendererAssets_.itemSpriteMeshes);
        std::vector<DroppedItemRenderPath::ItemSpriteMesh> moltenMeshes = rendererAssets_.moltenSurfaceMeshes;
        if (moltenMeshes.size() < 2)
        {
            moltenMeshes.resize(2);
        }
        DroppedItemRenderPath::ItemSpriteQuad surface{};
        surface.positions = {{{-0.5f, 0.0f, -0.5f}, {-0.5f, 0.0f, 0.5f}, {0.5f, 0.0f, 0.5f}, {0.5f, 0.0f, -0.5f}}};
        surface.uvs = {{{{0.0f, 0.0f}}, {{0.0f, 1.0f}}, {{1.0f, 1.0f}}, {{1.0f, 0.0f}}}};
        surface.ao = 1.0f;
        surface.textureLayer = -1.0f;
        if (moltenMeshes[1].quads.empty())
        {
            moltenMeshes[1].quads.push_back(surface);
        }
        crucibleMoltenRenderPath_.createBuffers(moltenMeshes);
        radialMenuRenderPath_.createBuffers();
    }

    void Renderer::createSelectionLineBuffer()
    {
        constexpr VkDeviceSize BufferSize = sizeof(LineVertex) * 24u;
        gpuResources_.createBuffer(
            BufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vulkan_.selectionLineVertexBuffer,
            vulkan_.selectionLineVertexMemory);
    }

    void Renderer::createPlayerMesh()
    {
        const std::filesystem::path characterDirectory = assetDirectory() / "textures" / "character";
        playerMeshRenderPath_.loadFromGlb(characterDirectory / "Character.glb");
    }
}
