#pragma once

#include "camera/Camera.h"
#include "config/ViewmodelConfig.h"
#include "renderer/PlayerModelLoader.h"
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

        void loadFromGlb(const std::filesystem::path& path);
        void update(Vec3 playerPosition, float playerYaw, float playerHeadYaw, float playerHeadPitch, float playerWalkPhase, float playerWalkAmount);
        void updateFirstPersonHand(const Camera& camera, Vec3 cameraPosition, const config::ViewmodelHandConfig& config);
        void draw(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const Texture& texture) const;
        void drawFirstPersonHand(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const Texture& texture) const;
        void destroy();
        bool ready() const;
        bool firstPersonHandReady() const;

    private:
        VkDevice device() const;
        VulkanResourceManager& gpuResources() const;
        void buildFirstPersonHandMesh();
        void createMeshBuffers(TerrainMesh& mesh, const std::vector<TerrainVertex>& vertices, const std::vector<uint32_t>& indices);
        void destroyMesh(TerrainMesh& mesh);

        const VkDevice* device_ = nullptr;
        VulkanResourceManager* gpuResources_ = nullptr;
        TerrainMesh mesh_{};
        TerrainMesh firstPersonHandMesh_{};
        std::vector<PlayerModelNode> nodes_;
        std::vector<PlayerModelVertex> sourceVertices_;
        std::vector<uint32_t> indices_;
        std::vector<PlayerModelVertex> firstPersonHandSourceVertices_;
        std::vector<uint32_t> firstPersonHandIndices_;
    };
}
