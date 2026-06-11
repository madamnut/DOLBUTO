#include "renderer/ItemSpriteMeshBuilder.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>

namespace dolbuto
{
    namespace
    {
        void applyLayerAndOffset(DroppedItemRenderPath::ItemSpriteMesh& mesh, uint32_t textureLayer, float yOffset)
        {
            for (DroppedItemRenderPath::ItemSpriteQuad& quad : mesh.quads)
            {
                quad.textureLayer = static_cast<float>(textureLayer);
                for (Vec3& position : quad.positions)
                {
                    position.y += yOffset;
                }
            }
        }

        void applyLayerAndYRange(DroppedItemRenderPath::ItemSpriteMesh& mesh, uint32_t textureLayer, float minY, float maxY)
        {
            const float height = maxY - minY;
            for (DroppedItemRenderPath::ItemSpriteQuad& quad : mesh.quads)
            {
                quad.textureLayer = static_cast<float>(textureLayer);
                for (Vec3& position : quad.positions)
                {
                    position.y = minY + (position.y + 0.5f) * height;
                }
            }
        }
    }

    DroppedItemRenderPath::ItemSpriteMesh ItemSpriteMeshBuilder::build(const std::filesystem::path& path)
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* loadedPixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (loadedPixels == nullptr)
        {
            throw std::runtime_error("Failed to load item sprite mesh texture: " + path.string());
        }

        DroppedItemRenderPath::ItemSpriteMesh mesh{};
        auto alphaAt = [&](int x, int y)
        {
            if (x < 0 || x >= width || y < 0 || y >= height)
            {
                return 0u;
            }
            const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u + 3u;
            return static_cast<unsigned int>(loadedPixels[index]);
        };
        auto opaqueAt = [&](int x, int y)
        {
            return alphaAt(x, y) >= 128u;
        };
        auto addQuad = [&](std::array<Vec3, 4> positions, std::array<std::array<float, 2>, 4> uvs, float ao)
        {
            DroppedItemRenderPath::ItemSpriteQuad quad{};
            quad.positions = positions;
            quad.uvs = uvs;
            quad.ao = ao;
            mesh.quads.push_back(quad);
        };

        addQuad(
            std::array<Vec3, 4>{Vec3{-0.5f, 0.5f, -0.5f}, Vec3{-0.5f, 0.5f, 0.5f}, Vec3{0.5f, 0.5f, 0.5f}, Vec3{0.5f, 0.5f, -0.5f}},
            std::array<std::array<float, 2>, 4>{{{0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}}},
            1.0f);
        addQuad(
            std::array<Vec3, 4>{Vec3{0.5f, -0.5f, -0.5f}, Vec3{0.5f, -0.5f, 0.5f}, Vec3{-0.5f, -0.5f, 0.5f}, Vec3{-0.5f, -0.5f, -0.5f}},
            std::array<std::array<float, 2>, 4>{{{1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}}},
            0.82f);

