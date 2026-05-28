#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "camera/Camera.h"

namespace dolbuto::assets
{
    inline constexpr size_t PropQuadRenderFloatCount = 4u * 3u + 4u * 2u;

    struct PropMesh
    {
        std::vector<float> quads;
        Vec3 boundsMin{};
        Vec3 boundsMax{};
        bool hasBounds = false;
    };

    void ensurePropModelBinary(const std::filesystem::path& modelDirectory, const std::string& modelName);
    PropMesh loadDpmRenderMesh(const std::filesystem::path& dpmPath);
}
