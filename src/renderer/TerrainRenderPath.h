#pragma once

#include "renderer/RendererGpuResources.h"
#include "renderer/TerrainTypes.h"
#include "world/WorldTypes.h"

#include <array>
#include <cstdint>
#include <deque>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <vulkan/vulkan.h>

namespace dolbuto
{
    class TerrainRenderPath
    {
    public:
        struct ChunkRenderData
        {
            uint64_t revision = 0;
            int chunkX = 0;
            int chunkZ = 0;
            double fadeStartSeconds = -1.0;
            std::array<TerrainMesh, SubchunkCount> solidSubchunks;
            std::array<TerrainMesh, SubchunkCount> blendSubchunks;
            std::array<TerrainMesh, SubchunkCount> fluidSubchunks;
        };

        struct RetiredChunkRenderData
        {
            uint32_t framesLeft = 0;
            ChunkRenderData chunk;
        };

        struct Stats
        {
            uint32_t drawCount = 0;
            uint32_t faceCount = 0;
            uint32_t vertexCount = 0;
        };

        enum class ChunkFadeSelection
        {
            All,
            Complete,
            Active
        };

        struct View
        {
            Vec3 cameraPosition{};
            Vec3 frustumPosition{};
            Vec3 frustumRight{};
            Vec3 frustumUp{};
            Vec3 frustumForward{};
            float tanHalfVertical = 1.0f;
            float tanHalfHorizontal = 1.0f;
            float nearPlane = 0.1f;
            float farPlane = 1.0f;
        };

        TerrainRenderPath() = default;
        TerrainRenderPath(
            const VkDevice* device,
            const VkDescriptorPool* descriptorPool,
            const VkDescriptorSetLayout* terrainVertexDescriptorSetLayout,
            VulkanResourceManager* gpuResources);

        void setHandles(
            const VkDevice* device,
            const VkDescriptorPool* descriptorPool,
            const VkDescriptorSetLayout* terrainVertexDescriptorSetLayout,
            VulkanResourceManager* gpuResources);

        const Stats& stats() const;

        bool empty() const;
        void reserve(std::size_t capacity);
        bool chunkMeshReady(uint64_t key, const RuntimeChunk* runtimeChunk) const;
        bool retireAndErase(uint64_t key, uint32_t framesLeft);
        uint32_t retireChunksNotIn(const std::unordered_set<uint64_t>& desiredKeys, uint32_t framesLeft);
        void installCompletedMesh(uint64_t key, const CompletedChunkMesh& mesh, uint32_t framesLeft);
        void replaceEditedSubchunk(uint64_t key, int chunkX, int chunkZ, uint64_t revision, int subchunkY, const TerrainSubchunkBuildData& buildData, uint32_t framesLeft);
        uint32_t processRetired(uint32_t maxDestroy);
        void destroyAll();
        Stats rebuildStats();
        void setVisibleStats(uint32_t drawCount, uint32_t faceCount, uint32_t vertexCount);

        void createTerrainBuffer(const TerrainBuildData& buildData, TerrainMesh& mesh, bool deviceLocal = true);
        void destroyTerrainMesh(TerrainMesh& mesh);
        Stats drawSolid(
            VkCommandBuffer commandBuffer,
            VkPipelineLayout terrainPipelineLayout,
            const View& view,
            double fadeNowSeconds,
            ChunkFadeSelection fadeSelection) const;
        Stats drawBlend(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const View& view, double fadeNowSeconds) const;
        Stats drawFluids(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const View& view, double fadeNowSeconds) const;
        Stats drawShadow(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout) const;

    private:
        struct StagingBuffer
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkDeviceSize size = 0;
        };

        struct TerrainVertexBuffer
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkDeviceSize size = 0;
        };

        struct PendingTerrainUpload
        {
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkFence fence = VK_NULL_HANDLE;
            StagingBuffer staging;
            std::vector<VkBuffer> destinationBuffers;
        };

        VkDevice device() const;
        VkDescriptorPool descriptorPool() const;
        VkDescriptorSetLayout terrainVertexDescriptorSetLayout() const;
        VulkanResourceManager& gpuResources() const;

        PackedTerrainQuad packTerrainQuad(const TerrainVertex& a, const TerrainVertex& b, const TerrainVertex& c, const TerrainVertex& d) const;
        std::vector<PackedTerrainQuad> buildPackedTerrainQuads(const TerrainBuildData& buildData) const;
        void retireChunk(ChunkRenderData&& chunk, uint32_t framesLeft);
        void createChunkTerrainBuffersBatch(const CompletedChunkMesh& buildData, ChunkRenderData& renderData);
        void createTerrainVertexDescriptorSet(TerrainMesh& mesh, VkDeviceSize vertexBufferSize);
        void destroyChunkRenderData(ChunkRenderData& chunk);
        StagingBuffer acquireStagingBuffer(VkDeviceSize size);
        void releaseStagingBuffer(StagingBuffer&& staging);
        void destroyStagingBuffer(StagingBuffer& staging);
        void destroyStagingPool();
        void acquireTerrainVertexBuffer(VkDeviceSize size, TerrainMesh& mesh);
        void releaseTerrainVertexBuffer(TerrainMesh& mesh);
        void destroyTerrainVertexBuffer(TerrainVertexBuffer& buffer);
        void destroyTerrainVertexBufferPool();
        VkDescriptorSet acquireTerrainVertexDescriptorSet();
        void releaseTerrainVertexDescriptorSet(TerrainMesh& mesh);
        void destroyTerrainVertexDescriptorSetPool();
        void enqueueTerrainUpload(VkCommandBuffer commandBuffer, StagingBuffer staging, std::vector<VkBuffer> destinationBuffers);
        void processCompletedUploads();
        void waitForPendingUploads();
        void destroyUploadResources(PendingTerrainUpload& upload);
        bool meshUploadPending(const TerrainMesh& mesh) const;
        bool chunkUploadPending(const ChunkRenderData& chunk) const;
        float chunkFadeProgress(const ChunkRenderData& chunk, double nowSeconds) const;
        void pushChunkFade(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, float fade) const;
        bool subchunkVisible(const ChunkRenderData& chunk, std::size_t subchunkY, float subchunkHeight, const View& view) const;
        void drawTerrainMeshBound(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const TerrainMesh& mesh) const;

        const VkDevice* device_ = nullptr;
        const VkDescriptorPool* descriptorPool_ = nullptr;
        const VkDescriptorSetLayout* terrainVertexDescriptorSetLayout_ = nullptr;
        VulkanResourceManager* gpuResources_ = nullptr;
        std::unordered_map<uint64_t, ChunkRenderData> chunks_;
        std::deque<RetiredChunkRenderData> retiredChunks_;
        std::deque<PendingTerrainUpload> pendingUploads_;
        std::vector<StagingBuffer> freeStagingBuffers_;
        std::vector<TerrainVertexBuffer> freeTerrainVertexBuffers_;
        std::vector<VkDescriptorSet> freeTerrainVertexDescriptorSets_;
        Stats stats_;
    };
}