        const float invWidth = width > 0 ? 1.0f / static_cast<float>(width) : 1.0f;
        const float invHeight = height > 0 ? 1.0f / static_cast<float>(height) : 1.0f;
        auto addLeftSpan = [&](int x, int y0, int y1)
        {
            const float u = (static_cast<float>(x) + 0.5f) * invWidth;
            const float v0 = static_cast<float>(y0) * invHeight;
            const float v1 = static_cast<float>(y1) * invHeight;
            const float localX = static_cast<float>(x) * invWidth - 0.5f;
            const float localZ0 = 0.5f - v0;
            const float localZ1 = 0.5f - v1;
            addQuad(
                std::array<Vec3, 4>{Vec3{localX, 0.5f, localZ1}, Vec3{localX, -0.5f, localZ1}, Vec3{localX, -0.5f, localZ0}, Vec3{localX, 0.5f, localZ0}},
                std::array<std::array<float, 2>, 4>{{{u, v1}, {u, v1}, {u, v0}, {u, v0}}},
                0.72f);
        };
        auto addRightSpan = [&](int x, int y0, int y1)
        {
            const float u = (static_cast<float>(x) + 0.5f) * invWidth;
            const float v0 = static_cast<float>(y0) * invHeight;
            const float v1 = static_cast<float>(y1) * invHeight;
            const float localX = static_cast<float>(x + 1) * invWidth - 0.5f;
            const float localZ0 = 0.5f - v0;
            const float localZ1 = 0.5f - v1;
            addQuad(
                std::array<Vec3, 4>{Vec3{localX, 0.5f, localZ0}, Vec3{localX, -0.5f, localZ0}, Vec3{localX, -0.5f, localZ1}, Vec3{localX, 0.5f, localZ1}},
                std::array<std::array<float, 2>, 4>{{{u, v0}, {u, v0}, {u, v1}, {u, v1}}},
                0.72f);
        };
        auto addTopSpan = [&](int y, int x0, int x1)
        {
            const float u0 = static_cast<float>(x0) * invWidth;
            const float u1 = static_cast<float>(x1) * invWidth;
            const float v = (static_cast<float>(y) + 0.5f) * invHeight;
            const float localX0 = u0 - 0.5f;
            const float localX1 = u1 - 0.5f;
            const float localZ = 0.5f - static_cast<float>(y) * invHeight;
            addQuad(
                std::array<Vec3, 4>{Vec3{localX0, 0.5f, localZ}, Vec3{localX1, 0.5f, localZ}, Vec3{localX1, -0.5f, localZ}, Vec3{localX0, -0.5f, localZ}},
                std::array<std::array<float, 2>, 4>{{{u0, v}, {u1, v}, {u1, v}, {u0, v}}},
                0.76f);
        };
        auto addBottomSpan = [&](int y, int x0, int x1)
        {
            const float u0 = static_cast<float>(x0) * invWidth;
            const float u1 = static_cast<float>(x1) * invWidth;
            const float v = (static_cast<float>(y) + 0.5f) * invHeight;
            const float localX0 = u0 - 0.5f;
            const float localX1 = u1 - 0.5f;
            const float localZ = 0.5f - static_cast<float>(y + 1) * invHeight;
            addQuad(
                std::array<Vec3, 4>{Vec3{localX1, 0.5f, localZ}, Vec3{localX0, 0.5f, localZ}, Vec3{localX0, -0.5f, localZ}, Vec3{localX1, -0.5f, localZ}},
                std::array<std::array<float, 2>, 4>{{{u1, v}, {u0, v}, {u0, v}, {u1, v}}},
                0.70f);
        };

        for (int x = 0; x < width; ++x)
        {
            int leftRunStart = -1;
            int rightRunStart = -1;
            for (int y = 0; y <= height; ++y)
            {
                const bool leftEdge = y < height && opaqueAt(x, y) && !opaqueAt(x - 1, y);
                const bool rightEdge = y < height && opaqueAt(x, y) && !opaqueAt(x + 1, y);
                if (leftEdge && leftRunStart < 0)
                {
                    leftRunStart = y;
                }
                else if (!leftEdge && leftRunStart >= 0)
                {
                    addLeftSpan(x, leftRunStart, y);
                    leftRunStart = -1;
                }
                if (rightEdge && rightRunStart < 0)
                {
                    rightRunStart = y;
                }
                else if (!rightEdge && rightRunStart >= 0)
                {
                    addRightSpan(x, rightRunStart, y);
                    rightRunStart = -1;
                }
            }
        }

        for (int y = 0; y < height; ++y)
        {
            int topRunStart = -1;
            int bottomRunStart = -1;
            for (int x = 0; x <= width; ++x)
            {
                const bool topEdge = x < width && opaqueAt(x, y) && !opaqueAt(x, y - 1);
                const bool bottomEdge = x < width && opaqueAt(x, y) && !opaqueAt(x, y + 1);
                if (topEdge && topRunStart < 0)
                {
                    topRunStart = x;
                }
                else if (!topEdge && topRunStart >= 0)
                {
                    addTopSpan(y, topRunStart, x);
                    topRunStart = -1;
                }
                if (bottomEdge && bottomRunStart < 0)
                {
                    bottomRunStart = x;
                }
                else if (!bottomEdge && bottomRunStart >= 0)
                {
                    addBottomSpan(y, bottomRunStart, x);
                    bottomRunStart = -1;
                }
            }
        }

