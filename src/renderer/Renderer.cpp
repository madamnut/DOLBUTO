#include "renderer/Renderer.h"

#include "camera/Camera.h"
#include "config/ConfigLoaders.h"
#include "data/DataLoaders.h"
#include "platform/Log.h"
#include "platform/RuntimePaths.h"
#include "world/DroppedItemSystem.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <FastNoise/FastNoise.h>
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace dolbuto
{
    namespace
    {
        constexpr int MaxFramesInFlight = 2;
        constexpr float FieldOfViewRadians = 1.0471975512f;
        constexpr int FontAtlasSize = 512;
        constexpr float FontPixelHeight = 18.0f;
        constexpr size_t MaxTextVertices = 65536;
        constexpr size_t MaxUiVertices = 262144;
        constexpr size_t MaxUiIndices = 393216;
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeY = 512;
        constexpr int ChunkSizeZ = 16;
        constexpr int SubchunkSize = 16;
        constexpr int SubchunksPerChunk = ChunkSizeY / SubchunkSize;
        constexpr int LoadGridUnitChunks = 16;
        constexpr int DefaultSeaLevel = 256;
        constexpr int CenterGroupChunks = 2;
        constexpr int DefaultLoadGridScale = 1;
        constexpr int DefaultTerrainWorkerCount = 4;
        constexpr int DefaultMaxTerrainUploadChunksPerFrame = 8;
        constexpr int DefaultMaxTerrainUnloadChunksPerFrame = 16;
        constexpr int DefaultMaxTerrainRetiredDestroyPerFrame = 4;
        constexpr int TerrainMinHeight = 120;
        constexpr int TerrainMaxHeight = 140;
        constexpr int TerrainTilePeriod = 65536;
        constexpr int WorldSizeBlocks = TerrainTilePeriod;
        constexpr int WorldSizeChunks = WorldSizeBlocks / ChunkSizeX;
        constexpr float DefaultTerrainNoiseFeatureScale = 220.0f;
        constexpr int DefaultTerrainNoiseOctaveCount = 4;
        constexpr float DefaultTerrainNoiseLacunarity = 2.0f;
        constexpr float DefaultTerrainNoiseGain = 0.5f;
        constexpr float DefaultTerrainNoiseSimplexScale = 1.0f;
        constexpr bool DefaultTerrainDomainWarpEnabled = false;
        constexpr float DefaultTerrainDomainWarpAmplitude = 0.0f;
        constexpr float DefaultTerrainDomainWarpFrequency = 1.0f;
        constexpr int DefaultTerrainDomainWarpOctaveCount = 2;
        constexpr float DefaultTerrainDomainWarpGain = 0.5f;
        constexpr float DefaultTemperatureNoiseStrength = 0.12f;
        constexpr float DefaultTemperatureNoiseFeatureScale = 8192.0f;
        constexpr int DefaultTemperatureNoiseOctaveCount = 2;
        constexpr float DefaultTemperatureNoiseLacunarity = 2.0f;
        constexpr float DefaultTemperatureNoiseGain = 0.5f;
        constexpr float DefaultTemperatureNoiseSimplexScale = 1.0f;
        constexpr int TemperatureNoiseSeed = 2400;
        constexpr float DefaultPrecipitationNoiseFeatureScale = 4096.0f;
        constexpr int DefaultPrecipitationNoiseOctaveCount = 3;
        constexpr float DefaultPrecipitationNoiseLacunarity = 2.0f;
        constexpr float DefaultPrecipitationNoiseGain = 0.5f;
        constexpr float DefaultPrecipitationNoiseSimplexScale = 1.0f;
        constexpr int PrecipitationNoiseSeed = 2401;
        constexpr int ClimateOverlaySize = 1024;
        constexpr float DefaultFluidWaterAlpha = 0.8f;
        constexpr float MenuButtonWidthPixels = 240.0f;
        constexpr float MenuButtonHeightPixels = 56.0f;
        constexpr float LobbyBackgroundTilePixels = 96.0f;
        constexpr float TerrainNearPlane = 0.1f;
        constexpr float TerrainFarPlane = 4000.0f;
        constexpr float HeightLutNoiseMin = -2.0f;
        constexpr float HeightLutNoiseMax = 2.0f;
        constexpr uint32_t HeightLutVersion = 1;
        constexpr uint32_t HeightLutCount = 1024;
        constexpr double PerformanceSampleSeconds = 0.5;
        constexpr uint16_t BlockAir = 0;
        constexpr uint16_t BlockRock = 1;
        constexpr uint16_t BlockTrunk = 8;
        constexpr uint16_t BlockLeaves = 9;
        constexpr uint16_t BlockPlant = 10000;
        constexpr uint16_t BlockStoneProp = 20000;
        constexpr uint16_t BlockBranchProp = 20001;
        constexpr uint16_t FluidNone = 0;
        constexpr int FluidAmountBits = 7;
        constexpr uint16_t FluidAmountMask = (1u << FluidAmountBits) - 1u;
        constexpr uint8_t ClimateMinByte = 0;
        constexpr uint8_t ClimateMaxByte = 255;
        constexpr size_t MaxBlockBreakParticles = 2048;
        constexpr uint32_t BlockBreakParticleCount = 24;
        constexpr float BlockBreakParticleGravity = 22.0f;
        constexpr float BlockBreakParticleDrag = 0.92f;
        constexpr size_t BlockBreakingStageCount = 10;
        constexpr size_t MaxDroppedItemRenderInstances = world::DroppedItemSystem::MaxDroppedItemRenderInstances;
        constexpr uint32_t TopFaceRotationSalt = 0x51A7E001u;
        constexpr uint32_t PlantPlacementSalt = 0x9A7D3E21u;
        constexpr uint8_t PlantPlacementMax = 151;
        constexpr uint8_t StonePlacementMax = 159;
        constexpr uint8_t BranchPlacementMax = 167;
        constexpr uint8_t TreePlacementMin = 168;
        constexpr uint8_t TreePlacementMax = 170;
        constexpr float RandomBlockOffsetHalfRange = 0.2f;
        constexpr float TerrainPositionPackScale = 256.0f;
        constexpr float TerrainUvPackScale = 256.0f;
        constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
        constexpr const char* VersionText = "DOLBUTO 0.0.0.2";
        constexpr std::array<const char*, 1> DeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        constexpr const char* MemoryBudgetExtension = "VK_EXT_memory_budget";
        constexpr const char* PhysicalDeviceProperties2Extension = "VK_KHR_get_physical_device_properties2";

        struct Mat4
        {
            float m[16]{};
        };

        struct TestChunk
        {
            std::array<uint16_t, ChunkSizeX * ChunkSizeY * ChunkSizeZ> blocks{};

            uint16_t& at(int x, int y, int z)
            {
                return blocks[(y * ChunkSizeZ + z) * ChunkSizeX + x];
            }

            uint16_t at(int x, int y, int z) const
            {
                return blocks[(y * ChunkSizeZ + z) * ChunkSizeX + x];
            }
        };

        struct Frustum
        {
            Vec3 position{};
            Vec3 right{};
            Vec3 up{};
            Vec3 forward{};
            float tanHalfVertical = 1.0f;
            float tanHalfHorizontal = 1.0f;
        };

        std::vector<char> readFile(const std::string& path)
        {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open())
            {
                throw std::runtime_error("Failed to open file: " + path);
            }

            const auto size = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(size);
            file.seekg(0);
            file.read(buffer.data(), static_cast<std::streamsize>(size));
            return buffer;
        }

        uint32_t worldRandomHash(int x, int y, int z, uint32_t salt)
        {
            uint32_t hash = static_cast<uint32_t>(x) * 0x8da6b343u;
            hash ^= static_cast<uint32_t>(y) * 0xd8163841u;
            hash ^= static_cast<uint32_t>(z) * 0xcb1ab31fu;
            hash ^= salt;
            hash ^= hash >> 16u;
            hash *= 0x7feb352du;
            hash ^= hash >> 15u;
            hash *= 0x846ca68bu;
            hash ^= hash >> 16u;
            return hash;
        }

        int positiveModulo(int value, int divisor)
        {
            return world::WorldRuntime::positiveModulo(value, divisor);
        }

        int wrapBlockCoordinate(int value)
        {
            return positiveModulo(value, WorldSizeBlocks);
        }

        int wrapChunkCoordinate(int value)
        {
            return positiveModulo(value, WorldSizeChunks);
        }

        uint8_t worldRandom8(int x, int y, int z, uint32_t salt)
        {
            return static_cast<uint8_t>(worldRandomHash(wrapBlockCoordinate(x), y, wrapBlockCoordinate(z), salt) & 255u);
        }

        void writePngRgba(const std::filesystem::path& path, const std::vector<unsigned char>& rgba, uint32_t width, uint32_t height)
        {
            std::filesystem::create_directories(path.parent_path());
            const int strideBytes = static_cast<int>(width * 4u);
            if (stbi_write_png(path.string().c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba.data(), strideBytes) == 0)
            {
                throw std::runtime_error("Failed to write generated mip texture: " + path.string());
            }
        }

        std::vector<unsigned char> downsampleRgba2x(const std::vector<unsigned char>& source, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t targetWidth, uint32_t targetHeight)
        {
            std::vector<unsigned char> result(static_cast<size_t>(targetWidth) * targetHeight * 4u);
            for (uint32_t y = 0; y < targetHeight; ++y)
            {
                for (uint32_t x = 0; x < targetWidth; ++x)
                {
                    uint32_t sum[4] = {};
                    uint32_t count = 0;
                    for (uint32_t oy = 0; oy < 2; ++oy)
                    {
                        const uint32_t sourceY = std::min(sourceHeight - 1u, y * 2u + oy);
                        for (uint32_t ox = 0; ox < 2; ++ox)
                        {
                            const uint32_t sourceX = std::min(sourceWidth - 1u, x * 2u + ox);
                            const unsigned char* pixel = source.data() + (static_cast<size_t>(sourceY) * sourceWidth + sourceX) * 4u;
                            for (int channel = 0; channel < 4; ++channel)
                            {
                                sum[channel] += pixel[channel];
                            }
                            ++count;
                        }
                    }

                    unsigned char* target = result.data() + (static_cast<size_t>(y) * targetWidth + x) * 4u;
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        target[channel] = static_cast<unsigned char>((sum[channel] + count / 2u) / count);
                    }
                }
            }
            return result;
        }

        bool isBlockTexturePath(const std::string& basePath)
        {
            std::filesystem::path path(basePath);
            return path.parent_path().filename() == "block";
        }

        std::filesystem::path manualMipPath(const std::string& basePath, uint32_t mipLevel)
        {
            std::filesystem::path path(basePath);
            return path.parent_path() / "mip" / (path.stem().string() + "_mip" + std::to_string(mipLevel) + ".png");
        }

        constexpr uint16_t fluidId(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid >> FluidAmountBits);
        }

        constexpr uint16_t fluidAmount(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid & FluidAmountMask);
        }

        uint8_t encodeClimateValue(float value)
        {
            return static_cast<uint8_t>(std::clamp(
                std::lround(std::clamp(value, 0.0f, 1.0f) * static_cast<float>(ClimateMaxByte)),
                static_cast<long>(ClimateMinByte),
                static_cast<long>(ClimateMaxByte)));
        }

        float decodeClimateValue(uint8_t value)
        {
            return static_cast<float>(value) / static_cast<float>(ClimateMaxByte);
        }

        std::filesystem::path screenshotPath()
        {
            const std::filesystem::path directory = screenshotDirectory();
            std::filesystem::create_directories(directory);

            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

            std::tm localTime{};
#ifdef _WIN32
            localtime_s(&localTime, &time);
#else
            localtime_r(&time, &localTime);
#endif

            std::ostringstream name;
            name << "DOLBUTO_"
                << std::put_time(&localTime, "%Y%m%d_%H%M%S")
                << "_" << std::setw(3) << std::setfill('0') << milliseconds
                << ".bmp";
            return directory / name.str();
        }

        Vec3 toVec3(DVec3 value)
        {
            return {
                static_cast<float>(value.x),
                static_cast<float>(value.y),
                static_cast<float>(value.z)
            };
        }

        int chunkCoordinate(double worldCoordinate)
        {
            const int blockCoordinate = static_cast<int>(std::floor(worldCoordinate + 0.5));
            return static_cast<int>(std::floor(static_cast<double>(blockCoordinate) / static_cast<double>(ChunkSizeX)));
        }

        int floorDiv(int value, int divisor)
        {
            return world::WorldRuntime::floorDiv(value, divisor);
        }

        int centerGroupCoordinate(int chunkCoordinate)
        {
            return floorDiv(chunkCoordinate, CenterGroupChunks) * CenterGroupChunks;
        }

        int blockCoordinateXz(double worldCoordinate)
        {
            return world::WorldRuntime::blockCoordinateXz(worldCoordinate);
        }

        int blockCoordinateY(double worldCoordinate)
        {
            return world::WorldRuntime::blockCoordinateY(worldCoordinate);
        }

        uint64_t chunkKey(int chunkX, int chunkZ)
        {
            return world::WorldRuntime::chunkKey(chunkX, chunkZ);
        }

        struct FeatureNeighborOffset
        {
            int x = 0;
            int z = 0;
        };

        constexpr std::array<FeatureNeighborOffset, 8> FeatureNeighborOffsets = {
            FeatureNeighborOffset{-1, -1},
            FeatureNeighborOffset{0, -1},
            FeatureNeighborOffset{1, -1},
            FeatureNeighborOffset{-1, 0},
            FeatureNeighborOffset{1, 0},
            FeatureNeighborOffset{-1, 1},
            FeatureNeighborOffset{0, 1},
            FeatureNeighborOffset{1, 1}
        };

        constexpr uint8_t AllFeatureSourcesMask = 0xFFu;

        std::optional<size_t> featureNeighborIndex(int offsetX, int offsetZ)
        {
            for (size_t i = 0; i < FeatureNeighborOffsets.size(); ++i)
            {
                if (FeatureNeighborOffsets[i].x == offsetX && FeatureNeighborOffsets[i].z == offsetZ)
                {
                    return i;
                }
            }
            return std::nullopt;
        }

        FastNoise::SmartNode<> terrainNoiseGenerator(float simplexScale, int octaveCount, float lacunarity, float gain)
        {
            struct CachedGenerator
            {
                float simplexScale = 0.0f;
                int octaveCount = 0;
                float lacunarity = 0.0f;
                float gain = 0.0f;
                FastNoise::SmartNode<> generator;
            };

            thread_local CachedGenerator cache{};
            if (cache.generator &&
                cache.simplexScale == simplexScale &&
                cache.octaveCount == octaveCount &&
                cache.lacunarity == lacunarity &&
                cache.gain == gain)
            {
                return cache.generator;
            }

            auto createGenerator = [&]() -> FastNoise::SmartNode<>
            {
                auto simplex = FastNoise::New<FastNoise::Simplex>();
                auto fbm = FastNoise::New<FastNoise::FractalFBm>();
                if (!simplex || !fbm)
                {
                    return FastNoise::SmartNode<>{};
                }

                simplex->SetScale(simplexScale);
                fbm->SetSource(simplex);
                fbm->SetOctaveCount(octaveCount);
                fbm->SetLacunarity(lacunarity);
                fbm->SetGain(gain);
                return FastNoise::SmartNode<>(fbm);
            };

            cache.simplexScale = simplexScale;
            cache.octaveCount = octaveCount;
            cache.lacunarity = lacunarity;
            cache.gain = gain;
            cache.generator = createGenerator();
            return cache.generator;
        }

        std::string formatProfileMs(const char* label, double milliseconds)
        {
            std::ostringstream text;
            text << label << ": " << std::fixed << std::setprecision(3) << milliseconds << "MS";
            return text.str();
        }

        bool deviceExtensionAvailable(VkPhysicalDevice device, const char* extensionName)
        {
            uint32_t extensionCount = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
            std::vector<VkExtensionProperties> extensions(extensionCount);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());

            return std::any_of(extensions.begin(), extensions.end(), [extensionName](const VkExtensionProperties& extension)
            {
                return std::strcmp(extension.extensionName, extensionName) == 0;
            });
        }

        bool instanceExtensionAvailable(const char* extensionName)
        {
            uint32_t extensionCount = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
            std::vector<VkExtensionProperties> extensions(extensionCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

            return std::any_of(extensions.begin(), extensions.end(), [extensionName](const VkExtensionProperties& extension)
            {
                return std::strcmp(extension.extensionName, extensionName) == 0;
            });
        }

        void writeBmp(const std::filesystem::path& path, const unsigned char* pixels, uint32_t width, uint32_t height, VkFormat format)
        {
            const bool bgra = format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM;
            const bool rgba = format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_R8G8B8A8_UNORM;
            if (!bgra && !rgba)
            {
                throw std::runtime_error("Unsupported screenshot swapchain format.");
            }

            const uint32_t rowStride = ((width * 3u) + 3u) & ~3u;
            const uint32_t imageSize = rowStride * height;
            const uint32_t fileSize = 54u + imageSize;
            std::vector<unsigned char> file(fileSize);

            file[0] = 'B';
            file[1] = 'M';
            std::memcpy(file.data() + 2, &fileSize, sizeof(fileSize));
            const uint32_t pixelOffset = 54;
            std::memcpy(file.data() + 10, &pixelOffset, sizeof(pixelOffset));
            const uint32_t dibSize = 40;
            const int32_t bmpWidth = static_cast<int32_t>(width);
            const int32_t bmpHeight = static_cast<int32_t>(height);
            const uint16_t planes = 1;
            const uint16_t bitsPerPixel = 24;
            std::memcpy(file.data() + 14, &dibSize, sizeof(dibSize));
            std::memcpy(file.data() + 18, &bmpWidth, sizeof(bmpWidth));
            std::memcpy(file.data() + 22, &bmpHeight, sizeof(bmpHeight));
            std::memcpy(file.data() + 26, &planes, sizeof(planes));
            std::memcpy(file.data() + 28, &bitsPerPixel, sizeof(bitsPerPixel));
            std::memcpy(file.data() + 34, &imageSize, sizeof(imageSize));

            unsigned char* out = file.data() + pixelOffset;
            for (uint32_t y = 0; y < height; ++y)
            {
                const uint32_t sourceY = height - 1u - y;
                const unsigned char* source = pixels + static_cast<size_t>(sourceY) * width * 4u;
                unsigned char* row = out + static_cast<size_t>(y) * rowStride;
                for (uint32_t x = 0; x < width; ++x)
                {
                    const unsigned char* pixel = source + static_cast<size_t>(x) * 4u;
                    row[x * 3u + 0u] = bgra ? pixel[0] : pixel[2];
                    row[x * 3u + 1u] = pixel[1];
                    row[x * 3u + 2u] = bgra ? pixel[2] : pixel[0];
                }
            }

            std::ofstream stream(path, std::ios::binary);
            if (!stream.is_open())
            {
                throw std::runtime_error("Failed to open screenshot file.");
            }
            stream.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        }

        Mat4 identity()
        {
            Mat4 matrix{};
            matrix.m[0] = 1.0f;
            matrix.m[5] = 1.0f;
            matrix.m[10] = 1.0f;
            matrix.m[15] = 1.0f;
            return matrix;
        }

        Mat4 multiply(const Mat4& left, const Mat4& right)
        {
            Mat4 result{};
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    result.m[column * 4 + row] =
                        left.m[0 * 4 + row] * right.m[column * 4 + 0] +
                        left.m[1 * 4 + row] * right.m[column * 4 + 1] +
                        left.m[2 * 4 + row] * right.m[column * 4 + 2] +
                        left.m[3 * 4 + row] * right.m[column * 4 + 3];
                }
            }
            return result;
        }

        Mat4 perspective(float fovRadians, float aspect, float nearPlane, float farPlane)
        {
            const float f = 1.0f / std::tan(fovRadians * 0.5f);
            Mat4 matrix{};
            matrix.m[0] = f / aspect;
            matrix.m[5] = -f;
            matrix.m[10] = farPlane / (farPlane - nearPlane);
            matrix.m[11] = 1.0f;
            matrix.m[14] = -(nearPlane * farPlane) / (farPlane - nearPlane);
            return matrix;
        }

        Mat4 viewMatrix(const Camera& camera, Vec3 position)
        {
            const Vec3 cameraRight = camera.right();
            const Vec3 terrainRight{-cameraRight.x, -cameraRight.y, -cameraRight.z};
            const Vec3 forward = camera.forward();
            const Vec3 terrainForward{forward.x, -forward.y, forward.z};
            const Vec3 terrainUp = normalize(cross(terrainForward, terrainRight));

            Mat4 matrix = identity();
            matrix.m[0] = terrainRight.x;
            matrix.m[4] = terrainRight.y;
            matrix.m[8] = terrainRight.z;
            matrix.m[12] = -dot(terrainRight, position);

            matrix.m[1] = terrainUp.x;
            matrix.m[5] = terrainUp.y;
            matrix.m[9] = terrainUp.z;
            matrix.m[13] = -dot(terrainUp, position);

            matrix.m[2] = terrainForward.x;
            matrix.m[6] = terrainForward.y;
            matrix.m[10] = terrainForward.z;
            matrix.m[14] = -dot(terrainForward, position);
            return matrix;
        }

        Frustum makeFrustum(const Camera& camera, Vec3 position, float aspect)
        {
            const Vec3 cameraRight = camera.right();
            const Vec3 terrainRight{-cameraRight.x, -cameraRight.y, -cameraRight.z};
            const Vec3 forward = camera.forward();
            const Vec3 terrainForward{forward.x, -forward.y, forward.z};
            const Vec3 terrainUp = normalize(cross(terrainForward, terrainRight));
            const float tanHalfVertical = std::tan(FieldOfViewRadians * 0.5f);

            return {
                position,
                terrainRight,
                terrainUp,
                terrainForward,
                tanHalfVertical,
                tanHalfVertical * aspect
            };
        }

        bool aabbIntersectsFrustum(const Frustum& frustum, Vec3 minCorner, Vec3 maxCorner)
        {
            const Vec3 center{
                (minCorner.x + maxCorner.x) * 0.5f,
                (minCorner.y + maxCorner.y) * 0.5f,
                (minCorner.z + maxCorner.z) * 0.5f
            };
            const Vec3 extent{
                (maxCorner.x - minCorner.x) * 0.5f,
                (maxCorner.y - minCorner.y) * 0.5f,
                (maxCorner.z - minCorner.z) * 0.5f
            };
            const Vec3 relative{
                center.x - frustum.position.x,
                center.y - frustum.position.y,
                center.z - frustum.position.z
            };

            const float viewX = dot(relative, frustum.right);
            const float viewY = dot(relative, frustum.up);
            const float viewZ = dot(relative, frustum.forward);
            const float radiusX =
                std::abs(frustum.right.x) * extent.x +
                std::abs(frustum.right.y) * extent.y +
                std::abs(frustum.right.z) * extent.z;
            const float radiusY =
                std::abs(frustum.up.x) * extent.x +
                std::abs(frustum.up.y) * extent.y +
                std::abs(frustum.up.z) * extent.z;
            const float radiusZ =
                std::abs(frustum.forward.x) * extent.x +
                std::abs(frustum.forward.y) * extent.y +
                std::abs(frustum.forward.z) * extent.z;

            if (viewZ + radiusZ < TerrainNearPlane || viewZ - radiusZ > TerrainFarPlane)
            {
                return false;
            }
            if (std::abs(viewX) > viewZ * frustum.tanHalfHorizontal + radiusX + radiusZ * frustum.tanHalfHorizontal)
            {
                return false;
            }
            if (std::abs(viewY) > viewZ * frustum.tanHalfVertical + radiusY + radiusZ * frustum.tanHalfVertical)
            {
                return false;
            }

            return true;
        }

    }

    bool Renderer::QueueFamilyIndices::complete() const
    {
        return graphics != UINT32_MAX && present != UINT32_MAX;
    }

    Renderer::Renderer(GLFWwindow* window)
        : window_(window)
    {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        collectHardwareInfo();
        createDevice();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createSceneRenderPass();
        createDepthResources();
        createDescriptorSetLayout();
        createTerrainVertexDescriptorSetLayout();
        createPipeline();
        createUiPipeline();
        createTerrainPipeline();
        createParticlePipeline();
        createSelectionPipeline();
        createCommandPool();
        createPerformanceQueries();
        createSampler();
        createDescriptorPool();
        createSceneTargets();
        createFramebuffers();
        createTextures();
        createFont();
        createTextVertexBuffer();
        createUiBuffers();
        createParticleBuffers();
        createSelectionLineBuffer();
        createPlayerMesh();
        initializeRmlUi();
        loadWorldConfig();
        loadRenderConfig();
        loadHeightLut();
        initializeAudio();
        createCommandBuffers();
        createSyncObjects();
    }

    Renderer::~Renderer()
    {
        stopTerrainWorkers();
        stopChunkLoadWorker();
        enqueueSaveAllRuntimeChunks();
        stopSaveWorker();
        vkDeviceWaitIdle(device_);
        shutdownRmlUi();
        shutdownAudio();

        cleanupSwapchain();
        destroyTexture(terrainTextureArray_);
        destroyTexture(fluidTextureArray_);
        destroyTexture(playerTexture_);
        destroyTexture(font_);
        destroyTexture(white_);
        destroyTexture(lobbyTitle_);
        destroyTexture(lobbyBackground_);
        destroyTexture(crosshair_);
        destroyTexture(climatePrecipitationOverlay_);
        destroyTexture(climateTemperatureOverlay_);
        destroyTexture(moon_);
        destroyTexture(sun_);
        destroyTexture(itemTextureArray_);

        destroyAllTerrainChunks();
        destroyTerrainMesh(playerMesh_);
        if (textVertexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, textVertexBuffer_, nullptr);
        }
        if (textVertexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, textVertexMemory_, nullptr);
        }
        if (uiVertexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, uiVertexBuffer_, nullptr);
        }
        if (uiVertexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, uiVertexMemory_, nullptr);
        }
        if (uiIndexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, uiIndexBuffer_, nullptr);
        }
        if (uiIndexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, uiIndexMemory_, nullptr);
        }
        if (particleVertexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, particleVertexBuffer_, nullptr);
        }
        if (particleVertexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, particleVertexMemory_, nullptr);
        }
        if (particleIndexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, particleIndexBuffer_, nullptr);
        }
        if (particleIndexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, particleIndexMemory_, nullptr);
        }
        if (droppedItemVertexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, droppedItemVertexBuffer_, nullptr);
        }
        if (droppedItemVertexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, droppedItemVertexMemory_, nullptr);
        }
        if (droppedItemIndexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, droppedItemIndexBuffer_, nullptr);
        }
        if (droppedItemIndexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, droppedItemIndexMemory_, nullptr);
        }
        if (droppedItemInstanceMapped_ != nullptr)
        {
            vkUnmapMemory(device_, droppedItemInstanceMemory_);
            droppedItemInstanceMapped_ = nullptr;
        }
        if (droppedItemInstanceBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, droppedItemInstanceBuffer_, nullptr);
        }
        if (droppedItemInstanceMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, droppedItemInstanceMemory_, nullptr);
        }
        if (selectionLineVertexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, selectionLineVertexBuffer_, nullptr);
        }
        if (selectionLineVertexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, selectionLineVertexMemory_, nullptr);
        }

        if (descriptorPool_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        }
        if (sampler_ != VK_NULL_HANDLE)
        {
            vkDestroySampler(device_, sampler_, nullptr);
        }

        for (size_t i = 0; i < imageAvailableSemaphores_.size(); ++i)
        {
            vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
            vkDestroySemaphore(device_, renderFinishedSemaphores_[i], nullptr);
            vkDestroyFence(device_, inFlightFences_[i], nullptr);
        }

        if (commandPool_ != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
        }
        if (timestampQueryPool_ != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(device_, timestampQueryPool_, nullptr);
        }
        if (terrainWireframePipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, terrainWireframePipeline_, nullptr);
        }
        if (fluidPipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, fluidPipeline_, nullptr);
        }
        if (playerPipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, playerPipeline_, nullptr);
        }
        if (particlePipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, particlePipeline_, nullptr);
        }
        if (itemPipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, itemPipeline_, nullptr);
        }
        if (particlePipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, particlePipelineLayout_, nullptr);
        }
        if (selectionPipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, selectionPipeline_, nullptr);
        }
        if (selectionPipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, selectionPipelineLayout_, nullptr);
        }
        if (terrainPipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, terrainPipeline_, nullptr);
        }
        if (terrainPipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, terrainPipelineLayout_, nullptr);
        }
        if (pipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, pipeline_, nullptr);
        }
        if (uiPipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, uiPipeline_, nullptr);
        }
        if (pipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        }
        if (uiPipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, uiPipelineLayout_, nullptr);
        }
        if (descriptorSetLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        }
        if (terrainVertexDescriptorSetLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device_, terrainVertexDescriptorSetLayout_, nullptr);
        }
        if (renderPass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
        }
        if (sceneRenderPass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device_, sceneRenderPass_, nullptr);
        }
        if (device_ != VK_NULL_HANDLE)
        {
            vkDestroyDevice(device_, nullptr);
        }
        if (surface_ != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
        }
        if (instance_ != VK_NULL_HANDLE)
        {
            vkDestroyInstance(instance_, nullptr);
        }
    }

    void Renderer::drawFrame(
        const Camera& camera,
        DVec3 cameraPosition,
        std::string_view fpsText,
        bool debugTextVisible,
        bool screenshotRequested,
        bool showPlayer,
        DVec3 playerPosition,
        float playerYaw,
        bool terrainWireframe,
        int climateOverlayMode,
        int menuOverlayMode,
        bool hudVisible,
        bool worldUpdateEnabled,
        bool gameSceneRenderEnabled,
        uint64_t worldTicks)
    {
        const Vec3 cameraPositionFloat = toVec3(cameraPosition);
        const Vec3 playerPositionFloat = toVec3(playerPosition);

        updateAudioListener(camera, cameraPositionFloat);
        updateMusicPlayback(menuOverlayMode, gameSceneRenderEnabled);
        updateTerrainDebugText();

        vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);
        if (timestampSupported_ && timestampQueryPool_ != VK_NULL_HANDLE && timestampQueryReady_[currentFrame_])
        {
            std::array<uint64_t, 2> timestamps{};
            const VkResult queryResult = vkGetQueryPoolResults(
                device_,
                timestampQueryPool_,
                currentFrame_ * 2,
                2,
                sizeof(uint64_t) * timestamps.size(),
                timestamps.data(),
                sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT);
            if (queryResult == VK_SUCCESS && timestamps[1] >= timestamps[0])
            {
                lastGpuFrameMs_ = static_cast<double>(timestamps[1] - timestamps[0]) * static_cast<double>(timestampPeriod_) / 1000000.0;
            }
        }
        if (worldUpdateEnabled)
        {
            updateLoadedChunks(playerPosition);
            processCompletedTerrainJobs();
        }
        const auto cpuStart = std::chrono::steady_clock::now();

        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapchain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            throw std::runtime_error("Failed to acquire swapchain image.");
        }

        vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);
        vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);

        VkBuffer screenshotBuffer = VK_NULL_HANDLE;
        VkDeviceMemory screenshotMemory = VK_NULL_HANDLE;
        const VkDeviceSize screenshotSize = static_cast<VkDeviceSize>(swapchainExtent_.width) * static_cast<VkDeviceSize>(swapchainExtent_.height) * 4u;
        if (screenshotRequested)
        {
            createBuffer(
                screenshotSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                screenshotBuffer,
                screenshotMemory);
        }

        if (showPlayer)
        {
            updatePlayerMesh(playerPositionFloat, playerYaw);
        }
        ensureClimateOverlayTexture(climateOverlayMode);

        recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, camera, cameraPositionFloat, playerPositionFloat, fpsText, debugTextVisible, screenshotBuffer, showPlayer, terrainWireframe, climateOverlayMode, menuOverlayMode, hudVisible, gameSceneRenderEnabled, worldTicks);

        VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentFrame_]};

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]) != VK_SUCCESS)
        {
            if (screenshotBuffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(device_, screenshotBuffer, nullptr);
                vkFreeMemory(device_, screenshotMemory, nullptr);
            }
            throw std::runtime_error("Failed to submit draw command buffer.");
        }
        if (timestampSupported_ && timestampQueryPool_ != VK_NULL_HANDLE)
        {
            timestampQueryReady_[currentFrame_] = true;
        }

        if (screenshotBuffer != VK_NULL_HANDLE)
        {
            vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);
            saveScreenshot(screenshotMemory, screenshotSize);
            vkDestroyBuffer(device_, screenshotBuffer, nullptr);
            vkFreeMemory(device_, screenshotMemory, nullptr);
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain_;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(presentQueue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized_)
        {
            framebufferResized_ = false;
            recreateSwapchain();
        }
        else if (result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to present swapchain image.");
        }

        const auto cpuEnd = std::chrono::steady_clock::now();
        updatePerformanceText(std::chrono::duration<double, std::milli>(cpuEnd - cpuStart).count());
        currentFrame_ = (currentFrame_ + 1) % MaxFramesInFlight;
    }

    void Renderer::setFramebufferResized()
    {
        framebufferResized_ = true;
    }

    bool Renderer::playerColliderIntersectsTerrain(DVec3 playerPosition) const
    {
        return gameplay::BlockInteractionSystem::playerColliderIntersectsTerrain(
            playerPosition,
            [this](int x, int y, int z) { return terrainCellBlocksPlayer(x, y, z); });
    }

    bool Renderer::editBlockInView(DVec3 origin, Vec3 direction, bool placeRock, DVec3 playerPosition)
    {
        BlockRaycastHit hit{};
        if (!raycastBlock(origin, direction, hit))
        {
            return false;
        }

        if (placeRock && blockIntersectsPlayerCollider(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ, BlockRock, playerPosition))
        {
            return false;
        }

        if (!placeRock)
        {
            return breakBlockAtHit(hit);
        }

        const bool changed = setBlockAtWorld(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ, BlockRock);
        if (changed)
        {
            rebuildEditedChunkMeshes(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ);
            playBlockPlaceSound(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ);
        }
        return changed;
    }

    void Renderer::updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, DVec3, float deltaSeconds)
    {
        const gameplay::BlockBreakingUpdate update = gameplay::BlockInteractionSystem::updateBreaking(
            blockBreaking_,
            origin,
            direction,
            breaking,
            deltaSeconds,
            [this](int x, int y, int z) { return blockAtWorld(x, y, z); },
            [this](uint16_t block) -> const BlockDefinition& { return blockDefinition(block); });

        if (update.spawnMiningParticle)
        {
            spawnBlockMiningParticle(update.hit, update.block);
        }
        if (update.breakBlock)
        {
            breakBlockAtHit(update.hit);
        }
    }

    bool Renderer::pickupDroppedItemInView(DVec3 origin, Vec3 direction)
    {
        WorldEntityHandle itemHandle{};
        if (!raycastDroppedItem(origin, direction, itemHandle))
        {
            return false;
        }

        RuntimeChunk* chunk = worldRuntime_.find(itemHandle.chunkKey);
        if (chunk == nullptr || !chunk->data ||
            itemHandle.index >= chunk->data->entities.size())
        {
            return false;
        }

        WorldEntity& item = chunk->data->entities[itemHandle.index];
        item.collecting = true;
        item.collectAge = 0.0f;
        setWorldEntityGrounded(item, false);
        item.velocity = {};
        item.previousPosition = item.position;
        item.renderSpinX = 8.0f;
        item.renderSpin = 8.0f;
        item.renderSpinZ = 8.0f;
        markRuntimeChunkDataDirty(*chunk);
        return true;
    }

    bool Renderer::dropSelectedHotbarItem(bool wholeStack, DVec3 playerPosition, Vec3 direction)
    {
        const size_t slotIndex = static_cast<size_t>(std::clamp(hotbarSelectedSlot_, 0, 9));
        const ItemStack& slot = playerInventory_.slot(slotIndex);
        if (slot.itemId == 0 || slot.count == 0 || static_cast<size_t>(slot.itemId) >= itemDefinitions_.size())
        {
            return false;
        }

        const uint16_t dropCount = wholeStack ? slot.count : 1u;
        ItemStack dropStack{};
        dropStack.itemId = slot.itemId;
        dropStack.count = dropCount;
        WorldEntity item = world::DroppedItemSystem::createManualDropEntity(
            dropStack,
            playerPosition,
            direction,
            [this]() { return allocateWorldEntityId(); });

        if (!addWorldEntity(std::move(item)))
        {
            return false;
        }

        if (!playerInventory_.removeFromSlot(slotIndex, dropCount))
        {
            return false;
        }
        updateInventoryUi();
        return true;
    }

    uint16_t Renderer::addItemToPlayerInventory(ItemStack stack)
    {
        if (stack.itemId == 0 || stack.count == 0 || static_cast<size_t>(stack.itemId) >= itemDefinitions_.size())
        {
            return stack.count;
        }

        const uint16_t maxStack = itemDefinitions_[stack.itemId].stackSize;
        if (maxStack == 0)
        {
            return stack.count;
        }

        stack.count = playerInventory_.add(stack, itemDefinitions_);
        updateInventoryUi();
        return stack.count;
    }

    std::array<ItemStack, gameplay::PlayerInventory::SlotCount> Renderer::inventorySnapshot() const
    {
        return playerInventory_.snapshot();
    }

    void Renderer::setInventorySnapshot(const std::array<ItemStack, gameplay::PlayerInventory::SlotCount>& slots)
    {
        playerInventory_.setSlots(slots, itemDefinitions_);
        updateInventoryUi();
        updateInventoryDebugSlots();
        updateInventoryCursorUi();
        updateItemTooltipUi();
    }

    bool Renderer::blockIntersectsPlayerCollider(int x, int y, int z, uint16_t block, DVec3 playerPosition) const
    {
        return gameplay::BlockInteractionSystem::blockIntersectsPlayerCollider(
            x,
            y,
            z,
            blockDefinition(block),
            playerPosition);
    }

    void Renderer::updateBlockSelection(DVec3 origin, Vec3 direction)
    {
        BlockRaycastHit hit{};
        hasSelectedBlock_ = raycastBlock(origin, direction, hit);
        if (!hasSelectedBlock_)
        {
            return;
        }

        selectedBlockX_ = hit.blockX;
        selectedBlockY_ = hit.blockY;
        selectedBlockZ_ = hit.blockZ;
        selectedBlockId_ = blockAtWorld(hit.blockX, hit.blockY, hit.blockZ);
    }

    std::string Renderer::selectedBlockText() const
    {
        if (!hasSelectedBlock_)
        {
            return "LOOKAT: none";
        }

        const BlockDefinition& definition = blockDefinition(selectedBlockId_);
        return "LOOKAT: " + definition.name +
            "[" + std::to_string(selectedBlockId_) + "] (x: " + std::to_string(wrapBlockCoordinate(selectedBlockX_)) +
            ", y: " + std::to_string(selectedBlockY_) +
            ", z: " + std::to_string(wrapBlockCoordinate(selectedBlockZ_)) + ")";
    }

    std::string Renderer::climateText(DVec3 position) const
    {
        const int blockX = blockCoordinateXz(position.x);
        const int blockZ = blockCoordinateXz(position.z);
        const int chunkX = floorDiv(blockX, ChunkSizeX);
        const int chunkZ = floorDiv(blockZ, ChunkSizeZ);
        const int localX = positiveModulo(blockX, ChunkSizeX);
        const int localZ = positiveModulo(blockZ, ChunkSizeZ);
        const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);

        float temperature = 0.0f;
        float precipitation = 0.0f;
        const RuntimeChunk* chunk = worldRuntime_.find(chunkKey(chunkX, chunkZ));
        if (chunk != nullptr && chunk->data)
        {
            const ChunkData& data = *chunk->data;
            temperature = decodeClimateValue(data.temperature[column]);
            precipitation = decodeClimateValue(data.precipitation[column]);
        }
        else
        {
            const int wrappedX = wrapBlockCoordinate(blockX);
            const int wrappedZ = wrapBlockCoordinate(blockZ);
            const float temperatureNoise = sampleTileableClimateNoise(
                wrappedX,
                wrappedZ,
                temperatureNoiseFeatureScale_,
                temperatureNoiseSimplexScale_,
                temperatureNoiseOctaveCount_,
                temperatureNoiseLacunarity_,
                temperatureNoiseGain_,
                    temperatureSeed());
            const float precipitationNoise = sampleTileableClimateNoise(
                wrappedX,
                wrappedZ,
                precipitationNoiseFeatureScale_,
                precipitationNoiseSimplexScale_,
                precipitationNoiseOctaveCount_,
                precipitationNoiseLacunarity_,
                precipitationNoiseGain_,
                precipitationSeed());
            temperature = temperatureAtWrapped(wrappedZ, temperatureNoise);
            precipitation = precipitationAtNoise(precipitationNoise);
        }

        std::ostringstream text;
        text << "CLIMATE: T[" << std::fixed << std::setprecision(3) << temperature << "] P[" << precipitation << "]";
        return text.str();
    }

    void Renderer::createInstance()
    {
        uint32_t extensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (glfwExtensions == nullptr || extensionCount == 0)
        {
            throw std::runtime_error("Failed to get required Vulkan instance extensions.");
        }

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + extensionCount);
        if (instanceExtensionAvailable(PhysicalDeviceProperties2Extension))
        {
            extensions.push_back(PhysicalDeviceProperties2Extension);
        }

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan instance.");
        }
    }

    void Renderer::createSurface()
    {
        if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create window surface.");
        }
    }

    void Renderer::pickPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
        if (deviceCount == 0)
        {
            throw std::runtime_error("No Vulkan physical device found.");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

        for (VkPhysicalDevice device : devices)
        {
            if (isDeviceSuitable(device))
            {
                physicalDevice_ = device;
                return;
            }
        }

        throw std::runtime_error("No suitable Vulkan physical device found.");
    }

    void Renderer::collectHardwareInfo()
    {
        cpuText_ = readCpuName();

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
        gpuText_ = properties.deviceName;
        vulkanText_ = "VULKAN: " + formatVersion(properties.apiVersion);
        driverText_ = "DRIVER: " + formatVersion(properties.driverVersion);
        timestampPeriod_ = properties.limits.timestampPeriod;
        log::info("CPU: " + cpuText_);
        log::info("GPU: " + gpuText_);
        log::info(vulkanText_);
        log::info(driverText_);
        const bool memoryProperties2Supported =
            vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceMemoryProperties2") != nullptr ||
            vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceMemoryProperties2KHR") != nullptr;
        memoryBudgetSupported_ = memoryProperties2Supported && deviceExtensionAvailable(physicalDevice_, MemoryBudgetExtension);

        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
        for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; ++i)
        {
            const VkMemoryHeap& heap = memoryProperties.memoryHeaps[i];
            if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0 && heap.size > localMemoryHeapSize_)
            {
                localMemoryHeapIndex_ = i;
                localMemoryHeapSize_ = heap.size;
            }
        }
        updateVramText();
    }

    void Renderer::createDevice()
    {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);
        std::set<uint32_t> uniqueFamilies = {indices.graphics, indices.present};
        float queuePriority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;

        for (uint32_t family : uniqueFamilies)
        {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;
            queueInfos.push_back(queueInfo);
        }

        std::vector<const char*> enabledExtensions(DeviceExtensions.begin(), DeviceExtensions.end());
        if (memoryBudgetSupported_)
        {
            enabledExtensions.push_back(MemoryBudgetExtension);
        }

        VkPhysicalDeviceFeatures features{};
        features.fillModeNonSolid = VK_TRUE;
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.pEnabledFeatures = &features;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
        createInfo.ppEnabledExtensionNames = enabledExtensions.data();

        if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan device.");
        }

        vkGetDeviceQueue(device_, indices.graphics, 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, indices.present, 0, &presentQueue_);
    }

    void Renderer::createSwapchain()
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, presentModes.data());

        VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
        VkPresentModeKHR presentMode = choosePresentMode(presentModes);
        VkExtent2D extent = chooseExtent(capabilities);

        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
        {
            imageCount = capabilities.maxImageCount;
        }

        QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);
        uint32_t queueFamilyIndices[] = {indices.graphics, indices.present};

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface_;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0)
        {
            throw std::runtime_error("Swapchain does not support screenshot transfer.");
        }
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        if (indices.graphics != indices.present)
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create swapchain.");
        }

        vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
        swapchainImages_.resize(imageCount);
        vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());

        swapchainImageFormat_ = surfaceFormat.format;
        swapchainExtent_ = extent;
    }

    void Renderer::createImageViews()
    {
        swapchainImageViews_.resize(swapchainImages_.size());
        for (size_t i = 0; i < swapchainImages_.size(); ++i)
        {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = swapchainImages_[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = swapchainImageFormat_;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_, &createInfo, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create swapchain image view.");
            }
        }
    }

    void Renderer::createRenderPass()
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchainImageFormat_;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = DepthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = 1;
        createInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create render pass.");
        }

    }

    void Renderer::createSceneRenderPass()
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchainImageFormat_;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = DepthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkSubpassDependency readDependency{};
        readDependency.srcSubpass = 0;
        readDependency.dstSubpass = VK_SUBPASS_EXTERNAL;
        readDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        readDependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        readDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        readDependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
        std::array<VkSubpassDependency, 2> dependencies = {dependency, readDependency};

        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        createInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(device_, &createInfo, nullptr, &sceneRenderPass_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create scene render pass.");
        }
    }

    void Renderer::createDepthResources()
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {swapchainExtent_.width, swapchainExtent_.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = DepthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device_, &imageInfo, nullptr, &depthImage_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create depth image.");
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, depthImage_, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &depthMemory_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate depth image memory.");
        }

        vkBindImageMemory(device_, depthImage_, depthMemory_, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depthImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = DepthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &viewInfo, nullptr, &depthImageView_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create depth image view.");
        }
    }

    void Renderer::createDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorCount = 1;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = 1;
        createInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(device_, &createInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor set layout.");
        }
    }

    void Renderer::createTerrainVertexDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorCount = 1;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = 1;
        createInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(device_, &createInfo, nullptr, &terrainVertexDescriptorSetLayout_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain vertex descriptor set layout.");
        }
    }

    void Renderer::createPipeline()
    {
        const std::filesystem::path shaderDir = shaderDirectory();
        VkShaderModule vertShader = createShaderModule((shaderDir / "sprite.vert.spv").string());
        VkShaderModule fragShader = createShaderModule((shaderDir / "sprite.frag.spv").string());

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(TextVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attributes{};
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[0].offset = offsetof(TextVertex, x);
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[1].offset = offsetof(TextVertex, u);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDescription;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlend;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(SpritePush);
        static_assert(sizeof(SpritePush) == sizeof(float) * 12);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout_;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create pipeline layout.");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout_;
        pipelineInfo.renderPass = renderPass_;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create graphics pipeline.");
        }

        vkDestroyShaderModule(device_, fragShader, nullptr);
        vkDestroyShaderModule(device_, vertShader, nullptr);
    }

    void Renderer::createUiPipeline()
    {
        const std::filesystem::path shaderDir = shaderDirectory();
        VkShaderModule vertShader = createShaderModule((shaderDir / "rmlui.vert.spv").string());
        VkShaderModule fragShader = createShaderModule((shaderDir / "rmlui.frag.spv").string());

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(UiVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> attributes{};
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[0].offset = offsetof(UiVertex, x);
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[1].offset = offsetof(UiVertex, r);
        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[2].offset = offsetof(UiVertex, u);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDescription;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlend;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(UiPush);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout_;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &uiPipelineLayout_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create RmlUi pipeline layout.");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = uiPipelineLayout_;
        pipelineInfo.renderPass = renderPass_;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &uiPipeline_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create RmlUi pipeline.");
        }

        vkDestroyShaderModule(device_, fragShader, nullptr);
        vkDestroyShaderModule(device_, vertShader, nullptr);
    }

    void Renderer::createTerrainPipeline()
    {
        const std::filesystem::path shaderDir = shaderDirectory();
        VkShaderModule vertShader = createShaderModule((shaderDir / "terrain.vert.spv").string());
        VkShaderModule playerVertShader = createShaderModule((shaderDir / "player.vert.spv").string());
        VkShaderModule fragShader = createShaderModule((shaderDir / "terrain.frag.spv").string());
        VkShaderModule fluidFragShader = createShaderModule((shaderDir / "fluid.frag.spv").string());

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkVertexInputBindingDescription playerBindingDescription{};
        playerBindingDescription.binding = 0;
        playerBindingDescription.stride = sizeof(TerrainVertex);
        playerBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 5> playerAttributes{};
        playerAttributes[0].binding = 0;
        playerAttributes[0].location = 0;
        playerAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        playerAttributes[0].offset = offsetof(TerrainVertex, x);
        playerAttributes[1].binding = 0;
        playerAttributes[1].location = 1;
        playerAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        playerAttributes[1].offset = offsetof(TerrainVertex, u);
        playerAttributes[2].binding = 0;
        playerAttributes[2].location = 2;
        playerAttributes[2].format = VK_FORMAT_R32_SFLOAT;
        playerAttributes[2].offset = offsetof(TerrainVertex, ao);
        playerAttributes[3].binding = 0;
        playerAttributes[3].location = 3;
        playerAttributes[3].format = VK_FORMAT_R32_SFLOAT;
        playerAttributes[3].offset = offsetof(TerrainVertex, textureLayer);
        playerAttributes[4].binding = 0;
        playerAttributes[4].location = 4;
        playerAttributes[4].format = VK_FORMAT_R32_SFLOAT;
        playerAttributes[4].offset = offsetof(TerrainVertex, mipDistanceScale);

        VkPipelineVertexInputStateCreateInfo playerVertexInput{};
        playerVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        playerVertexInput.vertexBindingDescriptionCount = 1;
        playerVertexInput.pVertexBindingDescriptions = &playerBindingDescription;
        playerVertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(playerAttributes.size());
        playerVertexInput.pVertexAttributeDescriptions = playerAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.blendEnable = VK_FALSE;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlend;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(TerrainPush);
        static_assert(sizeof(TerrainPush) == sizeof(float) * 24);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        std::array<VkDescriptorSetLayout, 2> terrainSetLayouts = {
            descriptorSetLayout_,
            terrainVertexDescriptorSetLayout_
        };
        layoutInfo.setLayoutCount = static_cast<uint32_t>(terrainSetLayouts.size());
        layoutInfo.pSetLayouts = terrainSetLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &terrainPipelineLayout_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain pipeline layout.");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = terrainPipelineLayout_;
        pipelineInfo.renderPass = renderPass_;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &terrainPipeline_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain pipeline.");
        }

        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        depthStencil.depthWriteEnable = VK_FALSE;
        stages[1].module = fluidFragShader;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &fluidPipeline_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create fluid pipeline.");
        }

        colorBlend.blendEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_TRUE;
        stages[1].module = fragShader;
        rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &terrainWireframePipeline_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain wireframe pipeline.");
        }

        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        vertStage.module = playerVertShader;
        stages[0] = vertStage;
        pipelineInfo.pVertexInputState = &playerVertexInput;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &playerPipeline_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create player pipeline.");
        }

        vkDestroyShaderModule(device_, fragShader, nullptr);
        vkDestroyShaderModule(device_, fluidFragShader, nullptr);
        vkDestroyShaderModule(device_, playerVertShader, nullptr);
        vkDestroyShaderModule(device_, vertShader, nullptr);
    }

    void Renderer::createParticlePipeline()
    {
        const std::filesystem::path shaderDir = shaderDirectory();
        VkShaderModule vertShader = createShaderModule((shaderDir / "player.vert.spv").string());
        VkShaderModule itemVertShader = createShaderModule((shaderDir / "item.vert.spv").string());
        VkShaderModule fragShader = createShaderModule((shaderDir / "terrain.frag.spv").string());

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";

        std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(TerrainVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 5> attributes{};
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(TerrainVertex, x);
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[1].offset = offsetof(TerrainVertex, u);
        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32_SFLOAT;
        attributes[2].offset = offsetof(TerrainVertex, ao);
        attributes[3].binding = 0;
        attributes[3].location = 3;
        attributes[3].format = VK_FORMAT_R32_SFLOAT;
        attributes[3].offset = offsetof(TerrainVertex, textureLayer);
        attributes[4].binding = 0;
        attributes[4].location = 4;
        attributes[4].format = VK_FORMAT_R32_SFLOAT;
        attributes[4].offset = offsetof(TerrainVertex, mipDistanceScale);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        std::array<VkVertexInputBindingDescription, 2> itemBindings{};
        itemBindings[0].binding = 0;
        itemBindings[0].stride = sizeof(ItemLocalVertex);
        itemBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        itemBindings[1].binding = 1;
        itemBindings[1].stride = sizeof(DroppedItemInstance);
        itemBindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        std::array<VkVertexInputAttributeDescription, 5> itemAttributes{};
        itemAttributes[0].binding = 0;
        itemAttributes[0].location = 0;
        itemAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        itemAttributes[0].offset = offsetof(ItemLocalVertex, x);
        itemAttributes[1].binding = 0;
        itemAttributes[1].location = 1;
        itemAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        itemAttributes[1].offset = offsetof(ItemLocalVertex, u);
        itemAttributes[2].binding = 0;
        itemAttributes[2].location = 2;
        itemAttributes[2].format = VK_FORMAT_R32_SFLOAT;
        itemAttributes[2].offset = offsetof(ItemLocalVertex, ao);
        itemAttributes[3].binding = 1;
        itemAttributes[3].location = 3;
        itemAttributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        itemAttributes[3].offset = offsetof(DroppedItemInstance, centerX);
        itemAttributes[4].binding = 1;
        itemAttributes[4].location = 4;
        itemAttributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        itemAttributes[4].offset = offsetof(DroppedItemInstance, rotationY);

        VkPipelineVertexInputStateCreateInfo itemVertexInput{};
        itemVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        itemVertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(itemBindings.size());
        itemVertexInput.pVertexBindingDescriptions = itemBindings.data();
        itemVertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(itemAttributes.size());
        itemVertexInput.pVertexAttributeDescriptions = itemAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.blendEnable = VK_FALSE;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlend;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(TerrainPush);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout_;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &particlePipelineLayout_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create particle pipeline layout.");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = particlePipelineLayout_;
        pipelineInfo.renderPass = renderPass_;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &particlePipeline_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create particle pipeline.");
        }

        depthStencil.depthWriteEnable = VK_TRUE;
        stages[0].module = itemVertShader;
        pipelineInfo.pVertexInputState = &itemVertexInput;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &itemPipeline_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create item pipeline.");
        }

        vkDestroyShaderModule(device_, fragShader, nullptr);
        vkDestroyShaderModule(device_, itemVertShader, nullptr);
        vkDestroyShaderModule(device_, vertShader, nullptr);
    }

    void Renderer::createSelectionPipeline()
    {
        const std::filesystem::path shaderDir = shaderDirectory();
        VkShaderModule vertShader = createShaderModule((shaderDir / "selection.vert.spv").string());
        VkShaderModule fragShader = createShaderModule((shaderDir / "selection.frag.spv").string());

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(LineVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attribute{};
        attribute.binding = 0;
        attribute.location = 0;
        attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
        attribute.offset = offsetof(LineVertex, x);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDescription;
        vertexInput.vertexAttributeDescriptionCount = 1;
        vertexInput.pVertexAttributeDescriptions = &attribute;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.blendEnable = VK_FALSE;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlend;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(TerrainPush);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &selectionPipelineLayout_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create selection pipeline layout.");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = selectionPipelineLayout_;
        pipelineInfo.renderPass = renderPass_;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &selectionPipeline_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create selection pipeline.");
        }

        vkDestroyShaderModule(device_, fragShader, nullptr);
        vkDestroyShaderModule(device_, vertShader, nullptr);
    }

    void Renderer::createFramebuffers()
    {
        sceneFramebuffers_.resize(swapchainImageViews_.size());
        for (size_t i = 0; i < sceneFramebuffers_.size(); ++i)
        {
            std::array<VkImageView, 2> sceneAttachments = {sceneColorTargets_[i].view, sceneDepthTargets_[i].view};

            VkFramebufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            createInfo.renderPass = sceneRenderPass_;
            createInfo.attachmentCount = static_cast<uint32_t>(sceneAttachments.size());
            createInfo.pAttachments = sceneAttachments.data();
            createInfo.width = swapchainExtent_.width;
            createInfo.height = swapchainExtent_.height;
            createInfo.layers = 1;

            if (vkCreateFramebuffer(device_, &createInfo, nullptr, &sceneFramebuffers_[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create scene framebuffer.");
            }
        }

        framebuffers_.resize(swapchainImageViews_.size());
        for (size_t i = 0; i < swapchainImageViews_.size(); ++i)
        {
            std::array<VkImageView, 2> attachments = {swapchainImageViews_[i], depthImageView_};

            VkFramebufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            createInfo.renderPass = renderPass_;
            createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            createInfo.pAttachments = attachments.data();
            createInfo.width = swapchainExtent_.width;
            createInfo.height = swapchainExtent_.height;
            createInfo.layers = 1;

            if (vkCreateFramebuffer(device_, &createInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create framebuffer.");
            }
        }
    }

    void Renderer::createCommandPool()
    {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);

        VkCommandPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        createInfo.queueFamilyIndex = indices.graphics;

        if (vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create command pool.");
        }
    }

    void Renderer::createPerformanceQueries()
    {
        performanceSampleStart_ = std::chrono::steady_clock::now();
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, families.data());

        if (indices.graphics >= families.size() || families[indices.graphics].timestampValidBits == 0)
        {
            timestampSupported_ = false;
            gpuFrameText_ = "GPU: N/A";
            return;
        }

        timestampSupported_ = true;
        VkQueryPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        createInfo.queryCount = MaxFramesInFlight * 2;

        if (vkCreateQueryPool(device_, &createInfo, nullptr, &timestampQueryPool_) != VK_SUCCESS)
        {
            timestampSupported_ = false;
            gpuFrameText_ = "GPU: N/A";
            timestampQueryPool_ = VK_NULL_HANDLE;
            return;
        }
    }

    void Renderer::createSampler()
    {
        VkSamplerCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        createInfo.magFilter = VK_FILTER_NEAREST;
        createInfo.minFilter = VK_FILTER_NEAREST;
        createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        createInfo.minLod = 0.0f;
        createInfo.maxLod = VK_LOD_CLAMP_NONE;

        if (vkCreateSampler(device_, &createInfo, nullptr, &sampler_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create texture sampler.");
        }
    }

    void Renderer::createDescriptorPool()
    {
        constexpr uint32_t MaxTextureDescriptorSets = 256;
        constexpr uint32_t MaxTerrainVertexDescriptorSets = 65536;
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = MaxTextureDescriptorSets;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[1].descriptorCount = MaxTerrainVertexDescriptorSets;

        VkDescriptorPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        createInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        createInfo.pPoolSizes = poolSizes.data();
        createInfo.maxSets = MaxTextureDescriptorSets + MaxTerrainVertexDescriptorSets;

        if (vkCreateDescriptorPool(device_, &createInfo, nullptr, &descriptorPool_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor pool.");
        }
    }

    void Renderer::createSceneTargets()
    {
        sceneColorTargets_.clear();
        sceneDepthTargets_.clear();
        sceneColorTargets_.reserve(swapchainImageViews_.size());
        sceneDepthTargets_.reserve(swapchainImageViews_.size());
        for (size_t i = 0; i < swapchainImageViews_.size(); ++i)
        {
            sceneColorTargets_.push_back(createRenderTargetTexture(
                swapchainImageFormat_,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
            sceneDepthTargets_.push_back(createRenderTargetTexture(
                DepthFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL));
        }
    }

    void Renderer::createTextures()
    {
        const std::filesystem::path assetDir = assetDirectory();
        const std::string blockTextureDir = (assetDir / "textures" / "block").string() + "/";
        const std::string fluidTextureDir = (assetDir / "textures" / "fluid").string() + "/";
        const std::string itemTextureDir = (assetDir / "textures" / "item").string() + "/";
        sun_ = createTexture((assetDir / "textures" / "sky" / "Sun.png").string());
        moon_ = createTexture((assetDir / "textures" / "sky" / "Moon.png").string());
        crosshair_ = createTexture((assetDir / "textures" / "ui" / "Crosshair.png").string());
        const std::array<unsigned char, 4> whitePixel = {255u, 255u, 255u, 255u};
        white_ = createTextureFromRgba(whitePixel.data(), 1, 1);
        lobbyBackground_ = createTexture((assetDir / "textures" / "block" / "rock.png").string());
        lobbyTitle_ = createTexture((assetDir / "textures" / "ui" / "Title.png").string());
        playerTexture_ = createTextureArray({(assetDir / "textures" / "character" / "Character.png").string()});

        const std::vector<char> blockDefinitionData = readFile((assetDir / "data" / "blocks.json").string());
        const std::string blockDefinitionText(blockDefinitionData.begin(), blockDefinitionData.end());
        const std::vector<data::ParsedBlockDefinition> blockDefinitions = data::parseBlockDefinitions(blockDefinitionText);

        const std::vector<char> itemDefinitionData = readFile((assetDir / "data" / "items.json").string());
        const std::string itemDefinitionText(itemDefinitionData.begin(), itemDefinitionData.end());
        const std::vector<data::ParsedItemDefinition> itemDefinitions = data::parseItemDefinitions(itemDefinitionText);

        auto parseItemRenderType = [](const std::string& value)
        {
            if (value == "extruded_sprite" || value == "sprite")
            {
                return ItemRenderType::ExtrudedSprite;
            }
            throw std::runtime_error("Unknown item render type: " + value);
        };

        itemDefinitions_.assign(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u, {});
        itemSpriteMeshes_.assign(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u, {});
        itemIdByKey_.clear();
        std::vector<std::string> itemTextureNames;
        std::unordered_map<std::string, uint32_t> itemTextureLayerByName;
        auto layerForItemTexture = [&](const std::string& textureName) -> uint32_t
        {
            auto it = itemTextureLayerByName.find(textureName);
            if (it != itemTextureLayerByName.end())
            {
                return it->second;
            }

            const uint32_t layer = static_cast<uint32_t>(itemTextureNames.size());
            itemTextureLayerByName.emplace(textureName, layer);
            itemTextureNames.push_back(textureName);
            return layer;
        };
        for (const data::ParsedItemDefinition& definition : itemDefinitions)
        {
            const std::string droppedTexture = definition.droppedTexture != "none" ? definition.droppedTexture : definition.texture;
            const std::string heldTexture = definition.heldTexture != "none" ? definition.heldTexture : droppedTexture;
            const std::string slotTexture = definition.slotTexture != "none" ? definition.slotTexture : droppedTexture;
            ItemDefinition itemDefinition{};
            itemDefinition.key = definition.key;
            itemDefinition.name = definition.name;
            itemDefinition.slotTexture = slotTexture;
            itemDefinition.droppedTexture = droppedTexture;
            itemDefinition.heldTexture = heldTexture;
            itemDefinition.stackSize = definition.stackSize;
            itemDefinition.droppedRender = parseItemRenderType(definition.droppedRender);
            itemDefinition.heldRender = parseItemRenderType(definition.heldRender);
            if (droppedTexture != "none")
            {
                itemDefinition.droppedTextureLayer = layerForItemTexture(droppedTexture);
                itemSpriteMeshes_[definition.id] = buildItemSpriteMesh(std::filesystem::path(itemTextureDir) / (droppedTexture + ".png"));
            }
            if (heldTexture != "none")
            {
                itemDefinition.heldTextureLayer = layerForItemTexture(heldTexture);
            }
            itemDefinitions_[definition.id] = itemDefinition;
            if (!definition.key.empty())
            {
                itemIdByKey_[definition.key] = definition.id;
            }
        }

        std::vector<std::string> textureNames;
        std::unordered_map<std::string, uint32_t> textureLayerByName;
        auto layerForTexture = [&](const std::string& textureName) -> uint32_t
        {
            auto it = textureLayerByName.find(textureName);
            if (it != textureLayerByName.end())
            {
                return it->second;
            }

            const uint32_t layer = static_cast<uint32_t>(textureNames.size());
            textureLayerByName.emplace(textureName, layer);
            textureNames.push_back(textureName);
            return layer;
        };

        auto parseRenderType = [](const std::string& value)
        {
            if (value == "cube")
            {
                return BlockRenderType::Cube;
            }
            if (value == "cross")
            {
                return BlockRenderType::Cross;
            }
            if (value == "prop")
            {
                return BlockRenderType::Prop;
            }
            return BlockRenderType::None;
        };
        auto parseFaceOcclusion = [](const std::string& value)
        {
            if (value == "opaque")
            {
                return BlockFaceOcclusion::Opaque;
            }
            if (value == "cutout")
            {
                return BlockFaceOcclusion::Cutout;
            }
            return BlockFaceOcclusion::None;
        };
        auto parseAlphaMode = [](const std::string& value)
        {
            if (value == "cutout")
            {
                return BlockAlphaMode::Cutout;
            }
            if (value == "blend")
            {
                return BlockAlphaMode::Blend;
            }
            return BlockAlphaMode::Opaque;
        };

        blockDefinitions_.assign(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u, {});
        blockTextureLayers_.assign(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u, {});
        for (const data::ParsedBlockDefinition& definition : blockDefinitions)
        {
            BlockDefinition blockDefinition{};
            blockDefinition.name = definition.name;
            blockDefinition.renderType = parseRenderType(definition.renderType);
            blockDefinition.directional = definition.directional;
            blockDefinition.collision = definition.collision;
            blockDefinition.ao = definition.ao;
            blockDefinition.faceOcclusion = parseFaceOcclusion(definition.faceOcclusion);
            blockDefinition.sameBlockFaceCulling = definition.sameBlockFaceCulling;
            blockDefinition.alphaMode = parseAlphaMode(definition.alphaMode);
            blockDefinition.alphaCutoff = definition.alphaCutoff;
            blockDefinition.mipDistanceScale = definition.mipDistanceScale;
            blockDefinition.hardness = definition.hardness;
            blockDefinition.randomOffset = definition.randomOffset;
            for (size_t dropIndex = 0; dropIndex < definition.dropItemKeys.size(); ++dropIndex)
            {
                const auto itemIt = itemIdByKey_.find(definition.dropItemKeys[dropIndex]);
                if (itemIt == itemIdByKey_.end())
                {
                    log::warn("Block drop references unknown item key: " + definition.name + " -> " + definition.dropItemKeys[dropIndex]);
                    continue;
                }

                BlockDrop drop{};
                drop.itemId = itemIt->second;
                drop.min = dropIndex < definition.dropMins.size() ? definition.dropMins[dropIndex] : 1;
                drop.max = dropIndex < definition.dropMaxes.size() ? definition.dropMaxes[dropIndex] : drop.min;
                if (drop.max < drop.min)
                {
                    std::swap(drop.min, drop.max);
                }
                drop.chance = dropIndex < definition.dropChances.size() ? definition.dropChances[dropIndex] : 1.0f;
                if (drop.itemId != 0 && drop.max > 0)
                {
                    blockDefinition.drops.push_back(drop);
                }
            }
            blockDefinitions_[definition.id] = blockDefinition;

            BlockTextureLayers layers{};
            if (const auto it = definition.textures.find("all"); it != definition.textures.end())
            {
                layers.faces.fill(layerForTexture(it->second));
            }
            if (const auto it = definition.textures.find("topBottom"); it != definition.textures.end())
            {
                const uint32_t layer = layerForTexture(it->second);
                layers.faces[0] = layer;
                layers.faces[1] = layer;
            }
            if (const auto it = definition.textures.find("side"); it != definition.textures.end())
            {
                const uint32_t layer = layerForTexture(it->second);
                layers.faces[2] = layer;
                layers.faces[3] = layer;
                layers.faces[4] = layer;
                layers.faces[5] = layer;
            }
            if (const auto it = definition.textures.find("top"); it != definition.textures.end())
            {
                layers.faces[0] = layerForTexture(it->second);
            }
            if (const auto it = definition.textures.find("bottom"); it != definition.textures.end())
            {
                layers.faces[1] = layerForTexture(it->second);
            }
            if (!definition.propTexture.empty())
            {
                layers.faces.fill(layerForTexture(definition.propTexture));
            }
            blockTextureLayers_[definition.id] = layers;
        }

        for (size_t i = 0; i < blockBreakingTextureLayers_.size(); ++i)
        {
            blockBreakingTextureLayers_[i] = layerForTexture("breaking/destroy_stage_" + std::to_string(i));
        }

        const std::filesystem::path propModelDirectory = assetDir / "textures" / "block" / "model";
        std::unordered_set<std::string> checkedPropModels;
        for (const data::ParsedBlockDefinition& definition : blockDefinitions)
        {
            if (definition.renderType == "prop" && !definition.propModel.empty() && checkedPropModels.insert(definition.propModel).second)
            {
                assets::ensurePropModelBinary(propModelDirectory, definition.propModel);
            }
        }
        propMeshesByBlock_.clear();
        for (const data::ParsedBlockDefinition& definition : blockDefinitions)
        {
            if (definition.renderType != "prop" || definition.propModel.empty())
            {
                continue;
            }

            const std::filesystem::path dpmPath = propModelDirectory / (definition.propModel + ".dpm");
            assets::PropMesh mesh = assets::loadDpmRenderMesh(dpmPath);
            if (mesh.quads.empty())
            {
                log::warn("Prop model dpm could not be loaded: " + dpmPath.string());
                continue;
            }
            propMeshesByBlock_[definition.id] = std::move(mesh);
        }

        if (textureNames.empty())
        {
            throw std::runtime_error("No block textures were found in blocks.json.");
        }

        std::vector<std::string> texturePaths;
        texturePaths.reserve(textureNames.size());
        for (const std::string& textureName : textureNames)
        {
            texturePaths.push_back(blockTextureDir + textureName + ".png");
        }
        terrainTextureArray_ = createTextureArray(texturePaths);
        fluidTextureArray_ = createTextureArray({fluidTextureDir + "water.png"});
        if (!itemTextureNames.empty())
        {
            std::vector<std::string> itemTexturePaths;
            itemTexturePaths.reserve(itemTextureNames.size());
            for (const std::string& textureName : itemTextureNames)
            {
                itemTexturePaths.push_back(itemTextureDir + textureName + ".png");
            }
            itemTextureArray_ = createTextureArray(itemTexturePaths);
        }
    }

    void Renderer::createFont()
    {
        std::vector<char> fontData = readFile((assetDirectory() / "fonts" / "VCR_OSD_MONO.ttf").string());

        FT_Library library = nullptr;
        if (FT_Init_FreeType(&library) != 0)
        {
            throw std::runtime_error("Failed to initialize FreeType.");
        }

        FT_Face face = nullptr;
        if (FT_New_Memory_Face(
                library,
                reinterpret_cast<const FT_Byte*>(fontData.data()),
                static_cast<FT_Long>(fontData.size()),
                0,
                &face) != 0)
        {
            FT_Done_FreeType(library);
            throw std::runtime_error("Failed to load debug font face.");
        }

        if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(FontPixelHeight)) != 0)
        {
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            throw std::runtime_error("Failed to set debug font pixel size.");
        }

        std::vector<unsigned char> alpha(FontAtlasSize * FontAtlasSize);
        int penX = 1;
        int penY = 1;
        int rowHeight = 0;

        for (char character = 32; character <= 126; ++character)
        {
            if (FT_Load_Char(face, static_cast<FT_ULong>(character), FT_LOAD_RENDER) != 0)
            {
                FT_Done_Face(face);
                FT_Done_FreeType(library);
                throw std::runtime_error("Failed to render debug font glyph.");
            }

            const FT_GlyphSlot glyph = face->glyph;
            const FT_Bitmap& bitmap = glyph->bitmap;
            const int glyphWidth = static_cast<int>(bitmap.width);
            const int glyphHeight = static_cast<int>(bitmap.rows);
            if (penX + glyphWidth + 1 >= FontAtlasSize)
            {
                penX = 1;
                penY += rowHeight + 1;
                rowHeight = 0;
            }
            if (penY + glyphHeight + 1 >= FontAtlasSize)
            {
                FT_Done_Face(face);
                FT_Done_FreeType(library);
                throw std::runtime_error("Debug font atlas is too small.");
            }

            FontCharacter& fontCharacter = fontCharacters_[static_cast<size_t>(character - 32)];
            fontCharacter.x0 = penX;
            fontCharacter.y0 = penY;
            fontCharacter.x1 = penX + glyphWidth;
            fontCharacter.y1 = penY + glyphHeight;
            fontCharacter.xOffset = static_cast<float>(glyph->bitmap_left);
            fontCharacter.yOffset = -static_cast<float>(glyph->bitmap_top);
            fontCharacter.advance = static_cast<float>(glyph->advance.x) / 64.0f;

            for (int row = 0; row < glyphHeight; ++row)
            {
                const unsigned char* source = bitmap.buffer + static_cast<size_t>(row) * static_cast<size_t>(std::abs(bitmap.pitch));
                unsigned char* destination = alpha.data() + static_cast<size_t>(penY + row) * FontAtlasSize + static_cast<size_t>(penX);
                std::memcpy(destination, source, static_cast<size_t>(glyphWidth));
            }

            penX += glyphWidth + 1;
            rowHeight = std::max(rowHeight, glyphHeight);
        }

        FT_Done_Face(face);
        FT_Done_FreeType(library);

        std::vector<unsigned char> rgba(FontAtlasSize * FontAtlasSize * 4);
        for (int i = 0; i < FontAtlasSize * FontAtlasSize; ++i)
        {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = alpha[i];
        }

        font_ = createTextureFromRgba(rgba.data(), FontAtlasSize, FontAtlasSize);
    }

    void Renderer::createTextVertexBuffer()
    {
        constexpr VkDeviceSize BufferSize = sizeof(TextVertex) * MaxTextVertices;
        createBuffer(
            BufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            textVertexBuffer_,
            textVertexMemory_);
    }

    void Renderer::createUiBuffers()
    {
        createBuffer(
            sizeof(UiVertex) * MaxUiVertices,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            uiVertexBuffer_,
            uiVertexMemory_);
        createBuffer(
            sizeof(uint32_t) * MaxUiIndices,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            uiIndexBuffer_,
            uiIndexMemory_);
    }

    void Renderer::createParticleBuffers()
    {
        createBuffer(
            sizeof(TerrainVertex) * MaxBlockBreakParticles * 4u,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            particleVertexBuffer_,
            particleVertexMemory_);
        createBuffer(
            sizeof(uint32_t) * MaxBlockBreakParticles * 6u,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            particleIndexBuffer_,
            particleIndexMemory_);

        std::vector<ItemLocalVertex> itemVertices;
        std::vector<uint32_t> itemIndices;
        itemSpriteGpuMeshes_.assign(itemSpriteMeshes_.size(), {});
        for (size_t itemId = 0; itemId < itemSpriteMeshes_.size(); ++itemId)
        {
            const ItemSpriteMesh& mesh = itemSpriteMeshes_[itemId];
            if (mesh.quads.empty())
            {
                continue;
            }

            ItemSpriteGpuMesh gpuMesh{};
            gpuMesh.firstIndex = static_cast<uint32_t>(itemIndices.size());
            for (const ItemSpriteQuad& quad : mesh.quads)
            {
                const uint32_t baseVertex = static_cast<uint32_t>(itemVertices.size());
                for (size_t vertexIndex = 0; vertexIndex < quad.positions.size(); ++vertexIndex)
                {
                    const Vec3& position = quad.positions[vertexIndex];
                    itemVertices.push_back(ItemLocalVertex{
                        position.x,
                        position.y,
                        position.z,
                        quad.uvs[vertexIndex][0],
                        quad.uvs[vertexIndex][1],
                        quad.ao
                    });
                }

                itemIndices.push_back(baseVertex);
                itemIndices.push_back(baseVertex + 1u);
                itemIndices.push_back(baseVertex + 2u);
                itemIndices.push_back(baseVertex);
                itemIndices.push_back(baseVertex + 2u);
                itemIndices.push_back(baseVertex + 3u);
            }
            gpuMesh.indexCount = static_cast<uint32_t>(itemIndices.size() - gpuMesh.firstIndex);
            itemSpriteGpuMeshes_[itemId] = gpuMesh;
        }

        if (!itemVertices.empty() && !itemIndices.empty())
        {
            createBuffer(
                sizeof(ItemLocalVertex) * itemVertices.size(),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                droppedItemVertexBuffer_,
                droppedItemVertexMemory_);
            createBuffer(
                sizeof(uint32_t) * itemIndices.size(),
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                droppedItemIndexBuffer_,
                droppedItemIndexMemory_);

            void* vertexData = nullptr;
            vkMapMemory(device_, droppedItemVertexMemory_, 0, sizeof(ItemLocalVertex) * itemVertices.size(), 0, &vertexData);
            std::memcpy(vertexData, itemVertices.data(), sizeof(ItemLocalVertex) * itemVertices.size());
            vkUnmapMemory(device_, droppedItemVertexMemory_);

            void* indexData = nullptr;
            vkMapMemory(device_, droppedItemIndexMemory_, 0, sizeof(uint32_t) * itemIndices.size(), 0, &indexData);
            std::memcpy(indexData, itemIndices.data(), sizeof(uint32_t) * itemIndices.size());
            vkUnmapMemory(device_, droppedItemIndexMemory_);
        }

        createBuffer(
            sizeof(DroppedItemInstance) * MaxDroppedItemRenderInstances,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            droppedItemInstanceBuffer_,
            droppedItemInstanceMemory_);
        if (vkMapMemory(device_, droppedItemInstanceMemory_, 0, sizeof(DroppedItemInstance) * MaxDroppedItemRenderInstances, 0, &droppedItemInstanceMapped_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to map dropped item instance buffer.");
        }
    }

    void Renderer::createSelectionLineBuffer()
    {
        constexpr VkDeviceSize BufferSize = sizeof(LineVertex) * 24u;
        createBuffer(
            BufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            selectionLineVertexBuffer_,
            selectionLineVertexMemory_);
    }

    void Renderer::createPlayerMesh()
    {
        const std::vector<char> meshData = readFile((assetDirectory() / "textures" / "character" / "Character.mesh").string());
        if (meshData.size() < 12 || std::memcmp(meshData.data(), "PMSH", 4) != 0)
        {
            throw std::runtime_error("Invalid player mesh file.");
        }

        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        std::memcpy(&vertexCount, meshData.data() + 4, sizeof(vertexCount));
        std::memcpy(&indexCount, meshData.data() + 8, sizeof(indexCount));

        const size_t verticesOffset = 12;
        constexpr size_t LegacyPlayerVertexSize = sizeof(float) * 5;
        constexpr size_t AoPlayerVertexSize = sizeof(float) * 6;
        constexpr size_t LayerPlayerVertexSize = sizeof(float) * 7;
        const size_t currentIndicesOffset = verticesOffset + static_cast<size_t>(vertexCount) * sizeof(TerrainVertex);
        const size_t currentExpectedSize = currentIndicesOffset + static_cast<size_t>(indexCount) * sizeof(uint32_t);
        const size_t layerIndicesOffset = verticesOffset + static_cast<size_t>(vertexCount) * LayerPlayerVertexSize;
        const size_t layerExpectedSize = layerIndicesOffset + static_cast<size_t>(indexCount) * sizeof(uint32_t);
        const size_t aoIndicesOffset = verticesOffset + static_cast<size_t>(vertexCount) * AoPlayerVertexSize;
        const size_t aoExpectedSize = aoIndicesOffset + static_cast<size_t>(indexCount) * sizeof(uint32_t);
        const bool hasCurrentVertexData = meshData.size() >= currentExpectedSize;
        const bool hasLayerVertexData = !hasCurrentVertexData && meshData.size() >= layerExpectedSize;
        const bool hasAoVertexData = !hasCurrentVertexData && !hasLayerVertexData && meshData.size() >= aoExpectedSize;
        const size_t vertexStride = hasCurrentVertexData ? sizeof(TerrainVertex) : (hasLayerVertexData ? LayerPlayerVertexSize : (hasAoVertexData ? AoPlayerVertexSize : LegacyPlayerVertexSize));
        const size_t indicesOffset = verticesOffset + static_cast<size_t>(vertexCount) * vertexStride;
        const size_t expectedSize = indicesOffset + static_cast<size_t>(indexCount) * sizeof(uint32_t);
        if (meshData.size() < expectedSize)
        {
            throw std::runtime_error("Incomplete player mesh file.");
        }

        static_assert(sizeof(TerrainVertex) == sizeof(float) * 8);
        std::vector<TerrainVertex> sourceVertices(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            const char* source = meshData.data() + verticesOffset + static_cast<size_t>(i) * vertexStride;
            if (hasCurrentVertexData)
            {
                std::memcpy(&sourceVertices[i], source, sizeof(TerrainVertex));
            }
            else if (hasLayerVertexData)
            {
                std::memcpy(&sourceVertices[i].x, source, LayerPlayerVertexSize);
                sourceVertices[i].mipDistanceScale = 1.0f;
            }
            else if (hasAoVertexData)
            {
                std::memcpy(&sourceVertices[i].x, source, AoPlayerVertexSize);
                sourceVertices[i].textureLayer = 0.0f;
                sourceVertices[i].mipDistanceScale = 1.0f;
            }
            else
            {
                std::memcpy(&sourceVertices[i].x, source, LegacyPlayerVertexSize);
                sourceVertices[i].ao = 1.0f;
                sourceVertices[i].textureLayer = 0.0f;
                sourceVertices[i].mipDistanceScale = 1.0f;
            }
        }

        playerLocalVertices_ = std::move(sourceVertices);
        playerIndices_.clear();
        playerIndices_.reserve(indexCount);
        for (uint32_t i = 0; i < indexCount; ++i)
        {
            uint32_t index = 0;
            std::memcpy(&index, meshData.data() + indicesOffset + static_cast<size_t>(i) * sizeof(uint32_t), sizeof(index));
            if (index >= vertexCount)
            {
                throw std::runtime_error("Invalid player mesh index.");
            }
            playerIndices_.push_back(index);
        }

        createTerrainBuffer({playerLocalVertices_, playerIndices_}, playerMesh_, false);
    }

    void Renderer::initializeAudio()
    {
        audio_.initialize(assetDirectory());
    }

    void Renderer::shutdownAudio()
    {
        audio_.shutdown();
    }

    void Renderer::updateAudioListener(const Camera& camera, Vec3 cameraPosition)
    {
        audio_.updateListener(cameraPosition, camera.forward(), camera.up());
    }

    void Renderer::updateMusicPlayback(int menuOverlayMode, bool gameSceneRenderEnabled)
    {
        audio::MusicScene scene = audio::MusicScene::None;
        if (gameSceneRenderEnabled)
        {
            scene = audio::MusicScene::Game;
        }
        else if (menuOverlayMode == 1 || menuOverlayMode == 3 || menuOverlayMode == 4)
        {
            scene = audio::MusicScene::Lobby;
        }
        audio_.updateMusicPlayback(scene, glfwGetTime());
    }

    void Renderer::playBlockBreakSound(int x, int y, int z)
    {
        audio_.playBlockBreak(Vec3{
            static_cast<float>(x),
            static_cast<float>(y) + 0.5f,
            static_cast<float>(z)
        });
    }

    void Renderer::playBlockPlaceSound(int x, int y, int z)
    {
        audio_.playBlockPlace(Vec3{
            static_cast<float>(x),
            static_cast<float>(y) + 0.5f,
            static_cast<float>(z)
        });
    }

    void Renderer::playItemPickupSound()
    {
        audio_.playItemPickup();
    }

    void Renderer::loadWorldConfig()
    {
        config::WorldConfig defaults{};
        defaults.loadGridScale = DefaultLoadGridScale;
        defaults.terrainWorkerCount = DefaultTerrainWorkerCount;
        defaults.maxTerrainUploadChunksPerFrame = DefaultMaxTerrainUploadChunksPerFrame;
        defaults.maxTerrainUnloadChunksPerFrame = DefaultMaxTerrainUnloadChunksPerFrame;
        defaults.maxTerrainRetiredDestroyPerFrame = DefaultMaxTerrainRetiredDestroyPerFrame;
        defaults.terrainNoiseFeatureScale = DefaultTerrainNoiseFeatureScale;
        defaults.terrainNoiseOctaveCount = DefaultTerrainNoiseOctaveCount;
        defaults.terrainNoiseLacunarity = DefaultTerrainNoiseLacunarity;
        defaults.terrainNoiseGain = DefaultTerrainNoiseGain;
        defaults.terrainNoiseSimplexScale = DefaultTerrainNoiseSimplexScale;
        defaults.terrainDomainWarpEnabled = DefaultTerrainDomainWarpEnabled;
        defaults.terrainDomainWarpAmplitude = DefaultTerrainDomainWarpAmplitude;
        defaults.terrainDomainWarpFrequency = DefaultTerrainDomainWarpFrequency;
        defaults.terrainDomainWarpOctaveCount = DefaultTerrainDomainWarpOctaveCount;
        defaults.terrainDomainWarpGain = DefaultTerrainDomainWarpGain;
        defaults.temperatureNoiseStrength = DefaultTemperatureNoiseStrength;
        defaults.temperatureNoiseFeatureScale = DefaultTemperatureNoiseFeatureScale;
        defaults.temperatureNoiseOctaveCount = DefaultTemperatureNoiseOctaveCount;
        defaults.temperatureNoiseLacunarity = DefaultTemperatureNoiseLacunarity;
        defaults.temperatureNoiseGain = DefaultTemperatureNoiseGain;
        defaults.temperatureNoiseSimplexScale = DefaultTemperatureNoiseSimplexScale;
        defaults.precipitationNoiseFeatureScale = DefaultPrecipitationNoiseFeatureScale;
        defaults.precipitationNoiseOctaveCount = DefaultPrecipitationNoiseOctaveCount;
        defaults.precipitationNoiseLacunarity = DefaultPrecipitationNoiseLacunarity;
        defaults.precipitationNoiseGain = DefaultPrecipitationNoiseGain;
        defaults.precipitationNoiseSimplexScale = DefaultPrecipitationNoiseSimplexScale;
        defaults.seaLevel = DefaultSeaLevel;

        const config::WorldConfig worldConfig = config::loadWorldConfig(configDirectory() / "world.json", defaults, ChunkSizeY - 1);
        loadGridScale_ = worldConfig.loadGridScale;
        terrainWorkerCount_ = worldConfig.terrainWorkerCount;
        maxTerrainUploadChunksPerFrame_ = worldConfig.maxTerrainUploadChunksPerFrame;
        maxTerrainUnloadChunksPerFrame_ = worldConfig.maxTerrainUnloadChunksPerFrame;
        maxTerrainRetiredDestroyPerFrame_ = worldConfig.maxTerrainRetiredDestroyPerFrame;
        terrainNoiseFeatureScale_ = worldConfig.terrainNoiseFeatureScale;
        terrainNoiseOctaveCount_ = worldConfig.terrainNoiseOctaveCount;
        terrainNoiseLacunarity_ = worldConfig.terrainNoiseLacunarity;
        terrainNoiseGain_ = worldConfig.terrainNoiseGain;
        terrainNoiseSimplexScale_ = worldConfig.terrainNoiseSimplexScale;
        terrainDomainWarpEnabled_ = worldConfig.terrainDomainWarpEnabled;
        terrainDomainWarpAmplitude_ = worldConfig.terrainDomainWarpAmplitude;
        terrainDomainWarpFrequency_ = worldConfig.terrainDomainWarpFrequency;
        terrainDomainWarpOctaveCount_ = worldConfig.terrainDomainWarpOctaveCount;
        terrainDomainWarpGain_ = worldConfig.terrainDomainWarpGain;
        temperatureNoiseStrength_ = worldConfig.temperatureNoiseStrength;
        temperatureNoiseFeatureScale_ = worldConfig.temperatureNoiseFeatureScale;
        temperatureNoiseOctaveCount_ = worldConfig.temperatureNoiseOctaveCount;
        temperatureNoiseLacunarity_ = worldConfig.temperatureNoiseLacunarity;
        temperatureNoiseGain_ = worldConfig.temperatureNoiseGain;
        temperatureNoiseSimplexScale_ = worldConfig.temperatureNoiseSimplexScale;
        precipitationNoiseFeatureScale_ = worldConfig.precipitationNoiseFeatureScale;
        precipitationNoiseOctaveCount_ = worldConfig.precipitationNoiseOctaveCount;
        precipitationNoiseLacunarity_ = worldConfig.precipitationNoiseLacunarity;
        precipitationNoiseGain_ = worldConfig.precipitationNoiseGain;
        precipitationNoiseSimplexScale_ = worldConfig.precipitationNoiseSimplexScale;
        seaLevel_ = worldConfig.seaLevel;
    }

    void Renderer::loadRenderConfig()
    {
        config::RenderConfig defaults{};
        defaults.fluidWaterAlpha = DefaultFluidWaterAlpha;

        const config::RenderConfig renderConfig = config::loadRenderConfig(configDirectory() / "render.json", defaults);
        fluidWaterAlpha_ = renderConfig.fluidWaterAlpha;
    }

    void Renderer::loadHeightLut()
    {
        for (uint32_t i = 0; i < HeightLutCount; ++i)
        {
            const double t = static_cast<double>(i) / static_cast<double>(HeightLutCount - 1u);
            heightLut_[i] = static_cast<uint16_t>(std::lround(static_cast<double>(TerrainMinHeight) + t * static_cast<double>(TerrainMaxHeight - TerrainMinHeight)));
        }

        const std::filesystem::path path = assetDirectory() / "data" / "world" / "height_lut.bin";
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return;
        }

        char magic[4]{};
        uint32_t version = 0;
        uint32_t count = 0;
        float noiseMin = 0.0f;
        float noiseMax = 0.0f;

        file.read(magic, sizeof(magic));
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        file.read(reinterpret_cast<char*>(&noiseMin), sizeof(noiseMin));
        file.read(reinterpret_cast<char*>(&noiseMax), sizeof(noiseMax));
        if (!file || std::memcmp(magic, "DLHT", 4) != 0 || version != HeightLutVersion || count != HeightLutCount || noiseMin != HeightLutNoiseMin || noiseMax != HeightLutNoiseMax)
        {
            return;
        }

        std::array<uint16_t, HeightLutCount> loaded{};
        file.read(reinterpret_cast<char*>(loaded.data()), static_cast<std::streamsize>(loaded.size() * sizeof(uint16_t)));
        if (!file)
        {
            return;
        }

        heightLut_ = loaded;
    }

    void Renderer::updateLoadedChunks(DVec3 playerPosition)
    {
        const int centerGroupChunkX = centerGroupCoordinate(chunkCoordinate(playerPosition.x));
        const int centerGroupChunkZ = centerGroupCoordinate(chunkCoordinate(playerPosition.z));
        if (terrainLoadRequested_ && centerGroupChunkX == loadedCenterGroupChunkX_ && centerGroupChunkZ == loadedCenterGroupChunkZ_)
        {
            return;
        }

        terrainLoadRequested_ = true;
        loadedCenterGroupChunkX_ = centerGroupChunkX;
        loadedCenterGroupChunkZ_ = centerGroupChunkZ;

        requestTerrainLoad(centerGroupChunkX, centerGroupChunkZ);
    }

    void Renderer::requestTerrainLoad(int centerGroupChunkX, int centerGroupChunkZ)
    {
        loadedChunkDiameter_ = std::max(1, loadGridScale_) * LoadGridUnitChunks;
        loadedCenterGroupChunkX_ = centerGroupChunkX;
        loadedCenterGroupChunkZ_ = centerGroupChunkZ;
        const uint64_t generation = ++terrainGeneration_;
        rebuildLoadOrderIfNeeded();

        desiredTerrainChunks_.clear();
        desiredFeatureChunks_.clear();
        desiredRenderChunks_.clear();
        desiredTerrainChunks_.reserve(static_cast<size_t>(loadedChunkDiameter_ + 4) * static_cast<size_t>(loadedChunkDiameter_ + 4));
        desiredFeatureChunks_.reserve(static_cast<size_t>(loadedChunkDiameter_ + 2) * static_cast<size_t>(loadedChunkDiameter_ + 2));
        desiredRenderChunks_.reserve(static_cast<size_t>(loadedChunkDiameter_) * loadedChunkDiameter_);
        const size_t runtimeCapacity = static_cast<size_t>(loadedChunkDiameter_ + 4) * static_cast<size_t>(loadedChunkDiameter_ + 4);
        const size_t featureCapacity = static_cast<size_t>(loadedChunkDiameter_ + 2) * static_cast<size_t>(loadedChunkDiameter_ + 2);
        const size_t renderCapacity = static_cast<size_t>(loadedChunkDiameter_) * static_cast<size_t>(loadedChunkDiameter_);
        worldRuntime_.reserve(runtimeCapacity + 256u);
        droppedItemCountsByChunk_.reserve(runtimeCapacity + 256u);
        terrainChunks_.reserve(renderCapacity + 256u);
        pendingUnloadSet_.reserve(runtimeCapacity + 256u);
        requestedChunkJobs_.reserve(featureCapacity + 256u);
        requestedMeshJobs_.reserve(renderCapacity + 256u);
        requestedChunkJobs_.clear();
        requestedMeshJobs_.clear();

        const int renderMin = -(loadedChunkDiameter_ / 2 - 1);
        const int renderMax = loadedChunkDiameter_ / 2;
        const int runtimeKeepMin = renderMin - 2;
        const int runtimeKeepMax = renderMax + 2;

        terrainJobSystem_.clearQueuedJobsAndMeshes();

        for (auto& entry : worldRuntime_.chunks())
        {
            entry.second.bestPriority = UINT32_MAX;
        }

        for (const ChunkOffset& offset : loadOrder_)
        {
            if (offset.x < runtimeKeepMin || offset.x > runtimeKeepMax || offset.z < runtimeKeepMin || offset.z > runtimeKeepMax)
            {
                continue;
            }
            ensureRuntimeChunk(
                loadedCenterGroupChunkX_ + offset.x,
                loadedCenterGroupChunkZ_ + offset.z,
                generation);
        }

        auto distanceToCenterGroupSquared = [](const ChunkOffset& offset)
        {
            const int dx = offset.x < 0 ? -offset.x : (offset.x > 1 ? offset.x - 1 : 0);
            const int dz = offset.z < 0 ? -offset.z : (offset.z > 1 ? offset.z - 1 : 0);
            return static_cast<uint32_t>(dx * dx + dz * dz);
        };

        for (const ChunkOffset& offset : loadOrder_)
        {
            if (offset.x < renderMin || offset.x > renderMax || offset.z < renderMin || offset.z > renderMax)
            {
                continue;
            }
            wantRender(
                loadedCenterGroupChunkX_ + offset.x,
                loadedCenterGroupChunkZ_ + offset.z,
                distanceToCenterGroupSquared(offset));
        }

        for (auto it = terrainChunks_.begin(); it != terrainChunks_.end();)
        {
            if (desiredRenderChunks_.find(it->first) == desiredRenderChunks_.end())
            {
                retiredTerrainChunks_.push_back(RetiredChunkRenderData{
                    static_cast<uint32_t>(MaxFramesInFlight + 1),
                    std::move(it->second)});
                it = terrainChunks_.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (const auto& entry : worldRuntime_.chunks())
        {
            if (desiredTerrainChunks_.find(entry.first) == desiredTerrainChunks_.end())
            {
                if (pendingUnloadSet_.insert(entry.first).second)
                {
                    pendingUnloadChunks_.push_back(entry.first);
                }
            }
            else
            {
                pendingUnloadSet_.erase(entry.first);
            }
        }

        updateTerrainStats();
        debugTextBatchDirty_ = true;
    }

    void Renderer::rebuildLoadOrderIfNeeded()
    {
        const int dataDiameter = loadedChunkDiameter_ + 4;
        if (loadOrderDiameter_ == dataDiameter && !loadOrder_.empty())
        {
            return;
        }

        loadOrderDiameter_ = dataDiameter;
        loadOrder_.clear();
        loadOrder_.reserve(static_cast<size_t>(dataDiameter) * dataDiameter);

        const int min = -(dataDiameter / 2 - 1);
        const int max = dataDiameter / 2;
        for (int z = min; z <= max; ++z)
        {
            for (int x = min; x <= max; ++x)
            {
                loadOrder_.push_back({x, z});
            }
        }

        auto distanceToCenterGroupSquared = [](const ChunkOffset& offset)
        {
            const int dx = offset.x < 0 ? -offset.x : (offset.x > 1 ? offset.x - 1 : 0);
            const int dz = offset.z < 0 ? -offset.z : (offset.z > 1 ? offset.z - 1 : 0);
            return dx * dx + dz * dz;
        };

        std::stable_sort(loadOrder_.begin(), loadOrder_.end(), [&](const ChunkOffset& left, const ChunkOffset& right)
        {
            return distanceToCenterGroupSquared(left) < distanceToCenterGroupSquared(right);
        });
    }

    void Renderer::startTerrainWorkers()
    {
        terrainJobSystem_.start(
            terrainWorkerCount_,
            [this]
            {
                return terrainGeneration_.load();
            },
            [this](TerrainJob job)
            {
                const world::TerrainBuilder terrainBuilder(terrainBuilderConfig());
                world::TerrainJobResult result{};
                if (job.type == TerrainJob::Type::BuildFeaturing)
                {
                    std::shared_ptr<ChunkData> chunk = terrainBuilder.buildChunkData(job.chunkX, job.chunkZ);
                    chunk->generation = job.generation;
                    chunk->revision = 0;
                    const std::array<int, ChunkColumnCount> heights = terrainBuilder.buildChunkHeightmap(job.chunkX, job.chunkZ);
                    std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingFeatureSlots = terrainBuilder.buildTreeFeatures(chunk, heights);
                    result.completedChunkData = CompletedChunkData{std::move(chunk), std::move(outgoingFeatureSlots)};
                }
                else if (job.type == TerrainJob::Type::FinalizeFeatures && job.chunk)
                {
                    terrainBuilder.applyFeatureWrites(job.chunk, job.incomingFeatureSlots);
                    result.completedMergedChunk = std::move(job.chunk);
                }
                else if (job.meshChunks[4])
                {
                    if (job.revision != job.meshChunks[4]->revision)
                    {
                        CompletedChunkMesh mesh{};
                        mesh.generation = job.generation;
                        mesh.revision = job.revision;
                        mesh.chunkX = job.chunkX;
                        mesh.chunkZ = job.chunkZ;
                        result.completedChunkMesh = std::move(mesh);
                    }
                    else
                    {
                        result.completedChunkMesh = buildChunkMesh(job.meshChunks, job.generation);
                    }
                }
                return result;
            });
    }

    void Renderer::stopTerrainWorkers()
    {
        terrainJobSystem_.stop();
        world::TerrainCompletedBatch completed = terrainJobSystem_.drainForShutdown();

        for (CompletedChunkData& completedData : completed.completedChunks)
        {
            if (!completedData.chunk)
            {
                continue;
            }

            SaveChunkSnapshot snapshot{};
            snapshot.chunkX = completedData.chunk->chunkX;
            snapshot.chunkZ = completedData.chunk->chunkZ;
            snapshot.genState = ChunkGenState::Featuring;
            snapshot.revision = completedData.chunk->revision;
            snapshot.hasData = true;
            snapshot.chunkData = completedData.chunk;
            enqueueSaveSnapshot(std::move(snapshot));
        }

        for (const std::shared_ptr<ChunkData>& chunk : completed.completedMergedChunks)
        {
            if (!chunk)
            {
                continue;
            }

            SaveChunkSnapshot snapshot{};
            snapshot.chunkX = chunk->chunkX;
            snapshot.chunkZ = chunk->chunkZ;
            snapshot.genState = ChunkGenState::Full;
            snapshot.revision = chunk->revision;
            snapshot.hasData = true;
            snapshot.chunkData = chunk;
            enqueueSaveSnapshot(std::move(snapshot));
        }
    }

    void Renderer::startChunkLoadWorker()
    {
        chunkLoadSystem_.start([this](int chunkX, int chunkZ)
        {
            return saveSystem_.load(chunkX, chunkZ);
        });
    }

    void Renderer::stopChunkLoadWorker()
    {
        chunkLoadSystem_.stop();
    }

    void Renderer::enqueueChunkLoadJob(int chunkX, int chunkZ, uint64_t generation)
    {
        chunkLoadSystem_.enqueue(chunkX, chunkZ, generation);
    }

    void Renderer::startSaveWorker()
    {
        saveSystem_.start(activeWorldDirectory_, [this](const SaveChunkSnapshot& snapshot, bool savedChunkData)
        {
            if (!savedChunkData)
            {
                return;
            }

            const uint64_t runtimeKey = chunkKey(snapshot.chunkX, snapshot.chunkZ);
            RuntimeChunk* runtimeChunk = worldRuntime_.find(runtimeKey);
            if (runtimeChunk != nullptr &&
                runtimeChunk->data &&
                runtimeChunk->data->revision == snapshot.revision)
            {
                runtimeChunk->hasSavedBacking = true;
                if (runtimeChunk->dataDirtySerial == snapshot.dataDirtySerial)
                {
                    runtimeChunk->dataDirtyForSave = false;
                }
            }
        });
    }

    void Renderer::stopSaveWorker()
    {
        saveSystem_.stop();
    }

    void Renderer::markRuntimeChunkDataDirty(RuntimeChunk& chunk)
    {
        world::WorldRuntime::markDataDirty(chunk);
    }

    void Renderer::enqueueTerrainJob(TerrainJob job)
    {
        terrainJobSystem_.enqueue(std::move(job));
    }

    void Renderer::processCompletedTerrainJobs()
    {
        const uint64_t generation = terrainGeneration_.load();
        std::vector<CompletedChunkData> completedChunks;
        std::vector<std::shared_ptr<ChunkData>> completedMergedChunks;
        std::vector<CompletedChunkMesh> completedMeshes;
        std::vector<CompletedChunkLoad> completedLoads;
        std::vector<uint64_t> uploadChunkKeys;
        uploadChunkKeys.reserve(static_cast<size_t>(maxTerrainUploadChunksPerFrame_));
        uint32_t uploadChunkCount = 0;
        auto canUploadChunk = [&](uint64_t key) -> bool
        {
            for (uint64_t uploadKey : uploadChunkKeys)
            {
                if (uploadKey == key)
                {
                    return true;
                }
            }

            if (uploadChunkCount >= static_cast<uint32_t>(maxTerrainUploadChunksPerFrame_))
            {
                return false;
            }

            uploadChunkKeys.push_back(key);
            ++uploadChunkCount;
            return true;
        };

        world::TerrainCompletedBatch terrainCompleted = terrainJobSystem_.drainCompleted(
            [&](const CompletedChunkMesh& mesh)
            {
                const uint64_t key = chunkKey(mesh.chunkX, mesh.chunkZ);
                return mesh.generation != generation || desiredRenderChunks_.find(key) == desiredRenderChunks_.end();
            },
            [&](const CompletedChunkMesh& mesh)
            {
                return canUploadChunk(chunkKey(mesh.chunkX, mesh.chunkZ));
            });
        completedChunks = std::move(terrainCompleted.completedChunks);
        completedMergedChunks = std::move(terrainCompleted.completedMergedChunks);
        completedMeshes = std::move(terrainCompleted.completedMeshes);
        completedLoads = chunkLoadSystem_.drainCompleted();

        for (CompletedChunkLoad& completed : completedLoads)
        {
            const uint64_t key = chunkKey(completed.chunkX, completed.chunkZ);
            RuntimeChunk* existingChunk = worldRuntime_.finishSnapshotLoad(key);
            if (existingChunk == nullptr)
            {
                continue;
            }

            const world::WorldRuntime::RuntimeChunkLoadState loadState = world::WorldRuntime::captureLoadState(*existingChunk);
            if (desiredTerrainChunks_.find(key) == desiredTerrainChunks_.end())
            {
                continue;
            }

            if (completed.snapshot)
            {
                RuntimeChunk loaded = runtimeChunkFromSnapshot(*completed.snapshot, generation);
                if (loadState.incomingFeatureMask != 0)
                {
                    if ((loaded.genState == ChunkGenState::Full || loaded.genState == ChunkGenState::Meshed) && loaded.data)
                    {
                        const world::TerrainBuilder terrainBuilder(terrainBuilderConfig());
                        if (terrainBuilder.applyFeatureWrites(loaded.data, loadState.incomingFeatureSlots))
                        {
                            loaded.genState = ChunkGenState::Full;
                        }
                    }
                    else
                    {
                        for (size_t slot = 0; slot < FeatureNeighborCount; ++slot)
                        {
                            const uint8_t bit = static_cast<uint8_t>(1u << static_cast<uint32_t>(slot));
                            if ((loadState.incomingFeatureMask & bit) != 0 && loadState.incomingFeatureSlots[slot] && !loaded.incomingFeatureSlots[slot])
                            {
                                loaded.incomingFeatureSlots[slot] = loadState.incomingFeatureSlots[slot];
                                loaded.incomingFeatureMask |= bit;
                            }
                        }
                    }
                }
                worldRuntime_.installLoadedChunk(std::move(loaded), loadState);
                refreshDroppedItemChunkTracking(key);
            }

            if (desiredRenderChunks_.find(key) != desiredRenderChunks_.end())
            {
                wantRender(completed.chunkX, completed.chunkZ, loadState.bestPriority);
            }
            else if (loadState.meshTicket == generation)
            {
                wantMesh(completed.chunkX, completed.chunkZ, loadState.bestPriority);
            }
            else if (loadState.fullTicket == generation)
            {
                wantFull(completed.chunkX, completed.chunkZ, loadState.bestPriority);
            }
            else if (loadState.featuringTicket == generation)
            {
                wantFeaturing(completed.chunkX, completed.chunkZ, loadState.bestPriority);
            }

            const RuntimeChunk* readyChunk = worldRuntime_.find(key);
            if (readyChunk != nullptr &&
                readyChunk->data &&
                (readyChunk->genState == ChunkGenState::Full ||
                    readyChunk->genState == ChunkGenState::Meshed))
            {
                tryQueueMeshesAround(completed.chunkX, completed.chunkZ);
            }
        }

        for (CompletedChunkData& completed : completedChunks)
        {
            const std::shared_ptr<ChunkData>& chunk = completed.chunk;
            const uint64_t key = chunkKey(chunk->chunkX, chunk->chunkZ);
            requestedChunkJobs_.erase(key);
            if (chunk->generation != generation || desiredTerrainChunks_.find(key) == desiredTerrainChunks_.end())
            {
                if (chunk->generation != generation && desiredTerrainChunks_.find(key) != desiredTerrainChunks_.end())
                {
                    continue;
                }

                SaveChunkSnapshot snapshot{};
                snapshot.chunkX = chunk->chunkX;
                snapshot.chunkZ = chunk->chunkZ;
                snapshot.genState = ChunkGenState::Featuring;
                snapshot.revision = chunk->revision;
                snapshot.hasData = true;
                snapshot.chunkData = chunk;
                enqueueSaveSnapshot(std::move(snapshot));

                for (size_t slot = 0; slot < FeatureNeighborOffsets.size(); ++slot)
                {
                    const int targetChunkX = chunk->chunkX + FeatureNeighborOffsets[slot].x;
                    const int targetChunkZ = chunk->chunkZ + FeatureNeighborOffsets[slot].z;
                    const std::optional<size_t> sourceSlot = featureNeighborIndex(-FeatureNeighborOffsets[slot].x, -FeatureNeighborOffsets[slot].z);
                    if (!sourceSlot)
                    {
                        continue;
                    }

                    if (desiredTerrainChunks_.find(chunkKey(targetChunkX, targetChunkZ)) != desiredTerrainChunks_.end())
                    {
                        acceptFeatureSlot(targetChunkX, targetChunkZ, *sourceSlot, completed.outgoingFeatureSlots[slot]);
                    }
                }
                continue;
            }
            RuntimeChunk& runtimeChunk = worldRuntime_.installFeaturingChunk(chunk, std::move(completed.outgoingFeatureSlots));
            refreshDroppedItemChunkTracking(key);
            publishFeatureSlots(runtimeChunk);
            tryQueueFeatureFinalize(key);
            tryQueueMeshesAround(chunk->chunkX, chunk->chunkZ);
        }

        for (const std::shared_ptr<ChunkData>& chunk : completedMergedChunks)
        {
            const uint64_t key = chunkKey(chunk->chunkX, chunk->chunkZ);
            if (chunk->generation != generation || desiredTerrainChunks_.find(key) == desiredTerrainChunks_.end())
            {
                if (chunk->generation != generation && desiredTerrainChunks_.find(key) != desiredTerrainChunks_.end())
                {
                    continue;
                }

                SaveChunkSnapshot snapshot{};
                snapshot.chunkX = chunk->chunkX;
                snapshot.chunkZ = chunk->chunkZ;
                snapshot.genState = ChunkGenState::Full;
                snapshot.revision = chunk->revision;
                snapshot.hasData = true;
                snapshot.chunkData = chunk;
                enqueueSaveSnapshot(std::move(snapshot));
                continue;
            }
            worldRuntime_.installFullChunk(chunk);
            refreshDroppedItemChunkTracking(key);
            tryQueueMeshesAround(chunk->chunkX, chunk->chunkZ);
        }

        for (CompletedChunkMesh& mesh : completedMeshes)
        {
            const uint64_t key = chunkKey(mesh.chunkX, mesh.chunkZ);
            requestedMeshJobs_.erase(key);
            worldRuntime_.clearMeshQueued(key);

            if (mesh.generation != generation || desiredRenderChunks_.find(key) == desiredRenderChunks_.end())
            {
                continue;
            }
            if (!worldRuntime_.meshRevisionMatches(key, mesh.revision))
            {
                tryQueueMeshIfReady(mesh.chunkX, mesh.chunkZ);
                continue;
            }

            ChunkRenderData& renderData = terrainChunks_[key];
            retiredTerrainChunks_.push_back(RetiredChunkRenderData{
                static_cast<uint32_t>(MaxFramesInFlight + 1),
                std::move(renderData)});
            renderData = {};
            renderData.revision = mesh.revision;
            renderData.chunkX = mesh.chunkX;
            renderData.chunkZ = mesh.chunkZ;
            createChunkTerrainBuffers(mesh.solidSubchunks, renderData.solidSubchunks);
            createChunkTerrainBuffers(mesh.fluidSubchunks, renderData.fluidSubchunks);
            worldRuntime_.markMeshed(key);
        }

        if (!completedChunks.empty() || !completedMergedChunks.empty() || !completedMeshes.empty())
        {
            updateTerrainStats();
        }
        processRetiredTerrainChunks();
        processPendingTerrainUnloads();
    }

    uint32_t Renderer::processPendingTerrainUnloads()
    {
        uint32_t unloadedCount = 0;
        while (!pendingUnloadChunks_.empty() && unloadedCount < static_cast<uint32_t>(maxTerrainUnloadChunksPerFrame_))
        {
            const uint64_t key = pendingUnloadChunks_.front();
            pendingUnloadChunks_.pop_front();

            if (desiredTerrainChunks_.find(key) != desiredTerrainChunks_.end())
            {
                pendingUnloadSet_.erase(key);
                continue;
            }

            auto renderIt = terrainChunks_.find(key);
            if (renderIt != terrainChunks_.end())
            {
                retiredTerrainChunks_.push_back(RetiredChunkRenderData{
                    static_cast<uint32_t>(MaxFramesInFlight + 1),
                    std::move(renderIt->second)});
                terrainChunks_.erase(renderIt);
            }
            RuntimeChunk* runtimeChunk = worldRuntime_.find(key);
            if (runtimeChunk != nullptr)
            {
                enqueueSaveSnapshot(makeSaveSnapshot(*runtimeChunk));
            }
            removeDroppedItemChunkTracking(key);
            worldRuntime_.erase(key);
            requestedChunkJobs_.erase(key);
            requestedMeshJobs_.erase(key);
            pendingUnloadSet_.erase(key);
            ++unloadedCount;
        }

        if (unloadedCount > 0)
        {
            updateTerrainStats();
            debugTextBatchDirty_ = true;
        }

        return unloadedCount;
    }

    void Renderer::processRetiredTerrainChunks()
    {
        uint32_t destroyedCount = 0;
        for (auto it = retiredTerrainChunks_.begin(); it != retiredTerrainChunks_.end();)
        {
            if (it->framesLeft > 0)
            {
                --it->framesLeft;
                ++it;
                continue;
            }
            if (destroyedCount >= static_cast<uint32_t>(maxTerrainRetiredDestroyPerFrame_))
            {
                ++it;
                continue;
            }

            destroyChunkRenderData(it->chunk);
            it = retiredTerrainChunks_.erase(it);
            ++destroyedCount;
        }
    }

    RuntimeChunk& Renderer::ensureRuntimeChunk(int chunkX, int chunkZ, uint64_t generation)
    {
        const uint64_t key = chunkKey(chunkX, chunkZ);

        desiredTerrainChunks_.insert(key);
        pendingUnloadSet_.erase(key);

        bool created = false;
        RuntimeChunk& chunk = worldRuntime_.ensureChunkShell(chunkX, chunkZ, created);
        if (created)
        {
            chunk.snapshotLoadRequested = true;
            chunk.snapshotLoadFinished = false;
            enqueueChunkLoadJob(chunkX, chunkZ, generation);
        }

        if (!chunk.data && !chunk.snapshotLoadRequested && !chunk.snapshotLoadFinished)
        {
            chunk.snapshotLoadRequested = true;
            enqueueChunkLoadJob(chunkX, chunkZ, generation);
        }
        if (chunk.data)
        {
            chunk.data->generation = generation;
        }
        return chunk;
    }

    void Renderer::wantRender(int chunkX, int chunkZ, uint32_t priority)
    {
        const uint64_t generation = terrainGeneration_.load();
        const uint64_t key = chunkKey(chunkX, chunkZ);
        RuntimeChunk& chunk = ensureRuntimeChunk(chunkX, chunkZ, generation);

        desiredRenderChunks_.insert(key);
        chunk.renderTicket = generation;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);

        if (chunkMeshReady(key))
        {
            return;
        }

        wantMesh(chunkX, chunkZ, priority);
    }

    void Renderer::wantMesh(int chunkX, int chunkZ, uint32_t priority)
    {
        const uint64_t generation = terrainGeneration_.load();
        const uint64_t key = chunkKey(chunkX, chunkZ);
        RuntimeChunk& chunk = ensureRuntimeChunk(chunkX, chunkZ, generation);
        chunk.meshTicket = generation;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);

        if (chunkMeshReady(key))
        {
            return;
        }

        wantFull(chunkX, chunkZ, priority);
        tryQueueMeshIfReady(chunkX, chunkZ);
    }

    void Renderer::wantFull(int chunkX, int chunkZ, uint32_t priority)
    {
        const uint64_t generation = terrainGeneration_.load();
        const uint64_t key = chunkKey(chunkX, chunkZ);
        RuntimeChunk& chunk = ensureRuntimeChunk(chunkX, chunkZ, generation);
        chunk.fullTicket = generation;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);

        if (chunk.genState == ChunkGenState::Full || chunk.genState == ChunkGenState::Meshed)
        {
            return;
        }

        wantFeaturing(chunkX, chunkZ, priority);
        for (const FeatureNeighborOffset& offset : FeatureNeighborOffsets)
        {
            wantFeaturing(chunkX + offset.x, chunkZ + offset.z, priority);
        }

        if (chunk.genState == ChunkGenState::Featuring)
        {
            publishFeatureSlots(chunk);
        }
        else if (chunk.genState == ChunkGenState::Full || chunk.genState == ChunkGenState::Meshed)
        {
            publishFeatureSlots(chunk);
        }
        tryQueueFeatureFinalize(key);
    }

    void Renderer::wantFeaturing(int chunkX, int chunkZ, uint32_t priority)
    {
        const uint64_t generation = terrainGeneration_.load();
        RuntimeChunk& chunk = ensureRuntimeChunk(chunkX, chunkZ, generation);
        desiredFeatureChunks_.insert(chunkKey(chunkX, chunkZ));
        chunk.featuringTicket = generation;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);
        if (!chunk.data && chunk.snapshotLoadRequested && !chunk.snapshotLoadFinished)
        {
            return;
        }

        if (chunk.outgoingPublishedTicket == generation &&
            (chunk.genState == ChunkGenState::Featuring ||
                chunk.genState == ChunkGenState::Full ||
                chunk.genState == ChunkGenState::Meshed))
        {
            return;
        }

        if (chunk.genState == ChunkGenState::Featuring ||
            chunk.genState == ChunkGenState::Full ||
            chunk.genState == ChunkGenState::Meshed)
        {
            publishFeatureSlots(chunk);
            return;
        }

        if (chunk.buildQueuedTicket == generation)
        {
            return;
        }

        TerrainJob job{};
        job.type = TerrainJob::Type::BuildFeaturing;
        job.generation = generation;
        job.priority = chunk.bestPriority;
        job.chunkX = chunkX;
        job.chunkZ = chunkZ;
        enqueueTerrainJob(std::move(job));
        requestedChunkJobs_.insert(chunkKey(chunkX, chunkZ));
        chunk.buildQueuedTicket = generation;
    }

    SaveChunkSnapshot Renderer::makeSaveSnapshot(const RuntimeChunk& chunk) const
    {
        SaveChunkSnapshot snapshot{};
        snapshot.chunkX = chunk.chunkX;
        snapshot.chunkZ = chunk.chunkZ;
        snapshot.genState = chunk.genState == ChunkGenState::Meshed ? ChunkGenState::Full : chunk.genState;
        snapshot.incomingFeatureMask = snapshot.genState == ChunkGenState::Full ? 0 : chunk.incomingFeatureMask;
        if (chunk.data)
        {
            snapshot.hasData = true;
            snapshot.revision = chunk.data->revision;
            snapshot.hasSavedBacking = chunk.hasSavedBacking;
            snapshot.forceSave = chunk.dataDirtyForSave || !chunk.hasSavedBacking;
            snapshot.dataDirtySerial = chunk.dataDirtySerial;
            snapshot.chunkData = chunk.data;
        }
        if (snapshot.genState != ChunkGenState::Full)
        {
            snapshot.incomingFeatureSlots = chunk.incomingFeatureSlots;
        }
        return snapshot;
    }

    void Renderer::enqueueSaveSnapshot(SaveChunkSnapshot snapshot)
    {
        saveSystem_.enqueue(std::move(snapshot));
    }

    void Renderer::enqueueSaveAllRuntimeChunks()
    {
        for (const auto& entry : worldRuntime_.chunks())
        {
            enqueueSaveSnapshot(makeSaveSnapshot(entry.second));
        }
    }

    void Renderer::loadGameScene(const std::filesystem::path& worldDirectory, uint64_t worldSeed)
    {
        if (gameSceneLoaded_)
        {
            return;
        }

        log::info("Loading game scene: " + worldDirectory.string());
        activeWorldDirectory_ = worldDirectory;
        activeWorldSeed_ = worldSeed;
        activeWorldSeedSalt_ = static_cast<int>((worldSeed ^ (worldSeed >> 32u)) & 0x7fffffffu);
        blockBreakParticles_.clear();
        resetBlockBreaking();
        nextWorldEntityId_ = 1;
        resetDroppedItemTracking();
        playerInventory_.clear();
        updateInventoryUi();
        updateInventoryDebugSlots();
        lastParticleUpdateTime_ = glfwGetTime();
        lastDroppedItemUpdateTime_ = glfwGetTime();
        droppedItemTickAccumulator_ = 0.0f;
        droppedItemRenderAlpha_ = 0.0f;
        climateTemperatureOverlayReady_ = false;
        climatePrecipitationOverlayReady_ = false;
        terrainLoadRequested_ = false;
        startSaveWorker();
        startChunkLoadWorker();
        startTerrainWorkers();
        gameSceneLoaded_ = true;
        log::info("Game scene loaded.");
    }

    void Renderer::unloadGameScene()
    {
        if (!gameSceneLoaded_)
        {
            return;
        }

        log::info("Unloading game scene.");
        stopTerrainWorkers();
        stopChunkLoadWorker();
        enqueueSaveAllRuntimeChunks();
        stopSaveWorker();
        vkDeviceWaitIdle(device_);
        destroyAllTerrainChunks();
        saveSystem_.clear();
        terrainLoadRequested_ = false;
        blockBreakParticles_.clear();
        resetBlockBreaking();
        nextWorldEntityId_ = 1;
        resetDroppedItemTracking();
        playerInventory_.clear();
        updateInventoryUi();
        updateInventoryDebugSlots();
        lastParticleUpdateTime_ = 0.0;
        lastDroppedItemUpdateTime_ = 0.0;
        droppedItemTickAccumulator_ = 0.0f;
        droppedItemRenderAlpha_ = 0.0f;
        loadedChunkDiameter_ = 0;
        loadedCenterGroupChunkX_ = 0;
        loadedCenterGroupChunkZ_ = 0;
        updateTerrainStats();
        debugTextBatchDirty_ = true;
        gameSceneLoaded_ = false;
        log::info("Game scene unloaded.");
    }

    RuntimeChunk Renderer::runtimeChunkFromSnapshot(const SaveChunkSnapshot& snapshot, uint64_t generation)
    {
        RuntimeChunk chunk{};
        chunk.chunkX = snapshot.chunkX;
        chunk.chunkZ = snapshot.chunkZ;
        chunk.genState = snapshot.genState == ChunkGenState::Meshed ? ChunkGenState::Full : snapshot.genState;
        chunk.incomingFeatureMask = chunk.genState == ChunkGenState::Full ? 0 : snapshot.incomingFeatureMask;
        chunk.incomingFeatureSlots = chunk.genState == ChunkGenState::Full ? std::array<FeatureWriteListPtr, FeatureNeighborCount>{} : snapshot.incomingFeatureSlots;
        chunk.hasSavedBacking = true;
        chunk.dataDirtyForSave = false;

        if (snapshot.hasData &&
            ((snapshot.chunkData && snapshot.chunkData->blocks.size() == ChunkBlockCount) ||
                snapshot.blocks.size() == ChunkBlockCount))
        {
            auto data = std::make_shared<ChunkData>();
            if (snapshot.chunkData && snapshot.chunkData->blocks.size() == ChunkBlockCount)
            {
                *data = *snapshot.chunkData;
            }
            else
            {
                data->revision = snapshot.revision;
                data->chunkX = snapshot.chunkX;
                data->chunkZ = snapshot.chunkZ;
                data->blocks = snapshot.blocks;
                data->fluids = snapshot.fluids;
                data->temperature = snapshot.temperature;
                data->precipitation = snapshot.precipitation;
            }
            if (data->fluids.size() != ChunkBlockCount)
            {
                data->fluids.assign(ChunkBlockCount, FluidNone);
            }
            rebuildChunkDerivedCaches(*data);
            if (!snapshot.chunkData)
            {
                data->entities = snapshot.entities;
            }
            data->generation = generation;
            data->revision = snapshot.revision;
            data->chunkX = snapshot.chunkX;
            data->chunkZ = snapshot.chunkZ;
            for (WorldEntity& entity : data->entities)
            {
                if (entity.entityId == 0)
                {
                    entity.entityId = allocateWorldEntityId();
                }
                nextWorldEntityId_ = std::max(nextWorldEntityId_, entity.entityId + 1u);
                entity.previousPosition = entity.position;
                entity.collecting = false;
                entity.collectAge = 0.0f;
                if (worldEntityGrounded(entity))
                {
                    entity.renderRotationX = 0.0f;
                    entity.renderRotation = std::fmod(entity.renderRotation, 6.2831853f);
                    entity.renderRotationZ = 0.0f;
                    entity.renderSpinX = 0.0f;
                    entity.renderSpin = 0.0f;
                    entity.renderSpinZ = 0.0f;
                }
                else
                {
                    entity.renderSpinX = entity.renderSpinX == 0.0f ? 5.0f : entity.renderSpinX;
                    entity.renderSpin = entity.renderSpin == 0.0f ? 5.0f : entity.renderSpin;
                    entity.renderSpinZ = entity.renderSpinZ == 0.0f ? 5.0f : entity.renderSpinZ;
                }
            }
            chunk.data = std::move(data);
        }

        if (chunk.genState == ChunkGenState::Full)
        {
        saveSystem_.markClean(chunk.chunkX, chunk.chunkZ, chunk.data ? chunk.data->revision : 0);
        }
        return chunk;
    }

    world::TerrainBuilderConfig Renderer::terrainBuilderConfig() const
    {
        world::TerrainBuilderConfig config{};
        config.heightLut = heightLut_;
        config.activeWorldSeedSalt = activeWorldSeedSalt_;
        config.seaLevel = seaLevel_;
        config.terrainNoiseFeatureScale = terrainNoiseFeatureScale_;
        config.terrainNoiseOctaveCount = terrainNoiseOctaveCount_;
        config.terrainNoiseLacunarity = terrainNoiseLacunarity_;
        config.terrainNoiseGain = terrainNoiseGain_;
        config.terrainNoiseSimplexScale = terrainNoiseSimplexScale_;
        config.terrainDomainWarpEnabled = terrainDomainWarpEnabled_;
        config.terrainDomainWarpAmplitude = terrainDomainWarpAmplitude_;
        config.terrainDomainWarpFrequency = terrainDomainWarpFrequency_;
        config.terrainDomainWarpOctaveCount = terrainDomainWarpOctaveCount_;
        config.terrainDomainWarpGain = terrainDomainWarpGain_;
        config.temperatureNoiseStrength = temperatureNoiseStrength_;
        config.temperatureNoiseFeatureScale = temperatureNoiseFeatureScale_;
        config.temperatureNoiseOctaveCount = temperatureNoiseOctaveCount_;
        config.temperatureNoiseLacunarity = temperatureNoiseLacunarity_;
        config.temperatureNoiseGain = temperatureNoiseGain_;
        config.temperatureNoiseSimplexScale = temperatureNoiseSimplexScale_;
        config.precipitationNoiseFeatureScale = precipitationNoiseFeatureScale_;
        config.precipitationNoiseOctaveCount = precipitationNoiseOctaveCount_;
        config.precipitationNoiseLacunarity = precipitationNoiseLacunarity_;
        config.precipitationNoiseGain = precipitationNoiseGain_;
        config.precipitationNoiseSimplexScale = precipitationNoiseSimplexScale_;
        return config;
    }

    std::shared_ptr<ChunkData> Renderer::buildChunkData(int chunkX, int chunkZ) const
    {
        return world::TerrainBuilder(terrainBuilderConfig()).buildChunkData(chunkX, chunkZ);
    }

    std::array<FeatureWriteListPtr, FeatureNeighborCount> Renderer::buildTreeFeatures(const std::shared_ptr<ChunkData>& chunk, const std::array<int, ChunkColumnCount>& heights) const
    {
        return world::TerrainBuilder(terrainBuilderConfig()).buildTreeFeatures(chunk, heights);
    }

    bool Renderer::applyFeatureWrites(const std::shared_ptr<ChunkData>& chunk, const std::array<FeatureWriteListPtr, FeatureNeighborCount>& incomingFeatureSlots) const
    {
        return world::TerrainBuilder(terrainBuilderConfig()).applyFeatureWrites(chunk, incomingFeatureSlots);
    }

    void Renderer::acceptFeatureSlot(int targetChunkX, int targetChunkZ, size_t sourceSlot, FeatureWriteListPtr writes)
    {
        if (sourceSlot >= FeatureNeighborCount || !writes)
        {
            return;
        }

        const uint64_t targetKey = chunkKey(targetChunkX, targetChunkZ);
        RuntimeChunk* existingTarget = worldRuntime_.find(targetKey);
        if (existingTarget == nullptr && desiredTerrainChunks_.find(targetKey) == desiredTerrainChunks_.end())
        {
            return;
        }

        bool created = false;
        RuntimeChunk& target = existingTarget != nullptr
            ? *existingTarget
            : worldRuntime_.ensureChunkShell(targetChunkX, targetChunkZ, created);

        if ((target.genState == ChunkGenState::Full || target.genState == ChunkGenState::Meshed) && target.data)
        {
            std::array<FeatureWriteListPtr, FeatureNeighborCount> singleSlot{};
            singleSlot[sourceSlot] = std::move(writes);
            if (world::TerrainBuilder(terrainBuilderConfig()).applyFeatureWrites(target.data, singleSlot))
            {
                target.genState = ChunkGenState::Full;
                tryQueueMeshesAround(targetChunkX, targetChunkZ);
            }
            return;
        }

        const uint8_t sourceBit = static_cast<uint8_t>(1u << static_cast<uint32_t>(sourceSlot));
        if ((target.incomingFeatureMask & sourceBit) != 0)
        {
            return;
        }

        target.incomingFeatureSlots[sourceSlot] = std::move(writes);
        target.incomingFeatureMask |= sourceBit;
        tryQueueFeatureFinalize(targetKey);
    }

    void Renderer::publishFeatureSlots(RuntimeChunk& sourceChunk)
    {
        if (sourceChunk.genState == ChunkGenState::Empty)
        {
            return;
        }

        const uint64_t generation = terrainGeneration_.load();
        if (sourceChunk.outgoingPublishedTicket == generation)
        {
            return;
        }

        const bool hasOutgoingSlots = std::any_of(sourceChunk.outgoingFeatureSlots.begin(), sourceChunk.outgoingFeatureSlots.end(), [](const FeatureWriteListPtr& slot)
        {
            return slot != nullptr;
        });
        if (!hasOutgoingSlots && sourceChunk.data)
        {
            const world::TerrainBuilder terrainBuilder(terrainBuilderConfig());
            const std::array<int, ChunkColumnCount> heights = terrainBuilder.buildChunkHeightmap(sourceChunk.chunkX, sourceChunk.chunkZ);
            sourceChunk.outgoingFeatureSlots = terrainBuilder.buildTreeFeatures(sourceChunk.data, heights);
        }

        for (size_t slot = 0; slot < FeatureNeighborOffsets.size(); ++slot)
        {
            const int targetChunkX = sourceChunk.chunkX + FeatureNeighborOffsets[slot].x;
            const int targetChunkZ = sourceChunk.chunkZ + FeatureNeighborOffsets[slot].z;
            const std::optional<size_t> sourceSlot = featureNeighborIndex(-FeatureNeighborOffsets[slot].x, -FeatureNeighborOffsets[slot].z);
            if (!sourceSlot)
            {
                continue;
            }
            acceptFeatureSlot(targetChunkX, targetChunkZ, *sourceSlot, sourceChunk.outgoingFeatureSlots[slot]);
        }
        sourceChunk.outgoingPublishedTicket = generation;
    }

    void Renderer::tryQueueFeatureFinalize(uint64_t key)
    {
        RuntimeChunk* chunk = worldRuntime_.find(key);
        if (chunk == nullptr)
        {
            return;
        }

        const uint64_t generation = terrainGeneration_.load();
        if (chunk->fullTicket != generation ||
            chunk->genState != ChunkGenState::Featuring ||
            !chunk->data ||
            chunk->finalizeQueuedTicket == generation)
        {
            return;
        }

        if ((chunk->incomingFeatureMask & AllFeatureSourcesMask) != AllFeatureSourcesMask)
        {
            return;
        }

        TerrainJob job{};
        job.type = TerrainJob::Type::FinalizeFeatures;
        job.generation = generation;
        job.priority = chunk->bestPriority;
        job.chunkX = chunk->chunkX;
        job.chunkZ = chunk->chunkZ;
        job.chunk = chunk->data;
        job.incomingFeatureSlots = chunk->incomingFeatureSlots;
        chunk->finalizeQueuedTicket = generation;
        enqueueTerrainJob(std::move(job));
    }

    void Renderer::tryQueueMeshIfReady(int chunkX, int chunkZ)
    {
        const uint64_t key = chunkKey(chunkX, chunkZ);
        RuntimeChunk* target = worldRuntime_.find(key);
        const uint64_t generation = terrainGeneration_.load();
        if (target == nullptr ||
            desiredRenderChunks_.find(key) == desiredRenderChunks_.end() ||
            target->meshTicket != generation ||
            requestedMeshJobs_.find(key) != requestedMeshJobs_.end() ||
            target->meshQueuedTicket == generation ||
            (target->genState != ChunkGenState::Full && target->genState != ChunkGenState::Meshed))
        {
            return;
        }

        std::array<std::shared_ptr<ChunkData>, 9> chunks{};
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const RuntimeChunk* chunk = worldRuntime_.find(chunkKey(chunkX + dx, chunkZ + dz));
                if (chunk == nullptr || !chunk->data ||
                    chunk->genState == ChunkGenState::Empty)
                {
                    return;
                }
                chunks[static_cast<size_t>((dz + 1) * 3 + (dx + 1))] = chunk->data;
            }
        }

        if (chunkMeshReady(key))
        {
            return;
        }

        TerrainJob job{};
        job.type = TerrainJob::Type::BuildChunkMesh;
        job.generation = generation;
        job.revision = chunks[4]->revision;
        job.priority = target->bestPriority;
        job.chunkX = chunkX;
        job.chunkZ = chunkZ;
        job.meshChunks = chunks;
        requestedMeshJobs_.insert(key);
        target->meshQueuedTicket = generation;
        enqueueTerrainJob(std::move(job));
    }

    void Renderer::tryQueueMeshesAround(int chunkX, int chunkZ)
    {
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                tryQueueMeshIfReady(chunkX + dx, chunkZ + dz);
            }
        }
    }

    bool Renderer::setBlockAtWorld(int x, int y, int z, uint16_t block)
    {
        return worldRuntime_.setBlockAtWorld(x, y, z, block);
    }

    void Renderer::updateChunkEmptySubchunk(const std::shared_ptr<ChunkData>& chunk, int subchunkY)
    {
        world::WorldRuntime::updateChunkEmptySubchunk(chunk, subchunkY);
    }

    void Renderer::rebuildChunkDerivedCaches(ChunkData& chunk) const
    {
        world::WorldRuntime::rebuildDerivedCaches(chunk);
    }

    void Renderer::rebuildSubchunkMeshNow(int chunkX, int chunkZ, int subchunkY)
    {
        if (subchunkY < 0 || subchunkY >= SubchunksPerChunk)
        {
            return;
        }

        const uint64_t key = chunkKey(chunkX, chunkZ);
        RuntimeChunk* chunk = worldRuntime_.find(key);
        if (chunk == nullptr || !chunk->data || desiredTerrainChunks_.find(key) == desiredTerrainChunks_.end())
        {
            return;
        }

        const uint64_t generation = terrainGeneration_.load();
        chunk->data->generation = generation;
        const uint64_t revision = chunk->data->revision;
        TerrainBuildData mesh = buildEditedSubchunkMesh(chunk->data, subchunkY);

        requestedMeshJobs_.erase(key);
        chunk->meshQueuedTicket = 0;
        ChunkRenderData& renderData = terrainChunks_[key];
        renderData.revision = chunk->data->revision;
        renderData.chunkX = chunkX;
        renderData.chunkZ = chunkZ;
        if (chunk->data->revision != revision || chunk->data->generation != generation)
        {
            return;
        }

        TerrainMesh& targetMesh = renderData.solidSubchunks[static_cast<size_t>(subchunkY)];
        if (targetMesh.vertexBuffer != VK_NULL_HANDLE || targetMesh.indexBuffer != VK_NULL_HANDLE)
        {
            ChunkRenderData retired{};
            retired.solidSubchunks[static_cast<size_t>(subchunkY)] = std::move(targetMesh);
            targetMesh = {};
            retiredTerrainChunks_.push_back(RetiredChunkRenderData{
                static_cast<uint32_t>(MaxFramesInFlight + 1),
                std::move(retired)});
        }
        createTerrainBuffer(mesh, targetMesh);
        renderData.revision = chunk->data->revision;
        chunk->genState = ChunkGenState::Meshed;
    }

    void Renderer::rebuildEditedChunkMeshes(int blockX, int blockY, int blockZ)
    {
        if (blockY < 0 || blockY >= ChunkSizeY)
        {
            return;
        }

        const int chunkX = floorDiv(blockX, ChunkSizeX);
        const int chunkZ = floorDiv(blockZ, ChunkSizeZ);
        const int subchunkY = blockY / SubchunkSize;
        std::vector<int> chunkOffsetsX = {0};
        std::vector<int> chunkOffsetsZ = {0};
        std::vector<int> subchunkYs = {subchunkY};
        if (positiveModulo(blockX, ChunkSizeX) == 0)
        {
            chunkOffsetsX.push_back(-1);
        }
        if (positiveModulo(blockX, ChunkSizeX) == ChunkSizeX - 1)
        {
            chunkOffsetsX.push_back(1);
        }
        if (positiveModulo(blockZ, ChunkSizeZ) == 0)
        {
            chunkOffsetsZ.push_back(-1);
        }
        if (positiveModulo(blockZ, ChunkSizeZ) == ChunkSizeZ - 1)
        {
            chunkOffsetsZ.push_back(1);
        }
        if (positiveModulo(blockY, SubchunkSize) == 0)
        {
            subchunkYs.push_back(subchunkY - 1);
        }
        if (positiveModulo(blockY, SubchunkSize) == SubchunkSize - 1)
        {
            subchunkYs.push_back(subchunkY + 1);
        }

        struct AffectedSubchunk
        {
            int chunkX = 0;
            int chunkZ = 0;
            int subchunkY = 0;
        };
        std::vector<AffectedSubchunk> affectedSubchunks;
        auto addAffectedSubchunk = [&](int affectedChunkX, int affectedChunkZ, int affectedSubchunkY)
        {
            if (affectedSubchunkY < 0 || affectedSubchunkY >= SubchunksPerChunk)
            {
                return;
            }
            for (const AffectedSubchunk& existing : affectedSubchunks)
            {
                if (existing.chunkX == affectedChunkX && existing.chunkZ == affectedChunkZ && existing.subchunkY == affectedSubchunkY)
                {
                    return;
                }
            }
            affectedSubchunks.push_back({affectedChunkX, affectedChunkZ, affectedSubchunkY});
        };

        for (int offsetZ : chunkOffsetsZ)
        {
            for (int offsetX : chunkOffsetsX)
            {
                for (int affectedSubchunkY : subchunkYs)
                {
                    addAffectedSubchunk(chunkX + offsetX, chunkZ + offsetZ, affectedSubchunkY);
                }
            }
        }

        for (const AffectedSubchunk& affected : affectedSubchunks)
        {
            rebuildSubchunkMeshNow(affected.chunkX, affected.chunkZ, affected.subchunkY);
        }

        updateTerrainStats();
        debugTextBatchDirty_ = true;
    }

    TerrainBuildData Renderer::buildEditedSubchunkMesh(const std::shared_ptr<ChunkData>& chunk, int subchunkY) const
    {
        return world::TerrainMesher().buildEditedSubchunkMesh(
            chunk,
            subchunkY,
            [this](int x, int y, int z)
            {
                return blockAtWorld(x, y, z);
            },
            [this](const std::shared_ptr<ChunkData>& sourceChunk, int sourceSubchunkY, const world::TerrainMesher::BlockSampler& blockAt)
            {
                return buildSubchunkMesh(sourceChunk, sourceSubchunkY, blockAt);
            });
    }

    TerrainBuildData Renderer::buildSubchunkMesh(
        const std::shared_ptr<ChunkData>& chunk,
        int subchunkY,
        const world::TerrainMesher::BlockSampler& blockAt) const
    {
        TerrainBuildData result{};

        if (subchunkY < 0 || subchunkY >= SubchunksPerChunk || chunk->emptySubchunks[static_cast<size_t>(subchunkY)])
        {
            return result;
        }

        auto vertexAoIndex = [&](int worldX, int worldY, int worldZ, int nx, int ny, int nz, int ax, int ay, int az, int bx, int by, int bz) -> int
        {
            const int localX = worldX - chunk->chunkX * ChunkSizeX;
            const int localZ = worldZ - chunk->chunkZ * ChunkSizeZ;
            const bool sideA = blockContributesAo(blockAt(localX + nx + ax, worldY + ny + ay, localZ + nz + az));
            const bool sideB = blockContributesAo(blockAt(localX + nx + bx, worldY + ny + by, localZ + nz + bz));
            const bool corner = blockContributesAo(blockAt(localX + nx + ax + bx, worldY + ny + ay + by, localZ + nz + az + bz));
            return std::clamp(sideA && sideB ? 0 : 3 - static_cast<int>(sideA) - static_cast<int>(sideB) - static_cast<int>(corner), 0, 3);
        };

        auto vertexAoStrength = [&](int worldX, int worldY, int worldZ, int nx, int ny, int nz, int ax, int ay, int az, int bx, int by, int bz) -> float
        {
            constexpr std::array<float, 4> AoStrength = {0.55f, 0.68f, 0.82f, 1.0f};
            return AoStrength[static_cast<size_t>(vertexAoIndex(worldX, worldY, worldZ, nx, ny, nz, ax, ay, az, bx, by, bz))];
        };

        auto packAo = [](int a0, int a1, int a2, int a3) -> uint32_t
        {
            return 1u |
                (static_cast<uint32_t>(a0) << 1u) |
                (static_cast<uint32_t>(a1) << 3u) |
                (static_cast<uint32_t>(a2) << 5u) |
                (static_cast<uint32_t>(a3) << 7u);
        };

        auto faceAoMergeSignature = [&](int a0, int a1, int a2, int a3, int x, int y, int z, int face) -> uint32_t
        {
            const uint32_t signature = packAo(a0, a1, a2, a3);
            if (a0 == a1 && a0 == a2 && a0 == a3)
            {
                return signature;
            }

            return signature |
                (1u << 9u) |
                ((static_cast<uint32_t>(x) & 0x0fu) << 10u) |
                ((static_cast<uint32_t>(y) & 0x0fu) << 14u) |
                ((static_cast<uint32_t>(z) & 0x0fu) << 18u) |
                (static_cast<uint32_t>(face) << 22u);
        };

        auto faceAoSignature = [&](int x, int y, int z, int face) -> uint32_t
        {
            if (face == 0)
            {
                return faceAoMergeSignature(
                    vertexAoIndex(x, y, z, 0, 1, 0, -1, 0, 0, 0, 0, -1),
                    vertexAoIndex(x, y, z, 0, 1, 0, -1, 0, 0, 0, 0, 1),
                    vertexAoIndex(x, y, z, 0, 1, 0, 1, 0, 0, 0, 0, 1),
                    vertexAoIndex(x, y, z, 0, 1, 0, 1, 0, 0, 0, 0, -1),
                    x, y, z, face);
            }
            if (face == 1)
            {
                return faceAoMergeSignature(
                    vertexAoIndex(x, y, z, 0, -1, 0, -1, 0, 0, 0, 0, 1),
                    vertexAoIndex(x, y, z, 0, -1, 0, -1, 0, 0, 0, 0, -1),
                    vertexAoIndex(x, y, z, 0, -1, 0, 1, 0, 0, 0, 0, -1),
                    vertexAoIndex(x, y, z, 0, -1, 0, 1, 0, 0, 0, 0, 1),
                    x, y, z, face);
            }
            if (face == 2)
            {
                return faceAoMergeSignature(
                    vertexAoIndex(x, y, z, 1, 0, 0, 0, -1, 0, 0, 0, -1),
                    vertexAoIndex(x, y, z, 1, 0, 0, 0, 1, 0, 0, 0, -1),
                    vertexAoIndex(x, y, z, 1, 0, 0, 0, 1, 0, 0, 0, 1),
                    vertexAoIndex(x, y, z, 1, 0, 0, 0, -1, 0, 0, 0, 1),
                    x, y, z, face);
            }
            if (face == 3)
            {
                return faceAoMergeSignature(
                    vertexAoIndex(x, y, z, -1, 0, 0, 0, -1, 0, 0, 0, 1),
                    vertexAoIndex(x, y, z, -1, 0, 0, 0, 1, 0, 0, 0, 1),
                    vertexAoIndex(x, y, z, -1, 0, 0, 0, 1, 0, 0, 0, -1),
                    vertexAoIndex(x, y, z, -1, 0, 0, 0, -1, 0, 0, 0, -1),
                    x, y, z, face);
            }
            if (face == 4)
            {
                return faceAoMergeSignature(
                    vertexAoIndex(x, y, z, 0, 0, 1, 1, 0, 0, 0, -1, 0),
                    vertexAoIndex(x, y, z, 0, 0, 1, 1, 0, 0, 0, 1, 0),
                    vertexAoIndex(x, y, z, 0, 0, 1, -1, 0, 0, 0, 1, 0),
                    vertexAoIndex(x, y, z, 0, 0, 1, -1, 0, 0, 0, -1, 0),
                    x, y, z, face);
            }

            return faceAoMergeSignature(
                vertexAoIndex(x, y, z, 0, 0, -1, -1, 0, 0, 0, -1, 0),
                vertexAoIndex(x, y, z, 0, 0, -1, -1, 0, 0, 0, 1, 0),
                vertexAoIndex(x, y, z, 0, 0, -1, 1, 0, 0, 0, 1, 0),
                vertexAoIndex(x, y, z, 0, 0, -1, 1, 0, 0, 0, -1, 0),
                x, y, z, face);
        };

        auto topFaceRotation = [&](uint16_t block, int x, int y, int z) -> uint8_t
        {
            if (block == BlockAir || blockDefinition(block).directional)
            {
                return 0;
            }
            return static_cast<uint8_t>(worldRandom8(x, y, z, TopFaceRotationSalt) & 3u);
        };

        auto randomBlockOffset = [&](uint16_t block, int x, int y, int z) -> std::array<float, 2>
        {
            if (!blockDefinition(block).randomOffset)
            {
                return {0.0f, 0.0f};
            }

            const auto offsetFromByte = [](uint8_t value)
            {
                return (static_cast<float>(value) / 255.0f) * (RandomBlockOffsetHalfRange * 2.0f) - RandomBlockOffsetHalfRange;
            };
            return {
                offsetFromByte(worldRandom8(x, y, z, PlantPlacementSalt)),
                offsetFromByte(worldRandom8(z, y, x, PlantPlacementSalt))
            };
        };

        auto rotateLocalXz = [](float localX, float localZ, uint8_t rotation) -> std::array<float, 2>
        {
            switch (rotation & 3u)
            {
            case 1: return {1.0f - localZ, localX};
            case 2: return {1.0f - localX, 1.0f - localZ};
            case 3: return {localZ, 1.0f - localX};
            default: return {localX, localZ};
            }
        };

        auto appendFace = [&](TerrainBuildData& buildData, int x, int y, int z, int face, int width, int height, uint32_t textureLayer, uint8_t rotation, float mipDistanceScale)
        {
            const float x0 = static_cast<float>(x) - 0.5f;
            const float x1 = static_cast<float>(x + width) - 0.5f;
            const float y0 = static_cast<float>(y);
            const float y1 = static_cast<float>(y + height);
            const float z0 = static_cast<float>(z) - 0.5f;
            const float z1 = static_cast<float>(z + width) - 0.5f;
            const float uMax = static_cast<float>(width);
            const float vMax = static_cast<float>(height);

            std::array<TerrainVertex, 4> quad{};
            if (face == 0)
            {
                const float topX1 = static_cast<float>(x + width) - 0.5f;
                const float topZ1 = static_cast<float>(z + height) - 0.5f;
                quad = {{{x0, static_cast<float>(y + 1), z0, 0.0f, 0.0f}, {x0, static_cast<float>(y + 1), topZ1, vMax, 0.0f}, {topX1, static_cast<float>(y + 1), topZ1, vMax, uMax}, {topX1, static_cast<float>(y + 1), z0, 0.0f, uMax}}};
            }
            else if (face == 1)
            {
                const float bottomX1 = static_cast<float>(x + width) - 0.5f;
                const float bottomZ1 = static_cast<float>(z + height) - 0.5f;
                quad = {{{x0, y0, bottomZ1, 0.0f, 0.0f}, {x0, y0, z0, vMax, 0.0f}, {bottomX1, y0, z0, vMax, uMax}, {bottomX1, y0, bottomZ1, 0.0f, uMax}}};
            }
            else if (face == 2)
            {
                const float faceX = static_cast<float>(x) + 0.5f;
                quad = {{{faceX, y0, z0, 0.0f, 0.0f}, {faceX, y1, z0, vMax, 0.0f}, {faceX, y1, z1, vMax, uMax}, {faceX, y0, z1, 0.0f, uMax}}};
            }
            else if (face == 3)
            {
                const float faceX = static_cast<float>(x) - 0.5f;
                quad = {{{faceX, y0, z1, 0.0f, 0.0f}, {faceX, y1, z1, vMax, 0.0f}, {faceX, y1, z0, vMax, uMax}, {faceX, y0, z0, 0.0f, uMax}}};
            }
            else if (face == 4)
            {
                const float faceZ = static_cast<float>(z) + 0.5f;
                quad = {{{x1, y0, faceZ, 0.0f, 0.0f}, {x1, y1, faceZ, vMax, 0.0f}, {x0, y1, faceZ, vMax, uMax}, {x0, y0, faceZ, 0.0f, uMax}}};
            }
            else
            {
                const float faceZ = static_cast<float>(z) - 0.5f;
                quad = {{{x0, y0, faceZ, 0.0f, 0.0f}, {x0, y1, faceZ, vMax, 0.0f}, {x1, y1, faceZ, vMax, uMax}, {x1, y0, faceZ, 0.0f, uMax}}};
            }

            if (face >= 2)
            {
                for (TerrainVertex& vertex : quad)
                {
                    const float u = vertex.u;
                    const float v = vertex.v;
                    vertex.u = v;
                    vertex.v = vMax - u;
                }
            }
            else if (face == 0 && rotation != 0)
            {
                for (TerrainVertex& vertex : quad)
                {
                    const float u = vertex.u;
                    const float v = vertex.v;
                    if (rotation == 1)
                    {
                        vertex.u = v;
                        vertex.v = uMax - u;
                    }
                    else if (rotation == 2)
                    {
                        vertex.u = uMax - u;
                        vertex.v = vMax - v;
                    }
                    else
                    {
                        vertex.u = vMax - v;
                        vertex.v = u;
                    }
                }
            }

            if (face == 0)
            {
                quad[0].ao = vertexAoStrength(x, y, z, 0, 1, 0, -1, 0, 0, 0, 0, -1);
                quad[1].ao = vertexAoStrength(x, y, z + height - 1, 0, 1, 0, -1, 0, 0, 0, 0, 1);
                quad[2].ao = vertexAoStrength(x + width - 1, y, z + height - 1, 0, 1, 0, 1, 0, 0, 0, 0, 1);
                quad[3].ao = vertexAoStrength(x + width - 1, y, z, 0, 1, 0, 1, 0, 0, 0, 0, -1);
            }
            else if (face == 1)
            {
                quad[0].ao = vertexAoStrength(x, y, z + height - 1, 0, -1, 0, -1, 0, 0, 0, 0, 1);
                quad[1].ao = vertexAoStrength(x, y, z, 0, -1, 0, -1, 0, 0, 0, 0, -1);
                quad[2].ao = vertexAoStrength(x + width - 1, y, z, 0, -1, 0, 1, 0, 0, 0, 0, -1);
                quad[3].ao = vertexAoStrength(x + width - 1, y, z + height - 1, 0, -1, 0, 1, 0, 0, 0, 0, 1);
            }
            else if (face == 2)
            {
                quad[0].ao = vertexAoStrength(x, y, z, 1, 0, 0, 0, -1, 0, 0, 0, -1);
                quad[1].ao = vertexAoStrength(x, y + height - 1, z, 1, 0, 0, 0, 1, 0, 0, 0, -1);
                quad[2].ao = vertexAoStrength(x, y + height - 1, z + width - 1, 1, 0, 0, 0, 1, 0, 0, 0, 1);
                quad[3].ao = vertexAoStrength(x, y, z + width - 1, 1, 0, 0, 0, -1, 0, 0, 0, 1);
            }
            else if (face == 3)
            {
                quad[0].ao = vertexAoStrength(x, y, z + width - 1, -1, 0, 0, 0, -1, 0, 0, 0, 1);
                quad[1].ao = vertexAoStrength(x, y + height - 1, z + width - 1, -1, 0, 0, 0, 1, 0, 0, 0, 1);
                quad[2].ao = vertexAoStrength(x, y + height - 1, z, -1, 0, 0, 0, 1, 0, 0, 0, -1);
                quad[3].ao = vertexAoStrength(x, y, z, -1, 0, 0, 0, -1, 0, 0, 0, -1);
            }
            else if (face == 4)
            {
                quad[0].ao = vertexAoStrength(x + width - 1, y, z, 0, 0, 1, 1, 0, 0, 0, -1, 0);
                quad[1].ao = vertexAoStrength(x + width - 1, y + height - 1, z, 0, 0, 1, 1, 0, 0, 0, 1, 0);
                quad[2].ao = vertexAoStrength(x, y + height - 1, z, 0, 0, 1, -1, 0, 0, 0, 1, 0);
                quad[3].ao = vertexAoStrength(x, y, z, 0, 0, 1, -1, 0, 0, 0, -1, 0);
            }
            else
            {
                quad[0].ao = vertexAoStrength(x, y, z, 0, 0, -1, -1, 0, 0, 0, -1, 0);
                quad[1].ao = vertexAoStrength(x, y + height - 1, z, 0, 0, -1, -1, 0, 0, 0, 1, 0);
                quad[2].ao = vertexAoStrength(x + width - 1, y + height - 1, z, 0, 0, -1, 1, 0, 0, 0, 1, 0);
                quad[3].ao = vertexAoStrength(x + width - 1, y, z, 0, 0, -1, 1, 0, 0, 0, -1, 0);
            }

            for (TerrainVertex& vertex : quad)
            {
                vertex.textureLayer = static_cast<float>(textureLayer);
                vertex.mipDistanceScale = mipDistanceScale;
            }

            const uint32_t baseIndex = static_cast<uint32_t>(buildData.vertices.size());
            buildData.vertices.push_back(quad[0]);
            buildData.vertices.push_back(quad[1]);
            buildData.vertices.push_back(quad[2]);
            buildData.vertices.push_back(quad[3]);
            buildData.indices.push_back(baseIndex);
            buildData.indices.push_back(baseIndex + 1);
            buildData.indices.push_back(baseIndex + 2);
            buildData.indices.push_back(baseIndex);
            buildData.indices.push_back(baseIndex + 2);
            buildData.indices.push_back(baseIndex + 3);
        };

        auto appendCrossBlock = [&](TerrainBuildData& buildData, int x, int y, int z, uint16_t block, uint32_t textureLayer, float mipDistanceScale)
        {
            const std::array<float, 2> offset = randomBlockOffset(block, x, y, z);
            const float y0 = static_cast<float>(y);
            const float y1 = static_cast<float>(y + 1);
            const float originX = static_cast<float>(x) - 0.5f + offset[0];
            const float originZ = static_cast<float>(z) - 0.5f + offset[1];
            const uint8_t rotation = topFaceRotation(block, x, y, z);
            auto crossVertex = [&](float localX, float localY, float localZ, float u, float v)
            {
                const std::array<float, 2> rotated = rotateLocalXz(localX, localZ, rotation);
                return TerrainVertex{originX + rotated[0], localY, originZ + rotated[1], u, v, 1.0f};
            };

            auto appendDoubleSidedQuad = [&](TerrainVertex a, TerrainVertex b, TerrainVertex c, TerrainVertex d)
            {
                a.textureLayer = static_cast<float>(textureLayer);
                b.textureLayer = static_cast<float>(textureLayer);
                c.textureLayer = static_cast<float>(textureLayer);
                d.textureLayer = static_cast<float>(textureLayer);
                a.mipDistanceScale = mipDistanceScale;
                b.mipDistanceScale = mipDistanceScale;
                c.mipDistanceScale = mipDistanceScale;
                d.mipDistanceScale = mipDistanceScale;

                const uint32_t baseIndex = static_cast<uint32_t>(buildData.vertices.size());
                buildData.vertices.push_back(a);
                buildData.vertices.push_back(b);
                buildData.vertices.push_back(c);
                buildData.vertices.push_back(d);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 1);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex + 3);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex + 1);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 3);
                buildData.indices.push_back(baseIndex + 2);
            };

            appendDoubleSidedQuad(
                crossVertex(0.0f, y0, 0.0f, 0.0f, 1.0f),
                crossVertex(0.0f, y1, 0.0f, 0.0f, 0.0f),
                crossVertex(1.0f, y1, 1.0f, 1.0f, 0.0f),
                crossVertex(1.0f, y0, 1.0f, 1.0f, 1.0f));
            appendDoubleSidedQuad(
                crossVertex(1.0f, y0, 0.0f, 0.0f, 1.0f),
                crossVertex(1.0f, y1, 0.0f, 0.0f, 0.0f),
                crossVertex(0.0f, y1, 1.0f, 1.0f, 0.0f),
                crossVertex(0.0f, y0, 1.0f, 1.0f, 1.0f));
        };

        auto appendPropBlock = [&](TerrainBuildData& buildData, int x, int y, int z, uint16_t block)
        {
            const auto meshIt = propMeshesByBlock_.find(block);
            if (meshIt == propMeshesByBlock_.end())
            {
                return;
            }

            const assets::PropMesh& mesh = meshIt->second;
            const uint32_t textureLayer = blockFaceTextureLayer(block, 0);
            const float mipDistanceScale = blockDefinition(block).mipDistanceScale;
            const std::array<float, 2> offset = randomBlockOffset(block, x, y, z);
            const float originX = static_cast<float>(x) - 0.5f + offset[0];
            const float originY = static_cast<float>(y);
            const float originZ = static_cast<float>(z) - 0.5f + offset[1];
            const uint8_t rotation = topFaceRotation(block, x, y, z);

            for (size_t offset = 0; offset + assets::PropQuadRenderFloatCount <= mesh.quads.size(); offset += assets::PropQuadRenderFloatCount)
            {
                std::array<TerrainVertex, 4> quad{};
                size_t cursor = offset;
                for (TerrainVertex& vertex : quad)
                {
                    const float localX = mesh.quads[cursor++];
                    vertex.y = originY + mesh.quads[cursor++];
                    const float localZ = mesh.quads[cursor++];
                    const std::array<float, 2> rotated = rotateLocalXz(localX, localZ, rotation);
                    vertex.x = originX + rotated[0];
                    vertex.z = originZ + rotated[1];
                    vertex.ao = 1.0f;
                    vertex.textureLayer = static_cast<float>(textureLayer);
                    vertex.mipDistanceScale = mipDistanceScale;
                }
                for (TerrainVertex& vertex : quad)
                {
                    vertex.u = mesh.quads[cursor++];
                    vertex.v = mesh.quads[cursor++];
                }

                const uint32_t baseIndex = static_cast<uint32_t>(buildData.vertices.size());
                buildData.vertices.push_back(quad[0]);
                buildData.vertices.push_back(quad[1]);
                buildData.vertices.push_back(quad[2]);
                buildData.vertices.push_back(quad[3]);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 1);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex + 3);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex + 1);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 3);
                buildData.indices.push_back(baseIndex + 2);
            }
        };

        auto faceSignature = [&](uint16_t block, int x, int y, int z, int face) -> uint64_t
        {
            const uint32_t mipSignature = static_cast<uint32_t>(std::clamp(
                static_cast<int>(std::lround(blockDefinition(block).mipDistanceScale * 16.0f)),
                0,
                127));
            uint64_t signature = static_cast<uint64_t>(faceAoSignature(x, y, z, face)) |
                (static_cast<uint64_t>(mipSignature) << 25u) |
                (static_cast<uint64_t>(blockFaceTextureLayer(block, face)) << 32u);
            if (face == 0)
            {
                signature |= static_cast<uint64_t>(topFaceRotation(block, x, y, z)) << 56u;
            }
            return signature;
        };

        auto emitGreedy = [](std::vector<uint64_t>& mask, int maskWidth, int maskHeight, auto emit)
        {
            for (int y = 0; y < maskHeight; ++y)
            {
                for (int x = 0; x < maskWidth;)
                {
                    if (mask[y * maskWidth + x] == 0)
                    {
                        ++x;
                        continue;
                    }

                    int width = 1;
                    const uint64_t signature = mask[y * maskWidth + x];
                    while (x + width < maskWidth && mask[y * maskWidth + x + width] == signature)
                    {
                        ++width;
                    }

                    int height = 1;
                    bool canGrow = true;
                    while (y + height < maskHeight && canGrow)
                    {
                        for (int offset = 0; offset < width; ++offset)
                        {
                            if (mask[(y + height) * maskWidth + x + offset] != signature)
                            {
                                canGrow = false;
                                break;
                            }
                        }
                        if (canGrow)
                        {
                            ++height;
                        }
                    }

                    for (int clearY = 0; clearY < height; ++clearY)
                    {
                        for (int clearX = 0; clearX < width; ++clearX)
                        {
                            mask[(y + clearY) * maskWidth + x + clearX] = 0;
                        }
                    }

                    emit(x, y, width, height);
                    x += width;
                }
            }
        };

        result.vertices.reserve(256);
        result.indices.reserve(384);

        std::vector<uint64_t> mask(SubchunkSize * SubchunkSize);
        const int worldXStart = chunk->chunkX * ChunkSizeX;
        const int worldYStart = subchunkY * SubchunkSize;
        const int worldZStart = chunk->chunkZ * ChunkSizeZ;

        for (int localY = 0; localY < SubchunkSize; ++localY)
        {
            const int y = worldYStart + localY;
            std::fill(mask.begin(), mask.end(), 0);
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    mask[localZ * ChunkSizeX + localX] = blockUsesCubeMesh(block) && !neighborCullsFace(block, blockAt(localX, y + 1, localZ))
                        ? faceSignature(block, worldXStart + localX, y, worldZStart + localZ, 0)
                        : 0;
                }
            }
            emitGreedy(mask, ChunkSizeX, ChunkSizeZ, [&](int localX, int localZ, int width, int height)
            {
                const uint16_t block = blockAt(localX, y, localZ);
                appendFace(result, worldXStart + localX, y, worldZStart + localZ, 0, width, height, blockFaceTextureLayer(block, 0), topFaceRotation(block, worldXStart + localX, y, worldZStart + localZ), blockDefinition(block).mipDistanceScale);
            });

            std::fill(mask.begin(), mask.end(), 0);
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    mask[localZ * ChunkSizeX + localX] = blockUsesCubeMesh(block) && !neighborCullsFace(block, blockAt(localX, y - 1, localZ))
                        ? faceSignature(block, worldXStart + localX, y, worldZStart + localZ, 1)
                        : 0;
                }
            }
            emitGreedy(mask, ChunkSizeX, ChunkSizeZ, [&](int localX, int localZ, int width, int height)
            {
                const uint16_t block = blockAt(localX, y, localZ);
                appendFace(result, worldXStart + localX, y, worldZStart + localZ, 1, width, height, blockFaceTextureLayer(block, 1), 0, blockDefinition(block).mipDistanceScale);
            });
        }

        for (int localX = 0; localX < ChunkSizeX; ++localX)
        {
            const int worldX = worldXStart + localX;
            std::fill(mask.begin(), mask.end(), 0);
            for (int localY = 0; localY < SubchunkSize; ++localY)
            {
                const int y = worldYStart + localY;
                for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    mask[localY * ChunkSizeZ + localZ] = blockUsesCubeMesh(block) && !neighborCullsFace(block, blockAt(localX + 1, y, localZ))
                        ? faceSignature(block, worldX, y, worldZStart + localZ, 2)
                        : 0;
                }
            }
            emitGreedy(mask, ChunkSizeZ, SubchunkSize, [&](int localZ, int localY, int width, int height)
            {
                const uint16_t block = blockAt(localX, worldYStart + localY, localZ);
                appendFace(result, worldX, worldYStart + localY, worldZStart + localZ, 2, width, height, blockFaceTextureLayer(block, 2), 0, blockDefinition(block).mipDistanceScale);
            });

            std::fill(mask.begin(), mask.end(), 0);
            for (int localY = 0; localY < SubchunkSize; ++localY)
            {
                const int y = worldYStart + localY;
                for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    mask[localY * ChunkSizeZ + localZ] = blockUsesCubeMesh(block) && !neighborCullsFace(block, blockAt(localX - 1, y, localZ))
                        ? faceSignature(block, worldX, y, worldZStart + localZ, 3)
                        : 0;
                }
            }
            emitGreedy(mask, ChunkSizeZ, SubchunkSize, [&](int localZ, int localY, int width, int height)
            {
                const uint16_t block = blockAt(localX, worldYStart + localY, localZ);
                appendFace(result, worldX, worldYStart + localY, worldZStart + localZ, 3, width, height, blockFaceTextureLayer(block, 3), 0, blockDefinition(block).mipDistanceScale);
            });
        }

        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            const int worldZ = worldZStart + localZ;
            std::fill(mask.begin(), mask.end(), 0);
            for (int localY = 0; localY < SubchunkSize; ++localY)
            {
                const int y = worldYStart + localY;
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    mask[localY * ChunkSizeX + localX] = blockUsesCubeMesh(block) && !neighborCullsFace(block, blockAt(localX, y, localZ + 1))
                        ? faceSignature(block, worldXStart + localX, y, worldZ, 4)
                        : 0;
                }
            }
            emitGreedy(mask, ChunkSizeX, SubchunkSize, [&](int localX, int localY, int width, int height)
            {
                const uint16_t block = blockAt(localX, worldYStart + localY, localZ);
                appendFace(result, worldXStart + localX, worldYStart + localY, worldZ, 4, width, height, blockFaceTextureLayer(block, 4), 0, blockDefinition(block).mipDistanceScale);
            });

            std::fill(mask.begin(), mask.end(), 0);
            for (int localY = 0; localY < SubchunkSize; ++localY)
            {
                const int y = worldYStart + localY;
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    mask[localY * ChunkSizeX + localX] = blockUsesCubeMesh(block) && !neighborCullsFace(block, blockAt(localX, y, localZ - 1))
                        ? faceSignature(block, worldXStart + localX, y, worldZ, 5)
                        : 0;
                }
            }
            emitGreedy(mask, ChunkSizeX, SubchunkSize, [&](int localX, int localY, int width, int height)
            {
                const uint16_t block = blockAt(localX, worldYStart + localY, localZ);
                appendFace(result, worldXStart + localX, worldYStart + localY, worldZ, 5, width, height, blockFaceTextureLayer(block, 5), 0, blockDefinition(block).mipDistanceScale);
            });
        }

        for (int localY = 0; localY < SubchunkSize; ++localY)
        {
            const int y = worldYStart + localY;
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    if (blockDefinition(block).renderType == BlockRenderType::Cross)
                    {
                        appendCrossBlock(result, worldXStart + localX, y, worldZStart + localZ, block, blockFaceTextureLayer(block, 0), blockDefinition(block).mipDistanceScale);
                    }
                    else if (blockDefinition(block).renderType == BlockRenderType::Prop)
                    {
                        appendPropBlock(result, worldXStart + localX, y, worldZStart + localZ, block);
                    }
                }
            }
        }

        return result;
    }

    CompletedChunkMesh Renderer::buildChunkMesh(const std::array<std::shared_ptr<ChunkData>, 9>& chunks, uint64_t generation) const
    {
        return world::TerrainMesher().buildChunkMesh(
            chunks,
            generation,
            [this](const std::shared_ptr<ChunkData>& chunk, int subchunkY, const world::TerrainMesher::BlockSampler& blockAt)
            {
                return buildSubchunkMesh(chunk, subchunkY, blockAt);
            },
            [this](uint16_t block)
            {
                return block != BlockAir && blockDefinition(block).faceOcclusion == BlockFaceOcclusion::Opaque;
            });
    }

    bool Renderer::chunkMeshReady(uint64_t key) const
    {
        auto renderIt = terrainChunks_.find(key);
        if (renderIt == terrainChunks_.end())
        {
            return false;
        }
        const RuntimeChunk* chunk = worldRuntime_.find(key);
        if (chunk == nullptr || !chunk->data || renderIt->second.revision != chunk->data->revision)
        {
            return false;
        }

        for (const TerrainMesh& mesh : renderIt->second.solidSubchunks)
        {
            if (mesh.indexCount > 0)
            {
                return true;
            }
        }
        for (const TerrainMesh& mesh : renderIt->second.fluidSubchunks)
        {
            if (mesh.indexCount > 0)
            {
                return true;
            }
        }
        return false;
    }

    void Renderer::destroyChunkRenderData(Renderer::ChunkRenderData& chunk)
    {
        for (TerrainMesh& mesh : chunk.solidSubchunks)
        {
            destroyTerrainMesh(mesh);
        }
        for (TerrainMesh& mesh : chunk.fluidSubchunks)
        {
            destroyTerrainMesh(mesh);
        }
    }

    void Renderer::destroyAllTerrainChunks()
    {
        for (auto& entry : terrainChunks_)
        {
            destroyChunkRenderData(entry.second);
        }
        for (RetiredChunkRenderData& retired : retiredTerrainChunks_)
        {
            destroyChunkRenderData(retired.chunk);
        }
        terrainChunks_.clear();
        retiredTerrainChunks_.clear();
        pendingUnloadChunks_.clear();
        pendingUnloadSet_.clear();
        requestedChunkJobs_.clear();
        requestedMeshJobs_.clear();
        desiredTerrainChunks_.clear();
        desiredFeatureChunks_.clear();
        desiredRenderChunks_.clear();
        worldRuntime_.clear();
        resetDroppedItemTracking();
    }

    void Renderer::updateTerrainStats()
    {
        terrainDrawCount_ = 0;
        terrainFaceCount_ = 0;
        terrainVertexCount_ = 0;

        for (const auto& entry : terrainChunks_)
        {
            for (const TerrainMesh& mesh : entry.second.solidSubchunks)
            {
                if (mesh.indexCount == 0)
                {
                    continue;
                }

                ++terrainDrawCount_;
                terrainVertexCount_ += mesh.vertexCount;
                terrainFaceCount_ += mesh.indexCount / 6;
            }
            for (const TerrainMesh& mesh : entry.second.fluidSubchunks)
            {
                if (mesh.indexCount == 0)
                {
                    continue;
                }

                ++terrainDrawCount_;
                terrainVertexCount_ += mesh.vertexCount;
                terrainFaceCount_ += mesh.indexCount / 6;
            }
        }

        terrainDrawText_ = "DRAWS: " + std::to_string(terrainDrawCount_);
        terrainFaceText_ = "FACES: " + std::to_string(terrainFaceCount_);
        terrainVertexText_ = "QUADS: " + std::to_string(terrainVertexCount_);
    }

    std::array<int, ChunkColumnCount> Renderer::buildChunkHeightmap(int chunkX, int chunkZ) const
    {
        return world::TerrainBuilder(terrainBuilderConfig()).buildChunkHeightmap(chunkX, chunkZ);
    }

    PackedTerrainQuad Renderer::packTerrainQuad(const TerrainVertex& a, const TerrainVertex& b, const TerrainVertex& c, const TerrainVertex& d) const
    {
        auto encodeSignedFixed = [](float value) -> uint32_t
        {
            const int32_t fixed = static_cast<int32_t>(std::lround(value * TerrainPositionPackScale));
            const uint32_t magnitude = fixed < 0 ? static_cast<uint32_t>(-fixed) : static_cast<uint32_t>(fixed);
            return (magnitude << 1u) | (fixed < 0 ? 1u : 0u);
        };
        auto quantizeUnsigned = [](float value, float scale, int maxValue) -> uint32_t
        {
            return static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(value * scale)), 0, maxValue));
        };
        auto quantizeSigned = [](float value, float scale) -> int32_t
        {
            return std::clamp(static_cast<int32_t>(std::lround(value * scale)), -32768, 32767);
        };
        auto packI16Pair = [](int32_t a, int32_t b) -> uint32_t
        {
            return (static_cast<uint32_t>(static_cast<uint16_t>(a)) & 0xFFFFu) |
                (static_cast<uint32_t>(static_cast<uint16_t>(b)) << 16u);
        };
        auto aoIndex = [](float ao) -> uint32_t
        {
            if (ao <= 0.615f)
            {
                return 0;
            }
            if (ao <= 0.75f)
            {
                return 1;
            }
            if (ao <= 0.91f)
            {
                return 2;
            }
            return 3;
        };

        const float edgeUx = b.x - a.x;
        const float edgeUy = b.y - a.y;
        const float edgeUz = b.z - a.z;
        const float edgeVx = d.x - a.x;
        const float edgeVy = d.y - a.y;
        const float edgeVz = d.z - a.z;

        PackedTerrainQuad packed{};
        packed.p0x = encodeSignedFixed(a.x);
        packed.p0y = encodeSignedFixed(a.y);
        packed.p0z = encodeSignedFixed(a.z);
        packed.edgeUxy = packI16Pair(quantizeSigned(edgeUx, TerrainPositionPackScale), quantizeSigned(edgeUy, TerrainPositionPackScale));
        packed.edgeUzVx = packI16Pair(quantizeSigned(edgeUz, TerrainPositionPackScale), quantizeSigned(edgeVx, TerrainPositionPackScale));
        packed.edgeVyz = packI16Pair(quantizeSigned(edgeVy, TerrainPositionPackScale), quantizeSigned(edgeVz, TerrainPositionPackScale));
        packed.uv0 = packI16Pair(quantizeSigned(a.u, TerrainUvPackScale), quantizeSigned(a.v, TerrainUvPackScale));
        packed.uvU = packI16Pair(quantizeSigned(b.u - a.u, TerrainUvPackScale), quantizeSigned(b.v - a.v, TerrainUvPackScale));
        packed.uvV = packI16Pair(quantizeSigned(d.u - a.u, TerrainUvPackScale), quantizeSigned(d.v - a.v, TerrainUvPackScale));
        const uint32_t textureLayer = quantizeUnsigned(a.textureLayer, 1.0f, 0xFF);
        const uint32_t mipDistanceScale = quantizeUnsigned(a.mipDistanceScale, 16.0f, 0x3FF);
        packed.material = textureLayer |
            (mipDistanceScale << 8u) |
            (aoIndex(a.ao) << 18u) |
            (aoIndex(b.ao) << 20u) |
            (aoIndex(c.ao) << 22u) |
            (aoIndex(d.ao) << 24u);
        return packed;
    }

    std::vector<PackedTerrainQuad> Renderer::buildPackedTerrainQuads(const TerrainBuildData& buildData) const
    {
        std::vector<PackedTerrainQuad> quads;
        quads.reserve(buildData.vertices.size() / 4u);
        size_t indexCursor = 0;
        for (size_t base = 0; base + 3 < buildData.vertices.size(); base += 4)
        {
            const uint32_t baseIndex = static_cast<uint32_t>(base);
            size_t referencedIndices = 0;
            while (indexCursor + referencedIndices < buildData.indices.size())
            {
                const uint32_t index = buildData.indices[indexCursor + referencedIndices];
                if (index < baseIndex || index > baseIndex + 3u)
                {
                    break;
                }
                ++referencedIndices;
            }
            indexCursor += referencedIndices;
            if (referencedIndices == 0)
            {
                continue;
            }

            const TerrainVertex& a = buildData.vertices[base + 0u];
            const TerrainVertex& b = buildData.vertices[base + 1u];
            const TerrainVertex& c = buildData.vertices[base + 2u];
            const TerrainVertex& d = buildData.vertices[base + 3u];
            quads.push_back(packTerrainQuad(a, b, c, d));
            if (referencedIndices >= 12)
            {
                quads.push_back(packTerrainQuad(a, d, c, b));
            }
        }
        return quads;
    }

    void Renderer::createTerrainBuffer(const TerrainBuildData& buildData, TerrainMesh& mesh, bool deviceLocal)
    {
        if (buildData.vertices.empty() || buildData.indices.empty())
        {
            return;
        }

        mesh.vertexCount = static_cast<uint32_t>(buildData.vertices.size());
        mesh.indexCount = static_cast<uint32_t>(buildData.indices.size());

        if (!deviceLocal)
        {
            const VkDeviceSize vertexBufferSize = sizeof(TerrainVertex) * buildData.vertices.size();
            const VkDeviceSize indexBufferSize = sizeof(uint32_t) * buildData.indices.size();
            createBuffer(
                vertexBufferSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                mesh.vertexBuffer,
                mesh.vertexMemory);

            void* data = nullptr;
            vkMapMemory(device_, mesh.vertexMemory, 0, vertexBufferSize, 0, &data);
            std::memcpy(data, buildData.vertices.data(), static_cast<size_t>(vertexBufferSize));
            vkUnmapMemory(device_, mesh.vertexMemory);

            createBuffer(
                indexBufferSize,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                mesh.indexBuffer,
                mesh.indexMemory);

            vkMapMemory(device_, mesh.indexMemory, 0, indexBufferSize, 0, &data);
            std::memcpy(data, buildData.indices.data(), static_cast<size_t>(indexBufferSize));
            vkUnmapMemory(device_, mesh.indexMemory);
            return;
        }

        const std::vector<PackedTerrainQuad> packedQuads = buildPackedTerrainQuads(buildData);
        if (packedQuads.empty())
        {
            mesh = {};
            return;
        }
        mesh.vertexCount = static_cast<uint32_t>(packedQuads.size());
        mesh.indexCount = static_cast<uint32_t>(packedQuads.size() * 6u);

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        const VkDeviceSize vertexBufferSize = sizeof(PackedTerrainQuad) * packedQuads.size();
        const VkDeviceSize stagingSize = vertexBufferSize;
        createBuffer(
            stagingSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingMemory);

        void* data = nullptr;
        vkMapMemory(device_, stagingMemory, 0, stagingSize, 0, &data);
        std::memcpy(data, packedQuads.data(), static_cast<size_t>(vertexBufferSize));
        vkUnmapMemory(device_, stagingMemory);

        createBuffer(
            vertexBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            mesh.vertexBuffer,
            mesh.vertexMemory);

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferCopy vertexCopy{};
        vertexCopy.srcOffset = 0;
        vertexCopy.dstOffset = 0;
        vertexCopy.size = vertexBufferSize;
        vkCmdCopyBuffer(commandBuffer, stagingBuffer, mesh.vertexBuffer, 1, &vertexCopy);

        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = mesh.vertexBuffer;
        barrier.size = vertexBufferSize;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            0,
            0,
            nullptr,
            1,
            &barrier,
            0,
            nullptr);

        endSingleTimeCommands(commandBuffer);
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingMemory, nullptr);
        createTerrainVertexDescriptorSet(mesh, vertexBufferSize);
    }

    void Renderer::createChunkTerrainBuffers(const std::array<TerrainBuildData, SubchunkCount>& buildData, std::array<TerrainMesh, SubchunkCount>& meshes)
    {
        struct PendingUpload
        {
            size_t subchunkY = 0;
            VkDeviceSize vertexSize = 0;
            VkDeviceSize vertexOffset = 0;
        };

        auto alignCopyOffset = [](VkDeviceSize value)
        {
            return (value + 3) & ~VkDeviceSize{3};
        };

        std::vector<PendingUpload> uploads;
        uploads.reserve(buildData.size());
        std::array<std::vector<PackedTerrainQuad>, SubchunkCount> packedSubchunks{};
        VkDeviceSize stagingSize = 0;
        for (size_t subchunkY = 0; subchunkY < buildData.size(); ++subchunkY)
        {
            const TerrainBuildData& source = buildData[subchunkY];
            if (source.vertices.empty() || source.indices.empty())
            {
                continue;
            }
            packedSubchunks[subchunkY] = buildPackedTerrainQuads(source);
            if (packedSubchunks[subchunkY].empty())
            {
                continue;
            }

            TerrainMesh& mesh = meshes[subchunkY];
            mesh.vertexCount = static_cast<uint32_t>(packedSubchunks[subchunkY].size());
            mesh.indexCount = static_cast<uint32_t>(packedSubchunks[subchunkY].size() * 6u);

            PendingUpload upload{};
            upload.subchunkY = subchunkY;
            upload.vertexSize = sizeof(PackedTerrainQuad) * packedSubchunks[subchunkY].size();
            upload.vertexOffset = alignCopyOffset(stagingSize);
            stagingSize = upload.vertexOffset + upload.vertexSize;
            uploads.push_back(upload);

            createBuffer(
                upload.vertexSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                mesh.vertexBuffer,
                mesh.vertexMemory);
        }

        if (uploads.empty())
        {
            return;
        }

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(
            stagingSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingMemory);

        void* data = nullptr;
        vkMapMemory(device_, stagingMemory, 0, stagingSize, 0, &data);
        for (const PendingUpload& upload : uploads)
        {
            const std::vector<PackedTerrainQuad>& source = packedSubchunks[upload.subchunkY];
            std::memcpy(static_cast<char*>(data) + upload.vertexOffset, source.data(), static_cast<size_t>(upload.vertexSize));
        }
        vkUnmapMemory(device_, stagingMemory);

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        std::vector<VkBufferMemoryBarrier> barriers;
        barriers.reserve(uploads.size());
        for (const PendingUpload& upload : uploads)
        {
            const TerrainMesh& mesh = meshes[upload.subchunkY];

            VkBufferCopy vertexCopy{};
            vertexCopy.srcOffset = upload.vertexOffset;
            vertexCopy.dstOffset = 0;
            vertexCopy.size = upload.vertexSize;
            vkCmdCopyBuffer(commandBuffer, stagingBuffer, mesh.vertexBuffer, 1, &vertexCopy);

            VkBufferMemoryBarrier vertexBarrier{};
            vertexBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            vertexBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vertexBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vertexBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vertexBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vertexBarrier.buffer = mesh.vertexBuffer;
            vertexBarrier.size = upload.vertexSize;
            barriers.push_back(vertexBarrier);
        }

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            0,
            0,
            nullptr,
            static_cast<uint32_t>(barriers.size()),
            barriers.data(),
            0,
            nullptr);

        endSingleTimeCommands(commandBuffer);
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingMemory, nullptr);

        for (const PendingUpload& upload : uploads)
        {
            createTerrainVertexDescriptorSet(meshes[upload.subchunkY], upload.vertexSize);
        }
    }

    void Renderer::createTerrainVertexDescriptorSet(TerrainMesh& mesh, VkDeviceSize vertexBufferSize)
    {
        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool_;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &terrainVertexDescriptorSetLayout_;

        if (vkAllocateDescriptorSets(device_, &setInfo, &mesh.vertexDescriptorSet) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate terrain vertex descriptor set.");
        }

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mesh.vertexBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = vertexBufferSize;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = mesh.vertexDescriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    void Renderer::createCommandBuffers()
    {
        commandBuffers_.resize(MaxFramesInFlight);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool_;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());

        if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate command buffers.");
        }
    }

    void Renderer::createSyncObjects()
    {
        imageAvailableSemaphores_.resize(MaxFramesInFlight);
        renderFinishedSemaphores_.resize(MaxFramesInFlight);
        inFlightFences_.resize(MaxFramesInFlight);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MaxFramesInFlight; ++i)
        {
            if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
                vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create sync objects.");
            }
        }
    }

    int Renderer::temperatureSeed() const
    {
        return TemperatureNoiseSeed + activeWorldSeedSalt_;
    }

    int Renderer::precipitationSeed() const
    {
        return PrecipitationNoiseSeed + activeWorldSeedSalt_;
    }

    void Renderer::cleanupSwapchain()
    {
        for (VkFramebuffer framebuffer : sceneFramebuffers_)
        {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
        sceneFramebuffers_.clear();

        for (VkFramebuffer framebuffer : framebuffers_)
        {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
        framebuffers_.clear();

        for (Texture& texture : sceneColorTargets_)
        {
            destroyTexture(texture);
        }
        sceneColorTargets_.clear();
        for (Texture& texture : sceneDepthTargets_)
        {
            destroyTexture(texture);
        }
        sceneDepthTargets_.clear();

        if (depthImageView_ != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device_, depthImageView_, nullptr);
            depthImageView_ = VK_NULL_HANDLE;
        }
        if (depthImage_ != VK_NULL_HANDLE)
        {
            vkDestroyImage(device_, depthImage_, nullptr);
            depthImage_ = VK_NULL_HANDLE;
        }
        if (depthMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, depthMemory_, nullptr);
            depthMemory_ = VK_NULL_HANDLE;
        }

        for (VkImageView view : swapchainImageViews_)
        {
            vkDestroyImageView(device_, view, nullptr);
        }
        swapchainImageViews_.clear();

        if (swapchain_ != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    void Renderer::recreateSwapchain()
    {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        while (width == 0 || height == 0)
        {
            glfwWaitEvents();
            glfwGetFramebufferSize(window_, &width, &height);
        }

        vkDeviceWaitIdle(device_);
        cleanupSwapchain();
        createSwapchain();
        createImageViews();
        createDepthResources();
        createSceneTargets();
        createFramebuffers();
    }

    Renderer::QueueFamilyIndices Renderer::findQueueFamilies(VkPhysicalDevice device) const
    {
        QueueFamilyIndices indices;

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

        for (uint32_t i = 0; i < familyCount; ++i)
        {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                indices.graphics = i;
            }

            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
            if (presentSupport)
            {
                indices.present = i;
            }

            if (indices.complete())
            {
                break;
            }
        }

        return indices;
    }

    bool Renderer::isDeviceSuitable(VkPhysicalDevice device) const
    {
        QueueFamilyIndices indices = findQueueFamilies(device);
        if (!indices.complete())
        {
            return false;
        }

        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> available(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());

        std::set<std::string> required(DeviceExtensions.begin(), DeviceExtensions.end());
        for (const auto& extension : available)
        {
            required.erase(extension.extensionName);
        }

        if (!required.empty())
        {
            return false;
        }

        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(device, &features);
        if (features.fillModeNonSolid != VK_TRUE)
        {
            return false;
        }

        uint32_t formatCount = 0;
        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr);
        return formatCount > 0 && presentModeCount > 0;
    }

    VkSurfaceFormatKHR Renderer::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
    {
        for (const auto& format : formats)
        {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return format;
            }
        }
        return formats.front();
    }

    VkPresentModeKHR Renderer::choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const
    {
        for (VkPresentModeKHR mode : modes)
        {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return mode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D Renderer::chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);

        VkExtent2D extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return extent;
    }

    VkShaderModule Renderer::createShaderModule(const std::string& path) const
    {
        std::vector<char> code = readFile(path);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device_, &createInfo, nullptr, &module) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create shader module: " + path);
        }

        return module;
    }

    Renderer::Texture Renderer::createTexture(const std::string& path, VkFormat format)
    {
        Texture texture;
        int channels = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &texture.width, &texture.height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            throw std::runtime_error("Failed to load texture: " + path);
        }

        Texture result = createTextureFromRgba(pixels, texture.width, texture.height, format);
        stbi_image_free(pixels);
        return result;
    }

    Renderer::Texture Renderer::createTextureFromRgba(const unsigned char* pixels, int width, int height, VkFormat format)
    {
        Texture texture;
        texture.width = width;
        texture.height = height;
        texture.mipLevels = calculateMipLevels(width, height);

        VkDeviceSize imageSize = static_cast<VkDeviceSize>(texture.width) * static_cast<VkDeviceSize>(texture.height) * 4;
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory);

        void* data = nullptr;
        vkMapMemory(device_, stagingMemory, 0, imageSize, 0, &data);
        std::memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device_, stagingMemory);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height), 1};
        imageInfo.mipLevels = texture.mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device_, &imageInfo, nullptr, &texture.image) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create texture image.");
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, texture.image, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &texture.memory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate texture memory.");
        }

        vkBindImageMemory(device_, texture.image, texture.memory, 0);
        transitionImageLayout(texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mipLevels);
        copyBufferToImage(stagingBuffer, texture.image, static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height));
        generateMipmaps(texture.image, texture.width, texture.height, texture.mipLevels);

        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingMemory, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = texture.mipLevels;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &viewInfo, nullptr, &texture.view) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create texture image view.");
        }

        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool_;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &descriptorSetLayout_;

        if (vkAllocateDescriptorSets(device_, &setInfo, &texture.descriptorSet) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate texture descriptor set.");
        }

        VkDescriptorImageInfo imageDescriptor{};
        imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageDescriptor.imageView = texture.view;
        imageDescriptor.sampler = sampler_;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = texture.descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

        return texture;
    }

    Renderer::Texture Renderer::createRenderTargetTexture(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspectMask, VkImageLayout descriptorLayout)
    {
        Texture texture;
        texture.width = static_cast<int>(swapchainExtent_.width);
        texture.height = static_cast<int>(swapchainExtent_.height);
        texture.mipLevels = 1;
        texture.layers = 1;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {swapchainExtent_.width, swapchainExtent_.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device_, &imageInfo, nullptr, &texture.image) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create render target image.");
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, texture.image, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &texture.memory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate render target memory.");
        }

        vkBindImageMemory(device_, texture.image, texture.memory, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectMask;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &viewInfo, nullptr, &texture.view) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create render target image view.");
        }

        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool_;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &descriptorSetLayout_;

        if (vkAllocateDescriptorSets(device_, &setInfo, &texture.descriptorSet) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate render target descriptor set.");
        }

        VkDescriptorImageInfo imageDescriptor{};
        imageDescriptor.imageLayout = descriptorLayout;
        imageDescriptor.imageView = texture.view;
        imageDescriptor.sampler = sampler_;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = texture.descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

        return texture;
    }

    Renderer::Texture Renderer::createTextureArray(const std::vector<std::string>& paths, float alphaMultiplier)
    {
        if (paths.empty())
        {
            throw std::runtime_error("Cannot create an empty texture array.");
        }

        Texture texture;
        texture.layers = static_cast<uint32_t>(paths.size());

        std::vector<unsigned char> pixels;
        std::vector<unsigned char> mipOverridePixels;
        std::vector<TextureMipOverride> mipOverrides;
        for (size_t layer = 0; layer < paths.size(); ++layer)
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_uc* loadedPixels = stbi_load(paths[layer].c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (loadedPixels == nullptr)
            {
                throw std::runtime_error("Failed to load texture: " + paths[layer]);
            }

            if (layer == 0)
            {
                texture.width = width;
                texture.height = height;
                texture.mipLevels = calculateMipLevels(width, height);
                pixels.resize(static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height) * 4u * paths.size());
            }
            else if (width != texture.width || height != texture.height)
            {
                stbi_image_free(loadedPixels);
                throw std::runtime_error("Texture array images must have the same size: " + paths[layer]);
            }

            const size_t layerSize = static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height) * 4u;
            std::memcpy(pixels.data() + layer * layerSize, loadedPixels, layerSize);
            if (alphaMultiplier < 1.0f)
            {
                unsigned char* layerPixels = pixels.data() + layer * layerSize;
                for (size_t pixel = 0; pixel < layerSize / 4u; ++pixel)
                {
                    unsigned char& alpha = layerPixels[pixel * 4u + 3u];
                    alpha = static_cast<unsigned char>(std::clamp(
                        static_cast<int>(std::lround(static_cast<float>(alpha) * alphaMultiplier)),
                        0,
                        255));
                }
            }
            stbi_image_free(loadedPixels);
        }

        for (size_t layer = 0; layer < paths.size(); ++layer)
        {
            if (!isBlockTexturePath(paths[layer]))
            {
                continue;
            }

            const size_t layerSize = static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height) * 4u;
            std::vector<unsigned char> previousMip(layerSize);
            std::memcpy(previousMip.data(), pixels.data() + layer * layerSize, layerSize);
            uint32_t previousWidth = static_cast<uint32_t>(texture.width);
            uint32_t previousHeight = static_cast<uint32_t>(texture.height);

            for (uint32_t mip = 1; mip < texture.mipLevels; ++mip)
            {
                const std::filesystem::path path = manualMipPath(paths[layer], mip);
                const uint32_t expectedWidth = std::max(1u, static_cast<uint32_t>(texture.width) >> mip);
                const uint32_t expectedHeight = std::max(1u, static_cast<uint32_t>(texture.height) >> mip);
                std::vector<unsigned char> mipPixels;

                if (std::filesystem::exists(path))
                {
                    int width = 0;
                    int height = 0;
                    int channels = 0;
                    stbi_uc* loadedPixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
                    if (loadedPixels == nullptr)
                    {
                        throw std::runtime_error("Failed to load manual mip texture: " + path.string());
                    }
                    if (static_cast<uint32_t>(width) != expectedWidth || static_cast<uint32_t>(height) != expectedHeight)
                    {
                        stbi_image_free(loadedPixels);
                        throw std::runtime_error("Manual mip texture has wrong size: " + path.string());
                    }

                    const size_t mipSize = static_cast<size_t>(expectedWidth) * static_cast<size_t>(expectedHeight) * 4u;
                    mipPixels.resize(mipSize);
                    std::memcpy(mipPixels.data(), loadedPixels, mipSize);
                    stbi_image_free(loadedPixels);
                }
                else
                {
                    mipPixels = downsampleRgba2x(previousMip, previousWidth, previousHeight, expectedWidth, expectedHeight);
                    writePngRgba(path, mipPixels, expectedWidth, expectedHeight);
                }

                const VkDeviceSize offset = static_cast<VkDeviceSize>(mipOverridePixels.size());
                const size_t mipSize = static_cast<size_t>(expectedWidth) * static_cast<size_t>(expectedHeight) * 4u;
                mipOverridePixels.resize(mipOverridePixels.size() + mipSize);
                std::memcpy(mipOverridePixels.data() + static_cast<size_t>(offset), mipPixels.data(), mipSize);

                mipOverrides.push_back({
                    static_cast<uint32_t>(layer),
                    mip,
                    expectedWidth,
                    expectedHeight,
                    offset
                });

                previousMip = std::move(mipPixels);
                previousWidth = expectedWidth;
                previousHeight = expectedHeight;
            }
        }

        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(pixels.size());
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory);

        void* data = nullptr;
        vkMapMemory(device_, stagingMemory, 0, imageSize, 0, &data);
        std::memcpy(data, pixels.data(), pixels.size());
        vkUnmapMemory(device_, stagingMemory);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height), 1};
        imageInfo.mipLevels = texture.mipLevels;
        imageInfo.arrayLayers = texture.layers;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device_, &imageInfo, nullptr, &texture.image) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create texture array image.");
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, texture.image, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &texture.memory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate texture array memory.");
        }

        vkBindImageMemory(device_, texture.image, texture.memory, 0);
        transitionImageLayout(texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mipLevels, texture.layers);
        copyBufferToImage(stagingBuffer, texture.image, static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height), texture.layers);

        if (mipOverrides.empty())
        {
            generateMipmaps(texture.image, texture.width, texture.height, texture.mipLevels, texture.layers);
        }
        else
        {
            VkBuffer mipOverrideBuffer = VK_NULL_HANDLE;
            VkDeviceMemory mipOverrideMemory = VK_NULL_HANDLE;
            const VkDeviceSize mipOverrideSize = static_cast<VkDeviceSize>(mipOverridePixels.size());
            createBuffer(mipOverrideSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mipOverrideBuffer, mipOverrideMemory);

            void* mipData = nullptr;
            vkMapMemory(device_, mipOverrideMemory, 0, mipOverrideSize, 0, &mipData);
            std::memcpy(mipData, mipOverridePixels.data(), mipOverridePixels.size());
            vkUnmapMemory(device_, mipOverrideMemory);

            generateTextureArrayMipmaps(texture.image, texture.width, texture.height, texture.mipLevels, texture.layers, mipOverrides, mipOverrideBuffer);

            vkDestroyBuffer(device_, mipOverrideBuffer, nullptr);
            vkFreeMemory(device_, mipOverrideMemory, nullptr);
        }

        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingMemory, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = texture.mipLevels;
        viewInfo.subresourceRange.layerCount = texture.layers;

        if (vkCreateImageView(device_, &viewInfo, nullptr, &texture.view) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create texture array image view.");
        }

        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool_;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &descriptorSetLayout_;

        if (vkAllocateDescriptorSets(device_, &setInfo, &texture.descriptorSet) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate texture array descriptor set.");
        }

        VkDescriptorImageInfo imageDescriptor{};
        imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageDescriptor.imageView = texture.view;
        imageDescriptor.sampler = sampler_;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = texture.descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

        return texture;
    }

    uint32_t Renderer::calculateMipLevels(int width, int height) const
    {
        const int maxDimension = std::max(width, height);
        return static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(maxDimension)))) + 1;
    }

    void Renderer::generateMipmaps(VkImage image, int32_t width, int32_t height, uint32_t mipLevels, uint32_t layerCount) const
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = layerCount;
        barrier.subresourceRange.levelCount = 1;

        int32_t mipWidth = width;
        int32_t mipHeight = height;
        for (uint32_t mip = 1; mip < mipLevels; ++mip)
        {
            barrier.subresourceRange.baseMipLevel = mip - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);

            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = mip - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = layerCount;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = mip;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = layerCount;

            vkCmdBlitImage(
                commandBuffer,
                image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &blit,
                VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);

            mipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
            mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
        }

        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);

        endSingleTimeCommands(commandBuffer);
    }

    void Renderer::generateTextureArrayMipmaps(
        VkImage image,
        int32_t width,
        int32_t height,
        uint32_t mipLevels,
        uint32_t layerCount,
        const std::vector<TextureMipOverride>& mipOverrides,
        VkBuffer mipOverrideBuffer) const
    {
        std::vector<const TextureMipOverride*> overrideBySubresource(static_cast<size_t>(layerCount) * mipLevels, nullptr);
        for (const TextureMipOverride& mipOverride : mipOverrides)
        {
            if (mipOverride.layer >= layerCount || mipOverride.mipLevel == 0 || mipOverride.mipLevel >= mipLevels)
            {
                continue;
            }
            overrideBySubresource[static_cast<size_t>(mipOverride.layer) * mipLevels + mipOverride.mipLevel] = &mipOverride;
        }

        std::vector<VkImageLayout> layouts(static_cast<size_t>(layerCount) * mipLevels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        auto subresourceIndex = [mipLevels](uint32_t layer, uint32_t mip)
        {
            return static_cast<size_t>(layer) * mipLevels + mip;
        };

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        auto transitionSubresource = [&](uint32_t layer, uint32_t mip, VkImageLayout newLayout)
        {
            VkImageLayout& oldLayout = layouts[subresourceIndex(layer, mip)];
            if (oldLayout == newLayout)
            {
                return;
            }

            VkAccessFlags srcAccess = 0;
            VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            {
                srcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            {
                srcAccess = VK_ACCESS_TRANSFER_READ_BIT;
            }

            VkAccessFlags dstAccess = 0;
            VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            {
                dstAccess = VK_ACCESS_TRANSFER_READ_BIT;
            }
            else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                dstAccess = VK_ACCESS_SHADER_READ_BIT;
                dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            {
                dstAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            }

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = dstAccess;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = mip;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = layer;
            barrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(
                commandBuffer,
                srcStage,
                dstStage,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);

            oldLayout = newLayout;
        };

        for (uint32_t layer = 0; layer < layerCount; ++layer)
        {
            for (uint32_t mip = 1; mip < mipLevels; ++mip)
            {
                if (const TextureMipOverride* mipOverride = overrideBySubresource[subresourceIndex(layer, mip)])
                {
                    VkBufferImageCopy region{};
                    region.bufferOffset = mipOverride->bufferOffset;
                    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    region.imageSubresource.mipLevel = mip;
                    region.imageSubresource.baseArrayLayer = layer;
                    region.imageSubresource.layerCount = 1;
                    region.imageExtent = {mipOverride->width, mipOverride->height, 1};

                    vkCmdCopyBufferToImage(commandBuffer, mipOverrideBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                    continue;
                }

                transitionSubresource(layer, mip - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

                const int32_t sourceWidth = std::max(1, width >> (mip - 1));
                const int32_t sourceHeight = std::max(1, height >> (mip - 1));
                const int32_t destinationWidth = std::max(1, width >> mip);
                const int32_t destinationHeight = std::max(1, height >> mip);

                VkImageBlit blit{};
                blit.srcOffsets[0] = {0, 0, 0};
                blit.srcOffsets[1] = {sourceWidth, sourceHeight, 1};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = mip - 1;
                blit.srcSubresource.baseArrayLayer = layer;
                blit.srcSubresource.layerCount = 1;
                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {destinationWidth, destinationHeight, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = mip;
                blit.dstSubresource.baseArrayLayer = layer;
                blit.dstSubresource.layerCount = 1;

                vkCmdBlitImage(
                    commandBuffer,
                    image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &blit,
                    VK_FILTER_LINEAR);

                transitionSubresource(layer, mip - 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }

        for (uint32_t layer = 0; layer < layerCount; ++layer)
        {
            for (uint32_t mip = 0; mip < mipLevels; ++mip)
            {
                transitionSubresource(layer, mip, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }

        endSingleTimeCommands(commandBuffer);
    }

    void Renderer::destroyTexture(Texture& texture)
    {
        if (texture.descriptorSet != VK_NULL_HANDLE && descriptorPool_ != VK_NULL_HANDLE)
        {
            vkFreeDescriptorSets(device_, descriptorPool_, 1, &texture.descriptorSet);
        }
        if (texture.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device_, texture.view, nullptr);
        }
        if (texture.image != VK_NULL_HANDLE)
        {
            vkDestroyImage(device_, texture.image, nullptr);
        }
        if (texture.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, texture.memory, nullptr);
        }
        texture = {};
    }

    void Renderer::destroyTerrainMesh(TerrainMesh& mesh)
    {
        if (mesh.vertexDescriptorSet != VK_NULL_HANDLE && descriptorPool_ != VK_NULL_HANDLE)
        {
            vkFreeDescriptorSets(device_, descriptorPool_, 1, &mesh.vertexDescriptorSet);
        }
        if (mesh.vertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, mesh.vertexBuffer, nullptr);
        }
        if (mesh.vertexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, mesh.vertexMemory, nullptr);
        }
        if (mesh.indexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, mesh.indexBuffer, nullptr);
        }
        if (mesh.indexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, mesh.indexMemory, nullptr);
        }
        mesh = {};
    }

    uint32_t Renderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
    {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);

        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
        {
            if ((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        throw std::runtime_error("Failed to find suitable memory type.");
    }

    void Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory, uint32_t* memoryTypeIndex) const
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create buffer.");
        }

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, buffer, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);
        if (memoryTypeIndex != nullptr)
        {
            *memoryTypeIndex = allocInfo.memoryTypeIndex;
        }

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate buffer memory.");
        }

        vkBindBufferMemory(device_, buffer, memory, 0);
    }

    void Renderer::createDeviceLocalBuffer(const void* source, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory, uint32_t* memoryTypeIndex) const
    {
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingMemory);

        void* data = nullptr;
        vkMapMemory(device_, stagingMemory, 0, size, 0, &data);
        std::memcpy(data, source, static_cast<size_t>(size));
        vkUnmapMemory(device_, stagingMemory);

        createBuffer(
            size,
            usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            buffer,
            memory,
            memoryTypeIndex);

        copyBuffer(stagingBuffer, buffer, size);
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingMemory, nullptr);
    }

    void Renderer::copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) const
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferCopy region{};
        region.size = size;
        vkCmdCopyBuffer(commandBuffer, source, destination, 1, &region);

        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = destination;
        barrier.size = size;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            1,
            &barrier,
            0,
            nullptr);

        endSingleTimeCommands(commandBuffer);
    }

    void Renderer::uploadBufferRegions(VkBuffer destination, const std::vector<BufferUploadRegion>& regions) const
    {
        if (regions.empty())
        {
            return;
        }

        VkDeviceSize stagingSize = 0;
        for (const BufferUploadRegion& region : regions)
        {
            stagingSize += region.size;
        }

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(
            stagingSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingMemory);

        std::vector<VkBufferCopy> copies;
        copies.reserve(regions.size());

        void* data = nullptr;
        vkMapMemory(device_, stagingMemory, 0, stagingSize, 0, &data);
        VkDeviceSize sourceOffset = 0;
        for (const BufferUploadRegion& region : regions)
        {
            std::memcpy(static_cast<char*>(data) + sourceOffset, region.source, static_cast<size_t>(region.size));

            VkBufferCopy copy{};
            copy.srcOffset = sourceOffset;
            copy.dstOffset = region.destinationOffset;
            copy.size = region.size;
            copies.push_back(copy);

            sourceOffset += region.size;
        }
        vkUnmapMemory(device_, stagingMemory);

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        vkCmdCopyBuffer(commandBuffer, stagingBuffer, destination, static_cast<uint32_t>(copies.size()), copies.data());

        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = destination;
        barrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            1,
            &barrier,
            0,
            nullptr);

        endSingleTimeCommands(commandBuffer);
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingMemory, nullptr);
    }

    void Renderer::uploadBufferData(VkBuffer destination, const void* source, VkDeviceSize size, VkDeviceSize destinationOffset) const
    {
        const BufferUploadRegion region{source, size, destinationOffset};
        uploadBufferRegions(destination, std::vector<BufferUploadRegion>{region});
    }

    void Renderer::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount) const
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        std::vector<VkBufferImageCopy> regions(layerCount);
        const VkDeviceSize layerSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
        for (uint32_t layer = 0; layer < layerCount; ++layer)
        {
            VkBufferImageCopy& region = regions[layer];
            region.bufferOffset = layerSize * layer;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.baseArrayLayer = layer;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {width, height, 1};
        }

        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), regions.data());
        endSingleTimeCommands(commandBuffer);
    }

    void Renderer::transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount) const
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.layerCount = layerCount;

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else
        {
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        }

        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        endSingleTimeCommands(commandBuffer);
    }

    VkCommandBuffer Renderer::beginSingleTimeCommands() const
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool_;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void Renderer::endSingleTimeCommands(VkCommandBuffer commandBuffer) const
    {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue_);
        vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
    }

    void Renderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera, Vec3 cameraPosition, Vec3 playerPosition, std::string_view fpsText, bool debugTextVisible, VkBuffer screenshotBuffer, bool showPlayer, bool terrainWireframe, int climateOverlayMode, int menuOverlayMode, bool hudVisible, bool gameSceneRenderEnabled, uint64_t worldTicks)
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to begin command buffer.");
        }
        if (timestampSupported_ && timestampQueryPool_ != VK_NULL_HANDLE)
        {
            const uint32_t firstQuery = currentFrame_ * 2;
            vkCmdResetQueryPool(commandBuffer, timestampQueryPool_, firstQuery, 2);
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampQueryPool_, firstQuery);
        }

        VkClearValue clearColor{};
        clearColor.color = {{0.45f, 0.68f, 0.95f, 1.0f}};
        VkClearValue clearDepth{};
        clearDepth.depthStencil = {1.0f, 0};
        std::array<VkClearValue, 2> clearValues = {clearColor, clearDepth};

        VkRenderPassBeginInfo scenePassInfo{};
        scenePassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        scenePassInfo.renderPass = sceneRenderPass_;
        scenePassInfo.framebuffer = sceneFramebuffers_[imageIndex];
        scenePassInfo.renderArea.offset = {0, 0};
        scenePassInfo.renderArea.extent = swapchainExtent_;
        scenePassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        scenePassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &scenePassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapchainExtent_.width);
        viewport.height = static_cast<float>(swapchainExtent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapchainExtent_;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        if (gameSceneRenderEnabled)
        {
            const float aspect = static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);
            constexpr uint64_t SkyTicksPerDay = 28800;
            constexpr double TwoPi = 6.283185307179586;
            constexpr double HalfPi = 1.5707963267948966;
            const double dayPhase = static_cast<double>(worldTicks % SkyTicksPerDay) / static_cast<double>(SkyTicksPerDay);
            const double skyAngle = HalfPi - dayPhase * TwoPi;
            const std::array<float, 3> sunDirection{
                static_cast<float>(std::cos(skyAngle)),
                static_cast<float>(std::sin(skyAngle)),
                0.0f
            };
            const std::array<float, 3> moonDirection{-sunDirection[0], -sunDirection[1], -sunDirection[2]};
            SpriteRect rect;
            if (projectSkyDirection(camera, aspect, sunDirection, rect))
            {
                drawSprite(commandBuffer, sun_, rect);
            }
            if (projectSkyDirection(camera, aspect, moonDirection, rect))
            {
                drawSprite(commandBuffer, moon_, rect);
            }

            drawTerrain(commandBuffer, camera, cameraPosition, terrainWireframe, true, false, imageIndex);
            drawTerrain(commandBuffer, camera, cameraPosition, terrainWireframe, false, true, imageIndex);
            if (showPlayer && menuOverlayMode == 0)
            {
                drawPlayer(commandBuffer, camera, cameraPosition);
            }
            drawBlockBreakParticles(commandBuffer, camera, cameraPosition);
            drawDroppedItems(commandBuffer, camera, cameraPosition, playerPosition);
            if (menuOverlayMode == 0)
            {
                drawBlockSelection(commandBuffer, camera, cameraPosition);
            }
        }
        vkCmdEndRenderPass(commandBuffer);

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass_;
        renderPassInfo.framebuffer = framebuffers_[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchainExtent_;
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        SpriteRect sceneRect{};
        sceneRect.halfWidth = 1.0f;
        sceneRect.halfHeight = 1.0f;
        if (gameSceneRenderEnabled)
        {
            drawSprite(commandBuffer, sceneColorTargets_[imageIndex], sceneRect, {0.0f, 1.0f, 1.0f, -1.0f});
            drawClimateOverlay(commandBuffer, climateOverlayMode);
        }

        if (gameSceneRenderEnabled && menuOverlayMode == 0 && hudVisible)
        {
            if (cachedMenuOverlayMode_ != 0)
            {
                cachedMenuOverlayMode_ = 0;
                debugTextBufferDirty_ = true;
            }
            const float crosshairPixels = 32.0f;
            SpriteRect crosshairRect{};
            crosshairRect.halfWidth = crosshairPixels / static_cast<float>(swapchainExtent_.width);
            crosshairRect.halfHeight = crosshairPixels / static_cast<float>(swapchainExtent_.height);
            drawSprite(commandBuffer, crosshair_, crosshairRect);
        }

        if (debugTextVisible)
        {
            updateDebugTextBatch(fpsText);
            drawTextBatch(commandBuffer, debugTextBatch_);
        }
        if (!renderRmlUi(commandBuffer, menuOverlayMode, hudVisible))
        {
            drawMenuOverlay(commandBuffer, menuOverlayMode);
        }

        vkCmdEndRenderPass(commandBuffer);

        if (screenshotBuffer != VK_NULL_HANDLE)
        {
            copySwapchainImageToBuffer(commandBuffer, imageIndex, screenshotBuffer);
        }
        if (timestampSupported_ && timestampQueryPool_ != VK_NULL_HANDLE)
        {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampQueryPool_, currentFrame_ * 2 + 1);
        }

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to record command buffer.");
        }
    }

    void Renderer::copySwapchainImageToBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, VkBuffer buffer) const
    {
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = swapchainImages_[imageIndex];
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toTransfer);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {swapchainExtent_.width, swapchainExtent_.height, 1};
        vkCmdCopyImageToBuffer(commandBuffer, swapchainImages_[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);

        VkImageMemoryBarrier toPresent{};
        toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = swapchainImages_[imageIndex];
        toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toPresent.subresourceRange.levelCount = 1;
        toPresent.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toPresent);
    }

    void Renderer::saveScreenshot(VkDeviceMemory memory, VkDeviceSize size) const
    {
        void* data = nullptr;
        vkMapMemory(device_, memory, 0, size, 0, &data);
        writeBmp(screenshotPath(), static_cast<const unsigned char*>(data), swapchainExtent_.width, swapchainExtent_.height, swapchainImageFormat_);
        vkUnmapMemory(device_, memory);
    }

    void Renderer::updatePlayerMesh(Vec3 playerPosition, float playerYaw)
    {
        if (playerMesh_.vertexCount == 0)
        {
            return;
        }

        const Vec3 forward{std::cos(playerYaw), 0.0f, std::sin(playerYaw)};
        const Vec3 right{std::sin(playerYaw), 0.0f, -std::cos(playerYaw)};
        const VkDeviceSize size = sizeof(TerrainVertex) * playerLocalVertices_.size();
        void* data = nullptr;
        vkMapMemory(device_, playerMesh_.vertexMemory, 0, size, 0, &data);
        auto* vertices = static_cast<TerrainVertex*>(data);
        for (size_t i = 0; i < playerLocalVertices_.size(); ++i)
        {
            const TerrainVertex& local = playerLocalVertices_[i];
            TerrainVertex vertex = local;
            vertex.x = playerPosition.x + local.x * right.x - local.z * forward.x;
            vertex.y = playerPosition.y + local.y;
            vertex.z = playerPosition.z + local.x * right.z - local.z * forward.z;
            vertices[i] = vertex;
        }
        vkUnmapMemory(device_, playerMesh_.vertexMemory);
    }

    void Renderer::drawTerrain(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, bool wireframe, bool drawBlocks, bool drawFluids, uint32_t sceneImageIndex)
    {
        if (terrainChunks_.empty())
        {
            return;
        }

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapchainExtent_.width);
        viewport.height = static_cast<float>(swapchainExtent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapchainExtent_;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        const float aspect = static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);
        const Mat4 projection = perspective(FieldOfViewRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);
        const Frustum frustum = makeFrustum(camera, {}, aspect);

        TerrainPush push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = static_cast<float>(glfwGetTime());
        push.fluidWaterParams[0] = fluidWaterAlpha_;

        uint32_t visibleDrawCount = 0;
        uint32_t visibleFaceCount = 0;
        uint32_t visibleVertexCount = 0;
        if (drawBlocks)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe ? terrainWireframePipeline_ : terrainPipeline_);
            vkCmdPushConstants(commandBuffer, terrainPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout_, 0, 1, &terrainTextureArray_.descriptorSet, 0, nullptr);

            for (const auto& entry : terrainChunks_)
            {
                const ChunkRenderData& chunk = entry.second;
                const float minX = static_cast<float>(chunk.chunkX * ChunkSizeX) - 0.5f - cameraPosition.x;
                const float maxX = static_cast<float>(chunk.chunkX * ChunkSizeX + ChunkSizeX) - 0.5f - cameraPosition.x;
                const float minZ = static_cast<float>(chunk.chunkZ * ChunkSizeZ) - 0.5f - cameraPosition.z;
                const float maxZ = static_cast<float>(chunk.chunkZ * ChunkSizeZ + ChunkSizeZ) - 0.5f - cameraPosition.z;
                for (size_t subchunkY = 0; subchunkY < chunk.solidSubchunks.size(); ++subchunkY)
                {
                    const TerrainMesh& mesh = chunk.solidSubchunks[subchunkY];
                    if (mesh.indexCount == 0)
                    {
                        continue;
                    }

                    const float minY = static_cast<float>(subchunkY * SubchunkSize) - cameraPosition.y;
                    const float maxY = minY + static_cast<float>(SubchunkSize);
                    if (!aabbIntersectsFrustum(frustum, {minX, minY, minZ}, {maxX, maxY, maxZ}))
                    {
                        continue;
                    }

                    drawTerrainMeshBound(commandBuffer, mesh);
                    ++visibleDrawCount;
                    visibleFaceCount += mesh.indexCount / 6;
                    visibleVertexCount += mesh.vertexCount;
                }
            }
        }

        if (drawFluids)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, fluidPipeline_);
            vkCmdPushConstants(commandBuffer, terrainPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout_, 0, 1, &fluidTextureArray_.descriptorSet, 0, nullptr);
            for (const auto& entry : terrainChunks_)
            {
                const ChunkRenderData& chunk = entry.second;
                const float minX = static_cast<float>(chunk.chunkX * ChunkSizeX) - 0.5f - cameraPosition.x;
                const float maxX = static_cast<float>(chunk.chunkX * ChunkSizeX + ChunkSizeX) - 0.5f - cameraPosition.x;
                const float minZ = static_cast<float>(chunk.chunkZ * ChunkSizeZ) - 0.5f - cameraPosition.z;
                const float maxZ = static_cast<float>(chunk.chunkZ * ChunkSizeZ + ChunkSizeZ) - 0.5f - cameraPosition.z;
                for (size_t subchunkY = 0; subchunkY < chunk.fluidSubchunks.size(); ++subchunkY)
                {
                    const TerrainMesh& mesh = chunk.fluidSubchunks[subchunkY];
                    if (mesh.indexCount == 0)
                    {
                        continue;
                    }

                    const float minY = static_cast<float>(subchunkY * SubchunkSize) - cameraPosition.y;
                    const float maxY = minY + static_cast<float>(SubchunkSize + 1);
                    if (!aabbIntersectsFrustum(frustum, {minX, minY, minZ}, {maxX, maxY, maxZ}))
                    {
                        continue;
                    }

                    drawTerrainMeshBound(commandBuffer, mesh);
                    ++visibleDrawCount;
                    visibleFaceCount += mesh.indexCount / 6;
                    visibleVertexCount += mesh.vertexCount;
                }
            }
        }

        if (drawBlocks && !drawFluids && (visibleDrawCount != terrainDrawCount_ ||
            visibleFaceCount != terrainFaceCount_ ||
            visibleVertexCount != terrainVertexCount_))
        {
            terrainDrawCount_ = visibleDrawCount;
            terrainFaceCount_ = visibleFaceCount;
            terrainVertexCount_ = visibleVertexCount;
            terrainDrawText_ = "DRAWS: " + std::to_string(terrainDrawCount_);
            terrainFaceText_ = "FACES: " + std::to_string(terrainFaceCount_);
            terrainVertexText_ = "QUADS: " + std::to_string(terrainVertexCount_);
            debugTextBatchDirty_ = true;
        }
    }

    const BlockDefinition& Renderer::blockDefinition(uint16_t block) const
    {
        static const BlockDefinition fallback{};
        if (static_cast<size_t>(block) >= blockDefinitions_.size())
        {
            return fallback;
        }
        return blockDefinitions_[block];
    }

    bool Renderer::raycastBlock(DVec3 origin, Vec3 direction, BlockRaycastHit& hit) const
    {
        return gameplay::BlockInteractionSystem::raycastBlock(
            origin,
            direction,
            [this](int x, int y, int z) { return blockAtWorld(x, y, z); },
            [this](uint16_t block) -> const BlockDefinition& { return blockDefinition(block); },
            hit);
    }

    uint16_t Renderer::blockAtWorld(int x, int y, int z) const
    {
        return worldRuntime_.blockAtWorld(x, y, z);
    }

    bool Renderer::breakBlockAtHit(const BlockRaycastHit& hit)
    {
        const uint16_t destroyedBlock = blockAtWorld(hit.blockX, hit.blockY, hit.blockZ);
        if (destroyedBlock == BlockAir || blockDefinition(destroyedBlock).hardness < 0.0f)
        {
            return false;
        }

        const bool changed = setBlockAtWorld(hit.blockX, hit.blockY, hit.blockZ, BlockAir);
        if (!changed)
        {
            return false;
        }

        spawnBlockBreakParticles(hit.blockX, hit.blockY, hit.blockZ, destroyedBlock);
        spawnBlockDrops(hit.blockX, hit.blockY, hit.blockZ, destroyedBlock);
        playBlockBreakSound(hit.blockX, hit.blockY, hit.blockZ);
        rebuildEditedChunkMeshes(hit.blockX, hit.blockY, hit.blockZ);
        return true;
    }

    void Renderer::resetBlockBreaking()
    {
        gameplay::BlockInteractionSystem::resetBreaking(blockBreaking_);
    }

    bool Renderer::terrainCellBlocksPlayer(int x, int y, int z) const
    {
        return worldRuntime_.terrainCellBlocksPlayer(
            x,
            y,
            z,
            [this](uint16_t block) -> const BlockDefinition& { return blockDefinition(block); });
    }

    uint32_t Renderer::blockFaceTextureLayer(uint16_t block, int face) const
    {
        if (face < 0 || face >= 6 || static_cast<size_t>(block) >= blockTextureLayers_.size())
        {
            return 0;
        }

        return blockTextureLayers_[block].faces[static_cast<size_t>(face)];
    }

    uint32_t Renderer::blockFaceTextureLayerForHit(uint16_t block, const BlockRaycastHit& hit) const
    {
        const int dx = hit.previousBlockX - hit.blockX;
        const int dy = hit.previousBlockY - hit.blockY;
        const int dz = hit.previousBlockZ - hit.blockZ;
        if (dy > 0)
        {
            return blockFaceTextureLayer(block, 0);
        }
        if (dy < 0)
        {
            return blockFaceTextureLayer(block, 1);
        }
        if (dx != 0 || dz != 0)
        {
            return blockFaceTextureLayer(block, 2);
        }
        return blockFaceTextureLayer(block, 0);
    }

    bool Renderer::blockUsesCubeMesh(uint16_t block) const
    {
        return blockDefinition(block).renderType == BlockRenderType::Cube;
    }

    bool Renderer::blockContributesAo(uint16_t block) const
    {
        return blockDefinition(block).ao;
    }

    bool Renderer::neighborCullsFace(uint16_t block, uint16_t neighbor) const
    {
        if (neighbor == BlockAir)
        {
            return false;
        }

        const BlockDefinition& neighborDefinition = blockDefinition(neighbor);
        if (block == neighbor && neighborDefinition.sameBlockFaceCulling)
        {
            return true;
        }

        return neighborDefinition.faceOcclusion == BlockFaceOcclusion::Opaque;
    }

    void Renderer::drawPlayer(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition) const
    {
        const float aspect = static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);
        const Mat4 projection = perspective(FieldOfViewRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);

        TerrainPush push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = 0.0f;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, playerPipeline_);
        vkCmdPushConstants(commandBuffer, terrainPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
        drawTerrainMesh(commandBuffer, playerMesh_, playerTexture_);
    }

    void Renderer::spawnBlockBreakParticles(int x, int y, int z, uint16_t block)
    {
        const BlockDefinition& definition = blockDefinition(block);
        if (definition.renderType == BlockRenderType::None)
        {
            return;
        }

        if (blockBreakParticles_.size() + BlockBreakParticleCount > MaxBlockBreakParticles)
        {
            const size_t removeCount = std::min(blockBreakParticles_.size(), blockBreakParticles_.size() + BlockBreakParticleCount - MaxBlockBreakParticles);
            blockBreakParticles_.erase(blockBreakParticles_.begin(), blockBreakParticles_.begin() + static_cast<std::ptrdiff_t>(removeCount));
        }

        uint32_t state = worldRandomHash(x, y, z, PlantPlacementSalt) ^ (static_cast<uint32_t>(block) * 0x45d9f3bu);
        auto nextRandom = [&]()
        {
            state = state * 1664525u + 1013904223u;
            state ^= state >> 16u;
            return static_cast<float>(state & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
        };
        auto randomRange = [&](float minValue, float maxValue)
        {
            return minValue + (maxValue - minValue) * nextRandom();
        };

        const uint32_t textureLayer = blockFaceTextureLayer(block, 0);
        for (uint32_t i = 0; i < BlockBreakParticleCount; ++i)
        {
            const int tileX = static_cast<int>(nextRandom() * 4.0f) & 3;
            const int tileY = static_cast<int>(nextRandom() * 4.0f) & 3;
            constexpr float TileSize = 0.25f;

            BlockBreakParticle particle{};
            particle.position = {
                static_cast<float>(x) - 0.5f + nextRandom(),
                static_cast<float>(y) + nextRandom(),
                static_cast<float>(z) - 0.5f + nextRandom()
            };
            particle.velocity = {
                randomRange(-1.2f, 1.2f),
                randomRange(1.5f, 4.0f),
                randomRange(-1.2f, 1.2f)
            };
            particle.lifetime = randomRange(0.45f, 0.75f);
            particle.size = randomRange(0.10f, 0.16f);
            particle.textureLayer = textureLayer;
            particle.u0 = static_cast<float>(tileX) * TileSize;
            particle.v0 = static_cast<float>(tileY) * TileSize;
            particle.u1 = particle.u0 + TileSize;
            particle.v1 = particle.v0 + TileSize;
            blockBreakParticles_.push_back(particle);
        }
    }

    void Renderer::spawnBlockMiningParticle(const BlockRaycastHit& hit, uint16_t block)
    {
        const BlockDefinition& definition = blockDefinition(block);
        if (definition.renderType == BlockRenderType::None)
        {
            return;
        }

        if (blockBreakParticles_.size() + 1u > MaxBlockBreakParticles)
        {
            blockBreakParticles_.erase(blockBreakParticles_.begin());
        }

        static thread_local std::mt19937 random{std::random_device{}()};
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        std::uniform_real_distribution<float> signedUnit(-1.0f, 1.0f);

        Vec3 normal{
            static_cast<float>(hit.previousBlockX - hit.blockX),
            static_cast<float>(hit.previousBlockY - hit.blockY),
            static_cast<float>(hit.previousBlockZ - hit.blockZ)
        };
        if (normal.x == 0.0f && normal.y == 0.0f && normal.z == 0.0f)
        {
            normal = {0.0f, 1.0f, 0.0f};
        }
        normal = normalize(normal);

        Vec3 tangentA = std::abs(normal.y) > 0.5f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
        Vec3 tangentB = normalize(cross(normal, tangentA));
        tangentA = normalize(cross(tangentB, normal));

        const float localA = signedUnit(random) * 0.42f;
        const float localB = signedUnit(random) * 0.42f;
        const int tileX = static_cast<int>(unit(random) * 4.0f) & 3;
        const int tileY = static_cast<int>(unit(random) * 4.0f) & 3;
        constexpr float TileSize = 0.25f;

        BlockBreakParticle particle{};
        particle.position = {
            static_cast<float>(hit.blockX) + normal.x * 0.51f + tangentA.x * localA + tangentB.x * localB,
            static_cast<float>(hit.blockY) + 0.5f + normal.y * 0.51f + tangentA.y * localA + tangentB.y * localB,
            static_cast<float>(hit.blockZ) + normal.z * 0.51f + tangentA.z * localA + tangentB.z * localB
        };
        particle.velocity = {
            normal.x * 1.6f + signedUnit(random) * 0.35f,
            normal.y * 1.6f + 1.0f + unit(random) * 0.8f,
            normal.z * 1.6f + signedUnit(random) * 0.35f
        };
        particle.lifetime = 0.28f + unit(random) * 0.18f;
        particle.size = 0.08f + unit(random) * 0.04f;
        particle.textureLayer = blockFaceTextureLayerForHit(block, hit);
        particle.u0 = static_cast<float>(tileX) * TileSize;
        particle.v0 = static_cast<float>(tileY) * TileSize;
        particle.u1 = particle.u0 + TileSize;
        particle.v1 = particle.v0 + TileSize;
        blockBreakParticles_.push_back(particle);
    }

    void Renderer::updateBlockBreakParticles()
    {
        const double now = glfwGetTime();
        if (lastParticleUpdateTime_ <= 0.0)
        {
            lastParticleUpdateTime_ = now;
            return;
        }

        const float dt = static_cast<float>(std::clamp(now - lastParticleUpdateTime_, 0.0, 0.05));
        lastParticleUpdateTime_ = now;
        if (dt <= 0.0f || blockBreakParticles_.empty())
        {
            return;
        }

        const float drag = std::pow(BlockBreakParticleDrag, dt * 60.0f);
        for (BlockBreakParticle& particle : blockBreakParticles_)
        {
            particle.age += dt;
            particle.velocity.x *= drag;
            particle.velocity.z *= drag;
            particle.velocity.y = particle.velocity.y * drag - BlockBreakParticleGravity * dt;
            const float previousY = particle.position.y;
            particle.position.x += particle.velocity.x * dt;
            particle.position.y += particle.velocity.y * dt;
            particle.position.z += particle.velocity.z * dt;

            const float radius = particle.size * 0.5f;
            const int groundX = blockCoordinateXz(particle.position.x);
            const int groundY = blockCoordinateY(particle.position.y - radius);
            const int groundZ = blockCoordinateXz(particle.position.z);
            if (particle.velocity.y < 0.0f &&
                particle.position.y <= previousY &&
                terrainCellBlocksPlayer(groundX, groundY, groundZ))
            {
                particle.position.y = static_cast<float>(groundY + 1) + radius;
                particle.velocity.y *= -0.25f;
                particle.velocity.x *= 0.55f;
                particle.velocity.z *= 0.55f;
                if (std::abs(particle.velocity.y) < 0.35f)
                {
                    particle.velocity.y = 0.0f;
                }
            }
        }

        blockBreakParticles_.erase(std::remove_if(blockBreakParticles_.begin(), blockBreakParticles_.end(), [](const BlockBreakParticle& particle)
        {
            return particle.age >= particle.lifetime;
        }), blockBreakParticles_.end());
    }

    void Renderer::drawBlockBreakParticles(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition)
    {
        updateBlockBreakParticles();
        const bool drawBreakingOverlay = blockBreaking_.active &&
            blockBreaking_.progress > 0.0f &&
            blockBreaking_.progress < 1.0f &&
            blockDefinition(blockBreaking_.block).renderType == BlockRenderType::Cube;
        if ((!drawBreakingOverlay && blockBreakParticles_.empty()) ||
            particlePipeline_ == VK_NULL_HANDLE ||
            particleVertexBuffer_ == VK_NULL_HANDLE ||
            particleIndexBuffer_ == VK_NULL_HANDLE)
        {
            return;
        }

        std::vector<TerrainVertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(MaxBlockBreakParticles * 4u);
        indices.reserve(MaxBlockBreakParticles * 6u);

        auto appendQuad = [&](const std::array<Vec3, 4>& positions, float u0, float v0, float u1, float v1, float ao, uint32_t textureLayer, float mipDistanceScale)
        {
            if (vertices.size() + 4u > MaxBlockBreakParticles * 4u)
            {
                return;
            }

            const uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
            const float layer = static_cast<float>(textureLayer);
            vertices.push_back({positions[0].x, positions[0].y, positions[0].z, u0, v1, ao, layer, mipDistanceScale});
            vertices.push_back({positions[1].x, positions[1].y, positions[1].z, u0, v0, ao, layer, mipDistanceScale});
            vertices.push_back({positions[2].x, positions[2].y, positions[2].z, u1, v0, ao, layer, mipDistanceScale});
            vertices.push_back({positions[3].x, positions[3].y, positions[3].z, u1, v1, ao, layer, mipDistanceScale});
            indices.push_back(baseIndex);
            indices.push_back(baseIndex + 1u);
            indices.push_back(baseIndex + 2u);
            indices.push_back(baseIndex);
            indices.push_back(baseIndex + 2u);
            indices.push_back(baseIndex + 3u);
        };
        if (drawBreakingOverlay)
        {
            const size_t stage = std::min(
                blockBreakingTextureLayers_.size() - 1u,
                static_cast<size_t>(std::floor(blockBreaking_.progress * static_cast<float>(BlockBreakingStageCount))));
            const uint32_t layer = blockBreakingTextureLayers_[stage];
            constexpr float Expand = 0.006f;
            const float minX = static_cast<float>(blockBreaking_.x) - 0.5f - Expand;
            const float maxX = static_cast<float>(blockBreaking_.x) + 0.5f + Expand;
            const float minY = static_cast<float>(blockBreaking_.y) - Expand;
            const float maxY = static_cast<float>(blockBreaking_.y + 1) + Expand;
            const float minZ = static_cast<float>(blockBreaking_.z) - 0.5f - Expand;
            const float maxZ = static_cast<float>(blockBreaking_.z) + 0.5f + Expand;
            appendQuad({Vec3{minX, maxY, minZ}, Vec3{minX, maxY, maxZ}, Vec3{maxX, maxY, maxZ}, Vec3{maxX, maxY, minZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f);
            appendQuad({Vec3{minX, minY, maxZ}, Vec3{minX, minY, minZ}, Vec3{maxX, minY, minZ}, Vec3{maxX, minY, maxZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f);
            appendQuad({Vec3{minX, minY, maxZ}, Vec3{minX, maxY, maxZ}, Vec3{minX, maxY, minZ}, Vec3{minX, minY, minZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f);
            appendQuad({Vec3{maxX, minY, minZ}, Vec3{maxX, maxY, minZ}, Vec3{maxX, maxY, maxZ}, Vec3{maxX, minY, maxZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f);
            appendQuad({Vec3{minX, minY, minZ}, Vec3{minX, maxY, minZ}, Vec3{maxX, maxY, minZ}, Vec3{maxX, minY, minZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f);
            appendQuad({Vec3{maxX, minY, maxZ}, Vec3{maxX, maxY, maxZ}, Vec3{minX, maxY, maxZ}, Vec3{minX, minY, maxZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f);
        }

        const Vec3 cameraRight = camera.right();
        const Vec3 right{-cameraRight.x, -cameraRight.y, -cameraRight.z};
        const Vec3 forward = camera.forward();
        const Vec3 terrainForward{forward.x, -forward.y, forward.z};
        const Vec3 up = normalize(cross(terrainForward, right));
        const size_t remainingQuads = MaxBlockBreakParticles - std::min(MaxBlockBreakParticles, vertices.size() / 4u);
        const size_t particleCount = std::min(blockBreakParticles_.size(), remainingQuads);
        for (size_t i = 0; i < particleCount; ++i)
        {
            const BlockBreakParticle& particle = blockBreakParticles_[i];
            const float half = particle.size * 0.5f;
            const Vec3 rightOffset{right.x * half, right.y * half, right.z * half};
            const Vec3 upOffset{up.x * half, up.y * half, up.z * half};
            const float ao = std::clamp(1.0f - particle.age / particle.lifetime * 0.25f, 0.75f, 1.0f);
            appendQuad({
                Vec3{particle.position.x - rightOffset.x - upOffset.x, particle.position.y - rightOffset.y - upOffset.y, particle.position.z - rightOffset.z - upOffset.z},
                Vec3{particle.position.x - rightOffset.x + upOffset.x, particle.position.y - rightOffset.y + upOffset.y, particle.position.z - rightOffset.z + upOffset.z},
                Vec3{particle.position.x + rightOffset.x + upOffset.x, particle.position.y + rightOffset.y + upOffset.y, particle.position.z + rightOffset.z + upOffset.z},
                Vec3{particle.position.x + rightOffset.x - upOffset.x, particle.position.y + rightOffset.y - upOffset.y, particle.position.z + rightOffset.z - upOffset.z}},
                particle.u0,
                particle.v0,
                particle.u1,
                particle.v1,
                ao,
                particle.textureLayer,
                1.0f);
        }

        const VkDeviceSize vertexBytes = sizeof(TerrainVertex) * vertices.size();
        const VkDeviceSize indexBytes = sizeof(uint32_t) * indices.size();
        void* vertexData = nullptr;
        vkMapMemory(device_, particleVertexMemory_, 0, vertexBytes, 0, &vertexData);
        std::memcpy(vertexData, vertices.data(), static_cast<size_t>(vertexBytes));
        vkUnmapMemory(device_, particleVertexMemory_);

        void* indexData = nullptr;
        vkMapMemory(device_, particleIndexMemory_, 0, indexBytes, 0, &indexData);
        std::memcpy(indexData, indices.data(), static_cast<size_t>(indexBytes));
        vkUnmapMemory(device_, particleIndexMemory_);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapchainExtent_.width);
        viewport.height = static_cast<float>(swapchainExtent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapchainExtent_;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        const float aspect = static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);
        const Mat4 projection = perspective(FieldOfViewRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);

        TerrainPush push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = static_cast<float>(glfwGetTime());

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline_);
        vkCmdPushConstants(commandBuffer, particlePipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipelineLayout_, 0, 1, &terrainTextureArray_.descriptorSet, 0, nullptr);
        const VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &particleVertexBuffer_, &vertexOffset);
        vkCmdBindIndexBuffer(commandBuffer, particleIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    }

    void Renderer::drawTerrainMeshBound(VkCommandBuffer commandBuffer, const TerrainMesh& mesh) const
    {
        if (mesh.indexCount == 0 || mesh.vertexDescriptorSet == VK_NULL_HANDLE)
        {
            return;
        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout_, 1, 1, &mesh.vertexDescriptorSet, 0, nullptr);
        vkCmdDraw(commandBuffer, mesh.indexCount, 1, 0, 0);
    }

    void Renderer::drawTerrainMesh(VkCommandBuffer commandBuffer, const TerrainMesh& mesh, const Texture& texture) const
    {
        if (mesh.indexCount == 0)
        {
            return;
        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout_, 0, 1, &texture.descriptorSet, 0, nullptr);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
    }

    void Renderer::drawBlockSelection(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition)
    {
        if (!hasSelectedBlock_ || selectionPipeline_ == VK_NULL_HANDLE || selectionLineVertexBuffer_ == VK_NULL_HANDLE)
        {
            return;
        }

        constexpr float Expand = 0.003f;
        const float minX = static_cast<float>(selectedBlockX_) - 0.5f - Expand;
        const float maxX = static_cast<float>(selectedBlockX_) + 0.5f + Expand;
        const float minY = static_cast<float>(selectedBlockY_) - Expand;
        const float maxY = static_cast<float>(selectedBlockY_ + 1) + Expand;
        const float minZ = static_cast<float>(selectedBlockZ_) - 0.5f - Expand;
        const float maxZ = static_cast<float>(selectedBlockZ_) + 0.5f + Expand;

        const std::array<LineVertex, 24> vertices = {
            LineVertex{minX, minY, minZ}, LineVertex{maxX, minY, minZ},
            LineVertex{maxX, minY, minZ}, LineVertex{maxX, minY, maxZ},
            LineVertex{maxX, minY, maxZ}, LineVertex{minX, minY, maxZ},
            LineVertex{minX, minY, maxZ}, LineVertex{minX, minY, minZ},

            LineVertex{minX, maxY, minZ}, LineVertex{maxX, maxY, minZ},
            LineVertex{maxX, maxY, minZ}, LineVertex{maxX, maxY, maxZ},
            LineVertex{maxX, maxY, maxZ}, LineVertex{minX, maxY, maxZ},
            LineVertex{minX, maxY, maxZ}, LineVertex{minX, maxY, minZ},

            LineVertex{minX, minY, minZ}, LineVertex{minX, maxY, minZ},
            LineVertex{maxX, minY, minZ}, LineVertex{maxX, maxY, minZ},
            LineVertex{maxX, minY, maxZ}, LineVertex{maxX, maxY, maxZ},
            LineVertex{minX, minY, maxZ}, LineVertex{minX, maxY, maxZ}
        };

        void* data = nullptr;
        vkMapMemory(device_, selectionLineVertexMemory_, 0, sizeof(LineVertex) * vertices.size(), 0, &data);
        std::memcpy(data, vertices.data(), sizeof(LineVertex) * vertices.size());
        vkUnmapMemory(device_, selectionLineVertexMemory_);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapchainExtent_.width);
        viewport.height = static_cast<float>(swapchainExtent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapchainExtent_;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        const float aspect = static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);
        const Mat4 projection = perspective(FieldOfViewRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);

        TerrainPush push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = 0.0f;

        const VkDeviceSize offset = 0;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, selectionPipeline_);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &selectionLineVertexBuffer_, &offset);
        vkCmdPushConstants(commandBuffer, selectionPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    }

    void Renderer::drawSprite(VkCommandBuffer commandBuffer, const Texture& texture, SpriteRect rect, UvRect uv, Color color) const
    {
        drawSpriteDescriptor(commandBuffer, texture.descriptorSet, rect, uv, color);
    }

    void Renderer::ensureClimateOverlayTexture(int mode)
    {
        if (mode == 1 && !climateTemperatureOverlayReady_)
        {
            const std::vector<unsigned char> pixels = buildClimateOverlayPixels(mode);
            climateTemperatureOverlay_ = createTextureFromRgba(pixels.data(), ClimateOverlaySize, ClimateOverlaySize, VK_FORMAT_R8G8B8A8_SRGB);
            climateTemperatureOverlayReady_ = true;
        }
        else if (mode == 2 && !climatePrecipitationOverlayReady_)
        {
            const std::vector<unsigned char> pixels = buildClimateOverlayPixels(mode);
            climatePrecipitationOverlay_ = createTextureFromRgba(pixels.data(), ClimateOverlaySize, ClimateOverlaySize, VK_FORMAT_R8G8B8A8_SRGB);
            climatePrecipitationOverlayReady_ = true;
        }
    }

    std::vector<unsigned char> Renderer::buildClimateOverlayPixels(int mode) const
    {
        std::vector<unsigned char> pixels(static_cast<size_t>(ClimateOverlaySize) * static_cast<size_t>(ClimateOverlaySize) * 4u, 255u);
        auto writePixel = [&](int x, int y, float r, float g, float b)
        {
            const size_t index = (static_cast<size_t>(y) * ClimateOverlaySize + static_cast<size_t>(x)) * 4u;
            pixels[index + 0u] = static_cast<unsigned char>(std::clamp(std::lround(r * 255.0f), 0l, 255l));
            pixels[index + 1u] = static_cast<unsigned char>(std::clamp(std::lround(g * 255.0f), 0l, 255l));
            pixels[index + 2u] = static_cast<unsigned char>(std::clamp(std::lround(b * 255.0f), 0l, 255l));
        };

        if (mode == 1)
        {
            const std::vector<float> noise = buildTileableClimateNoise(
                temperatureNoiseFeatureScale_,
                temperatureNoiseSimplexScale_,
                temperatureNoiseOctaveCount_,
                temperatureNoiseLacunarity_,
                temperatureNoiseGain_,
                temperatureSeed());
            for (int y = 0; y < ClimateOverlaySize; ++y)
            {
                const int worldZ = (y * WorldSizeBlocks) / ClimateOverlaySize;
                for (int x = 0; x < ClimateOverlaySize; ++x)
                {
                    const int worldX = (x * WorldSizeBlocks) / ClimateOverlaySize;
                    const size_t index = static_cast<size_t>(y) * ClimateOverlaySize + static_cast<size_t>(x);
                    const float temperature = temperatureAtWrapped(worldZ, index < noise.size() ? noise[index] : 0.0f);
                    writePixel(x, y, temperature, 0.0f, 1.0f - temperature);
                }
            }
            return pixels;
        }

        const std::vector<float> noise = buildTileableClimateNoise(
            precipitationNoiseFeatureScale_,
            precipitationNoiseSimplexScale_,
            precipitationNoiseOctaveCount_,
            precipitationNoiseLacunarity_,
            precipitationNoiseGain_,
            precipitationSeed());
        if (noise.empty())
        {
            return pixels;
        }

        for (int y = 0; y < ClimateOverlaySize; ++y)
        {
            for (int x = 0; x < ClimateOverlaySize; ++x)
            {
                const size_t index = static_cast<size_t>(y) * ClimateOverlaySize + static_cast<size_t>(x);
                const float precipitation = precipitationAtNoise(noise[index]);
                const float gray = 0.45f;
                writePixel(x, y, gray * (1.0f - precipitation), gray * (1.0f - precipitation) + 0.35f * precipitation, gray * (1.0f - precipitation) + precipitation);
            }
        }

        return pixels;
    }

    std::vector<float> Renderer::buildTileableClimateNoise(float featureScale, float simplexScale, int octaveCount, float lacunarity, float gain, int seed) const
    {
        auto generator = terrainNoiseGenerator(simplexScale, octaveCount, lacunarity, gain);
        if (!generator)
        {
            return {};
        }

        constexpr float TwoPi = 6.28318530718f;
        const float angleScale = TwoPi / static_cast<float>(TerrainTilePeriod);
        const float radius = static_cast<float>(TerrainTilePeriod) / (TwoPi * featureScale);
        const size_t sampleCount = static_cast<size_t>(ClimateOverlaySize) * static_cast<size_t>(ClimateOverlaySize);
        std::vector<float> xPositions(sampleCount);
        std::vector<float> yPositions(sampleCount);
        std::vector<float> zPositions(sampleCount);
        std::vector<float> wPositions(sampleCount);
        std::vector<float> noise(sampleCount);

        for (int y = 0; y < ClimateOverlaySize; ++y)
        {
            const int worldZ = (y * WorldSizeBlocks) / ClimateOverlaySize;
            const float zAngle = static_cast<float>(positiveModulo(worldZ, TerrainTilePeriod)) * angleScale;
            const float zCos = std::cos(zAngle) * radius;
            const float zSin = std::sin(zAngle) * radius;
            for (int x = 0; x < ClimateOverlaySize; ++x)
            {
                const int worldX = (x * WorldSizeBlocks) / ClimateOverlaySize;
                const float xAngle = static_cast<float>(positiveModulo(worldX, TerrainTilePeriod)) * angleScale;
                const size_t index = static_cast<size_t>(y) * ClimateOverlaySize + static_cast<size_t>(x);
                xPositions[index] = std::cos(xAngle) * radius;
                yPositions[index] = zCos;
                zPositions[index] = std::sin(xAngle) * radius;
                wPositions[index] = zSin;
            }
        }

        generator->GenPositionArray4D(
            noise.data(),
            static_cast<int>(noise.size()),
            xPositions.data(),
            yPositions.data(),
            zPositions.data(),
            wPositions.data(),
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            seed);

        return noise;
    }

    std::array<float, ChunkColumnCount> Renderer::buildChunkTileableClimateNoise(
        int chunkX,
        int chunkZ,
        float featureScale,
        float simplexScale,
        int octaveCount,
        float lacunarity,
        float gain,
        int seed) const
    {
        return world::TerrainBuilder(terrainBuilderConfig()).buildChunkTileableClimateNoise(
            chunkX,
            chunkZ,
            featureScale,
            simplexScale,
            octaveCount,
            lacunarity,
            gain,
            seed);
    }

    float Renderer::sampleTileableClimateNoise(
        int wrappedX,
        int wrappedZ,
        float featureScale,
        float simplexScale,
        int octaveCount,
        float lacunarity,
        float gain,
        int seed) const
    {
        auto generator = terrainNoiseGenerator(simplexScale, octaveCount, lacunarity, gain);
        if (!generator)
        {
            return 0.0f;
        }

        constexpr float TwoPi = 6.28318530718f;
        const float angleScale = TwoPi / static_cast<float>(TerrainTilePeriod);
        const float radius = static_cast<float>(TerrainTilePeriod) / (TwoPi * featureScale);
        const float xAngle = static_cast<float>(positiveModulo(wrappedX, TerrainTilePeriod)) * angleScale;
        const float zAngle = static_cast<float>(positiveModulo(wrappedZ, TerrainTilePeriod)) * angleScale;
        float xPosition = std::cos(xAngle) * radius;
        float yPosition = std::cos(zAngle) * radius;
        float zPosition = std::sin(xAngle) * radius;
        float wPosition = std::sin(zAngle) * radius;
        float noise = 0.0f;

        generator->GenPositionArray4D(
            &noise,
            1,
            &xPosition,
            &yPosition,
            &zPosition,
            &wPosition,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            seed);

        return noise;
    }

    void Renderer::populateChunkClimate(ChunkData& chunk) const
    {
        world::TerrainBuilder(terrainBuilderConfig()).populateChunkClimate(chunk);
    }

    float Renderer::temperatureAtWrapped(int wrappedZ, float noise) const
    {
        return world::TerrainBuilder(terrainBuilderConfig()).temperatureAtWrapped(wrappedZ, noise);
    }

    float Renderer::precipitationAtNoise(float noise) const
    {
        return world::TerrainBuilder(terrainBuilderConfig()).precipitationAtNoise(noise);
    }

    void Renderer::drawClimateOverlay(VkCommandBuffer commandBuffer, int mode) const
    {
        const Texture* texture = nullptr;
        if (mode == 1 && climateTemperatureOverlayReady_)
        {
            texture = &climateTemperatureOverlay_;
        }
        else if (mode == 2 && climatePrecipitationOverlayReady_)
        {
            texture = &climatePrecipitationOverlay_;
        }
        if (texture == nullptr)
        {
            return;
        }

        const float aspect = static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);
        SpriteRect rect{};
        rect.halfHeight = 0.82f;
        rect.halfWidth = std::min(0.92f, rect.halfHeight / std::max(aspect, 0.001f));
        drawSprite(commandBuffer, *texture, rect, {}, {1.0f, 1.0f, 1.0f, 1.0f});
    }

    void Renderer::drawMenuOverlay(VkCommandBuffer commandBuffer, int menuOverlayMode)
    {
        if (menuOverlayMode == 0)
        {
            return;
        }

        auto rectFromPixels = [&](float centerX, float centerY, float width, float height)
        {
            SpriteRect rect{};
            rect.centerX = centerX / static_cast<float>(swapchainExtent_.width) * 2.0f - 1.0f;
            rect.centerY = centerY / static_cast<float>(swapchainExtent_.height) * 2.0f - 1.0f;
            rect.halfWidth = width / static_cast<float>(swapchainExtent_.width);
            rect.halfHeight = height / static_cast<float>(swapchainExtent_.height);
            return rect;
        };

        const float width = static_cast<float>(swapchainExtent_.width);
        const float height = static_cast<float>(swapchainExtent_.height);
        const SpriteRect fullScreenRect = rectFromPixels(width * 0.5f, height * 0.5f, width, height);
        if (menuOverlayMode == 1 || menuOverlayMode == 3)
        {
            const UvRect tiledUv{0.0f, height / LobbyBackgroundTilePixels, width / LobbyBackgroundTilePixels, -height / LobbyBackgroundTilePixels};
            drawSprite(commandBuffer, lobbyBackground_, fullScreenRect, tiledUv, {1.0f, 1.0f, 1.0f, 1.0f});
        }

        const SpriteRect dimRect = rectFromPixels(width * 0.5f, height * 0.5f, width, height);
        drawSprite(commandBuffer, white_, dimRect, {}, {0.02f, 0.03f, 0.04f, 0.62f});

        if ((menuOverlayMode == 1 || menuOverlayMode == 3) && lobbyTitle_.width > 0 && lobbyTitle_.height > 0)
        {
            const float titleWidth = std::min(width * 0.58f, static_cast<float>(lobbyTitle_.width) * 2.0f);
            const float titleHeight = titleWidth * static_cast<float>(lobbyTitle_.height) / static_cast<float>(lobbyTitle_.width);
            const SpriteRect titleRect = rectFromPixels(width * 0.5f, height * 0.22f, titleWidth, titleHeight);
            drawSprite(commandBuffer, lobbyTitle_, titleRect, {0.0f, 1.0f, 1.0f, -1.0f});
        }

        if (menuOverlayMode == 3)
        {
            const std::array<float, 4> buttonYs = {height * 0.34f, height * 0.45f, height * 0.56f, height * 0.72f};
            for (size_t i = 0; i < buttonYs.size(); ++i)
            {
                const SpriteRect button = rectFromPixels(width * 0.5f, buttonYs[i], MenuButtonWidthPixels, MenuButtonHeightPixels);
                const Color color = i == buttonYs.size() - 1u ? Color{0.08f, 0.11f, 0.14f, 0.92f} : Color{0.12f, 0.18f, 0.22f, 0.92f};
                drawSprite(commandBuffer, white_, button, {}, color);
            }
        }
        else
        {
            const float firstButtonY = menuOverlayMode == 1 ? height * 0.45f : height * 0.46f;
            const float secondButtonY = menuOverlayMode == 1 ? height * 0.56f : height * 0.57f;
            const SpriteRect firstButton = rectFromPixels(width * 0.5f, firstButtonY, MenuButtonWidthPixels, MenuButtonHeightPixels);
            const SpriteRect secondButton = rectFromPixels(width * 0.5f, secondButtonY, MenuButtonWidthPixels, MenuButtonHeightPixels);
            drawSprite(commandBuffer, white_, firstButton, {}, {0.12f, 0.18f, 0.22f, 0.92f});
            drawSprite(commandBuffer, white_, secondButton, {}, {0.08f, 0.11f, 0.14f, 0.92f});
        }

        buildMenuTextBatch(menuOverlayMode);
        debugTextBufferDirty_ = true;
        drawTextBatch(commandBuffer, menuTextBatch_);
    }

    void Renderer::drawSpriteDescriptor(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, SpriteRect rect, UvRect uv, Color color) const
    {
        SpritePush push{};
        push.data[0] = rect.centerX;
        push.data[1] = rect.centerY;
        push.data[2] = rect.halfWidth;
        push.data[3] = rect.halfHeight;
        push.data[4] = uv.x;
        push.data[5] = uv.y;
        push.data[6] = uv.width;
        push.data[7] = uv.height;
        push.data[8] = color.r;
        push.data[9] = color.g;
        push.data[10] = color.b;
        push.data[11] = color.a;

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &textVertexBuffer_, &offset);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SpritePush), &push);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    }

    std::string_view Renderer::resolutionText()
    {
        if (lastResolutionExtent_.width != swapchainExtent_.width || lastResolutionExtent_.height != swapchainExtent_.height)
        {
            lastResolutionExtent_ = swapchainExtent_;
            resolutionText_ = "RESOLUTION: " + std::to_string(swapchainExtent_.width) + " x " + std::to_string(swapchainExtent_.height);
            debugTextBatchDirty_ = true;
        }

        return resolutionText_;
    }

    void Renderer::updateDebugTextBatch(std::string_view fpsText)
    {
        resolutionText();

        if (cachedFpsText_ != fpsText)
        {
            cachedFpsText_ = fpsText;
            debugTextBatchDirty_ = true;
        }

        if (!debugTextBatchDirty_)
        {
            return;
        }

        debugTextBatch_.outline.clear();
        debugTextBatch_.fill.clear();
        debugTextBatch_.outline.reserve(8192);
        debugTextBatch_.fill.reserve(2048);

        const float rightX = static_cast<float>(swapchainExtent_.width) - 12.0f;
        addText(debugTextBatch_, cachedFpsText_, 12.0f, 24.0f, false);
        addText(debugTextBatch_, VersionText, rightX, 24.0f, true);
        addText(debugTextBatch_, cpuText_, rightX, 46.0f, true);
        addText(debugTextBatch_, gpuText_, rightX, 68.0f, true);
        addText(debugTextBatch_, vulkanText_, rightX, 90.0f, true);
        addText(debugTextBatch_, driverText_, rightX, 112.0f, true);
        addText(debugTextBatch_, resolutionText_, rightX, 134.0f, true);
        addText(debugTextBatch_, cpuFrameText_, rightX, 156.0f, true);
        addText(debugTextBatch_, gpuFrameText_, rightX, 178.0f, true);
        addText(debugTextBatch_, vramText_, rightX, 200.0f, true);
        addText(debugTextBatch_, terrainDrawText_, rightX, 222.0f, true);
        addText(debugTextBatch_, terrainFaceText_, rightX, 244.0f, true);
        addText(debugTextBatch_, terrainVertexText_, rightX, 266.0f, true);

        debugTextBatchDirty_ = false;
        debugTextBufferDirty_ = true;
    }

    void Renderer::buildMenuTextBatch(int menuOverlayMode)
    {
        if (cachedMenuOverlayMode_ == menuOverlayMode &&
            cachedMenuExtent_.width == swapchainExtent_.width &&
            cachedMenuExtent_.height == swapchainExtent_.height)
        {
            return;
        }

        cachedMenuOverlayMode_ = menuOverlayMode;
        cachedMenuExtent_ = swapchainExtent_;
        menuTextBatch_.outline.clear();
        menuTextBatch_.fill.clear();
        menuTextBatch_.outline.reserve(1024);
        menuTextBatch_.fill.reserve(512);

        const float width = static_cast<float>(swapchainExtent_.width);
        const float height = static_cast<float>(swapchainExtent_.height);
        const float centerX = width * 0.5f;

        if (menuOverlayMode == 1)
        {
            addText(menuTextBatch_, "START", centerX - measureText("START") * 0.5f, height * 0.45f + 6.0f, false);
            addText(menuTextBatch_, "EXIT", centerX - measureText("EXIT") * 0.5f, height * 0.56f + 6.0f, false);
        }
        else if (menuOverlayMode == 3)
        {
            addText(menuTextBatch_, "SELECT WORLD", centerX - measureText("SELECT WORLD") * 0.5f, height * 0.27f, false);
            addText(menuTextBatch_, "WORLD 1", centerX - measureText("WORLD 1") * 0.5f, height * 0.34f + 6.0f, false);
            addText(menuTextBatch_, "WORLD 2", centerX - measureText("WORLD 2") * 0.5f, height * 0.45f + 6.0f, false);
            addText(menuTextBatch_, "WORLD 3", centerX - measureText("WORLD 3") * 0.5f, height * 0.56f + 6.0f, false);
            addText(menuTextBatch_, "BACK", centerX - measureText("BACK") * 0.5f, height * 0.72f + 6.0f, false);
        }
        else if (menuOverlayMode == 2)
        {
            addText(menuTextBatch_, "RESUME", centerX - measureText("RESUME") * 0.5f, height * 0.46f + 6.0f, false);
            addText(menuTextBatch_, "EXIT", centerX - measureText("EXIT") * 0.5f, height * 0.57f + 6.0f, false);
        }
    }

    void Renderer::updatePerformanceText(double cpuFrameMs)
    {
        const auto now = std::chrono::steady_clock::now();
        if (performanceSampleStart_ == std::chrono::steady_clock::time_point{})
        {
            performanceSampleStart_ = now;
        }

        accumulatedCpuFrameMs_ += cpuFrameMs;
        accumulatedGpuFrameMs_ += lastGpuFrameMs_;
        ++performanceSampleCount_;

        const std::chrono::duration<double> elapsed = now - performanceSampleStart_;
        if (elapsed.count() < PerformanceSampleSeconds)
        {
            return;
        }

        const double sampleCount = static_cast<double>(std::max<uint32_t>(performanceSampleCount_, 1));
        {
            std::ostringstream text;
            text << "CPU: " << std::fixed << std::setprecision(3) << (accumulatedCpuFrameMs_ / sampleCount) << "MS";
            cpuFrameText_ = text.str();
        }
        if (timestampSupported_)
        {
            std::ostringstream text;
            text << "GPU: " << std::fixed << std::setprecision(3) << (accumulatedGpuFrameMs_ / sampleCount) << "MS";
            gpuFrameText_ = text.str();
        }
        updateVramText();

        accumulatedCpuFrameMs_ = 0.0;
        accumulatedGpuFrameMs_ = 0.0;
        performanceSampleCount_ = 0;
        performanceSampleStart_ = now;
        debugTextBatchDirty_ = true;
    }

    void Renderer::addText(TextBatch& batch, std::string_view text, float x, float y, bool alignRight) const
    {
        constexpr float LineHeight = 22.0f;

        float lineY = y;
        size_t lineStart = 0;
        while (lineStart <= text.size())
        {
            const size_t lineEnd = text.find('\n', lineStart);
            const std::string_view line = lineEnd == std::string_view::npos
                ? text.substr(lineStart)
                : text.substr(lineStart, lineEnd - lineStart);

            addTextPass(batch.outline, line, x, lineY, alignRight, -1.0f, 0.0f);
            addTextPass(batch.outline, line, x, lineY, alignRight, 1.0f, 0.0f);
            addTextPass(batch.outline, line, x, lineY, alignRight, 0.0f, -1.0f);
            addTextPass(batch.outline, line, x, lineY, alignRight, 0.0f, 1.0f);
            addTextPass(batch.fill, line, x, lineY, alignRight, 0.0f, 0.0f);

            if (lineEnd == std::string_view::npos)
            {
                break;
            }

            lineStart = lineEnd + 1;
            lineY += LineHeight;
        }
    }

    void Renderer::addTextPass(std::vector<TextVertex>& vertices, std::string_view text, float x, float y, bool alignRight, float offsetX, float offsetY) const
    {
        float cursorX = alignRight ? x - measureText(text) : x;
        const float cursorY = y;

        for (char character : text)
        {
            if (character < 32 || character > 126)
            {
                continue;
            }

            Glyph glyph = makeGlyph(character, cursorX + offsetX, cursorY + offsetY);
            cursorX += glyph.advance;
            appendGlyphQuad(vertices, glyph);
        }
    }

    void Renderer::appendGlyphQuad(std::vector<TextVertex>& vertices, const Glyph& glyph) const
    {
        const float left = glyph.rect.centerX - glyph.rect.halfWidth;
        const float right = glyph.rect.centerX + glyph.rect.halfWidth;
        const float top = glyph.rect.centerY - glyph.rect.halfHeight;
        const float bottom = glyph.rect.centerY + glyph.rect.halfHeight;
        const float u0 = glyph.uv.x;
        const float v0 = glyph.uv.y;
        const float u1 = glyph.uv.x + glyph.uv.width;
        const float v1 = glyph.uv.y + glyph.uv.height;

        vertices.push_back({left, top, u0, v0});
        vertices.push_back({right, top, u1, v0});
        vertices.push_back({right, bottom, u1, v1});
        vertices.push_back({left, top, u0, v0});
        vertices.push_back({right, bottom, u1, v1});
        vertices.push_back({left, bottom, u0, v1});
    }

    void Renderer::drawTextBatch(VkCommandBuffer commandBuffer, const TextBatch& batch)
    {
        const size_t totalVertices = batch.outline.size() + batch.fill.size();
        if (totalVertices == 0)
        {
            return;
        }
        if (totalVertices > MaxTextVertices)
        {
            return;
        }

        const VkDeviceSize outlineSize = sizeof(TextVertex) * batch.outline.size();
        const VkDeviceSize fillSize = sizeof(TextVertex) * batch.fill.size();
        const VkDeviceSize fillOffset = outlineSize;

        void* data = nullptr;
        vkMapMemory(device_, textVertexMemory_, 0, outlineSize + fillSize, 0, &data);
        if (outlineSize > 0)
        {
            std::memcpy(data, batch.outline.data(), static_cast<size_t>(outlineSize));
        }
        if (fillSize > 0)
        {
            std::memcpy(static_cast<char*>(data) + fillOffset, batch.fill.data(), static_cast<size_t>(fillSize));
        }
        vkUnmapMemory(device_, textVertexMemory_);
        debugTextBufferDirty_ = false;

        drawTextVertices(commandBuffer, batch.outline, {0.0f, 0.0f, 0.0f, 1.0f}, 0);
        drawTextVertices(commandBuffer, batch.fill, {1.0f, 1.0f, 1.0f, 1.0f}, fillOffset);
    }

    void Renderer::drawTextVertices(VkCommandBuffer commandBuffer, const std::vector<TextVertex>& vertices, Color color, VkDeviceSize bufferOffset) const
    {
        if (vertices.empty())
        {
            return;
        }

        TextPush push{};
        push.data[0] = 0.0f;
        push.data[1] = 0.0f;
        push.data[2] = -1.0f;
        push.data[3] = 1.0f;
        push.data[4] = 0.0f;
        push.data[5] = 0.0f;
        push.data[6] = 1.0f;
        push.data[7] = 1.0f;
        push.data[8] = color.r;
        push.data[9] = color.g;
        push.data[10] = color.b;
        push.data[11] = color.a;

        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &textVertexBuffer_, &bufferOffset);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &font_.descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TextPush), &push);
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    }

    Renderer::Glyph Renderer::makeGlyph(char character, float x, float y) const
    {
        const FontCharacter& fontCharacter = fontCharacters_[static_cast<size_t>(character - 32)];

        const float x0 = x + fontCharacter.xOffset;
        const float y0 = y + fontCharacter.yOffset;
        const float x1 = x0 + static_cast<float>(fontCharacter.x1 - fontCharacter.x0);
        const float y1 = y0 + static_cast<float>(fontCharacter.y1 - fontCharacter.y0);

        Glyph glyph{};
        glyph.rect.centerX = ((x0 + x1) * 0.5f / static_cast<float>(swapchainExtent_.width)) * 2.0f - 1.0f;
        glyph.rect.centerY = ((y0 + y1) * 0.5f / static_cast<float>(swapchainExtent_.height)) * 2.0f - 1.0f;
        glyph.rect.halfWidth = (x1 - x0) / static_cast<float>(swapchainExtent_.width);
        glyph.rect.halfHeight = (y1 - y0) / static_cast<float>(swapchainExtent_.height);
        glyph.uv.x = static_cast<float>(fontCharacter.x0) / static_cast<float>(FontAtlasSize);
        glyph.uv.y = static_cast<float>(fontCharacter.y0) / static_cast<float>(FontAtlasSize);
        glyph.uv.width = static_cast<float>(fontCharacter.x1 - fontCharacter.x0) / static_cast<float>(FontAtlasSize);
        glyph.uv.height = static_cast<float>(fontCharacter.y1 - fontCharacter.y0) / static_cast<float>(FontAtlasSize);
        glyph.advance = fontCharacter.advance;
        return glyph;
    }

    float Renderer::measureText(std::string_view text) const
    {
        float width = 0.0f;
        for (char character : text)
        {
            if (character < 32 || character > 126)
            {
                continue;
            }

            width += fontCharacters_[static_cast<size_t>(character - 32)].advance;
        }
        return width;
    }

    bool Renderer::projectSkyDirection(const Camera& camera, float aspect, const std::array<float, 3>& direction, SpriteRect& rect) const
    {
        Vec3 dir = normalize({direction[0], direction[1], direction[2]});
        const float x = -dot(dir, camera.right());
        const float y = dot(dir, camera.up());
        const float z = dot(dir, camera.forward());

        if (z <= 0.01f)
        {
            return false;
        }

        const float tanHalfFov = std::tan(FieldOfViewRadians * 0.5f);
        rect.centerX = (x / z) / (tanHalfFov * aspect);
        rect.centerY = (y / z) / tanHalfFov;
        rect.halfWidth = 0.04f;
        rect.halfHeight = 0.04f * aspect;
        return rect.centerX > -1.2f && rect.centerX < 1.2f && rect.centerY > -1.2f && rect.centerY < 1.2f;
    }

    std::string Renderer::readCpuName() const
    {
#ifdef _WIN32
        HKEY key = nullptr;
        const LONG openResult = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key);
        if (openResult != ERROR_SUCCESS)
        {
            return "Unknown";
        }

        char name[256]{};
        DWORD nameSize = sizeof(name);
        const LONG queryResult = RegQueryValueExA(key, "ProcessorNameString", nullptr, nullptr, reinterpret_cast<LPBYTE>(name), &nameSize);
        RegCloseKey(key);

        if (queryResult != ERROR_SUCCESS || name[0] == '\0')
        {
            return "Unknown";
        }

        return name;
#else
        return "Unknown";
#endif
    }

    std::string Renderer::formatVersion(uint32_t version) const
    {
        return std::to_string(VK_VERSION_MAJOR(version)) + "." +
            std::to_string(VK_VERSION_MINOR(version)) + "." +
            std::to_string(VK_VERSION_PATCH(version));
    }

    void Renderer::updateTerrainDebugText()
    {
        if (terrainDebugInitialized_)
        {
            return;
        }

        terrainDebugInitialized_ = true;
        terrainDrawText_ = "DRAWS: 2";
        terrainFaceText_ = "FACES: 0";
        terrainVertexText_ = "QUADS: 9";
        debugTextBatchDirty_ = true;
    }

    void Renderer::updateVramText()
    {
        if (localMemoryHeapIndex_ == UINT32_MAX)
        {
            vramText_ = "VRAM: N/A";
            debugTextBatchDirty_ = true;
            return;
        }

        if (memoryBudgetSupported_)
        {
            VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
            budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

            VkPhysicalDeviceMemoryProperties2 properties{};
            properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
            properties.pNext = &budget;

            const auto getMemoryProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(
                vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceMemoryProperties2"));
            if (getMemoryProperties2 != nullptr)
            {
                getMemoryProperties2(physicalDevice_, &properties);
            }
            else
            {
                const auto getMemoryProperties2Khr = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2KHR>(
                    vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceMemoryProperties2KHR"));
                if (getMemoryProperties2Khr == nullptr)
                {
                    vramText_ = "VRAM: " + std::to_string(static_cast<uint64_t>(localMemoryHeapSize_ / (1024u * 1024u))) + "MB";
                    debugTextBatchDirty_ = true;
                    return;
                }
                getMemoryProperties2Khr(physicalDevice_, reinterpret_cast<VkPhysicalDeviceMemoryProperties2KHR*>(&properties));
            }

            const uint64_t usedMb = static_cast<uint64_t>(budget.heapUsage[localMemoryHeapIndex_] / (1024u * 1024u));
            const uint64_t budgetMb = static_cast<uint64_t>(budget.heapBudget[localMemoryHeapIndex_] / (1024u * 1024u));
            vramText_ = "VRAM: " + std::to_string(usedMb) + " / " + std::to_string(budgetMb) + "MB";
            debugTextBatchDirty_ = true;
            return;
        }

        vramText_ = "VRAM: " + std::to_string(static_cast<uint64_t>(localMemoryHeapSize_ / (1024u * 1024u))) + "MB";
        debugTextBatchDirty_ = true;
    }
}
