#pragma once

#include "camera/Camera.h"
#include "renderer/RendererGpuResources.h"
#include "renderer/TerrainTypes.h"

#include <filesystem>
#include <vector>

#include <vulkan/vulkan.h>

namespace dolbuto
{
    class PlayerMeshRenderPath
    {
    public:
        PlayerMeshRenderPath() = default;
        PlayerMeshRenderPath(const VkDevice* device, VulkanResourceManager* gpuResources);

        void setHandles(const VkDevice* device, VulkanResourceManager* gpuResources);

        void loadFromFile(const std::filesystem::path& path);
        void update(Vec3 playerPosition, float playerYaw);
        void draw(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const Texture& texture) const;
        void destroy();
        bool ready() const;

    private:
        VkDevice device() const;
        VulkanResourceManager& gpuResources() const;

        const VkDevice* device_ = nullptr;
        VulkanResourceManager* gpuResources_ = nullptr;
        TerrainMesh mesh_{};
        std::vector<TerrainVertex> localVertices_;
        std::vector<uint32_t> indices_;
    };
}
