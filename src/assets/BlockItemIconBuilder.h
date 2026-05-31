#pragma once

#include "world/BlockData.h"

#include <filesystem>
#include <string>
#include <vector>

namespace dolbuto::assets
{
    bool writeBlockItemIcon(
        const std::filesystem::path& blockTextureDirectory,
        const std::vector<std::string>& blockTextureNames,
        const BlockTextureLayers& layers,
        BlockRenderType renderType,
        const std::filesystem::path& outputPath);
}
