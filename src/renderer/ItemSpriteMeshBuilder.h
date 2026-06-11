#pragma once

#include "renderer/DroppedItemRenderPath.h"

#include <filesystem>

namespace dolbuto
{
    class ItemSpriteMeshBuilder
    {
    public:
        static DroppedItemRenderPath::ItemSpriteMesh build(const std::filesystem::path& path);
        static DroppedItemRenderPath::ItemSpriteMesh buildLayered(
            const std::filesystem::path& bottomPath,
            uint32_t bottomTextureLayer,
            const std::filesystem::path& topPath,
            uint32_t topTextureLayer);
        static DroppedItemRenderPath::ItemSpriteMesh buildBlockMold(
            const std::filesystem::path& bottomPath,
            uint32_t bottomTextureLayer,
            const std::filesystem::path& topPath,
            uint32_t topTextureLayer);
        static DroppedItemRenderPath::ItemSpriteMesh buildMoldCavitySurface(const std::filesystem::path& topPath);
    };
}
