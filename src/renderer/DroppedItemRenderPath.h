#pragma once

#include "camera/Camera.h"
#include "renderer/RendererGpuResources.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace dolbuto
{
    class DroppedItemRenderPath
    {
    public:
        struct ItemSpriteQuad
        {
            std::array<Vec3, 4> positions{};
            std::array<std::array<float, 2>, 4> uvs{};
            float ao = 1.0f;
            float textureLayer = -1.0f;
        };

        struct ItemSpriteMesh
        {
            std::vector<ItemSpriteQuad> quads;
        };

        struct ItemLocalVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float u = 0.0f;
            float v = 0.0f;
            float ao = 1.0f;
            float textureLayer = -1.0f;
        };

        struct Instance
        {
            float centerX = 0.0f;
            float centerY = 0.0f;
            float centerZ = 0.0f;
            float rotationX = 0.0f;
            float rotationY = 0.0f;
            float rotationZ = 0.0f;
            float textureLayer = 0.0f;
            float mipDistanceScale = 1.0f;
            float scaleX = 1.0f;
            float scaleY = 1.0f;
            float scaleZ = 1.0f;
            float skyLight = 1.0f;
            float blockLight = 0.0f;
            float uvMirrorX = 0.0f;
            float geometryMirrorX = 0.0f;
        };

        struct RenderInstance
        {
            uint16_t itemId = 0;
            Instance instance{};
        };

        struct PushConstants
        {
            float mvp[16]{};
            float cameraPosition[4]{};
            float fluidWaterParams[4]{};
            float dynamicLightParams[4]{};
        };

        DroppedItemRenderPath() = default;
        DroppedItemRenderPath(const VkDevice* device, VulkanResourceManager* gpuResources);

        void setHandles(const VkDevice* device, VulkanResourceManager* gpuResources);

        void createBuffers(const std::vector<ItemSpriteMesh>& spriteMeshes);
        void destroy();
        bool ready() const;
        bool meshReady(uint16_t itemId) const;

        void draw(
            VkCommandBuffer commandBuffer,
            VkExtent2D extent,
            VkPipeline pipeline,
            VkPipelineLayout pipelineLayout,
            const Texture& texture,
            const PushConstants& push,
            std::vector<RenderInstance>& renderInstances,
            std::size_t instanceOffset = 0);

    private:
        struct ItemSpriteGpuMesh
        {
            uint32_t firstIndex = 0;
            uint32_t indexCount = 0;
        };

        VkDevice device() const;
        VulkanResourceManager& gpuResources() const;

        const VkDevice* device_ = nullptr;
        VulkanResourceManager* gpuResources_ = nullptr;
        VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
        VkBuffer indexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;
        VkBuffer instanceBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory instanceMemory_ = VK_NULL_HANDLE;
        void* instanceMapped_ = nullptr;
        std::vector<ItemSpriteGpuMesh> gpuMeshes_;
    };
}
