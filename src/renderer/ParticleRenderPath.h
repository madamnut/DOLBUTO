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
            float dynamicLightParams[4]{};
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

        using TerrainCollisionFn = std::function<bool(DVec3, DVec3)>;
        using LightSamplerFn = std::function<uint8_t(int, int, int)>;

        ParticleRenderPath() = default;
        ParticleRenderPath(const VkDevice* device, VulkanResourceManager* gpuResources);

        void setHandles(const VkDevice* device, VulkanResourceManager* gpuResources);

        void createBuffers();
        void destroy();
        void clear(double lastUpdateTime = 0.0);

        void registerFireEmitter(int x, int y, int z);
        void unregisterFireEmitter(int x, int y, int z);
        void removeFireEmittersForChunk(int chunkX, int chunkZ);
        void setFireEmitterSmokeStyle(int x, int y, int z, float multiplier, uint32_t textureSet);
        void handleBlockChanged(int x, int y, int z, uint16_t previousBlock, uint16_t nextBlock, uint16_t fireBlock);
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
            const Texture& smokeTexture,
            const PushConstants& push,
            const BreakingOverlay& overlay,
            double now,
            const TerrainCollisionFn& terrainBlocks,
            const LightSamplerFn& lightAtWorld);

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

        struct SmokeParticle
        {
            Vec3 position{};
            Vec3 velocity{};
            float age = 0.0f;
            float lifetime = 0.0f;
            float size = 0.0f;
            uint32_t textureSet = 0;
        };

        struct FireEmitter
        {
            int x = 0;
            int y = 0;
            int z = 0;
            float spawnTimer = 0.0f;
            float smokeMultiplier = 1.0f;
            uint32_t smokeTextureSet = 0;
            uint32_t randomState = 0;
        };

        VkDevice device() const;
        VulkanResourceManager& gpuResources() const;
        void trimForAdditional(std::size_t count);
        void trimSmokeForAdditional(std::size_t count);
        void spawnSmoke(FireEmitter& emitter);

        const VkDevice* device_ = nullptr;
        VulkanResourceManager* gpuResources_ = nullptr;
        VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
        void* vertexMapped_ = nullptr;
        VkBuffer indexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;
        VkBuffer smokeVertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory smokeVertexMemory_ = VK_NULL_HANDLE;
        void* smokeVertexMapped_ = nullptr;
        VkBuffer smokeIndexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory smokeIndexMemory_ = VK_NULL_HANDLE;
        std::vector<TerrainVertex> vertexScratch_;
        std::vector<TerrainVertex> smokeVertexScratch_;
        std::vector<BlockBreakParticle> particles_;
        std::vector<SmokeParticle> smokeParticles_;
        std::vector<FireEmitter> fireEmitters_;
        double lastUpdateTime_ = 0.0;
    };
}
