#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace dolbuto::assets
{
    inline constexpr size_t PropQuadRenderFloatCount = 4u * 3u + 4u * 2u;

    struct PropMesh
    {
        std::vector<float> quads;
    };

    void ensurePropModelBinary(const std::filesystem::path& modelDirectory, const std::string& modelName);
    PropMesh loadDpmRenderMesh(const std::filesystem::path& dpmPath);
}