        stbi_image_free(loadedPixels);
        return mesh;
    }

    DroppedItemRenderPath::ItemSpriteMesh ItemSpriteMeshBuilder::buildLayered(
        const std::filesystem::path& bottomPath,
        uint32_t bottomTextureLayer,
        const std::filesystem::path& topPath,
        uint32_t topTextureLayer)
    {
        DroppedItemRenderPath::ItemSpriteMesh bottom = build(bottomPath);
        DroppedItemRenderPath::ItemSpriteMesh top = build(topPath);
        applyLayerAndOffset(bottom, bottomTextureLayer, -0.505f);
        applyLayerAndOffset(top, topTextureLayer, 0.505f);

        bottom.quads.insert(bottom.quads.end(), top.quads.begin(), top.quads.end());
        return bottom;
    }

    DroppedItemRenderPath::ItemSpriteMesh ItemSpriteMeshBuilder::buildBlockMold(
        const std::filesystem::path& bottomPath,
        uint32_t bottomTextureLayer,
        const std::filesystem::path& topPath,
        uint32_t topTextureLayer)
    {
        constexpr float BottomMinY = 0.0f;
        constexpr float BottomMaxY = 0.0625f;
        constexpr float TopMinY = BottomMaxY;
        constexpr float TopMaxY = 0.125f;

        DroppedItemRenderPath::ItemSpriteMesh bottom = build(bottomPath);
        DroppedItemRenderPath::ItemSpriteMesh top = build(topPath);
        applyLayerAndYRange(bottom, bottomTextureLayer, BottomMinY, BottomMaxY);
        applyLayerAndYRange(top, topTextureLayer, TopMinY, TopMaxY);

        bottom.quads.insert(bottom.quads.end(), top.quads.begin(), top.quads.end());
        return bottom;
    }

    DroppedItemRenderPath::ItemSpriteMesh ItemSpriteMeshBuilder::buildMoldCavitySurface(const std::filesystem::path& topPath)
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* loadedPixels = stbi_load(topPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (loadedPixels == nullptr)
        {
            throw std::runtime_error("Failed to load mold cavity surface texture: " + topPath.string());
        }

        DroppedItemRenderPath::ItemSpriteMesh mesh{};
        auto alphaAt = [&](int x, int y)
        {
            if (x < 0 || x >= width || y < 0 || y >= height)
            {
                return 0u;
            }
            const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u + 3u;
            return static_cast<unsigned int>(loadedPixels[index]);
        };
        auto addSurfaceQuad = [&](int x0, int x1, int y)
        {
            const float invWidth = width > 0 ? 1.0f / static_cast<float>(width) : 1.0f;
            const float invHeight = height > 0 ? 1.0f / static_cast<float>(height) : 1.0f;
            const float u0 = static_cast<float>(x0) * invWidth;
            const float u1 = static_cast<float>(x1) * invWidth;
            const float v0 = static_cast<float>(y) * invHeight;
            const float v1 = static_cast<float>(y + 1) * invHeight;
            const float minX = u0 - 0.5f;
            const float maxX = u1 - 0.5f;
            const float minZ = 0.5f - v1;
            const float maxZ = 0.5f - v0;

            DroppedItemRenderPath::ItemSpriteQuad quad{};
            quad.positions = {{{minX, 0.0f, minZ}, {minX, 0.0f, maxZ}, {maxX, 0.0f, maxZ}, {maxX, 0.0f, minZ}}};
            quad.uvs = {{{{u0, v1}}, {{u0, v0}}, {{u1, v0}}, {{u1, v1}}}};
            quad.ao = 1.0f;
            quad.textureLayer = -1.0f;
            mesh.quads.push_back(quad);
        };

        const int minX = std::max(0, width / 2 - 10);
        const int maxX = std::min(width, width / 2 + 10);
        const int minY = std::max(0, height / 2 - 10);
        const int maxY = std::min(height, height / 2 + 10);
        for (int y = minY; y < maxY; ++y)
        {
            int runStart = -1;
            for (int x = minX; x <= maxX; ++x)
            {
                const bool cavity = x < maxX && alphaAt(x, y) < 128u;
                if (cavity && runStart < 0)
                {
                    runStart = x;
                }
                else if (!cavity && runStart >= 0)
                {
                    addSurfaceQuad(runStart, x, y);
                    runStart = -1;
                }
            }
        }

        if (mesh.quads.empty())
        {
            for (int y = minY; y < maxY; ++y)
            {
                addSurfaceQuad(minX, maxX, y);
            }
        }

        stbi_image_free(loadedPixels);
        return mesh;
    }
}
