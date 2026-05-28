#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace dolbuto
{
    struct TerrainVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float ao = 1.0f;
        float textureLayer = 0.0f;
        float mipDistanceScale = 1.0f;
        float alphaBlend = 1.0f;
        uint8_t packedLight = 0;
    };

    struct PackedTerrainQuad
    {
        uint32_t p0x = 0;
        uint32_t p0y = 0;
        uint32_t p0z = 0;
        uint32_t edgeUxy = 0;
        uint32_t edgeUzVx = 0;
        uint32_t edgeVyz = 0;
        uint32_t uv0 = 0;
        uint32_t uvU = 0;
        uint32_t uvV = 0;
        uint32_t material = 0;
        uint32_t light = 0;
    };

    struct TerrainMesh
    {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        VkDescriptorSet vertexDescriptorSet = VK_NULL_HANDLE;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
    };

    struct TerrainBuildData
    {
        std::vector<TerrainVertex> vertices;
        std::vector<uint32_t> indices;
    };

    struct TerrainSubchunkBuildData
    {
        TerrainBuildData solid;
        TerrainBuildData blend;
        TerrainBuildData fluid;
    };
}
