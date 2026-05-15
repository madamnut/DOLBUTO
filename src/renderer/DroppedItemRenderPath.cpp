#include "renderer/DroppedItemRenderPath.h"

#include "world/DroppedItemSystem.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace dolbuto
{
    namespace
    {
        constexpr std::size_t MaxDroppedItemRenderInstances = world::DroppedItemSystem::MaxDroppedItemRenderInstances;
    }

    DroppedItemRenderPath::DroppedItemRenderPath(const VkDevice* device, VulkanResourceManager* gpuResources)
        : device_(device),
        gpuResources_(gpuResources)
    {
    }

    void DroppedItemRenderPath::setHandles(const VkDevice* device, VulkanResourceManager* gpuResources)
    {
        device_ = device;
        gpuResources_ = gpuResources;
    }

    VkDevice DroppedItemRenderPath::device() const
    {
        if (device_ == nullptr || *device_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("DroppedItemRenderPath device handle is not initialized.");
        }
        return *device_;
    }

    VulkanResourceManager& DroppedItemRenderPath::gpuResources() const
    {
        if (gpuResources_ == nullptr)
        {
            throw std::runtime_error("DroppedItemRenderPath GPU resource manager is not initialized.");
        }
        return *gpuResources_;
    }

    void DroppedItemRenderPath::createBuffers(const std::vector<ItemSpriteMesh>& spriteMeshes)
    {
        if (instanceBuffer_ != VK_NULL_HANDLE)
        {
            return;
        }

        std::vector<ItemLocalVertex> itemVertices;
        std::vector<uint32_t> itemIndices;
        gpuMeshes_.assign(spriteMeshes.size(), {});
        for (std::size_t itemId = 0; itemId < spriteMeshes.size(); ++itemId)
        {
            const ItemSpriteMesh& mesh = spriteMeshes[itemId];
            if (mesh.quads.empty())
            {
                continue;
            }

            ItemSpriteGpuMesh gpuMesh{};
            gpuMesh.firstIndex = static_cast<uint32_t>(itemIndices.size());
            for (const ItemSpriteQuad& quad : mesh.quads)
            {
                const uint32_t baseVertex = static_cast<uint32_t>(itemVertices.size());
                for (std::size_t vertexIndex = 0; vertexIndex < quad.positions.size(); ++vertexIndex)
                {
                    const Vec3& position = quad.positions[vertexIndex];
                    itemVertices.push_back(ItemLocalVertex{
                        position.x,
                        position.y,
                        position.z,
                        quad.uvs[vertexIndex][0],
                        quad.uvs[vertexIndex][1],
                        quad.ao
                    });
                }

                itemIndices.push_back(baseVertex);
                itemIndices.push_back(baseVertex + 1u);
                itemIndices.push_back(baseVertex + 2u);
                itemIndices.push_back(baseVertex);
                itemIndices.push_back(baseVertex + 2u);
                itemIndices.push_back(baseVertex + 3u);
            }
            gpuMesh.indexCount = static_cast<uint32_t>(itemIndices.size() - gpuMesh.firstIndex);
            gpuMeshes_[itemId] = gpuMesh;
        }

        if (!itemVertices.empty() && !itemIndices.empty())
        {
            gpuResources().createBuffer(
                sizeof(ItemLocalVertex) * itemVertices.size(),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                vertexBuffer_,
                vertexMemory_);
            gpuResources().createBuffer(
                sizeof(uint32_t) * itemIndices.size(),
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                indexBuffer_,
                indexMemory_);

            void* vertexData = nullptr;
            vkMapMemory(device(), vertexMemory_, 0, sizeof(ItemLocalVertex) * itemVertices.size(), 0, &vertexData);
            std::memcpy(vertexData, itemVertices.data(), sizeof(ItemLocalVertex) * itemVertices.size());
            vkUnmapMemory(device(), vertexMemory_);

            void* indexData = nullptr;
            vkMapMemory(device(), indexMemory_, 0, sizeof(uint32_t) * itemIndices.size(), 0, &indexData);
            std::memcpy(indexData, itemIndices.data(), sizeof(uint32_t) * itemIndices.size());
            vkUnmapMemory(device(), indexMemory_);
        }

        gpuResources().createBuffer(
            sizeof(Instance) * MaxDroppedItemRenderInstances,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            instanceBuffer_,
            instanceMemory_);
        if (vkMapMemory(device(), instanceMemory_, 0, sizeof(Instance) * MaxDroppedItemRenderInstances, 0, &instanceMapped_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to map dropped item instance buffer.");
        }
    }

    void DroppedItemRenderPath::destroy()
    {
        if (device_ == nullptr || *device_ == VK_NULL_HANDLE)
        {
            vertexBuffer_ = VK_NULL_HANDLE;
            vertexMemory_ = VK_NULL_HANDLE;
            indexBuffer_ = VK_NULL_HANDLE;
            indexMemory_ = VK_NULL_HANDLE;
            instanceBuffer_ = VK_NULL_HANDLE;
            instanceMemory_ = VK_NULL_HANDLE;
            instanceMapped_ = nullptr;
            gpuMeshes_.clear();
            return;
        }

        const VkDevice currentDevice = *device_;
        if (vertexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(currentDevice, vertexBuffer_, nullptr);
            vertexBuffer_ = VK_NULL_HANDLE;
        }
        if (vertexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(currentDevice, vertexMemory_, nullptr);
            vertexMemory_ = VK_NULL_HANDLE;
        }
        if (indexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(currentDevice, indexBuffer_, nullptr);
            indexBuffer_ = VK_NULL_HANDLE;
        }
        if (indexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(currentDevice, indexMemory_, nullptr);
            indexMemory_ = VK_NULL_HANDLE;
        }
        if (instanceMapped_ != nullptr)
        {
            vkUnmapMemory(currentDevice, instanceMemory_);
            instanceMapped_ = nullptr;
        }
        if (instanceBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(currentDevice, instanceBuffer_, nullptr);
            instanceBuffer_ = VK_NULL_HANDLE;
        }
        if (instanceMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(currentDevice, instanceMemory_, nullptr);
            instanceMemory_ = VK_NULL_HANDLE;
        }
        gpuMeshes_.clear();
    }

    bool DroppedItemRenderPath::ready() const
    {
        return vertexBuffer_ != VK_NULL_HANDLE &&
            indexBuffer_ != VK_NULL_HANDLE &&
            instanceBuffer_ != VK_NULL_HANDLE &&
            instanceMapped_ != nullptr;
    }

    bool DroppedItemRenderPath::meshReady(uint16_t itemId) const
    {
        return static_cast<std::size_t>(itemId) < gpuMeshes_.size() &&
            gpuMeshes_[itemId].indexCount != 0;
    }

    void DroppedItemRenderPath::draw(
        VkCommandBuffer commandBuffer,
        VkExtent2D extent,
        VkPipeline pipeline,
        VkPipelineLayout pipelineLayout,
        const Texture& texture,
        const PushConstants& push,
        std::vector<RenderInstance>& renderInstances)
    {
        if (renderInstances.empty() ||
            !ready() ||
            pipeline == VK_NULL_HANDLE ||
            pipelineLayout == VK_NULL_HANDLE ||
            texture.descriptorSet == VK_NULL_HANDLE)
        {
            return;
        }

        std::sort(renderInstances.begin(), renderInstances.end(), [](const RenderInstance& lhs, const RenderInstance& rhs)
        {
            return lhs.itemId < rhs.itemId;
        });

        auto* mappedInstances = static_cast<Instance*>(instanceMapped_);
        for (std::size_t i = 0; i < renderInstances.size(); ++i)
        {
            mappedInstances[i] = renderInstances[i].instance;
        }

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = extent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &texture.descriptorSet, 0, nullptr);

        const std::array<VkBuffer, 2> vertexBuffers = {vertexBuffer_, instanceBuffer_};
        const std::array<VkDeviceSize, 2> vertexOffsets = {0, 0};
        vkCmdBindVertexBuffers(commandBuffer, 0, static_cast<uint32_t>(vertexBuffers.size()), vertexBuffers.data(), vertexOffsets.data());
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);

        std::size_t batchStart = 0;
        while (batchStart < renderInstances.size())
        {
            const uint16_t itemId = renderInstances[batchStart].itemId;
            std::size_t batchEnd = batchStart + 1u;
            while (batchEnd < renderInstances.size() && renderInstances[batchEnd].itemId == itemId)
            {
                ++batchEnd;
            }

            if (meshReady(itemId))
            {
                const ItemSpriteGpuMesh& mesh = gpuMeshes_[itemId];
                vkCmdDrawIndexed(
                    commandBuffer,
                    mesh.indexCount,
                    static_cast<uint32_t>(batchEnd - batchStart),
                    mesh.firstIndex,
                    0,
                    static_cast<uint32_t>(batchStart));
            }
            batchStart = batchEnd;
        }
    }
}
