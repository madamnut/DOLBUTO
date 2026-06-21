#pragma once

#include "world/BlockData.h"

#include <filesystem>
#include <string>
#include <vector>

namespace dolbuto::assets
{
    struct PropMesh;

    bool writeBlockItemIcon(
        const std::filesystem::path& blockTextureDirectory,
        const std::vector<std::string>& blockTextureNames,
        const BlockTextureLayers& layers,
        BlockRenderType renderType,
        float modelWidth,
        float modelHeight,
        float modelDepth,
        bool useVerticalSection,
        const std::filesystem::path& outputPath,
        const PropMesh* propMesh = nullptr);
}
