#pragma once

#include "renderer/TerrainTypes.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace dolbuto
{
    struct PlayerModelNode
    {
        std::string name;
        int parent = -1;
        std::vector<int> children;
        std::array<float, 16> localTransform{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct PlayerModelVertex
    {
        TerrainVertex vertex;
        int nodeIndex = -1;
    };

    struct PlayerModelData
    {
        std::vector<PlayerModelNode> nodes;
        std::vector<PlayerModelVertex> vertices;
        std::vector<uint32_t> indices;
    };

    PlayerModelData loadPlayerModelFromGlb(const std::filesystem::path& path);
}
