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
        bool hasMesh = false;
        std::array<float, 3> translation{0.0f, 0.0f, 0.0f};
        std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
        std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
        std::array<float, 16> localTransform{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};
    };

    enum class PlayerAnimationPath
    {
        Translation,
        Rotation,
        Scale
    };

    struct PlayerAnimationKeyframe
    {
        float time = 0.0f;
        std::array<float, 4> value{0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct PlayerAnimationChannel
    {
        int nodeIndex = -1;
        PlayerAnimationPath path = PlayerAnimationPath::Translation;
        std::vector<PlayerAnimationKeyframe> keyframes;
    };

    struct PlayerAnimationClip
    {
        std::string name;
        float duration = 0.0f;
        std::vector<PlayerAnimationChannel> channels;
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
        std::vector<PlayerAnimationClip> animations;
    };

    PlayerModelData loadPlayerModelFromGlb(const std::filesystem::path& path);
}
