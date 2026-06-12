#include "assets/BlockItemIconBuilder.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace dolbuto::assets
{
    namespace
    {
        constexpr int IconSize = 64;

        struct Image
        {
            int width = 0;
            int height = 0;
            std::vector<unsigned char> pixels;
        };

        struct Vec2
        {
            float x = 0.0f;
            float y = 0.0f;
        };

        struct Vec3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct IconQuad
        {
            std::array<Vec3, 4> positions{};
            std::array<Vec2, 4> uvs{};
            uint32_t layer = 0;
            float shade = 1.0f;
        };

        struct ProjectedVertex
        {
            Vec2 position{};
            Vec2 uv{};
            float depth = 0.0f;
        };

        bool loadTexture(const std::filesystem::path& path, Image& image)
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_uc* loadedPixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (loadedPixels == nullptr)
            {
                return false;
            }
            if (width <= 0 || height <= 0)
            {
                stbi_image_free(loadedPixels);
                return false;
            }

            image.width = width;
            image.height = height;
            const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
            image.pixels.assign(loadedPixels, loadedPixels + byteCount);
            stbi_image_free(loadedPixels);
            return true;
        }

        std::filesystem::path texturePathForLayer(
            const std::filesystem::path& blockTextureDirectory,
            const std::vector<std::string>& blockTextureNames,
            uint32_t layer)
        {
            if (static_cast<std::size_t>(layer) >= blockTextureNames.size())
            {
                return {};
            }
            return blockTextureDirectory / (blockTextureNames[layer] + ".png");
        }

        float edge(Vec2 a, Vec2 b, Vec2 p)
        {
            return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
        }

        void blendPixel(
            std::vector<unsigned char>& output,
            std::vector<float>& depthBuffer,
            int x,
            int y,
            float depth,
            const unsigned char* source,
            float shade)
        {
            if (x < 0 || y < 0 || x >= IconSize || y >= IconSize || source[3] < 16)
            {
                return;
            }

            const std::size_t pixelIndex = static_cast<std::size_t>(y) * IconSize + static_cast<std::size_t>(x);
            if (depth >= depthBuffer[pixelIndex] - 0.0001f)
            {
                return;
            }
            depthBuffer[pixelIndex] = depth;

            const std::size_t index = pixelIndex * 4u;
            const float sourceAlpha = static_cast<float>(source[3]) / 255.0f;
            const float destAlpha = static_cast<float>(output[index + 3u]) / 255.0f;
            const float outAlpha = sourceAlpha + destAlpha * (1.0f - sourceAlpha);
            if (outAlpha <= 0.0f)
            {
                return;
            }

            for (std::size_t channel = 0; channel < 3u; ++channel)
            {
                const float sourceColor = std::clamp(static_cast<float>(source[channel]) * shade, 0.0f, 255.0f) / 255.0f;
                const float destColor = static_cast<float>(output[index + channel]) / 255.0f;
                const float outColor = (sourceColor * sourceAlpha + destColor * destAlpha * (1.0f - sourceAlpha)) / outAlpha;
                output[index + channel] = static_cast<unsigned char>(std::round(std::clamp(outColor, 0.0f, 1.0f) * 255.0f));
            }
            output[index + 3u] = static_cast<unsigned char>(std::round(std::clamp(outAlpha, 0.0f, 1.0f) * 255.0f));
        }

        void drawTriangle(
            std::vector<unsigned char>& output,
            std::vector<float>& depthBuffer,
            const Image& texture,
            std::array<ProjectedVertex, 3> vertices,
            float shade)
        {
            const float area = edge(vertices[0].position, vertices[1].position, vertices[2].position);
            if (std::abs(area) <= 0.0001f)
            {
                return;
            }

            const int minX = std::clamp(static_cast<int>(std::floor(std::min({
                vertices[0].position.x,
                vertices[1].position.x,
                vertices[2].position.x
            }))), 0, IconSize - 1);
            const int maxX = std::clamp(static_cast<int>(std::ceil(std::max({
                vertices[0].position.x,
                vertices[1].position.x,
                vertices[2].position.x
            }))), 0, IconSize - 1);
            const int minY = std::clamp(static_cast<int>(std::floor(std::min({
                vertices[0].position.y,
                vertices[1].position.y,
                vertices[2].position.y
            }))), 0, IconSize - 1);
            const int maxY = std::clamp(static_cast<int>(std::ceil(std::max({
                vertices[0].position.y,
                vertices[1].position.y,
                vertices[2].position.y
            }))), 0, IconSize - 1);

            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    const Vec2 p{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
                    const float w0 = edge(vertices[1].position, vertices[2].position, p) / area;
                    const float w1 = edge(vertices[2].position, vertices[0].position, p) / area;
                    const float w2 = edge(vertices[0].position, vertices[1].position, p) / area;
                    if (w0 < -0.0001f || w1 < -0.0001f || w2 < -0.0001f)
                    {
                        continue;
                    }

                    const float u = std::clamp(vertices[0].uv.x * w0 + vertices[1].uv.x * w1 + vertices[2].uv.x * w2, 0.0f, 1.0f);
                    const float v = std::clamp(vertices[0].uv.y * w0 + vertices[1].uv.y * w1 + vertices[2].uv.y * w2, 0.0f, 1.0f);
                    const float depth = vertices[0].depth * w0 + vertices[1].depth * w1 + vertices[2].depth * w2;
                    const int sourceX = std::clamp(static_cast<int>(u * static_cast<float>(texture.width - 1) + 0.5f), 0, texture.width - 1);
                    const int sourceY = std::clamp(static_cast<int>(v * static_cast<float>(texture.height - 1) + 0.5f), 0, texture.height - 1);
                    const std::size_t sourceIndex = (static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(texture.width) + static_cast<std::size_t>(sourceX)) * 4u;
                    blendPixel(output, depthBuffer, x, y, depth, texture.pixels.data() + sourceIndex, shade);
                }
            }
        }

        float modelExtent(float explicitExtent, float fallback)
        {
            return std::clamp(explicitExtent > 0.0f ? explicitExtent : fallback, 0.0625f, 1.0f);
        }

        Vec2 uv(float u, float v)
        {
            return Vec2{std::clamp(u, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f)};
        }

        void addQuad(
            std::vector<IconQuad>& quads,
            std::array<Vec3, 4> positions,
            std::array<Vec2, 4> uvs,
            uint32_t layer,
            float shade)
        {
            IconQuad quad{};
            quad.positions = positions;
            quad.uvs = uvs;
            quad.layer = layer;
            quad.shade = shade;
            quads.push_back(quad);
        }

        void addCuboid(
            std::vector<IconQuad>& quads,
            const BlockTextureLayers& layers,
            bool useVerticalSection,
            float minX,
            float minY,
            float minZ,
            float maxX,
            float maxY,
            float maxZ)
        {
            const float sideTopV = 1.0f - maxY;
            const float sideBottomV = 1.0f - minY;
            const uint32_t eastLayer = useVerticalSection ? layers.verticalSection : layers.faces[2];

            addQuad(
                quads,
                {{{minX, maxY, minZ}, {maxX, maxY, minZ}, {maxX, maxY, maxZ}, {minX, maxY, maxZ}}},
                {uv(minX, minZ), uv(maxX, minZ), uv(maxX, maxZ), uv(minX, maxZ)},
                layers.faces[0],
                1.0f);
            addQuad(
                quads,
                {{{minX, minY, maxZ}, {maxX, minY, maxZ}, {maxX, minY, minZ}, {minX, minY, minZ}}},
                {uv(minX, maxZ), uv(maxX, maxZ), uv(maxX, minZ), uv(minX, minZ)},
                layers.faces[1],
                0.55f);
            addQuad(
                quads,
                {{{maxX, minY, minZ}, {maxX, maxY, minZ}, {maxX, maxY, maxZ}, {maxX, minY, maxZ}}},
                {uv(minZ, sideBottomV), uv(minZ, sideTopV), uv(maxZ, sideTopV), uv(maxZ, sideBottomV)},
                eastLayer,
                useVerticalSection ? 0.90f : 0.82f);
            addQuad(
                quads,
                {{{minX, minY, maxZ}, {minX, maxY, maxZ}, {minX, maxY, minZ}, {minX, minY, minZ}}},
                {uv(minZ, sideBottomV), uv(minZ, sideTopV), uv(maxZ, sideTopV), uv(maxZ, sideBottomV)},
                layers.faces[3],
                0.68f);
            addQuad(
                quads,
                {{{maxX, minY, maxZ}, {minX, minY, maxZ}, {minX, maxY, maxZ}, {maxX, maxY, maxZ}}},
                {uv(maxX, sideBottomV), uv(minX, sideBottomV), uv(minX, sideTopV), uv(maxX, sideTopV)},
                layers.faces[4],
                0.86f);
            addQuad(
                quads,
                {{{minX, minY, minZ}, {maxX, minY, minZ}, {maxX, maxY, minZ}, {minX, maxY, minZ}}},
                {uv(minX, sideBottomV), uv(maxX, sideBottomV), uv(maxX, sideTopV), uv(minX, sideTopV)},
                layers.faces[5],
                0.64f);
        }

        std::vector<IconQuad> buildModelQuads(
            const BlockTextureLayers& layers,
            BlockRenderType renderType,
            float modelWidth,
            float modelHeight,
            float modelDepth,
            bool useVerticalSection,
            float& width,
            float& height,
            float& depth)
        {
            width = modelExtent(modelWidth, 1.0f);
            height = modelExtent(
                modelHeight,
                (renderType == BlockRenderType::Slab || renderType == BlockRenderType::HalfSlab) ? 0.5f : 1.0f);
            depth = modelExtent(modelDepth, 1.0f);
            const float minX = (1.0f - width) * 0.5f;
            const float maxX = minX + width;
            const float minZ = (1.0f - depth) * 0.5f;
            const float maxZ = minZ + depth;

            std::vector<IconQuad> quads;
            if (renderType == BlockRenderType::Crucible)
            {
                const float floorTop = height * 0.2f;
                const float wallThicknessX = width * 0.2f;
                const float wallThicknessZ = depth * 0.2f;
                addCuboid(quads, layers, false, minX, 0.0f, minZ, maxX, floorTop, maxZ);
                addCuboid(quads, layers, false, minX, floorTop, minZ, maxX, height, minZ + wallThicknessZ);
                addCuboid(quads, layers, false, minX, floorTop, maxZ - wallThicknessZ, maxX, height, maxZ);
                addCuboid(quads, layers, false, minX, floorTop, minZ + wallThicknessZ, minX + wallThicknessX, height, maxZ - wallThicknessZ);
                addCuboid(quads, layers, false, maxX - wallThicknessX, floorTop, minZ + wallThicknessZ, maxX, height, maxZ - wallThicknessZ);
                return quads;
            }

            addCuboid(quads, layers, useVerticalSection, minX, 0.0f, minZ, maxX, height, maxZ);
            return quads;
        }

        ProjectedVertex projectVertex(Vec3 position, Vec2 uv, float width, float height, float depth)
        {
            const float topY = 8.0f + (1.0f - height) * 24.0f;
            const float originX = 32.0f + (20.0f * depth - 20.0f * width) * 0.5f;

            ProjectedVertex result{};
            result.position.x = originX + 20.0f * position.x - 20.0f * position.z;
            result.position.y = topY + 12.0f * position.x + 12.0f * position.z + 24.0f * (height - position.y);
            result.uv = uv;
            result.depth = -position.x - position.z - position.y;
            return result;
        }

        bool loadImagesForQuads(
            const std::filesystem::path& blockTextureDirectory,
            const std::vector<std::string>& blockTextureNames,
            const std::vector<IconQuad>& quads,
            std::unordered_map<uint32_t, Image>& images)
        {
            for (const IconQuad& quad : quads)
            {
                if (images.find(quad.layer) != images.end())
                {
                    continue;
                }

                const std::filesystem::path texturePath = texturePathForLayer(blockTextureDirectory, blockTextureNames, quad.layer);
                if (texturePath.empty())
                {
                    return false;
                }

                Image image{};
                if (!loadTexture(texturePath, image))
                {
                    return false;
                }
                images.emplace(quad.layer, std::move(image));
            }
            return true;
        }
    }

    bool writeBlockItemIcon(
        const std::filesystem::path& blockTextureDirectory,
        const std::vector<std::string>& blockTextureNames,
        const BlockTextureLayers& layers,
        BlockRenderType renderType,
        float modelWidth,
        float modelHeight,
        float modelDepth,
        bool useVerticalSection,
        const std::filesystem::path& outputPath)
    {
        float width = 1.0f;
        float height = 1.0f;
        float depth = 1.0f;
        const std::vector<IconQuad> quads = buildModelQuads(
            layers,
            renderType,
            modelWidth,
            modelHeight,
            modelDepth,
            useVerticalSection,
            width,
            height,
            depth);
        if (quads.empty())
        {
            return false;
        }

        if (std::filesystem::exists(outputPath))
        {
            return true;
        }

        std::unordered_map<uint32_t, Image> images;
        if (!loadImagesForQuads(blockTextureDirectory, blockTextureNames, quads, images))
        {
            return false;
        }

        std::vector<unsigned char> output(static_cast<std::size_t>(IconSize) * IconSize * 4u, 0u);
        std::vector<float> depthBuffer(static_cast<std::size_t>(IconSize) * IconSize, std::numeric_limits<float>::infinity());

        for (const IconQuad& quad : quads)
        {
            const auto imageIt = images.find(quad.layer);
            if (imageIt == images.end())
            {
                continue;
            }

            const std::array<ProjectedVertex, 4> projected{
                projectVertex(quad.positions[0], quad.uvs[0], width, height, depth),
                projectVertex(quad.positions[1], quad.uvs[1], width, height, depth),
                projectVertex(quad.positions[2], quad.uvs[2], width, height, depth),
                projectVertex(quad.positions[3], quad.uvs[3], width, height, depth)
            };
            drawTriangle(output, depthBuffer, imageIt->second, {projected[0], projected[1], projected[2]}, quad.shade);
            drawTriangle(output, depthBuffer, imageIt->second, {projected[0], projected[2], projected[3]}, quad.shade);
        }

        std::error_code error;
        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error)
        {
            return false;
        }

        if (stbi_write_png(outputPath.string().c_str(), IconSize, IconSize, 4, output.data(), IconSize * 4) == 0)
        {
            return false;
        }
        return true;
    }
}
