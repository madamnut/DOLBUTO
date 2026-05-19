#include "renderer/Renderer.h"

#include "platform/RuntimePaths.h"

#include <cstddef>
#include <cstdint>

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
        playerMeshRenderPath_.loadFromGlb(assetDirectory() / "textures" / "character" / "Character.glb");
    }
}
