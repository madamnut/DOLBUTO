#pragma once

#include "renderer/DroppedItemRenderPath.h"

#include <filesystem>

namespace dolbuto
{
    class ItemSpriteMeshBuilder
    {
    public:
        static DroppedItemRenderPath::ItemSpriteMesh build(const std::filesystem::path& path);
    };
}
