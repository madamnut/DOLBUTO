#include "renderer/PlayerMeshRenderPath.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace dolbuto
{
    namespace
    {
        std::vector<char> readFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open())
            {
                throw std::runtime_error("Failed to open file: " + path.string());
            }

            const auto size = static_cast<std::size_t>(file.tellg());
            std::vector<char> buffer(size);
            file.seekg(0);
            file.read(buffer.data(), static_cast<std::streamsize>(size));
            return buffer;
        }
    }

    PlayerMeshRenderPath::PlayerMeshRenderPath(const VkDevice* device, VulkanResourceManager* gpuResources)
    {
        setHandles(device, gpuResources);
    }

    void PlayerMeshRenderPath::setHandles(const VkDevice* device, VulkanResourceManager* gpuResources)
    {
        device_ = device;
        gpuResources_ = gpuResources;
    }

    VkDevice PlayerMeshRenderPath::device() const
    {
        return device_ != nullptr ? *device_ : VK_NULL_HANDLE;
    }

    VulkanResourceManager& PlayerMeshRenderPath::gpuResources() const
    {
        if (gpuResources_ == nullptr)
        {
            throw std::runtime_error("PlayerMeshRenderPath GPU resources are not configured.");
        }
        return *gpuResources_;
    }

    void PlayerMeshRenderPath::loadFromFile(const std::filesystem::path& path)
    {
        const std::vector<char> meshData = readFile(path);
        if (meshData.size() < 12 || std::memcmp(meshData.data(), "PMSH", 4) != 0)
        {
            throw std::runtime_error("Invalid player mesh file.");
        }

        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        std::memcpy(&vertexCount, meshData.data() + 4, sizeof(vertexCount));
        std::memcpy(&indexCount, meshData.data() + 8, sizeof(indexCount));

        const std::size_t verticesOffset = 12;
        constexpr std::size_t LegacyPlayerVertexSize = sizeof(float) * 5;
        constexpr std::size_t AoPlayerVertexSize = sizeof(float) * 6;
        constexpr std::size_t LayerPlayerVertexSize = sizeof(float) * 7;
        const std::size_t currentIndicesOffset = verticesOffset + static_cast<std::size_t>(vertexCount) * sizeof(TerrainVertex);
        const std::size_t currentExpectedSize = currentIndicesOffset + static_cast<std::size_t>(indexCount) * sizeof(uint32_t);
        const std::size_t layerIndicesOffset = verticesOffset + static_cast<std::size_t>(vertexCount) * LayerPlayerVertexSize;
        const std::size_t layerExpectedSize = layerIndicesOffset + static_cast<std::size_t>(indexCount) * sizeof(uint32_t);
        const std::size_t aoIndicesOffset = verticesOffset + static_cast<std::size_t>(vertexCount) * AoPlayerVertexSize;
        const std::size_t aoExpectedSize = aoIndicesOffset + static_cast<std::size_t>(indexCount) * sizeof(uint32_t);
        const bool hasCurrentVertexData = meshData.size() >= currentExpectedSize;
        const bool hasLayerVertexData = !hasCurrentVertexData && meshData.size() >= layerExpectedSize;
        const bool hasAoVertexData = !hasCurrentVertexData && !hasLayerVertexData && meshData.size() >= aoExpectedSize;
        const std::size_t vertexStride = hasCurrentVertexData ? sizeof(TerrainVertex) : (hasLayerVertexData ? LayerPlayerVertexSize : (hasAoVertexData ? AoPlayerVertexSize : LegacyPlayerVertexSize));
        const std::size_t indicesOffset = verticesOffset + static_cast<std::size_t>(vertexCount) * vertexStride;
        const std::size_t expectedSize = indicesOffset + static_cast<std::size_t>(indexCount) * sizeof(uint32_t);
        if (meshData.size() < expectedSize)
        {
            throw std::runtime_error("Incomplete player mesh file.");
        }

        static_assert(sizeof(TerrainVertex) == sizeof(float) * 8);
        std::vector<TerrainVertex> sourceVertices(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            const char* source = meshData.data() + verticesOffset + static_cast<std::size_t>(i) * vertexStride;
            if (hasCurrentVertexData)
            {
                std::memcpy(&sourceVertices[i], source, sizeof(TerrainVertex));
            }
            else if (hasLayerVertexData)
            {
                std::memcpy(&sourceVertices[i].x, source, LayerPlayerVertexSize);
                sourceVertices[i].mipDistanceScale = 1.0f;
            }
            else if (hasAoVertexData)
            {
                std::memcpy(&sourceVertices[i].x, source, AoPlayerVertexSize);
                sourceVertices[i].textureLayer = 0.0f;
                sourceVertices[i].mipDistanceScale = 1.0f;
            }
            else
            {
                std::memcpy(&sourceVertices[i].x, source, LegacyPlayerVertexSize);
                sourceVertices[i].ao = 1.0f;
                sourceVertices[i].textureLayer = 0.0f;
                sourceVertices[i].mipDistanceScale = 1.0f;
            }
        }

        std::vector<uint32_t> sourceIndices;
        sourceIndices.reserve(indexCount);
        for (uint32_t i = 0; i < indexCount; ++i)
        {
            uint32_t index = 0;
            std::memcpy(&index, meshData.data() + indicesOffset + static_cast<std::size_t>(i) * sizeof(uint32_t), sizeof(index));
            if (index >= vertexCount)
            {
                throw std::runtime_error("Invalid player mesh index.");
            }
            sourceIndices.push_back(index);
        }

        destroy();
        localVertices_ = std::move(sourceVertices);
        indices_ = std::move(sourceIndices);
        mesh_.vertexCount = static_cast<uint32_t>(localVertices_.size());
        mesh_.indexCount = static_cast<uint32_t>(indices_.size());

        const VkDeviceSize vertexBufferSize = sizeof(TerrainVertex) * localVertices_.size();
        const VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices_.size();
        gpuResources().createBuffer(
            vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            mesh_.vertexBuffer,
            mesh_.vertexMemory);

        void* data = nullptr;
        vkMapMemory(device(), mesh_.vertexMemory, 0, vertexBufferSize, 0, &data);
        std::memcpy(data, localVertices_.data(), static_cast<std::size_t>(vertexBufferSize));
        vkUnmapMemory(device(), mesh_.vertexMemory);

        gpuResources().createBuffer(
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            mesh_.indexBuffer,
            mesh_.indexMemory);

        vkMapMemory(device(), mesh_.indexMemory, 0, indexBufferSize, 0, &data);
        std::memcpy(data, indices_.data(), static_cast<std::size_t>(indexBufferSize));
        vkUnmapMemory(device(), mesh_.indexMemory);
    }

    void PlayerMeshRenderPath::update(Vec3 playerPosition, float playerYaw)
    {
        if (!ready())
        {
            return;
        }

        const Vec3 forward{std::cos(playerYaw), 0.0f, std::sin(playerYaw)};
        const Vec3 right{std::sin(playerYaw), 0.0f, -std::cos(playerYaw)};
        const VkDeviceSize size = sizeof(TerrainVertex) * localVertices_.size();
        void* data = nullptr;
        vkMapMemory(device(), mesh_.vertexMemory, 0, size, 0, &data);
        auto* vertices = static_cast<TerrainVertex*>(data);
        for (std::size_t i = 0; i < localVertices_.size(); ++i)
        {
            const TerrainVertex& local = localVertices_[i];
            TerrainVertex vertex = local;
            vertex.x = playerPosition.x + local.x * right.x - local.z * forward.x;
            vertex.y = playerPosition.y + local.y;
            vertex.z = playerPosition.z + local.x * right.z - local.z * forward.z;
            vertices[i] = vertex;
        }
        vkUnmapMemory(device(), mesh_.vertexMemory);
    }

    void PlayerMeshRenderPath::draw(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const Texture& texture) const
    {
        if (!ready())
        {
            return;
        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout, 0, 1, &texture.descriptorSet, 0, nullptr);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh_.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, mesh_.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, mesh_.indexCount, 1, 0, 0, 0);
    }

    void PlayerMeshRenderPath::destroy()
    {
        const VkDevice logicalDevice = device();
        if (mesh_.vertexDescriptorSet != VK_NULL_HANDLE)
        {
            mesh_.vertexDescriptorSet = VK_NULL_HANDLE;
        }
        if (mesh_.vertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(logicalDevice, mesh_.vertexBuffer, nullptr);
        }
        if (mesh_.vertexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(logicalDevice, mesh_.vertexMemory, nullptr);
        }
        if (mesh_.indexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(logicalDevice, mesh_.indexBuffer, nullptr);
        }
        if (mesh_.indexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(logicalDevice, mesh_.indexMemory, nullptr);
        }
        mesh_ = {};
        localVertices_.clear();
        indices_.clear();
    }

    bool PlayerMeshRenderPath::ready() const
    {
        return mesh_.indexCount > 0 && mesh_.vertexBuffer != VK_NULL_HANDLE && mesh_.indexBuffer != VK_NULL_HANDLE;
    }
}
