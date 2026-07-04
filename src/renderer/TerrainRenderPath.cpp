#include "renderer/TerrainRenderPath.h"

#include "renderer/RendererTypes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace dolbuto
{
    namespace
    {
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeZ = 16;
        constexpr int SubchunkSize = 16;
        constexpr float TerrainPositionPackScale = 256.0f;
        constexpr float TerrainUvPackScale = 256.0f;
        constexpr double TerrainChunkFadeDurationSeconds = 0.5;

        double steadySeconds()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    TerrainRenderPath::TerrainRenderPath(
        const VkDevice* device,
        const VkDescriptorPool* descriptorPool,
        const VkDescriptorSetLayout* terrainVertexDescriptorSetLayout,
        VulkanResourceManager* gpuResources)
    {
        setHandles(device, descriptorPool, terrainVertexDescriptorSetLayout, gpuResources);
    }

    void TerrainRenderPath::setHandles(
        const VkDevice* device,
        const VkDescriptorPool* descriptorPool,
        const VkDescriptorSetLayout* terrainVertexDescriptorSetLayout,
        VulkanResourceManager* gpuResources)
    {
        device_ = device;
        descriptorPool_ = descriptorPool;
        terrainVertexDescriptorSetLayout_ = terrainVertexDescriptorSetLayout;
        gpuResources_ = gpuResources;
    }

    VkDevice TerrainRenderPath::device() const
    {
        return device_ != nullptr ? *device_ : VK_NULL_HANDLE;
    }

    VkDescriptorPool TerrainRenderPath::descriptorPool() const
    {
        return descriptorPool_ != nullptr ? *descriptorPool_ : VK_NULL_HANDLE;
    }

    VkDescriptorSetLayout TerrainRenderPath::terrainVertexDescriptorSetLayout() const
    {
        return terrainVertexDescriptorSetLayout_ != nullptr ? *terrainVertexDescriptorSetLayout_ : VK_NULL_HANDLE;
    }

    VulkanResourceManager& TerrainRenderPath::gpuResources() const
    {
        if (gpuResources_ == nullptr)
        {
            throw std::runtime_error("TerrainRenderPath GPU resources are not configured.");
        }
        return *gpuResources_;
    }

    const TerrainRenderPath::Stats& TerrainRenderPath::stats() const
    {
        return stats_;
    }

    bool TerrainRenderPath::empty() const
    {
        return chunks_.empty();
    }

    void TerrainRenderPath::reserve(std::size_t capacity)
    {
        chunks_.reserve(capacity);
    }

    TerrainRenderPath::StagingBuffer TerrainRenderPath::acquireStagingBuffer(VkDeviceSize size)
    {
        std::size_t bestIndex = freeStagingBuffers_.size();
        VkDeviceSize bestSize = 0;
        for (std::size_t i = 0; i < freeStagingBuffers_.size(); ++i)
        {
            const VkDeviceSize candidateSize = freeStagingBuffers_[i].size;
            if (candidateSize < size)
            {
                continue;
            }
            if (bestIndex == freeStagingBuffers_.size() || candidateSize < bestSize)
            {
                bestIndex = i;
                bestSize = candidateSize;
            }
        }

        if (bestIndex != freeStagingBuffers_.size())
        {
            StagingBuffer staging = freeStagingBuffers_[bestIndex];
            freeStagingBuffers_.erase(freeStagingBuffers_.begin() + static_cast<std::ptrdiff_t>(bestIndex));
            return staging;
        }

        StagingBuffer staging{};
        staging.size = size;
        gpuResources().createBuffer(
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging.buffer,
            staging.memory);
        return staging;
    }

    void TerrainRenderPath::releaseStagingBuffer(StagingBuffer&& staging)
    {
        if (staging.buffer == VK_NULL_HANDLE || staging.memory == VK_NULL_HANDLE || staging.size == 0)
        {
            return;
        }
        freeStagingBuffers_.push_back(staging);
        staging = {};
    }

    void TerrainRenderPath::destroyStagingBuffer(StagingBuffer& staging)
    {
        const VkDevice logicalDevice = device();
        if (staging.buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(logicalDevice, staging.buffer, nullptr);
            staging.buffer = VK_NULL_HANDLE;
        }
        if (staging.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(logicalDevice, staging.memory, nullptr);
            staging.memory = VK_NULL_HANDLE;
        }
        staging.size = 0;
    }

    void TerrainRenderPath::destroyStagingPool()
    {
        for (StagingBuffer& staging : freeStagingBuffers_)
        {
            destroyStagingBuffer(staging);
        }
        freeStagingBuffers_.clear();
    }

    void TerrainRenderPath::acquireTerrainVertexBuffer(VkDeviceSize size, TerrainMesh& mesh)
    {
        std::size_t bestIndex = freeTerrainVertexBuffers_.size();
        VkDeviceSize bestSize = 0;
        for (std::size_t i = 0; i < freeTerrainVertexBuffers_.size(); ++i)
        {
            const VkDeviceSize candidateSize = freeTerrainVertexBuffers_[i].size;
            if (candidateSize < size)
            {
                continue;
            }
            if (bestIndex == freeTerrainVertexBuffers_.size() || candidateSize < bestSize)
            {
                bestIndex = i;
                bestSize = candidateSize;
            }
        }

        if (bestIndex != freeTerrainVertexBuffers_.size())
        {
            TerrainVertexBuffer buffer = freeTerrainVertexBuffers_[bestIndex];
            freeTerrainVertexBuffers_.erase(freeTerrainVertexBuffers_.begin() + static_cast<std::ptrdiff_t>(bestIndex));
            mesh.vertexBuffer = buffer.buffer;
            mesh.vertexMemory = buffer.memory;
            mesh.vertexBufferSize = buffer.size;
            return;
        }

        gpuResources().createBuffer(
            size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            mesh.vertexBuffer,
            mesh.vertexMemory);
        mesh.vertexBufferSize = size;
    }

    void TerrainRenderPath::releaseTerrainVertexBuffer(TerrainMesh& mesh)
    {
        if (mesh.vertexBuffer == VK_NULL_HANDLE || mesh.vertexMemory == VK_NULL_HANDLE || mesh.vertexBufferSize == 0)
        {
            return;
        }
        TerrainVertexBuffer buffer{};
        buffer.buffer = mesh.vertexBuffer;
        buffer.memory = mesh.vertexMemory;
        buffer.size = mesh.vertexBufferSize;
        freeTerrainVertexBuffers_.push_back(buffer);
        mesh.vertexBuffer = VK_NULL_HANDLE;
        mesh.vertexMemory = VK_NULL_HANDLE;
        mesh.vertexBufferSize = 0;
    }

    void TerrainRenderPath::destroyTerrainVertexBuffer(TerrainVertexBuffer& buffer)
    {
        const VkDevice logicalDevice = device();
        if (buffer.buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(logicalDevice, buffer.buffer, nullptr);
            buffer.buffer = VK_NULL_HANDLE;
        }
        if (buffer.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(logicalDevice, buffer.memory, nullptr);
            buffer.memory = VK_NULL_HANDLE;
        }
        buffer.size = 0;
    }

    void TerrainRenderPath::destroyTerrainVertexBufferPool()
    {
        for (TerrainVertexBuffer& buffer : freeTerrainVertexBuffers_)
        {
            destroyTerrainVertexBuffer(buffer);
        }
        freeTerrainVertexBuffers_.clear();
    }

    VkDescriptorSet TerrainRenderPath::acquireTerrainVertexDescriptorSet()
    {
        if (!freeTerrainVertexDescriptorSets_.empty())
        {
            const VkDescriptorSet descriptorSet = freeTerrainVertexDescriptorSets_.back();
            freeTerrainVertexDescriptorSets_.pop_back();
            return descriptorSet;
        }

        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool();
        setInfo.descriptorSetCount = 1;
        const VkDescriptorSetLayout layout = terrainVertexDescriptorSetLayout();
        setInfo.pSetLayouts = &layout;

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device(), &setInfo, &descriptorSet) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate terrain vertex descriptor set.");
        }
        return descriptorSet;
    }

    void TerrainRenderPath::releaseTerrainVertexDescriptorSet(TerrainMesh& mesh)
    {
        if (mesh.vertexDescriptorSet == VK_NULL_HANDLE)
        {
            return;
        }
        freeTerrainVertexDescriptorSets_.push_back(mesh.vertexDescriptorSet);
        mesh.vertexDescriptorSet = VK_NULL_HANDLE;
    }

    void TerrainRenderPath::destroyTerrainVertexDescriptorSetPool()
    {
        if (!freeTerrainVertexDescriptorSets_.empty() && descriptorPool() != VK_NULL_HANDLE)
        {
            vkFreeDescriptorSets(
                device(),
                descriptorPool(),
                static_cast<uint32_t>(freeTerrainVertexDescriptorSets_.size()),
                freeTerrainVertexDescriptorSets_.data());
        }
        freeTerrainVertexDescriptorSets_.clear();
    }

    void TerrainRenderPath::enqueueTerrainUpload(
        VkCommandBuffer commandBuffer,
        StagingBuffer staging,
        std::vector<VkBuffer> destinationBuffers)
    {
        PendingTerrainUpload upload{};
        upload.commandBuffer = commandBuffer;
        upload.staging = staging;
        upload.destinationBuffers = std::move(destinationBuffers);
        try
        {
            upload.fence = gpuResources().submitSingleTimeCommandsAsync(commandBuffer);
        }
        catch (...)
        {
            releaseStagingBuffer(std::move(staging));
            gpuResources().freeSingleTimeCommandBuffer(commandBuffer);
            throw;
        }
        pendingUploads_.push_back(std::move(upload));
    }

    void TerrainRenderPath::destroyUploadResources(PendingTerrainUpload& upload)
    {
        const VkDevice logicalDevice = device();
        releaseStagingBuffer(std::move(upload.staging));
        if (upload.fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(logicalDevice, upload.fence, nullptr);
            upload.fence = VK_NULL_HANDLE;
        }
        gpuResources().freeSingleTimeCommandBuffer(upload.commandBuffer);
        upload.commandBuffer = VK_NULL_HANDLE;
        upload.destinationBuffers.clear();
    }

    void TerrainRenderPath::processCompletedUploads()
    {
        for (auto it = pendingUploads_.begin(); it != pendingUploads_.end();)
        {
            const VkResult status = vkGetFenceStatus(device(), it->fence);
            if (status == VK_NOT_READY)
            {
                ++it;
                continue;
            }
            if (status != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to query terrain upload fence.");
            }

            destroyUploadResources(*it);
            it = pendingUploads_.erase(it);
        }
    }

    void TerrainRenderPath::waitForPendingUploads()
    {
        for (PendingTerrainUpload& upload : pendingUploads_)
        {
            if (upload.fence != VK_NULL_HANDLE)
            {
                vkWaitForFences(device(), 1, &upload.fence, VK_TRUE, UINT64_MAX);
            }
            destroyUploadResources(upload);
        }
        pendingUploads_.clear();
    }

    bool TerrainRenderPath::meshUploadPending(const TerrainMesh& mesh) const
    {
        if (mesh.vertexBuffer == VK_NULL_HANDLE)
        {
            return false;
        }
        for (const PendingTerrainUpload& upload : pendingUploads_)
        {
            if (std::find(upload.destinationBuffers.begin(), upload.destinationBuffers.end(), mesh.vertexBuffer) != upload.destinationBuffers.end())
            {
                return true;
            }
        }
        return false;
    }

    bool TerrainRenderPath::chunkUploadPending(const ChunkRenderData& chunk) const
    {
        for (const TerrainMesh& mesh : chunk.solidSubchunks)
        {
            if (meshUploadPending(mesh))
            {
                return true;
            }
        }
        for (const TerrainMesh& mesh : chunk.blendSubchunks)
        {
            if (meshUploadPending(mesh))
            {
                return true;
            }
        }
        for (const TerrainMesh& mesh : chunk.fluidSubchunks)
        {
            if (meshUploadPending(mesh))
            {
                return true;
            }
        }
        return false;
    }

    bool TerrainRenderPath::chunkMeshReady(uint64_t key, const RuntimeChunk* runtimeChunk) const
    {
        auto renderIt = chunks_.find(key);
        if (renderIt == chunks_.end())
        {
            return false;
        }
        if (runtimeChunk == nullptr || !runtimeChunk->data || renderIt->second.revision != runtimeChunk->data->revision)
        {
            return false;
        }

        for (const TerrainMesh& mesh : renderIt->second.solidSubchunks)
        {
            if (mesh.indexCount > 0)
            {
                return true;
            }
        }
        for (const TerrainMesh& mesh : renderIt->second.blendSubchunks)
        {
            if (mesh.indexCount > 0)
            {
                return true;
            }
        }
        for (const TerrainMesh& mesh : renderIt->second.fluidSubchunks)
        {
            if (mesh.indexCount > 0)
            {
                return true;
            }
        }
        return false;
    }

    void TerrainRenderPath::retireChunk(ChunkRenderData&& chunk, uint32_t framesLeft)
    {
        retiredChunks_.push_back(RetiredChunkRenderData{framesLeft, std::move(chunk)});
    }

    bool TerrainRenderPath::retireAndErase(uint64_t key, uint32_t framesLeft)
    {
        auto renderIt = chunks_.find(key);
        if (renderIt == chunks_.end())
        {
            return false;
        }

        retireChunk(std::move(renderIt->second), framesLeft);
        chunks_.erase(renderIt);
        return true;
    }

    uint32_t TerrainRenderPath::retireChunksNotIn(const std::unordered_set<uint64_t>& desiredKeys, uint32_t framesLeft)
    {
        uint32_t retiredCount = 0;
        for (auto it = chunks_.begin(); it != chunks_.end();)
        {
            if (desiredKeys.find(it->first) != desiredKeys.end())
            {
                ++it;
                continue;
            }

            retireChunk(std::move(it->second), framesLeft);
            it = chunks_.erase(it);
            ++retiredCount;
        }
        return retiredCount;
    }

    void TerrainRenderPath::installCompletedMesh(uint64_t key, const CompletedChunkMesh& mesh, uint32_t framesLeft)
    {
        ChunkRenderData& renderData = chunks_[key];
        retireChunk(std::move(renderData), framesLeft);
        renderData = {};
        renderData.revision = mesh.revision;
        renderData.chunkX = mesh.chunkX;
        renderData.chunkZ = mesh.chunkZ;
        renderData.fadeStartSeconds = steadySeconds();
        createChunkTerrainBuffersBatch(mesh, renderData);
    }

    void TerrainRenderPath::replaceEditedSubchunk(
        uint64_t key,
        int chunkX,
        int chunkZ,
        uint64_t revision,
        int subchunkY,
        const TerrainSubchunkBuildData& buildData,
        uint32_t framesLeft)
    {
        if (subchunkY < 0 || subchunkY >= static_cast<int>(SubchunkCount))
        {
            return;
        }

        ChunkRenderData& renderData = chunks_[key];
        renderData.revision = revision;
        renderData.chunkX = chunkX;
        renderData.chunkZ = chunkZ;

        TerrainMesh& targetSolidMesh = renderData.solidSubchunks[static_cast<std::size_t>(subchunkY)];
        TerrainMesh& targetBlendMesh = renderData.blendSubchunks[static_cast<std::size_t>(subchunkY)];
        TerrainMesh& targetFluidMesh = renderData.fluidSubchunks[static_cast<std::size_t>(subchunkY)];
        if (targetSolidMesh.vertexBuffer != VK_NULL_HANDLE || targetSolidMesh.indexBuffer != VK_NULL_HANDLE ||
            targetBlendMesh.vertexBuffer != VK_NULL_HANDLE || targetBlendMesh.indexBuffer != VK_NULL_HANDLE ||
            targetFluidMesh.vertexBuffer != VK_NULL_HANDLE || targetFluidMesh.indexBuffer != VK_NULL_HANDLE)
        {
            ChunkRenderData retired{};
            retired.solidSubchunks[static_cast<std::size_t>(subchunkY)] = std::move(targetSolidMesh);
            retired.blendSubchunks[static_cast<std::size_t>(subchunkY)] = std::move(targetBlendMesh);
            retired.fluidSubchunks[static_cast<std::size_t>(subchunkY)] = std::move(targetFluidMesh);
            targetSolidMesh = {};
            targetBlendMesh = {};
            targetFluidMesh = {};
            retireChunk(std::move(retired), framesLeft);
        }
        createTerrainBuffer(buildData.solid, targetSolidMesh);
        createTerrainBuffer(buildData.blend, targetBlendMesh);
        createTerrainBuffer(buildData.fluid, targetFluidMesh);
        renderData.revision = revision;
    }

    uint32_t TerrainRenderPath::processRetired(uint32_t maxDestroy)
    {
        processCompletedUploads();
        uint32_t destroyedCount = 0;
        for (auto it = retiredChunks_.begin(); it != retiredChunks_.end();)
        {
            if (it->framesLeft > 0)
            {
                --it->framesLeft;
                ++it;
                continue;
            }
            if (destroyedCount >= maxDestroy)
            {
                ++it;
                continue;
            }
            if (chunkUploadPending(it->chunk))
            {
                ++it;
                continue;
            }
            destroyChunkRenderData(it->chunk);
            it = retiredChunks_.erase(it);
            ++destroyedCount;
        }
        return destroyedCount;
    }

    void TerrainRenderPath::destroyAll()
    {
        waitForPendingUploads();
        destroyStagingPool();
        for (auto& entry : chunks_)
        {
            destroyChunkRenderData(entry.second);
        }
        for (RetiredChunkRenderData& retired : retiredChunks_)
        {
            destroyChunkRenderData(retired.chunk);
        }
        chunks_.clear();
        retiredChunks_.clear();
        destroyTerrainVertexBufferPool();
        destroyTerrainVertexDescriptorSetPool();
        stats_ = {};
    }

    TerrainRenderPath::Stats TerrainRenderPath::rebuildStats()
    {
        stats_ = {};
        for (const auto& entry : chunks_)
        {
            for (const TerrainMesh& mesh : entry.second.solidSubchunks)
            {
                if (mesh.indexCount == 0)
                {
                    continue;
                }

                ++stats_.drawCount;
                stats_.vertexCount += mesh.vertexCount;
                stats_.faceCount += mesh.indexCount / 6;
            }
            for (const TerrainMesh& mesh : entry.second.blendSubchunks)
            {
                if (mesh.indexCount == 0)
                {
                    continue;
                }

                ++stats_.drawCount;
                stats_.vertexCount += mesh.vertexCount;
                stats_.faceCount += mesh.indexCount / 6;
            }
            for (const TerrainMesh& mesh : entry.second.fluidSubchunks)
            {
                if (mesh.indexCount == 0)
                {
                    continue;
                }

                ++stats_.drawCount;
                stats_.vertexCount += mesh.vertexCount;
                stats_.faceCount += mesh.indexCount / 6;
            }
        }
        return stats_;
    }

    void TerrainRenderPath::setVisibleStats(uint32_t drawCount, uint32_t faceCount, uint32_t vertexCount)
    {
        stats_.drawCount = drawCount;
        stats_.faceCount = faceCount;
        stats_.vertexCount = vertexCount;
    }

    float TerrainRenderPath::chunkFadeProgress(const ChunkRenderData& chunk, double nowSeconds) const
    {
        if (chunk.fadeStartSeconds < 0.0)
        {
            return 1.0f;
        }
        const float progress = static_cast<float>((nowSeconds - chunk.fadeStartSeconds) / TerrainChunkFadeDurationSeconds);
        const float clamped = std::clamp(progress, 0.0f, 1.0f);
        return clamped * clamped * (3.0f - 2.0f * clamped);
    }

    void TerrainRenderPath::pushChunkFade(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, float fade) const
    {
        constexpr uint32_t FadeOffset = static_cast<uint32_t>(offsetof(TerrainPush, dynamicLightParams) + sizeof(float));
        vkCmdPushConstants(
            commandBuffer,
            terrainPipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            FadeOffset,
            sizeof(float),
            &fade);
    }

    bool TerrainRenderPath::subchunkVisible(const ChunkRenderData& chunk, std::size_t subchunkY, float subchunkHeight, const View& view) const
    {
        const float minX = static_cast<float>(chunk.chunkX * ChunkSizeX) - 0.5f - view.cameraPosition.x;
        const float maxX = static_cast<float>(chunk.chunkX * ChunkSizeX + ChunkSizeX) - 0.5f - view.cameraPosition.x;
        const float minY = static_cast<float>(subchunkY * SubchunkSize) - view.cameraPosition.y;
        const float maxY = minY + subchunkHeight;
        const float minZ = static_cast<float>(chunk.chunkZ * ChunkSizeZ) - 0.5f - view.cameraPosition.z;
        const float maxZ = static_cast<float>(chunk.chunkZ * ChunkSizeZ + ChunkSizeZ) - 0.5f - view.cameraPosition.z;

        const Vec3 center{
            (minX + maxX) * 0.5f,
            (minY + maxY) * 0.5f,
            (minZ + maxZ) * 0.5f
        };
        const Vec3 extent{
            (maxX - minX) * 0.5f,
            (maxY - minY) * 0.5f,
            (maxZ - minZ) * 0.5f
        };
        const Vec3 relative{
            center.x - view.frustumPosition.x,
            center.y - view.frustumPosition.y,
            center.z - view.frustumPosition.z
        };

        const float viewX = dot(relative, view.frustumRight);
        const float viewY = dot(relative, view.frustumUp);
        const float viewZ = dot(relative, view.frustumForward);
        const float radiusX =
            std::abs(view.frustumRight.x) * extent.x +
            std::abs(view.frustumRight.y) * extent.y +
            std::abs(view.frustumRight.z) * extent.z;
        const float radiusY =
            std::abs(view.frustumUp.x) * extent.x +
            std::abs(view.frustumUp.y) * extent.y +
            std::abs(view.frustumUp.z) * extent.z;
        const float radiusZ =
            std::abs(view.frustumForward.x) * extent.x +
            std::abs(view.frustumForward.y) * extent.y +
            std::abs(view.frustumForward.z) * extent.z;

        if (viewZ + radiusZ < view.nearPlane || viewZ - radiusZ > view.farPlane)
        {
            return false;
        }
        if (std::abs(viewX) > viewZ * view.tanHalfHorizontal + radiusX + radiusZ * view.tanHalfHorizontal)
        {
            return false;
        }
        if (std::abs(viewY) > viewZ * view.tanHalfVertical + radiusY + radiusZ * view.tanHalfVertical)
        {
            return false;
        }

        return true;
    }

    void TerrainRenderPath::drawTerrainMeshBound(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const TerrainMesh& mesh) const
    {
        if (mesh.indexCount == 0 || mesh.vertexDescriptorSet == VK_NULL_HANDLE)
        {
            return;
        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout, 1, 1, &mesh.vertexDescriptorSet, 0, nullptr);
        vkCmdDraw(commandBuffer, mesh.indexCount, 1, 0, 0);
    }

    TerrainRenderPath::Stats TerrainRenderPath::drawSolid(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout terrainPipelineLayout,
        const View& view,
        double fadeNowSeconds,
        ChunkFadeSelection fadeSelection) const
    {
        Stats visibleStats{};
        float boundFade = -1.0f;
        for (const auto& entry : chunks_)
        {
            const ChunkRenderData& chunk = entry.second;
            const float fade = chunkFadeProgress(chunk, fadeNowSeconds);
            if (fadeSelection == ChunkFadeSelection::Complete && fade < 1.0f)
            {
                continue;
            }
            if (fadeSelection == ChunkFadeSelection::Active && fade >= 1.0f)
            {
                continue;
            }
            bool chunkFadeBound = false;
            for (std::size_t subchunkY = 0; subchunkY < chunk.solidSubchunks.size(); ++subchunkY)
            {
                const TerrainMesh& mesh = chunk.solidSubchunks[subchunkY];
                if (mesh.indexCount == 0)
                {
                    continue;
                }
                if (!subchunkVisible(chunk, subchunkY, static_cast<float>(SubchunkSize), view))
                {
                    continue;
                }

                if (!chunkFadeBound)
                {
                    if (std::abs(fade - boundFade) > 0.0001f)
                    {
                        pushChunkFade(commandBuffer, terrainPipelineLayout, fade);
                        boundFade = fade;
                    }
                    chunkFadeBound = true;
                }

                drawTerrainMeshBound(commandBuffer, terrainPipelineLayout, mesh);
                ++visibleStats.drawCount;
                visibleStats.faceCount += mesh.indexCount / 6;
                visibleStats.vertexCount += mesh.vertexCount;
            }
        }
        return visibleStats;
    }

    TerrainRenderPath::Stats TerrainRenderPath::drawBlend(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout terrainPipelineLayout,
        const View& view,
        double fadeNowSeconds) const
    {
        Stats visibleStats{};
        float boundFade = -1.0f;
        for (const auto& entry : chunks_)
        {
            const ChunkRenderData& chunk = entry.second;
            const float fade = chunkFadeProgress(chunk, fadeNowSeconds);
            bool chunkFadeBound = false;
            for (std::size_t subchunkY = 0; subchunkY < chunk.blendSubchunks.size(); ++subchunkY)
            {
                const TerrainMesh& mesh = chunk.blendSubchunks[subchunkY];
                if (mesh.indexCount == 0)
                {
                    continue;
                }
                if (!subchunkVisible(chunk, subchunkY, static_cast<float>(SubchunkSize), view))
                {
                    continue;
                }

                if (!chunkFadeBound)
                {
                    if (std::abs(fade - boundFade) > 0.0001f)
                    {
                        pushChunkFade(commandBuffer, terrainPipelineLayout, fade);
                        boundFade = fade;
                    }
                    chunkFadeBound = true;
                }

                drawTerrainMeshBound(commandBuffer, terrainPipelineLayout, mesh);
                ++visibleStats.drawCount;
                visibleStats.faceCount += mesh.indexCount / 6;
                visibleStats.vertexCount += mesh.vertexCount;
            }
        }
        return visibleStats;
    }

    TerrainRenderPath::Stats TerrainRenderPath::drawShadow(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout) const
    {
        Stats visibleStats{};
        for (const auto& entry : chunks_)
        {
            const ChunkRenderData& chunk = entry.second;
            for (const TerrainMesh& mesh : chunk.solidSubchunks)
            {
                if (mesh.indexCount == 0)
                {
                    continue;
                }
                drawTerrainMeshBound(commandBuffer, terrainPipelineLayout, mesh);
                ++visibleStats.drawCount;
                visibleStats.faceCount += mesh.indexCount / 6;
                visibleStats.vertexCount += mesh.vertexCount;
            }
            for (const TerrainMesh& mesh : chunk.blendSubchunks)
            {
                if (mesh.indexCount == 0)
                {
                    continue;
                }
                drawTerrainMeshBound(commandBuffer, terrainPipelineLayout, mesh);
                ++visibleStats.drawCount;
                visibleStats.faceCount += mesh.indexCount / 6;
                visibleStats.vertexCount += mesh.vertexCount;
            }
        }
        return visibleStats;
    }

    TerrainRenderPath::Stats TerrainRenderPath::drawFluids(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout terrainPipelineLayout,
        const View& view,
        double fadeNowSeconds) const
    {
        Stats visibleStats{};
        float boundFade = -1.0f;
        for (const auto& entry : chunks_)
        {
            const ChunkRenderData& chunk = entry.second;
            const float fade = chunkFadeProgress(chunk, fadeNowSeconds);
            bool chunkFadeBound = false;
            for (std::size_t subchunkY = 0; subchunkY < chunk.fluidSubchunks.size(); ++subchunkY)
            {
                const TerrainMesh& mesh = chunk.fluidSubchunks[subchunkY];
                if (mesh.indexCount == 0)
                {
                    continue;
                }
                if (!subchunkVisible(chunk, subchunkY, static_cast<float>(SubchunkSize + 1), view))
                {
                    continue;
                }

                if (!chunkFadeBound)
                {
                    if (std::abs(fade - boundFade) > 0.0001f)
                    {
                        pushChunkFade(commandBuffer, terrainPipelineLayout, fade);
                        boundFade = fade;
                    }
                    chunkFadeBound = true;
                }

                drawTerrainMeshBound(commandBuffer, terrainPipelineLayout, mesh);
                ++visibleStats.drawCount;
                visibleStats.faceCount += mesh.indexCount / 6;
                visibleStats.vertexCount += mesh.vertexCount;
            }
        }
        return visibleStats;
    }

    PackedTerrainQuad TerrainRenderPath::packTerrainQuad(const TerrainVertex& a, const TerrainVertex& b, const TerrainVertex& c, const TerrainVertex& d) const
    {
        auto encodeSignedFixed = [](float value) -> uint32_t
        {
            const int32_t fixed = static_cast<int32_t>(std::lround(value * TerrainPositionPackScale));
            const uint32_t magnitude = fixed < 0 ? static_cast<uint32_t>(-fixed) : static_cast<uint32_t>(fixed);
            return (magnitude << 1u) | (fixed < 0 ? 1u : 0u);
        };
        auto quantizeUnsigned = [](float value, float scale, int maxValue) -> uint32_t
        {
            return static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(value * scale)), 0, maxValue));
        };
        auto quantizeSigned = [](float value, float scale) -> int32_t
        {
            return std::clamp(static_cast<int32_t>(std::lround(value * scale)), -32768, 32767);
        };
        auto packI16Pair = [](int32_t aValue, int32_t bValue) -> uint32_t
        {
            return (static_cast<uint32_t>(static_cast<uint16_t>(aValue)) & 0xFFFFu) |
                (static_cast<uint32_t>(static_cast<uint16_t>(bValue)) << 16u);
        };
        auto aoIndex = [](float ao) -> uint32_t
        {
            if (ao <= 0.615f)
            {
                return 0;
            }
            if (ao <= 0.75f)
            {
                return 1;
            }
            if (ao <= 0.91f)
            {
                return 2;
            }
            return 3;
        };

        const float edgeUx = b.x - a.x;
        const float edgeUy = b.y - a.y;
        const float edgeUz = b.z - a.z;
        const float edgeVx = d.x - a.x;
        const float edgeVy = d.y - a.y;
        const float edgeVz = d.z - a.z;

        PackedTerrainQuad packed{};
        packed.p0x = encodeSignedFixed(a.x);
        packed.p0y = encodeSignedFixed(a.y);
        packed.p0z = encodeSignedFixed(a.z);
        packed.edgeUxy = packI16Pair(quantizeSigned(edgeUx, TerrainPositionPackScale), quantizeSigned(edgeUy, TerrainPositionPackScale));
        packed.edgeUzVx = packI16Pair(quantizeSigned(edgeUz, TerrainPositionPackScale), quantizeSigned(edgeVx, TerrainPositionPackScale));
        packed.edgeVyz = packI16Pair(quantizeSigned(edgeVy, TerrainPositionPackScale), quantizeSigned(edgeVz, TerrainPositionPackScale));
        packed.uv0 = packI16Pair(quantizeSigned(a.u, TerrainUvPackScale), quantizeSigned(a.v, TerrainUvPackScale));
        packed.uvU = packI16Pair(quantizeSigned(b.u - a.u, TerrainUvPackScale), quantizeSigned(b.v - a.v, TerrainUvPackScale));
        packed.uvV = packI16Pair(quantizeSigned(d.u - a.u, TerrainUvPackScale), quantizeSigned(d.v - a.v, TerrainUvPackScale));
        const uint32_t textureLayer = quantizeUnsigned(a.textureLayer, 1.0f, 0xFF);
        const uint32_t mipDistanceScale = quantizeUnsigned(a.mipDistanceScale, 16.0f, 0x3FF);
        const uint32_t alphaBlend = quantizeUnsigned(a.alphaBlend, 63.0f, 0x3F);
        const uint32_t packedLight = static_cast<uint32_t>(a.packedLight);
        const uint32_t wavingType = static_cast<uint32_t>(a.wavingType) & 0x3u;
        packed.material = textureLayer |
            (mipDistanceScale << 8u) |
            (aoIndex(a.ao) << 18u) |
            (aoIndex(b.ao) << 20u) |
            (aoIndex(c.ao) << 22u) |
            (aoIndex(d.ao) << 24u) |
            (alphaBlend << 26u);
        packed.light = packedLight | (wavingType << 8u);
        return packed;
    }

    std::vector<PackedTerrainQuad> TerrainRenderPath::buildPackedTerrainQuads(const TerrainBuildData& buildData) const
    {
        std::vector<PackedTerrainQuad> quads;
        quads.reserve(buildData.vertices.size() / 4u);
        std::size_t indexCursor = 0;
        for (std::size_t base = 0; base + 3 < buildData.vertices.size(); base += 4)
        {
            const uint32_t baseIndex = static_cast<uint32_t>(base);
            std::size_t referencedIndices = 0;
            while (indexCursor + referencedIndices < buildData.indices.size())
            {
                const uint32_t index = buildData.indices[indexCursor + referencedIndices];
                if (index < baseIndex || index > baseIndex + 3u)
                {
                    break;
                }
                ++referencedIndices;
            }
            indexCursor += referencedIndices;
            if (referencedIndices == 0)
            {
                continue;
            }

            const TerrainVertex& a = buildData.vertices[base + 0u];
            const TerrainVertex& b = buildData.vertices[base + 1u];
            const TerrainVertex& c = buildData.vertices[base + 2u];
            const TerrainVertex& d = buildData.vertices[base + 3u];
            quads.push_back(packTerrainQuad(a, b, c, d));
            if (referencedIndices >= 12)
            {
                quads.push_back(packTerrainQuad(a, d, c, b));
            }
        }
        return quads;
    }

    void TerrainRenderPath::createTerrainBuffer(const TerrainBuildData& buildData, TerrainMesh& mesh, bool deviceLocal)
    {
        if (buildData.vertices.empty() || buildData.indices.empty())
        {
            return;
        }

        mesh.vertexCount = static_cast<uint32_t>(buildData.vertices.size());
        mesh.indexCount = static_cast<uint32_t>(buildData.indices.size());

        const VkDevice logicalDevice = device();
        if (!deviceLocal)
        {
            const VkDeviceSize vertexBufferSize = sizeof(TerrainVertex) * buildData.vertices.size();
            const VkDeviceSize indexBufferSize = sizeof(uint32_t) * buildData.indices.size();
            gpuResources().createBuffer(
                vertexBufferSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                mesh.vertexBuffer,
                mesh.vertexMemory);

            void* data = nullptr;
            vkMapMemory(logicalDevice, mesh.vertexMemory, 0, vertexBufferSize, 0, &data);
            std::memcpy(data, buildData.vertices.data(), static_cast<std::size_t>(vertexBufferSize));
            vkUnmapMemory(logicalDevice, mesh.vertexMemory);

            gpuResources().createBuffer(
                indexBufferSize,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                mesh.indexBuffer,
                mesh.indexMemory);

            vkMapMemory(logicalDevice, mesh.indexMemory, 0, indexBufferSize, 0, &data);
            std::memcpy(data, buildData.indices.data(), static_cast<std::size_t>(indexBufferSize));
            vkUnmapMemory(logicalDevice, mesh.indexMemory);
            mesh.vertexBufferSize = vertexBufferSize;
            return;
        }

        const std::vector<PackedTerrainQuad> packedQuads = buildPackedTerrainQuads(buildData);
        if (packedQuads.empty())
        {
            mesh = {};
            return;
        }
        mesh.vertexCount = static_cast<uint32_t>(packedQuads.size());
        mesh.indexCount = static_cast<uint32_t>(packedQuads.size() * 6u);

        const VkDeviceSize vertexBufferSize = sizeof(PackedTerrainQuad) * packedQuads.size();
        const VkDeviceSize stagingSize = vertexBufferSize;

        acquireTerrainVertexBuffer(vertexBufferSize, mesh);

        processCompletedUploads();
        StagingBuffer staging = acquireStagingBuffer(stagingSize);

        void* data = nullptr;
        vkMapMemory(logicalDevice, staging.memory, 0, stagingSize, 0, &data);
        std::memcpy(data, packedQuads.data(), static_cast<std::size_t>(vertexBufferSize));
        vkUnmapMemory(logicalDevice, staging.memory);

        VkCommandBuffer commandBuffer = gpuResources().beginSingleTimeCommands();

        VkBufferCopy vertexCopy{};
        vertexCopy.srcOffset = 0;
        vertexCopy.dstOffset = 0;
        vertexCopy.size = vertexBufferSize;
        vkCmdCopyBuffer(commandBuffer, staging.buffer, mesh.vertexBuffer, 1, &vertexCopy);

        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = mesh.vertexBuffer;
        barrier.size = vertexBufferSize;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            0,
            0,
            nullptr,
            1,
            &barrier,
            0,
            nullptr);

        enqueueTerrainUpload(commandBuffer, std::move(staging), std::vector<VkBuffer>{mesh.vertexBuffer});
        createTerrainVertexDescriptorSet(mesh, vertexBufferSize);
    }

    void TerrainRenderPath::createChunkTerrainBuffersBatch(const CompletedChunkMesh& buildData, ChunkRenderData& renderData)
    {
        struct PendingUpload
        {
            TerrainMesh* mesh = nullptr;
            std::vector<PackedTerrainQuad> quads;
            VkDeviceSize vertexSize = 0;
            VkDeviceSize vertexOffset = 0;
        };

        auto alignCopyOffset = [](VkDeviceSize value)
        {
            return (value + 3) & ~VkDeviceSize{3};
        };

        std::vector<PendingUpload> uploads;
        uploads.reserve(SubchunkCount * 3u);
        std::vector<VkBuffer> destinationBuffers;
        destinationBuffers.reserve(SubchunkCount * 3u);
        VkDeviceSize stagingSize = 0;

        auto appendUploads = [&](const std::array<TerrainBuildData, SubchunkCount>& sources, std::array<TerrainMesh, SubchunkCount>& meshes)
        {
            for (std::size_t subchunkY = 0; subchunkY < sources.size(); ++subchunkY)
            {
                const TerrainBuildData& source = sources[subchunkY];
                if (source.vertices.empty() || source.indices.empty())
                {
                    continue;
                }

                std::vector<PackedTerrainQuad> packedQuads = buildPackedTerrainQuads(source);
                if (packedQuads.empty())
                {
                    continue;
                }

                TerrainMesh& mesh = meshes[subchunkY];
                mesh.vertexCount = static_cast<uint32_t>(packedQuads.size());
                mesh.indexCount = static_cast<uint32_t>(packedQuads.size() * 6u);

                PendingUpload upload{};
                upload.mesh = &mesh;
                upload.vertexSize = sizeof(PackedTerrainQuad) * packedQuads.size();
                upload.vertexOffset = alignCopyOffset(stagingSize);
                upload.quads = std::move(packedQuads);
                stagingSize = upload.vertexOffset + upload.vertexSize;

                acquireTerrainVertexBuffer(upload.vertexSize, mesh);
                destinationBuffers.push_back(mesh.vertexBuffer);
                uploads.push_back(std::move(upload));
            }
        };

        appendUploads(buildData.solidSubchunks, renderData.solidSubchunks);
        appendUploads(buildData.blendSubchunks, renderData.blendSubchunks);
        appendUploads(buildData.fluidSubchunks, renderData.fluidSubchunks);

        if (uploads.empty())
        {
            return;
        }

        const VkDevice logicalDevice = device();
        processCompletedUploads();
        StagingBuffer staging = acquireStagingBuffer(stagingSize);

        void* data = nullptr;
        vkMapMemory(logicalDevice, staging.memory, 0, stagingSize, 0, &data);
        for (const PendingUpload& upload : uploads)
        {
            std::memcpy(static_cast<char*>(data) + upload.vertexOffset, upload.quads.data(), static_cast<std::size_t>(upload.vertexSize));
        }
        vkUnmapMemory(logicalDevice, staging.memory);

        VkCommandBuffer commandBuffer = gpuResources().beginSingleTimeCommands();
        std::vector<VkBufferMemoryBarrier> barriers;
        barriers.reserve(uploads.size());
        for (const PendingUpload& upload : uploads)
        {
            const TerrainMesh& mesh = *upload.mesh;

            VkBufferCopy vertexCopy{};
            vertexCopy.srcOffset = upload.vertexOffset;
            vertexCopy.dstOffset = 0;
            vertexCopy.size = upload.vertexSize;
            vkCmdCopyBuffer(commandBuffer, staging.buffer, mesh.vertexBuffer, 1, &vertexCopy);

            VkBufferMemoryBarrier vertexBarrier{};
            vertexBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            vertexBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vertexBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vertexBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vertexBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vertexBarrier.buffer = mesh.vertexBuffer;
            vertexBarrier.size = upload.vertexSize;
            barriers.push_back(vertexBarrier);
        }

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            0,
            0,
            nullptr,
            static_cast<uint32_t>(barriers.size()),
            barriers.data(),
            0,
            nullptr);

        enqueueTerrainUpload(commandBuffer, std::move(staging), std::move(destinationBuffers));

        for (const PendingUpload& upload : uploads)
        {
            createTerrainVertexDescriptorSet(*upload.mesh, upload.vertexSize);
        }
    }

    void TerrainRenderPath::createTerrainVertexDescriptorSet(TerrainMesh& mesh, VkDeviceSize vertexBufferSize)
    {
        if (mesh.vertexBuffer == VK_NULL_HANDLE || vertexBufferSize == 0)
        {
            return;
        }

        mesh.vertexDescriptorSet = acquireTerrainVertexDescriptorSet();

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mesh.vertexBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = vertexBufferSize;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = mesh.vertexDescriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device(), 1, &write, 0, nullptr);
    }

    void TerrainRenderPath::destroyTerrainMesh(TerrainMesh& mesh)
    {
        const VkDevice logicalDevice = device();
        releaseTerrainVertexDescriptorSet(mesh);
        if (mesh.vertexBuffer != VK_NULL_HANDLE
            && mesh.indexBuffer == VK_NULL_HANDLE
            && mesh.vertexMemory != VK_NULL_HANDLE
            && mesh.vertexBufferSize != 0)
        {
            releaseTerrainVertexBuffer(mesh);
        }
        else if (mesh.vertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(logicalDevice, mesh.vertexBuffer, nullptr);
        }
        if (mesh.vertexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(logicalDevice, mesh.vertexMemory, nullptr);
        }
        if (mesh.indexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(logicalDevice, mesh.indexBuffer, nullptr);
        }
        if (mesh.indexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(logicalDevice, mesh.indexMemory, nullptr);
        }
        mesh = {};
    }

    void TerrainRenderPath::destroyChunkRenderData(ChunkRenderData& chunk)
    {
        for (TerrainMesh& mesh : chunk.solidSubchunks)
        {
            destroyTerrainMesh(mesh);
        }
        for (TerrainMesh& mesh : chunk.blendSubchunks)
        {
            destroyTerrainMesh(mesh);
        }
        for (TerrainMesh& mesh : chunk.fluidSubchunks)
        {
            destroyTerrainMesh(mesh);
        }
    }
}
