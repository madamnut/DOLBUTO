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

        const Image* imageForLayer(uint32_t layer, const std::array<Image, 3>& images, const std::array<uint32_t, 3>& layers)
        {
            for (std::size_t i = 0; i < layers.size(); ++i)
            {
                if (layers[i] == layer && !images[i].pixels.empty())
                {
                    return &images[i];
                }
            }
            return nullptr;
        }

        float edge(Vec2 a, Vec2 b, Vec2 p)
        {
            return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
        }

        void blendPixel(std::vector<unsigned char>& output, int x, int y, const unsigned char* source, float shade)
        {
            if (x < 0 || y < 0 || x >= IconSize || y >= IconSize || source[3] == 0)
            {
                return;
            }

            const std::size_t index = (static_cast<std::size_t>(y) * IconSize + static_cast<std::size_t>(x)) * 4u;
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
            const Image& texture,
            std::array<Vec2, 3> positions,
            std::array<Vec2, 3> uvs,
            float shade)
        {
            const float area = edge(positions[0], positions[1], positions[2]);
            if (std::abs(area) <= 0.0001f)
            {
                return;
            }

            const int minX = std::clamp(static_cast<int>(std::floor(std::min({positions[0].x, positions[1].x, positions[2].x}))), 0, IconSize - 1);
            const int maxX = std::clamp(static_cast<int>(std::ceil(std::max({positions[0].x, positions[1].x, positions[2].x}))), 0, IconSize - 1);
            const int minY = std::clamp(static_cast<int>(std::floor(std::min({positions[0].y, positions[1].y, positions[2].y}))), 0, IconSize - 1);
            const int maxY = std::clamp(static_cast<int>(std::ceil(std::max({positions[0].y, positions[1].y, positions[2].y}))), 0, IconSize - 1);

            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    const Vec2 p{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
                    const float w0 = edge(positions[1], positions[2], p) / area;
                    const float w1 = edge(positions[2], positions[0], p) / area;
                    const float w2 = edge(positions[0], positions[1], p) / area;
                    if (w0 < -0.0001f || w1 < -0.0001f || w2 < -0.0001f)
                    {
                        continue;
                    }

                    const float u = std::clamp(uvs[0].x * w0 + uvs[1].x * w1 + uvs[2].x * w2, 0.0f, 1.0f);
                    const float v = std::clamp(uvs[0].y * w0 + uvs[1].y * w1 + uvs[2].y * w2, 0.0f, 1.0f);
                    const int sourceX = std::clamp(static_cast<int>(u * static_cast<float>(texture.width - 1) + 0.5f), 0, texture.width - 1);
                    const int sourceY = std::clamp(static_cast<int>(v * static_cast<float>(texture.height - 1) + 0.5f), 0, texture.height - 1);
                    const std::size_t sourceIndex = (static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(texture.width) + static_cast<std::size_t>(sourceX)) * 4u;
                    blendPixel(output, x, y, texture.pixels.data() + sourceIndex, shade);
                }
            }
        }

        void drawQuad(
            std::vector<unsigned char>& output,
            const Image& texture,
            std::array<Vec2, 4> positions,
            std::array<Vec2, 4> uvs,
            float shade)
        {
            drawTriangle(
                output,
                texture,
                std::array<Vec2, 3>{positions[0], positions[1], positions[2]},
                std::array<Vec2, 3>{uvs[0], uvs[1], uvs[2]},
                shade);
            drawTriangle(
                output,
                texture,
                std::array<Vec2, 3>{positions[0], positions[2], positions[3]},
                std::array<Vec2, 3>{uvs[0], uvs[2], uvs[3]},
                shade);
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
        const std::array<uint32_t, 3> visibleLayers = {
            layers.faces[0],
            layers.faces[3],
            useVerticalSection ? layers.verticalSection : layers.faces[4]
        };

        std::array<Image, 3> images{};
        for (std::size_t i = 0; i < visibleLayers.size(); ++i)
        {
            if (static_cast<std::size_t>(visibleLayers[i]) >= blockTextureNames.size())
            {
                return false;
            }

            const std::filesystem::path texturePath = blockTextureDirectory / (blockTextureNames[visibleLayers[i]] + ".png");
            if (!loadTexture(texturePath, images[i]))
            {
                return false;
            }
        }

        const Image* top = imageForLayer(layers.faces[0], images, visibleLayers);
        const Image* left = imageForLayer(layers.faces[3], images, visibleLayers);
        const Image* right = imageForLayer(useVerticalSection ? layers.verticalSection : layers.faces[4], images, visibleLayers);
        if (top == nullptr || left == nullptr || right == nullptr)
        {
            return false;
        }

        std::vector<unsigned char> output(static_cast<std::size_t>(IconSize) * IconSize * 4u, 0u);
        const float width = std::clamp(modelWidth > 0.0f ? modelWidth : 1.0f, 0.0625f, 1.0f);
        const float height = std::clamp(
            modelHeight > 0.0f ? modelHeight : (renderType == BlockRenderType::Slab ? 0.5f : 1.0f),
            0.0625f,
            1.0f);
        const float depth = std::clamp(modelDepth > 0.0f ? modelDepth : 1.0f, 0.0625f, 1.0f);
        const float topOffset = (1.0f - height) * 24.0f;
        const float topY = 8.0f + topOffset;
        const float widthX = 20.0f * width;
        const float widthY = 12.0f * width;
        const float depthX = 20.0f * depth;
        const float depthY = 12.0f * depth;
        const float originX = 32.0f + (depthX - widthX) * 0.5f;
        const float sideDown = 24.0f * height;
        const Vec2 top0{originX, topY};
        const Vec2 top1{originX + widthX, topY + widthY};
        const Vec2 top2{originX + widthX - depthX, topY + widthY + depthY};
        const Vec2 top3{originX - depthX, topY + depthY};
        const std::array<Vec2, 4> leftPositions{
            top3,
            top2,
            Vec2{top2.x, top2.y + sideDown},
            Vec2{top3.x, top3.y + sideDown}
        };
        const std::array<Vec2, 4> rightPositions{
            top2,
            top1,
            Vec2{top1.x, top1.y + sideDown},
            Vec2{top2.x, top2.y + sideDown}
        };
        const std::array<Vec2, 4> topPositions{
            top0,
            top1,
            top2,
            top3
        };
        const std::array<Vec2, 4> sideUvs{
            Vec2{0.0f, 1.0f - height},
            Vec2{1.0f, 1.0f - height},
            Vec2{1.0f, 1.0f},
            Vec2{0.0f, 1.0f}
        };
        const std::array<Vec2, 4> sectionUvs{
            Vec2{0.0f, 0.0f},
            Vec2{1.0f, 0.0f},
            Vec2{1.0f, 1.0f},
            Vec2{0.0f, 1.0f}
        };
        drawQuad(
            output,
            *left,
            leftPositions,
            sideUvs,
            0.70f);
        drawQuad(
            output,
            *right,
            rightPositions,
            useVerticalSection ? sectionUvs : sideUvs,
            0.86f);
        drawQuad(
            output,
            *top,
            topPositions,
            std::array<Vec2, 4>{
                Vec2{0.5f * width, 0.0f},
                Vec2{width, 0.5f * depth},
                Vec2{0.5f * width, depth},
                Vec2{0.0f, 0.5f * depth}
            },
            1.0f);

        std::error_code error;
        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error)
        {
            return false;
        }

        return stbi_write_png(outputPath.string().c_str(), IconSize, IconSize, 4, output.data(), IconSize * 4) != 0;
    }
}
