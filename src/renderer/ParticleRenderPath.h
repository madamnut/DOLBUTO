#pragma once

#include "camera/Camera.h"
#include "renderer/RendererGpuResources.h"
#include "renderer/TerrainTypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <vulkan/vulkan.h>

namespace dolbuto
{
    class ParticleRenderPath
    {
    public:
        struct PushConstants
        {
            float mvp[16]{};
            float cameraPosition[4]{};
            float fluidWaterParams[4]{};
        };

        struct BreakingOverlay
        {
            bool active = false;
            int x = 0;
            int y = 0;
            int z = 0;
            float progress = 0.0f;
            const uint32_t* textureLayers = nullptr;
            std::size_t textureLayerCount = 0;
        };

        struct MiningHit
        {
            int blockX = 0;
            int blockY = 0;
            int blockZ = 0;
            int previousBlockX = 0;
            int previousBlockY = 0;
            int previousBlockZ = 0;
        };

        using TerrainCollisionFn = std::function<bool(int, int, int)>;

        ParticleRenderPath() = default;
        ParticleRenderPath(const VkDevice* device, VulkanResourceManager* gpuResources);

        void setHandles(const VkDevice* device, VulkanResourceManager* gpuResources);

        void createBuffers();
        void destroy();
        void clear(double lastUpdateTime = 0.0);

        void spawnBlockBreak(int x, int y, int z, uint16_t block, uint32_t textureLayer);
        void spawnMiningParticle(const MiningHit& hit, uint32_t textureLayer);
        void update(double now, const TerrainCollisionFn& terrainBlocks);
        void draw(
            VkCommandBuffer commandBuffer,
            const Camera& camera,
            VkExtent2D extent,
            VkPipeline pipeline,
            VkPipelineLayout pipelineLayout,
            const Texture& terrainTexture,
            const PushConstants& push,
            const BreakingOverlay& overlay,
            double now,
            const TerrainCollisionFn& terrainBlocks);

        bool empty() const;

    private:
        struct BlockBreakParticle
        {
            Vec3 position{};
            Vec3 velocity{};
            float age = 0.0f;
            float lifetime = 0.0f;
            float size = 0.0f;
            uint32_t textureLayer = 0;
            float u0 = 0.0f;
            float v0 = 0.0f;
            float u1 = 1.0f;
            float v1 = 1.0f;
        };

        VkDevice device() const;
        VulkanResourceManager& gpuResources() const;
        void trimForAdditional(std::size_t count);

        const VkDevice* device_ = nullptr;
        VulkanResourceManager* gpuResources_ = nullptr;
        VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
        VkBuffer indexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;
        std::vector<BlockBreakParticle> particles_;
        double lastUpdateTime_ = 0.0;
    };
}
