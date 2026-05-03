#include "renderer/Renderer.h"

#include "camera/Camera.h"

#include <FastNoise/FastNoise.h>
#include <stb_image.h>
#include <stb_image_write.h>
#include <stb_truetype.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstddef>
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
        constexpr size_t MaxTextVertices = 16384;
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeY = 512;
        constexpr int ChunkSizeZ = 16;
        constexpr int SubchunkSize = 16;
        constexpr int SubchunksPerChunk = ChunkSizeY / SubchunkSize;
        constexpr int MeshingBorder = 1;
        constexpr int MeshingSizeX = ChunkSizeX + MeshingBorder * 2;
        constexpr int MeshingSizeZ = ChunkSizeZ + MeshingBorder * 2;
        constexpr size_t MeshingBlockCount = MeshingSizeX * ChunkSizeY * MeshingSizeZ;
        constexpr int LoadGridUnitChunks = 16;
        constexpr int CenterGroupChunks = 2;
        constexpr int DefaultLoadGridScale = 1;
        constexpr int DefaultTerrainWorkerCount = 4;
        constexpr int DefaultMaxTerrainUploadChunksPerFrame = 8;
        constexpr int DefaultMaxTerrainUnloadChunksPerFrame = 16;
        constexpr int TerrainMinHeight = 120;
        constexpr int TerrainMaxHeight = 140;
        constexpr int TerrainTilePeriod = 65536;
        constexpr int TerrainNoiseSeed = 1337;
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
        constexpr float TerrainNearPlane = 0.1f;
        constexpr float TerrainFarPlane = 4000.0f;
        constexpr float HeightLutNoiseMin = -2.0f;
        constexpr float HeightLutNoiseMax = 2.0f;
        constexpr uint32_t HeightLutVersion = 1;
        constexpr uint32_t HeightLutCount = 1024;
        constexpr double PerformanceSampleSeconds = 0.5;
        constexpr uint16_t BlockAir = 0;
        constexpr uint16_t BlockRock = 1;
        constexpr uint16_t BlockGrass = 2;
        constexpr uint16_t BlockDirt = 3;
        constexpr uint16_t BlockPlant = 10000;
        constexpr uint16_t BlockBedrock = 65535;
        constexpr uint32_t BedrockHeightSalt = 0xBEEFBEDu;
        constexpr uint32_t TopFaceRotationSalt = 0x51A7E001u;
        constexpr uint32_t PlantPlacementSalt = 0x9A7D3E21u;
        constexpr uint8_t PlantPlacementThreshold = 178;
        constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
        constexpr const char* VersionText = "DOLBUTO 0.0.0.0";
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

        struct ParsedBlockDefinition
        {
            uint16_t id = BlockAir;
            std::string renderType = "none";
            bool directional = false;
            bool collision = false;
            bool ao = false;
            std::string faceOcclusion = "none";
            bool sameBlockFaceCulling = false;
            std::string alphaMode = "opaque";
            float alphaCutoff = 0.5f;
            float mipDistanceScale = 1.0f;
            std::unordered_map<std::string, std::string> textures;
        };

        std::optional<std::string> jsonStringField(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t colonPos = object.find(':', keyPos + token.size());
            if (colonPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t quoteStart = object.find('"', colonPos + 1);
            if (quoteStart == std::string::npos)
            {
                return std::nullopt;
            }

            std::string value;
            bool escaped = false;
            for (size_t i = quoteStart + 1; i < object.size(); ++i)
            {
                const char c = object[i];
                if (escaped)
                {
                    value.push_back(c);
                    escaped = false;
                    continue;
                }
                if (c == '\\')
                {
                    escaped = true;
                    continue;
                }
                if (c == '"')
                {
                    return value;
                }
                value.push_back(c);
            }

            return std::nullopt;
        }

        std::optional<int> jsonIntField(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t colonPos = object.find(':', keyPos + token.size());
            if (colonPos == std::string::npos)
            {
                return std::nullopt;
            }

            size_t valueStart = object.find_first_not_of(" \t\r\n", colonPos + 1);
            if (valueStart == std::string::npos)
            {
                return std::nullopt;
            }

            size_t valueEnd = valueStart;
            if (object[valueEnd] == '-')
            {
                ++valueEnd;
            }
            while (valueEnd < object.size() && std::isdigit(static_cast<unsigned char>(object[valueEnd])) != 0)
            {
                ++valueEnd;
            }

            if (valueEnd == valueStart || (valueEnd == valueStart + 1 && object[valueStart] == '-'))
            {
                return std::nullopt;
            }

            try
            {
                return std::stoi(object.substr(valueStart, valueEnd - valueStart));
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        std::optional<float> jsonFloatField(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t colonPos = object.find(':', keyPos + token.size());
            if (colonPos == std::string::npos)
            {
                return std::nullopt;
            }

            size_t valueStart = object.find_first_not_of(" \t\r\n", colonPos + 1);
            if (valueStart == std::string::npos)
            {
                return std::nullopt;
            }

            size_t valueEnd = valueStart;
            while (valueEnd < object.size())
            {
                const char c = object[valueEnd];
                if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
                {
                    ++valueEnd;
                    continue;
                }
                break;
            }

            if (valueEnd == valueStart)
            {
                return std::nullopt;
            }

            try
            {
                return std::stof(object.substr(valueStart, valueEnd - valueStart));
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        std::optional<bool> jsonBoolField(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t colonPos = object.find(':', keyPos + token.size());
            if (colonPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t valueStart = object.find_first_not_of(" \t\r\n", colonPos + 1);
            if (valueStart == std::string::npos)
            {
                return std::nullopt;
            }

            if (object.compare(valueStart, 4, "true") == 0)
            {
                return true;
            }
            if (object.compare(valueStart, 5, "false") == 0)
            {
                return false;
            }

            return std::nullopt;
        }

        std::optional<std::string> jsonObjectField(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t openPos = object.find('{', keyPos + token.size());
            if (openPos == std::string::npos)
            {
                return std::nullopt;
            }

            int depth = 0;
            bool inString = false;
            bool escaped = false;
            for (size_t i = openPos; i < object.size(); ++i)
            {
                const char c = object[i];
                if (inString)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (c == '\\')
                    {
                        escaped = true;
                    }
                    else if (c == '"')
                    {
                        inString = false;
                    }
                    continue;
                }

                if (c == '"')
                {
                    inString = true;
                }
                else if (c == '{')
                {
                    ++depth;
                }
                else if (c == '}')
                {
                    --depth;
                    if (depth == 0)
                    {
                        return object.substr(openPos, i - openPos + 1);
                    }
                }
            }

            return std::nullopt;
        }

        std::vector<std::string> jsonTopLevelObjects(const std::string& text)
        {
            std::vector<std::string> objects;
            int depth = 0;
            size_t objectStart = std::string::npos;
            bool inString = false;
            bool escaped = false;

            for (size_t i = 0; i < text.size(); ++i)
            {
                const char c = text[i];
                if (inString)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (c == '\\')
                    {
                        escaped = true;
                    }
                    else if (c == '"')
                    {
                        inString = false;
                    }
                    continue;
                }

                if (c == '"')
                {
                    inString = true;
                }
                else if (c == '{')
                {
                    if (depth == 0)
                    {
                        objectStart = i;
                    }
                    ++depth;
                }
                else if (c == '}')
                {
                    --depth;
                    if (depth == 0 && objectStart != std::string::npos)
                    {
                        objects.push_back(text.substr(objectStart, i - objectStart + 1));
                        objectStart = std::string::npos;
                    }
                }
            }

            return objects;
        }

        std::vector<ParsedBlockDefinition> parseBlockDefinitions(const std::string& text)
        {
            std::vector<ParsedBlockDefinition> definitions;
            constexpr std::array<const char*, 5> TextureKeys = {"all", "top", "bottom", "side", "topBottom"};

            for (const std::string& object : jsonTopLevelObjects(text))
            {
                const std::optional<int> id = jsonIntField(object, "id");
                if (!id.has_value() || *id < 0 || *id > std::numeric_limits<uint16_t>::max())
                {
                    continue;
                }

                ParsedBlockDefinition definition{};
                definition.id = static_cast<uint16_t>(*id);
                if (const std::optional<std::string> renderType = jsonStringField(object, "renderType"); renderType.has_value())
                {
                    definition.renderType = *renderType;
                }
                if (const std::optional<bool> directional = jsonBoolField(object, "directional"); directional.has_value())
                {
                    definition.directional = *directional;
                }
                if (const std::optional<bool> collision = jsonBoolField(object, "collision"); collision.has_value())
                {
                    definition.collision = *collision;
                }
                if (const std::optional<bool> ao = jsonBoolField(object, "ao"); ao.has_value())
                {
                    definition.ao = *ao;
                }
                if (const std::optional<std::string> faceOcclusion = jsonStringField(object, "faceOcclusion"); faceOcclusion.has_value())
                {
                    definition.faceOcclusion = *faceOcclusion;
                }
                if (const std::optional<bool> sameBlockFaceCulling = jsonBoolField(object, "sameBlockFaceCulling"); sameBlockFaceCulling.has_value())
                {
                    definition.sameBlockFaceCulling = *sameBlockFaceCulling;
                }
                if (const std::optional<std::string> alphaMode = jsonStringField(object, "alphaMode"); alphaMode.has_value())
                {
                    definition.alphaMode = *alphaMode;
                }
                if (const std::optional<float> alphaCutoff = jsonFloatField(object, "alphaCutoff"); alphaCutoff.has_value())
                {
                    definition.alphaCutoff = std::clamp(*alphaCutoff, 0.0f, 1.0f);
                }
                if (const std::optional<float> mipDistanceScale = jsonFloatField(object, "mipDistanceScale"); mipDistanceScale.has_value())
                {
                    definition.mipDistanceScale = std::max(0.0f, *mipDistanceScale);
                }
                if (const std::optional<std::string> textures = jsonObjectField(object, "textures"); textures.has_value())
                {
                    for (const char* key : TextureKeys)
                    {
                        if (const std::optional<std::string> texture = jsonStringField(*textures, key); texture.has_value())
                        {
                            definition.textures[key] = *texture;
                        }
                    }
                }
                definitions.push_back(std::move(definition));
            }

            return definitions;
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

        uint8_t worldRandom8(int x, int y, int z, uint32_t salt)
        {
            return static_cast<uint8_t>(worldRandomHash(x, y, z, salt) & 255u);
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

        int bedrockHeightAt(int worldX, int worldZ)
        {
            return 1 + static_cast<int>(worldRandom8(worldX, 0, worldZ, BedrockHeightSalt) & 3u);
        }

        uint16_t terrainBlockForColumn(int worldX, int y, int worldZ, int height)
        {
            if (y < 0 || y >= height)
            {
                return BlockAir;
            }
            if (y < bedrockHeightAt(worldX, worldZ))
            {
                return BlockBedrock;
            }
            if (y == height - 1)
            {
                return BlockGrass;
            }
            if (y >= height - 5)
            {
                return BlockDirt;
            }
            return BlockRock;
        }

        std::filesystem::path screenshotPath()
        {
            const std::filesystem::path directory = std::filesystem::path(DOLBUTO_ASSET_DIR).parent_path() / "screenshots";
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
            int result = value / divisor;
            const int remainder = value % divisor;
            if (remainder != 0 && ((remainder < 0) != (divisor < 0)))
            {
                --result;
            }
            return result;
        }

        int centerGroupCoordinate(int chunkCoordinate)
        {
            return floorDiv(chunkCoordinate, CenterGroupChunks) * CenterGroupChunks;
        }

        int positiveModulo(int value, int divisor)
        {
            int result = value % divisor;
            return result < 0 ? result + divisor : result;
        }

        int blockCoordinateXz(double worldCoordinate)
        {
            return static_cast<int>(std::floor(worldCoordinate + 0.5));
        }

        int blockCoordinateY(double worldCoordinate)
        {
            return static_cast<int>(std::floor(worldCoordinate));
        }

        uint64_t chunkKey(int chunkX, int chunkZ)
        {
            return (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32u) |
                static_cast<uint64_t>(static_cast<uint32_t>(chunkZ));
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

        int heightFromLut(const std::array<uint16_t, HeightLutCount>& heightLut, float noise)
        {
            constexpr float scale = static_cast<float>(HeightLutCount - 1u) / (HeightLutNoiseMax - HeightLutNoiseMin);
            const float normalized = (noise - HeightLutNoiseMin) * scale;
            const int index = std::clamp(
                static_cast<int>(normalized + 0.5f),
                0,
                static_cast<int>(HeightLutCount - 1u));
            return static_cast<int>(heightLut[static_cast<size_t>(index)]);
        }

        void convertNoiseToHeights(
            const std::array<uint16_t, HeightLutCount>& heightLut,
            const std::array<float, ChunkSizeX * ChunkSizeZ>& noise,
            std::array<int, ChunkSizeX * ChunkSizeZ>& heights)
        {
            constexpr float scale = static_cast<float>(HeightLutCount - 1u) / (HeightLutNoiseMax - HeightLutNoiseMin);
            constexpr int maxIndex = static_cast<int>(HeightLutCount - 1u);
            for (size_t i = 0; i < noise.size(); ++i)
            {
                const float normalized = (noise[i] - HeightLutNoiseMin) * scale;
                const int index = std::clamp(static_cast<int>(normalized + 0.5f), 0, maxIndex);
                heights[i] = static_cast<int>(heightLut[static_cast<size_t>(index)]);
            }
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
        createDepthResources();
        createDescriptorSetLayout();
        createPipeline();
        createTerrainPipeline();
        createSelectionPipeline();
        createFramebuffers();
        createCommandPool();
        createPerformanceQueries();
        createSampler();
        createDescriptorPool();
        createTextures();
        createFont();
        createTextVertexBuffer();
        createSelectionLineBuffer();
        createPlayerMesh();
        loadWorldConfig();
        loadHeightLut();
        startTerrainWorkers();
        requestTerrainLoad(loadedCenterGroupChunkX_, loadedCenterGroupChunkZ_);
        createCommandBuffers();
        createSyncObjects();
    }

    Renderer::~Renderer()
    {
        stopTerrainWorkers();
        vkDeviceWaitIdle(device_);

        cleanupSwapchain();
        destroyTexture(terrainTextureArray_);
        destroyTexture(playerTexture_);
        destroyTexture(font_);
        destroyTexture(crosshair_);
        destroyTexture(moon_);
        destroyTexture(sun_);

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
        if (playerPipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, playerPipeline_, nullptr);
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
        if (pipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        }
        if (descriptorSetLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        }
        if (renderPass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
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
        bool terrainWireframe)
    {
        const Vec3 cameraPositionFloat = toVec3(cameraPosition);
        const Vec3 playerPositionFloat = toVec3(playerPosition);

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
        updateLoadedChunks(playerPosition);
        processCompletedTerrainJobs();
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

        recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, camera, cameraPositionFloat, fpsText, debugTextVisible, screenshotBuffer, showPlayer, terrainWireframe);

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
        constexpr double HalfWidth = 0.3;
        constexpr double Height = 1.75;
        constexpr double Epsilon = 0.000001;

        const double minX = playerPosition.x - HalfWidth;
        const double maxX = playerPosition.x + HalfWidth;
        const double minY = playerPosition.y;
        const double maxY = playerPosition.y + Height;
        const double minZ = playerPosition.z - HalfWidth;
        const double maxZ = playerPosition.z + HalfWidth;

        const int blockMinX = blockCoordinateXz(minX);
        const int blockMaxX = blockCoordinateXz(maxX - Epsilon);
        const int blockMinY = blockCoordinateY(minY);
        const int blockMaxY = blockCoordinateY(maxY - Epsilon);
        const int blockMinZ = blockCoordinateXz(minZ);
        const int blockMaxZ = blockCoordinateXz(maxZ - Epsilon);

        for (int y = blockMinY; y <= blockMaxY; ++y)
        {
            for (int z = blockMinZ; z <= blockMaxZ; ++z)
            {
                for (int x = blockMinX; x <= blockMaxX; ++x)
                {
                    if (terrainCellBlocksPlayer(x, y, z))
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    bool Renderer::editBlockInView(DVec3 origin, Vec3 direction, bool placeRock)
    {
        BlockRaycastHit hit{};
        if (!raycastBlock(origin, direction, hit))
        {
            return false;
        }

        const bool changed = placeRock
            ? setBlockAtWorld(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ, BlockRock)
            : setBlockAtWorld(hit.blockX, hit.blockY, hit.blockZ, BlockAir);
        if (changed)
        {
            const int changedX = placeRock ? hit.previousBlockX : hit.blockX;
            const int changedY = placeRock ? hit.previousBlockY : hit.blockY;
            const int changedZ = placeRock ? hit.previousBlockZ : hit.blockZ;
            rebuildEditedChunkMeshes(changedX, changedY, changedZ);
        }
        return changed;
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

    void Renderer::createPipeline()
    {
        VkShaderModule vertShader = createShaderModule(std::string(DOLBUTO_SHADER_DIR) + "/sprite.vert.spv");
        VkShaderModule fragShader = createShaderModule(std::string(DOLBUTO_SHADER_DIR) + "/sprite.frag.spv");

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

    void Renderer::createTerrainPipeline()
    {
        VkShaderModule vertShader = createShaderModule(std::string(DOLBUTO_SHADER_DIR) + "/terrain.vert.spv");
        VkShaderModule fragShader = createShaderModule(std::string(DOLBUTO_SHADER_DIR) + "/terrain.frag.spv");

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
        bindingDescription.stride = sizeof(TerrainVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

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
        static_assert(sizeof(TerrainPush) == sizeof(float) * 20);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout_;
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

        rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &terrainWireframePipeline_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain wireframe pipeline.");
        }

        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &playerPipeline_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create player pipeline.");
        }

        vkDestroyShaderModule(device_, fragShader, nullptr);
        vkDestroyShaderModule(device_, vertShader, nullptr);
    }

    void Renderer::createSelectionPipeline()
    {
        VkShaderModule vertShader = createShaderModule(std::string(DOLBUTO_SHADER_DIR) + "/selection.vert.spv");
        VkShaderModule fragShader = createShaderModule(std::string(DOLBUTO_SHADER_DIR) + "/selection.frag.spv");

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
        std::array<VkDescriptorPoolSize, 1> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = 8;

        VkDescriptorPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        createInfo.pPoolSizes = poolSizes.data();
        createInfo.maxSets = 8;

        if (vkCreateDescriptorPool(device_, &createInfo, nullptr, &descriptorPool_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor pool.");
        }
    }

    void Renderer::createTextures()
    {
        const std::string blockTextureDir = std::string(DOLBUTO_ASSET_DIR) + "/textures/block/";
        sun_ = createTexture(std::string(DOLBUTO_ASSET_DIR) + "/textures/sky/Sun.png");
        moon_ = createTexture(std::string(DOLBUTO_ASSET_DIR) + "/textures/sky/Moon.png");
        crosshair_ = createTexture(std::string(DOLBUTO_ASSET_DIR) + "/textures/ui/Crosshair.png");
        playerTexture_ = createTextureArray({std::string(DOLBUTO_ASSET_DIR) + "/textures/character/Character.png"});

        const std::vector<char> blockDefinitionData = readFile(std::string(DOLBUTO_ASSET_DIR) + "/data/blocks.json");
        const std::string blockDefinitionText(blockDefinitionData.begin(), blockDefinitionData.end());
        const std::vector<ParsedBlockDefinition> blockDefinitions = parseBlockDefinitions(blockDefinitionText);

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
        for (const ParsedBlockDefinition& definition : blockDefinitions)
        {
            BlockDefinition blockDefinition{};
            blockDefinition.renderType = parseRenderType(definition.renderType);
            blockDefinition.directional = definition.directional;
            blockDefinition.collision = definition.collision;
            blockDefinition.ao = definition.ao;
            blockDefinition.faceOcclusion = parseFaceOcclusion(definition.faceOcclusion);
            blockDefinition.sameBlockFaceCulling = definition.sameBlockFaceCulling;
            blockDefinition.alphaMode = parseAlphaMode(definition.alphaMode);
            blockDefinition.alphaCutoff = definition.alphaCutoff;
            blockDefinition.mipDistanceScale = definition.mipDistanceScale;
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
            blockTextureLayers_[definition.id] = layers;
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
    }

    void Renderer::createFont()
    {
        std::vector<char> fontData = readFile(std::string(DOLBUTO_ASSET_DIR) + "/fonts/VCR_OSD_MONO.ttf");
        std::vector<unsigned char> alpha(FontAtlasSize * FontAtlasSize);

        const int bakeResult = stbtt_BakeFontBitmap(
            reinterpret_cast<const unsigned char*>(fontData.data()),
            0,
            FontPixelHeight,
            alpha.data(),
            FontAtlasSize,
            FontAtlasSize,
            32,
            static_cast<int>(bakedChars_.size()),
            bakedChars_.data());

        if (bakeResult <= 0)
        {
            throw std::runtime_error("Failed to bake debug font atlas.");
        }

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
        const std::vector<char> meshData = readFile(std::string(DOLBUTO_ASSET_DIR) + "/textures/character/Character.mesh");
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

        createTerrainBuffer({playerLocalVertices_, playerIndices_}, playerMesh_);
    }

    void Renderer::loadWorldConfig()
    {
        loadGridScale_ = DefaultLoadGridScale;
        terrainWorkerCount_ = DefaultTerrainWorkerCount;
        maxTerrainUploadChunksPerFrame_ = DefaultMaxTerrainUploadChunksPerFrame;
        maxTerrainUnloadChunksPerFrame_ = DefaultMaxTerrainUnloadChunksPerFrame;
        terrainNoiseFeatureScale_ = DefaultTerrainNoiseFeatureScale;
        terrainNoiseOctaveCount_ = DefaultTerrainNoiseOctaveCount;
        terrainNoiseLacunarity_ = DefaultTerrainNoiseLacunarity;
        terrainNoiseGain_ = DefaultTerrainNoiseGain;
        terrainNoiseSimplexScale_ = DefaultTerrainNoiseSimplexScale;
        terrainDomainWarpEnabled_ = DefaultTerrainDomainWarpEnabled;
        terrainDomainWarpAmplitude_ = DefaultTerrainDomainWarpAmplitude;
        terrainDomainWarpFrequency_ = DefaultTerrainDomainWarpFrequency;
        terrainDomainWarpOctaveCount_ = DefaultTerrainDomainWarpOctaveCount;
        terrainDomainWarpGain_ = DefaultTerrainDomainWarpGain;

        const std::filesystem::path path = std::filesystem::path(DOLBUTO_CONFIG_DIR) / "world.json";
        std::ifstream file(path);
        if (!file.is_open())
        {
            return;
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        const std::string text = contents.str();
        const std::string chunkLoad = jsonObjectField(text, "chunkLoad").value_or("{}");
        const std::string terrain = jsonObjectField(text, "terrain").value_or("{}");
        const std::string terrainDomainWarp = jsonObjectField(terrain, "domainWarp").value_or("{}");
        const std::string terrainBaseNoise = jsonObjectField(terrain, "baseNoise").value_or("{}");

        if (const std::optional<int> value = jsonIntField(chunkLoad, "loadGridScale"); value.has_value())
        {
            loadGridScale_ = std::max(0, *value);
        }
        if (const std::optional<int> value = jsonIntField(chunkLoad, "workerCount"); value.has_value())
        {
            terrainWorkerCount_ = std::clamp(*value, 1, 16);
        }
        if (const std::optional<int> value = jsonIntField(chunkLoad, "maxCompletedChunksAppliedPerFrame"); value.has_value())
        {
            maxTerrainUploadChunksPerFrame_ = std::clamp(*value, 1, 64);
        }
        if (const std::optional<int> value = jsonIntField(chunkLoad, "maxUnloadedChunksPerFrame"); value.has_value())
        {
            maxTerrainUnloadChunksPerFrame_ = std::clamp(*value, 1, 64);
        }
        if (const std::optional<float> value = jsonFloatField(terrainBaseNoise, "featureScale"); value.has_value() && *value > 0.0f)
        {
            terrainNoiseFeatureScale_ = *value;
        }
        if (const std::optional<int> value = jsonIntField(terrainBaseNoise, "octaveCount"); value.has_value())
        {
            terrainNoiseOctaveCount_ = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(terrainBaseNoise, "lacunarity"); value.has_value() && *value > 0.0f)
        {
            terrainNoiseLacunarity_ = *value;
        }
        if (const std::optional<float> value = jsonFloatField(terrainBaseNoise, "gain"); value.has_value() && *value >= 0.0f)
        {
            terrainNoiseGain_ = *value;
        }
        if (const std::optional<float> value = jsonFloatField(terrainBaseNoise, "simplexScale"); value.has_value() && *value > 0.0f)
        {
            terrainNoiseSimplexScale_ = *value;
        }
        if (const std::optional<bool> value = jsonBoolField(terrainDomainWarp, "enabled"); value.has_value())
        {
            terrainDomainWarpEnabled_ = *value;
        }
        if (const std::optional<float> value = jsonFloatField(terrainDomainWarp, "amplitude"); value.has_value() && *value >= 0.0f)
        {
            terrainDomainWarpAmplitude_ = *value;
        }
        if (const std::optional<float> value = jsonFloatField(terrainDomainWarp, "frequency"); value.has_value() && *value > 0.0f)
        {
            terrainDomainWarpFrequency_ = *value;
        }
        if (const std::optional<int> value = jsonIntField(terrainDomainWarp, "octaveCount"); value.has_value())
        {
            terrainDomainWarpOctaveCount_ = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(terrainDomainWarp, "gain"); value.has_value() && *value >= 0.0f)
        {
            terrainDomainWarpGain_ = *value;
        }
    }

    void Renderer::loadHeightLut()
    {
        for (uint32_t i = 0; i < HeightLutCount; ++i)
        {
            const double t = static_cast<double>(i) / static_cast<double>(HeightLutCount - 1u);
            heightLut_[i] = static_cast<uint16_t>(std::lround(static_cast<double>(TerrainMinHeight) + t * static_cast<double>(TerrainMaxHeight - TerrainMinHeight)));
        }

        const std::filesystem::path path = std::filesystem::path(DOLBUTO_ASSET_DIR) / "data" / "world" / "height_lut.bin";
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
        if (centerGroupChunkX == loadedCenterGroupChunkX_ && centerGroupChunkZ == loadedCenterGroupChunkZ_)
        {
            return;
        }

        loadedCenterGroupChunkX_ = centerGroupChunkX;
        loadedCenterGroupChunkZ_ = centerGroupChunkZ;

        const auto chunkUpdateStart = std::chrono::steady_clock::now();
        requestTerrainLoad(centerGroupChunkX, centerGroupChunkZ);
        const auto chunkUpdateEnd = std::chrono::steady_clock::now();
        chunkUpdateProfileText_ = formatProfileMs("UPDATE TOTAL", std::chrono::duration<double, std::milli>(chunkUpdateEnd - chunkUpdateStart).count());
        debugTextBatchDirty_ = true;
    }

    void Renderer::requestTerrainLoad(int centerGroupChunkX, int centerGroupChunkZ)
    {
        const auto updateStart = std::chrono::steady_clock::now();
        loadedChunkDiameter_ = std::max(1, loadGridScale_) * LoadGridUnitChunks;
        loadedCenterGroupChunkX_ = centerGroupChunkX;
        loadedCenterGroupChunkZ_ = centerGroupChunkZ;
        const uint64_t generation = ++terrainGeneration_;
        rebuildLoadOrderIfNeeded();

        std::unordered_set<uint64_t> desired;
        desired.reserve(static_cast<size_t>(loadedChunkDiameter_) * loadedChunkDiameter_);

        const auto gridStart = std::chrono::steady_clock::now();
        for (const ChunkOffset& offset : loadOrder_)
        {
            desired.insert(chunkKey(loadedCenterGroupChunkX_ + offset.x, loadedCenterGroupChunkZ_ + offset.z));
        }
        const auto gridEnd = std::chrono::steady_clock::now();

        desiredTerrainChunks_ = std::move(desired);
        requestedChunkJobs_.clear();
        requestedMeshJobs_.clear();

        {
            std::lock_guard<std::mutex> lock(terrainJobMutex_);
            terrainDataJobs_.clear();
            terrainMeshJobs_.clear();
            completedChunkData_.clear();
            completedChunkMeshes_.clear();
        }

        for (auto it = terrainChunks_.begin(); it != terrainChunks_.end();)
        {
            if (desiredTerrainChunks_.find(it->first) == desiredTerrainChunks_.end())
            {
                if (pendingUnloadSet_.insert(it->first).second)
                {
                    pendingUnloadChunks_.push_back(it->first);
                }
                ++it;
            }
            else
            {
                pendingUnloadSet_.erase(it->first);
                ++it;
            }
        }

        uint32_t queuedChunks = 0;
        for (const ChunkOffset& offset : loadOrder_)
        {
            const int chunkX = loadedCenterGroupChunkX_ + offset.x;
            const int chunkZ = loadedCenterGroupChunkZ_ + offset.z;
            const uint64_t key = chunkKey(chunkX, chunkZ);
            pendingUnloadSet_.erase(key);
            auto dataIt = chunkData_.find(key);
            if (dataIt != chunkData_.end())
            {
                dataIt->second->generation = generation;
                if (!chunkMeshReady(key) && requestedMeshJobs_.insert(key).second)
                {
                    TerrainJob job{};
                    job.type = TerrainJob::Type::BuildChunkMesh;
                    job.generation = generation;
                    job.revision = dataIt->second->revision;
                    job.chunkX = chunkX;
                    job.chunkZ = chunkZ;
                    job.chunk = dataIt->second;
                    enqueueTerrainJob(std::move(job));
                }
                continue;
            }

            TerrainJob job{};
            job.type = TerrainJob::Type::BuildChunkData;
            job.generation = generation;
            job.chunkX = chunkX;
            job.chunkZ = chunkZ;
            enqueueTerrainJob(std::move(job));
            requestedChunkJobs_.insert(key);
            ++queuedChunks;
        }

        const auto updateEnd = std::chrono::steady_clock::now();
        chunkUpdateProfileText_ = formatProfileMs("UPDATE TOTAL", std::chrono::duration<double, std::milli>(updateEnd - updateStart).count());
        gridScanProfileText_ = formatProfileMs("GRID SCAN", std::chrono::duration<double, std::milli>(gridEnd - gridStart).count());
        newChunksProfileText_ = "NEW CHUNKS: " + std::to_string(queuedChunks);
        metadataBuildProfileText_ = formatProfileMs("META BUILD", 0.0);
        updateTerrainStats();
        debugTextBatchDirty_ = true;
    }

    void Renderer::rebuildLoadOrderIfNeeded()
    {
        if (loadOrderDiameter_ == loadedChunkDiameter_ && !loadOrder_.empty())
        {
            return;
        }

        loadOrderDiameter_ = loadedChunkDiameter_;
        loadOrder_.clear();
        loadOrder_.reserve(static_cast<size_t>(loadedChunkDiameter_) * loadedChunkDiameter_);

        const int min = -(loadedChunkDiameter_ / 2 - 1);
        const int max = loadedChunkDiameter_ / 2;
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
        stopTerrainWorkers_ = false;
        terrainWorkers_.reserve(static_cast<size_t>(terrainWorkerCount_));
        for (int i = 0; i < terrainWorkerCount_; ++i)
        {
            terrainWorkers_.emplace_back(&Renderer::terrainWorkerLoop, this);
        }
    }

    void Renderer::stopTerrainWorkers()
    {
        {
            std::lock_guard<std::mutex> lock(terrainJobMutex_);
            stopTerrainWorkers_ = true;
            terrainDataJobs_.clear();
            terrainMeshJobs_.clear();
            completedChunkData_.clear();
            completedChunkMeshes_.clear();
        }
        terrainJobCondition_.notify_all();

        for (std::thread& worker : terrainWorkers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        terrainWorkers_.clear();
    }

    void Renderer::terrainWorkerLoop()
    {
        for (;;)
        {
            TerrainJob job{};
            {
                std::unique_lock<std::mutex> lock(terrainJobMutex_);
                terrainJobCondition_.wait(lock, [this]
                {
                    return stopTerrainWorkers_ || !terrainMeshJobs_.empty() || !terrainDataJobs_.empty();
                });

                if (stopTerrainWorkers_)
                {
                    return;
                }

                if (!terrainMeshJobs_.empty())
                {
                    job = std::move(terrainMeshJobs_.front());
                    terrainMeshJobs_.pop_front();
                }
                else
                {
                    job = std::move(terrainDataJobs_.front());
                    terrainDataJobs_.pop_front();
                }
            }

            if (job.generation != terrainGeneration_.load())
            {
                continue;
            }

            if (job.type == TerrainJob::Type::BuildChunkData)
            {
                std::shared_ptr<ChunkData> chunk = buildChunkData(job.chunkX, job.chunkZ);
                chunk->generation = job.generation;
                chunk->revision = 0;
                std::lock_guard<std::mutex> lock(terrainJobMutex_);
                completedChunkData_.push_back(std::move(chunk));
            }
            else if (job.chunk)
            {
                if (job.revision != job.chunk->revision)
                {
                    continue;
                }
                CompletedChunkMesh mesh = buildChunkMesh(job.chunk, job.generation);
                std::lock_guard<std::mutex> lock(terrainJobMutex_);
                completedChunkMeshes_.push_back(std::move(mesh));
            }
        }
    }

    void Renderer::enqueueTerrainJob(TerrainJob job)
    {
        {
            std::lock_guard<std::mutex> lock(terrainJobMutex_);
            if (job.type == TerrainJob::Type::BuildChunkMesh)
            {
                terrainMeshJobs_.push_back(std::move(job));
            }
            else
            {
                terrainDataJobs_.push_back(std::move(job));
            }
        }
        terrainJobCondition_.notify_one();
    }

    void Renderer::processCompletedTerrainJobs()
    {
        const auto buildStart = std::chrono::steady_clock::now();
        const uint64_t generation = terrainGeneration_.load();
        std::vector<std::shared_ptr<ChunkData>> completedChunks;
        std::vector<CompletedChunkMesh> completedMeshes;
        size_t queuedDataJobCount = 0;
        size_t queuedMeshJobCount = 0;
        size_t queuedDataDoneCount = 0;
        size_t queuedMeshDoneCount = 0;
        uint32_t uploadedChunkCount = 0;
        {
            std::lock_guard<std::mutex> lock(terrainJobMutex_);
            queuedDataJobCount = terrainDataJobs_.size();
            queuedMeshJobCount = terrainMeshJobs_.size();
            queuedDataDoneCount = completedChunkData_.size();
            queuedMeshDoneCount = completedChunkMeshes_.size();
            while (!completedChunkData_.empty())
            {
                completedChunks.push_back(std::move(completedChunkData_.front()));
                completedChunkData_.pop_front();
            }

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

            while (!completedChunkMeshes_.empty())
            {
                const CompletedChunkMesh& frontMesh = completedChunkMeshes_.front();
                const uint64_t key = chunkKey(frontMesh.chunkX, frontMesh.chunkZ);
                if (frontMesh.generation != generation || desiredTerrainChunks_.find(key) == desiredTerrainChunks_.end())
                {
                    completedChunkMeshes_.pop_front();
                    continue;
                }

                if (!canUploadChunk(key))
                {
                    break;
                }

                completedMeshes.push_back(std::move(completedChunkMeshes_.front()));
                completedChunkMeshes_.pop_front();
                uploadedChunkCount = uploadChunkCount;
            }
        }

        for (const std::shared_ptr<ChunkData>& chunk : completedChunks)
        {
            const uint64_t key = chunkKey(chunk->chunkX, chunk->chunkZ);
            requestedChunkJobs_.erase(key);
            if (chunk->generation != generation || desiredTerrainChunks_.find(key) == desiredTerrainChunks_.end())
            {
                continue;
            }
            chunkData_[key] = chunk;
            ChunkRenderData& renderData = terrainChunks_[key];
            renderData.chunkX = chunk->chunkX;
            renderData.chunkZ = chunk->chunkZ;
            if (!chunkMeshReady(key) && requestedMeshJobs_.insert(key).second)
            {
                TerrainJob job{};
                job.type = TerrainJob::Type::BuildChunkMesh;
                job.generation = generation;
                job.revision = chunk->revision;
                job.chunkX = chunk->chunkX;
                job.chunkZ = chunk->chunkZ;
                job.chunk = chunk;
                enqueueTerrainJob(std::move(job));
            }
        }

        for (CompletedChunkMesh& mesh : completedMeshes)
        {
            const uint64_t key = chunkKey(mesh.chunkX, mesh.chunkZ);
            requestedMeshJobs_.erase(key);

            if (mesh.generation != generation || desiredTerrainChunks_.find(key) == desiredTerrainChunks_.end())
            {
                continue;
            }
            const auto chunkIt = chunkData_.find(key);
            if (chunkIt == chunkData_.end() || !chunkIt->second || mesh.revision != chunkIt->second->revision)
            {
                continue;
            }

            ChunkRenderData& renderData = terrainChunks_[key];
            renderData.chunkX = mesh.chunkX;
            renderData.chunkZ = mesh.chunkZ;
            for (size_t subchunkY = 0; subchunkY < mesh.rockSubchunks.size(); ++subchunkY)
            {
                TerrainMesh& targetMesh = renderData.rockSubchunks[subchunkY];
                destroyTerrainMesh(targetMesh);
                createTerrainBuffer(mesh.rockSubchunks[subchunkY], targetMesh);
            }
        }

        if (!completedChunks.empty() || !completedMeshes.empty())
        {
            updateTerrainStats();
        }
        processRetiredTerrainChunks();
        const uint32_t unloadedChunkCount = processPendingTerrainUnloads();
        const size_t retiredChunkCount = retiredTerrainChunks_.size();

        const auto buildEnd = std::chrono::steady_clock::now();
        const std::chrono::duration<double> terrainDebugElapsed = buildEnd - terrainDebugSampleTime_;
        if (terrainDebugSampleTime_ == std::chrono::steady_clock::time_point{} || terrainDebugElapsed.count() >= 0.05)
        {
            terrainDebugSampleTime_ = buildEnd;
            dataQueueText_ = "DATA QUEUE: " + std::to_string(queuedDataJobCount);
            meshQueueText_ = "MESH QUEUE: " + std::to_string(queuedMeshJobCount);
            dataDoneText_ = "DATA DONE: " + std::to_string(queuedDataDoneCount);
            meshDoneText_ = "MESH DONE: " + std::to_string(queuedMeshDoneCount);
            uploadText_ = "UPLOAD: " + std::to_string(uploadedChunkCount) + " / " + std::to_string(maxTerrainUploadChunksPerFrame_);
            unloadText_ = "UNLOAD: " + std::to_string(unloadedChunkCount) + " / " + std::to_string(maxTerrainUnloadChunksPerFrame_);
            retiredText_ = "RETIRED: " + std::to_string(retiredChunkCount);
            jobMainText_ = formatProfileMs("JOB MAIN", std::chrono::duration<double, std::milli>(buildEnd - buildStart).count());
            debugTextBatchDirty_ = true;
        }
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
                    static_cast<uint32_t>(MaxFramesInFlight),
                    std::move(renderIt->second)});
                terrainChunks_.erase(renderIt);
            }
            chunkData_.erase(key);
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
        for (auto it = retiredTerrainChunks_.begin(); it != retiredTerrainChunks_.end();)
        {
            if (it->framesLeft > 0)
            {
                --it->framesLeft;
                ++it;
                continue;
            }

            destroyChunkRenderData(it->chunk);
            it = retiredTerrainChunks_.erase(it);
        }
    }

    std::shared_ptr<Renderer::ChunkData> Renderer::buildChunkData(int chunkX, int chunkZ) const
    {
        auto chunk = std::make_shared<ChunkData>();
        chunk->chunkX = chunkX;
        chunk->chunkZ = chunkZ;
        chunk->blocks.assign(ChunkBlockCount, BlockAir);
        chunk->emptySubchunks.fill(true);

        std::array<int, Renderer::ChunkColumnCount> heights = buildChunkHeightmap(chunkX, chunkZ);
        int maxHeight = 0;
        for (int& height : heights)
        {
            height = std::clamp(height, 0, ChunkSizeY);
            maxHeight = std::max(maxHeight, height);
        }

        const int filledSubchunks = std::min(SubchunksPerChunk, (maxHeight + SubchunkSize - 1) / SubchunkSize);
        for (int subchunkY = 0; subchunkY < filledSubchunks; ++subchunkY)
        {
            chunk->emptySubchunks[static_cast<size_t>(subchunkY)] = false;
        }

        constexpr size_t BlocksPerLayer = ChunkSizeX * ChunkSizeZ;
        const int worldXStart = chunkX * ChunkSizeX;
        const int worldZStart = chunkZ * ChunkSizeZ;
        for (int y = 0; y < maxHeight; ++y)
        {
            uint16_t* layer = chunk->blocks.data() + static_cast<size_t>(y) * BlocksPerLayer;
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                    layer[column] = terrainBlockForColumn(worldXStart + localX, y, worldZStart + localZ, heights[column]);
                }
            }
        }

        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            for (int localX = 0; localX < ChunkSizeX; ++localX)
            {
                const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                const int height = heights[column];
                if (height <= 0 || height >= ChunkSizeY)
                {
                    continue;
                }

                const size_t topIndex = static_cast<size_t>(((height - 1) * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                const size_t plantIndex = static_cast<size_t>((height * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                if (topIndex < chunk->blocks.size() &&
                    plantIndex < chunk->blocks.size() &&
                    chunk->blocks[topIndex] == BlockGrass &&
                    worldRandom8(worldXStart + localX, height, worldZStart + localZ, PlantPlacementSalt) < PlantPlacementThreshold)
                {
                    chunk->blocks[plantIndex] = BlockPlant;
                    chunk->emptySubchunks[static_cast<size_t>(height / SubchunkSize)] = false;
                }
            }
        }

        return chunk;
    }

    bool Renderer::setBlockAtWorld(int x, int y, int z, uint16_t block)
    {
        if (y < 0 || y >= ChunkSizeY)
        {
            return false;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        const uint64_t key = chunkKey(chunkX, chunkZ);
        const auto chunkIt = chunkData_.find(key);
        if (chunkIt == chunkData_.end() || !chunkIt->second)
        {
            return false;
        }

        const int localX = positiveModulo(x, ChunkSizeX);
        const int localZ = positiveModulo(z, ChunkSizeZ);
        const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
        if (index >= chunkIt->second->blocks.size() || chunkIt->second->blocks[index] == block)
        {
            return false;
        }

        chunkIt->second->blocks[index] = block;
        ++chunkIt->second->revision;
        updateChunkEmptySubchunk(chunkIt->second, y / SubchunkSize);
        return true;
    }

    void Renderer::updateChunkEmptySubchunk(const std::shared_ptr<ChunkData>& chunk, int subchunkY)
    {
        if (!chunk || subchunkY < 0 || subchunkY >= SubchunksPerChunk)
        {
            return;
        }

        const int yStart = subchunkY * SubchunkSize;
        const int yEnd = yStart + SubchunkSize;
        bool empty = true;
        for (int y = yStart; y < yEnd && empty; ++y)
        {
            for (int z = 0; z < ChunkSizeZ && empty; ++z)
            {
                for (int x = 0; x < ChunkSizeX; ++x)
                {
                    const size_t index = static_cast<size_t>((y * ChunkSizeZ + z) * ChunkSizeX + x);
                    if (index < chunk->blocks.size() && chunk->blocks[index] != BlockAir)
                    {
                        empty = false;
                        break;
                    }
                }
            }
        }

        chunk->emptySubchunks[static_cast<size_t>(subchunkY)] = empty;
    }

    void Renderer::rebuildSubchunkMeshNow(int chunkX, int chunkZ, int subchunkY)
    {
        if (subchunkY < 0 || subchunkY >= SubchunksPerChunk)
        {
            return;
        }

        const uint64_t key = chunkKey(chunkX, chunkZ);
        const auto chunkIt = chunkData_.find(key);
        if (chunkIt == chunkData_.end() || !chunkIt->second || desiredTerrainChunks_.find(key) == desiredTerrainChunks_.end())
        {
            return;
        }

        const uint64_t generation = terrainGeneration_.load();
        chunkIt->second->generation = generation;
        const uint64_t revision = chunkIt->second->revision;
        TerrainBuildData mesh = buildEditedSubchunkMesh(chunkIt->second, subchunkY);

        requestedMeshJobs_.erase(key);
        ChunkRenderData& renderData = terrainChunks_[key];
        renderData.chunkX = chunkX;
        renderData.chunkZ = chunkZ;
        if (chunkIt->second->revision != revision || chunkIt->second->generation != generation)
        {
            return;
        }

        TerrainMesh& targetMesh = renderData.rockSubchunks[static_cast<size_t>(subchunkY)];
        destroyTerrainMesh(targetMesh);
        createTerrainBuffer(mesh, targetMesh);
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

    std::vector<uint16_t> Renderer::buildMeshingBlocks(const std::shared_ptr<Renderer::ChunkData>& chunk) const
    {
        std::vector<uint16_t> meshingBlocks(MeshingBlockCount, BlockAir);

        auto meshingIndex = [](int x, int y, int z) -> size_t
        {
            return static_cast<size_t>((y * MeshingSizeZ + z) * MeshingSizeX + x);
        };

        for (int y = 0; y < ChunkSizeY; ++y)
        {
            for (int z = 0; z < ChunkSizeZ; ++z)
            {
                for (int x = 0; x < ChunkSizeX; ++x)
                {
                    const size_t sourceIndex = static_cast<size_t>((y * ChunkSizeZ + z) * ChunkSizeX + x);
                    meshingBlocks[meshingIndex(x + MeshingBorder, y, z + MeshingBorder)] = chunk->blocks[sourceIndex];
                }
            }
        }

        std::unordered_map<uint64_t, std::array<int, Renderer::ChunkColumnCount>> heightCache;
        auto heightAtWorldColumn = [&](int worldX, int worldZ) -> int
        {
            const int neighborChunkX = floorDiv(worldX, ChunkSizeX);
            const int neighborChunkZ = floorDiv(worldZ, ChunkSizeZ);
            const uint64_t key = chunkKey(neighborChunkX, neighborChunkZ);
            auto it = heightCache.find(key);
            if (it == heightCache.end())
            {
                it = heightCache.emplace(key, buildChunkHeightmap(neighborChunkX, neighborChunkZ)).first;
            }

            const int localX = positiveModulo(worldX, ChunkSizeX);
            const int localZ = positiveModulo(worldZ, ChunkSizeZ);
            return std::clamp(it->second[localZ * ChunkSizeX + localX], 0, ChunkSizeY);
        };

        const int worldXStart = chunk->chunkX * ChunkSizeX;
        const int worldZStart = chunk->chunkZ * ChunkSizeZ;
        for (int meshZ = 0; meshZ < MeshingSizeZ; ++meshZ)
        {
            for (int meshX = 0; meshX < MeshingSizeX; ++meshX)
            {
                const bool insideCenter =
                    meshX >= MeshingBorder &&
                    meshX < MeshingBorder + ChunkSizeX &&
                    meshZ >= MeshingBorder &&
                    meshZ < MeshingBorder + ChunkSizeZ;
                if (insideCenter)
                {
                    continue;
                }

                const int worldX = worldXStart + meshX - MeshingBorder;
                const int worldZ = worldZStart + meshZ - MeshingBorder;
                const int height = heightAtWorldColumn(worldX, worldZ);
                for (int y = 0; y < height; ++y)
                {
                    meshingBlocks[meshingIndex(meshX, y, meshZ)] = terrainBlockForColumn(worldX, y, worldZ, height);
                }
            }
        }

        return meshingBlocks;
    }

    Renderer::TerrainBuildData Renderer::buildSubchunkMesh(const std::shared_ptr<Renderer::ChunkData>& chunk, const std::vector<uint16_t>& meshingBlocks, int subchunkY) const
    {
        auto blockAt = [&](int localX, int y, int localZ) -> uint16_t
        {
            if (y < 0 || y >= ChunkSizeY)
            {
                return BlockAir;
            }

            const int meshX = localX + MeshingBorder;
            const int meshZ = localZ + MeshingBorder;
            if (meshX < 0 || meshX >= MeshingSizeX || meshZ < 0 || meshZ >= MeshingSizeZ)
            {
                return BlockAir;
            }
            return meshingBlocks[static_cast<size_t>((y * MeshingSizeZ + meshZ) * MeshingSizeX + meshX)];
        };

        return buildSubchunkMesh(chunk, subchunkY, blockAt);
    }

    Renderer::TerrainBuildData Renderer::buildEditedSubchunkMesh(const std::shared_ptr<Renderer::ChunkData>& chunk, int subchunkY) const
    {
        constexpr int EditMeshingSizeY = SubchunkSize + MeshingBorder * 2;
        std::vector<uint16_t> meshingBlocks(static_cast<size_t>(MeshingSizeX * EditMeshingSizeY * MeshingSizeZ), BlockAir);
        const int worldXStart = chunk->chunkX * ChunkSizeX;
        const int worldYStart = subchunkY * SubchunkSize;
        const int worldZStart = chunk->chunkZ * ChunkSizeZ;
        const int yBase = worldYStart - MeshingBorder;

        auto meshingIndex = [](int x, int y, int z) -> size_t
        {
            return static_cast<size_t>((y * MeshingSizeZ + z) * MeshingSizeX + x);
        };

        for (int meshY = 0; meshY < EditMeshingSizeY; ++meshY)
        {
            const int worldY = yBase + meshY;
            if (worldY < 0 || worldY >= ChunkSizeY)
            {
                continue;
            }

            for (int meshZ = 0; meshZ < MeshingSizeZ; ++meshZ)
            {
                const int worldZ = worldZStart + meshZ - MeshingBorder;
                for (int meshX = 0; meshX < MeshingSizeX; ++meshX)
                {
                    const int worldX = worldXStart + meshX - MeshingBorder;
                    meshingBlocks[meshingIndex(meshX, meshY, meshZ)] = blockAtWorld(worldX, worldY, worldZ);
                }
            }
        }

        auto blockAt = [&](int localX, int y, int localZ) -> uint16_t
        {
            if (y < 0 || y >= ChunkSizeY)
            {
                return BlockAir;
            }

            const int meshY = y - yBase;
            if (meshY < 0 || meshY >= EditMeshingSizeY)
            {
                return BlockAir;
            }

            const int meshX = localX + MeshingBorder;
            const int meshZ = localZ + MeshingBorder;
            if (meshX < 0 || meshX >= MeshingSizeX || meshZ < 0 || meshZ >= MeshingSizeZ)
            {
                return BlockAir;
            }
            return meshingBlocks[meshingIndex(meshX, meshY, meshZ)];
        };

        return buildSubchunkMesh(chunk, subchunkY, blockAt);
    }

    Renderer::TerrainBuildData Renderer::buildSubchunkMesh(
        const std::shared_ptr<Renderer::ChunkData>& chunk,
        int subchunkY,
        const std::function<uint16_t(int, int, int)>& blockAt) const
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

        auto appendCrossBlock = [&](TerrainBuildData& buildData, int x, int y, int z, uint32_t textureLayer, float mipDistanceScale)
        {
            const float x0 = static_cast<float>(x) - 0.5f;
            const float x1 = static_cast<float>(x) + 0.5f;
            const float y0 = static_cast<float>(y);
            const float y1 = static_cast<float>(y + 1);
            const float z0 = static_cast<float>(z) - 0.5f;
            const float z1 = static_cast<float>(z) + 0.5f;

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
                {x0, y0, z0, 0.0f, 1.0f, 1.0f},
                {x0, y1, z0, 0.0f, 0.0f, 1.0f},
                {x1, y1, z1, 1.0f, 0.0f, 1.0f},
                {x1, y0, z1, 1.0f, 1.0f, 1.0f});
            appendDoubleSidedQuad(
                {x1, y0, z0, 0.0f, 1.0f, 1.0f},
                {x1, y1, z0, 0.0f, 0.0f, 1.0f},
                {x0, y1, z1, 1.0f, 0.0f, 1.0f},
                {x0, y0, z1, 1.0f, 1.0f, 1.0f});
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
                        appendCrossBlock(result, worldXStart + localX, y, worldZStart + localZ, blockFaceTextureLayer(block, 0), blockDefinition(block).mipDistanceScale);
                    }
                }
            }
        }

        return result;
    }

    Renderer::CompletedChunkMesh Renderer::buildChunkMesh(const std::shared_ptr<Renderer::ChunkData>& chunk, uint64_t generation) const
    {
        CompletedChunkMesh result{};
        result.generation = generation;
        result.revision = chunk->revision;
        result.chunkX = chunk->chunkX;
        result.chunkZ = chunk->chunkZ;
        const std::vector<uint16_t> meshingBlocks = buildMeshingBlocks(chunk);
        for (int subchunkY = 0; subchunkY < SubchunksPerChunk; ++subchunkY)
        {
            result.rockSubchunks[static_cast<size_t>(subchunkY)] = buildSubchunkMesh(chunk, meshingBlocks, subchunkY);
        }
        return result;
    }

    bool Renderer::chunkMeshReady(uint64_t key) const
    {
        auto renderIt = terrainChunks_.find(key);
        if (renderIt == terrainChunks_.end())
        {
            return false;
        }

        for (const TerrainMesh& mesh : renderIt->second.rockSubchunks)
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
        for (TerrainMesh& mesh : chunk.rockSubchunks)
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
    }

    void Renderer::updateTerrainStats()
    {
        terrainDrawCount_ = 0;
        terrainFaceCount_ = 0;
        terrainVertexCount_ = 0;

        for (const auto& entry : terrainChunks_)
        {
            for (const TerrainMesh& mesh : entry.second.rockSubchunks)
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
        terrainVertexText_ = "VERTICES: " + std::to_string(terrainVertexCount_);
    }

    std::array<int, Renderer::ChunkColumnCount> Renderer::buildChunkHeightmap(int chunkX, int chunkZ) const
    {
        std::array<int, Renderer::ChunkColumnCount> heights{};
        auto generator = terrainNoiseGenerator(
            terrainNoiseSimplexScale_,
            terrainNoiseOctaveCount_,
            terrainNoiseLacunarity_,
            terrainNoiseGain_);
        if (!generator)
        {
            heights.fill(heightFromLut(heightLut_, 0.0f));
            return heights;
        }

        constexpr float TwoPi = 6.28318530718f;
        const float angleScale = TwoPi / static_cast<float>(TerrainTilePeriod);
        const float radius = static_cast<float>(TerrainTilePeriod) / (TwoPi * terrainNoiseFeatureScale_);

        std::array<float, ChunkSizeX> xCos{};
        std::array<float, ChunkSizeX> xSin{};
        std::array<float, ChunkSizeZ> zCos{};
        std::array<float, ChunkSizeZ> zSin{};
        for (int localX = 0; localX < ChunkSizeX; ++localX)
        {
            const int worldX = chunkX * ChunkSizeX + localX;
            const float angle = static_cast<float>(positiveModulo(worldX, TerrainTilePeriod)) * angleScale;
            xCos[localX] = std::cos(angle) * radius;
            xSin[localX] = std::sin(angle) * radius;
        }
        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            const int worldZ = chunkZ * ChunkSizeZ + localZ;
            const float angle = static_cast<float>(positiveModulo(worldZ, TerrainTilePeriod)) * angleScale;
            zCos[localZ] = std::cos(angle) * radius;
            zSin[localZ] = std::sin(angle) * radius;
        }

        std::array<float, ChunkSizeX * ChunkSizeZ> xPositions{};
        std::array<float, ChunkSizeX * ChunkSizeZ> yPositions{};
        std::array<float, ChunkSizeX * ChunkSizeZ> zPositions{};
        std::array<float, ChunkSizeX * ChunkSizeZ> wPositions{};
        std::array<float, ChunkSizeX * ChunkSizeZ> noise{};
        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            for (int localX = 0; localX < ChunkSizeX; ++localX)
            {
                const size_t index = static_cast<size_t>(localZ * ChunkSizeX + localX);
                xPositions[index] = xCos[localX];
                yPositions[index] = zCos[localZ];
                zPositions[index] = xSin[localX];
                wPositions[index] = zSin[localZ];
            }
        }

        if (terrainDomainWarpEnabled_ && terrainDomainWarpAmplitude_ > 0.0f)
        {
            auto warpGenerator = terrainNoiseGenerator(
                terrainDomainWarpFrequency_,
                terrainDomainWarpOctaveCount_,
                DefaultTerrainNoiseLacunarity,
                terrainDomainWarpGain_);
            if (warpGenerator)
            {
                std::array<float, ChunkSizeX * ChunkSizeZ> xWarp{};
                std::array<float, ChunkSizeX * ChunkSizeZ> yWarp{};
                std::array<float, ChunkSizeX * ChunkSizeZ> zWarp{};
                std::array<float, ChunkSizeX * ChunkSizeZ> wWarp{};

                warpGenerator->GenPositionArray4D(
                    xWarp.data(),
                    static_cast<int>(xWarp.size()),
                    xPositions.data(),
                    yPositions.data(),
                    zPositions.data(),
                    wPositions.data(),
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    TerrainNoiseSeed + 101);
                warpGenerator->GenPositionArray4D(
                    yWarp.data(),
                    static_cast<int>(yWarp.size()),
                    xPositions.data(),
                    yPositions.data(),
                    zPositions.data(),
                    wPositions.data(),
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    TerrainNoiseSeed + 202);
                warpGenerator->GenPositionArray4D(
                    zWarp.data(),
                    static_cast<int>(zWarp.size()),
                    xPositions.data(),
                    yPositions.data(),
                    zPositions.data(),
                    wPositions.data(),
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    TerrainNoiseSeed + 303);
                warpGenerator->GenPositionArray4D(
                    wWarp.data(),
                    static_cast<int>(wWarp.size()),
                    xPositions.data(),
                    yPositions.data(),
                    zPositions.data(),
                    wPositions.data(),
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    TerrainNoiseSeed + 404);

                for (size_t i = 0; i < xPositions.size(); ++i)
                {
                    xPositions[i] += xWarp[i] * terrainDomainWarpAmplitude_;
                    yPositions[i] += yWarp[i] * terrainDomainWarpAmplitude_;
                    zPositions[i] += zWarp[i] * terrainDomainWarpAmplitude_;
                    wPositions[i] += wWarp[i] * terrainDomainWarpAmplitude_;
                }
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
            TerrainNoiseSeed);

        convertNoiseToHeights(heightLut_, noise, heights);
        return heights;
    }

    void Renderer::createTerrainBuffer(const TerrainBuildData& buildData, TerrainMesh& mesh)
    {
        if (buildData.vertices.empty() || buildData.indices.empty())
        {
            return;
        }

        mesh.vertexCount = static_cast<uint32_t>(buildData.vertices.size());
        mesh.indexCount = static_cast<uint32_t>(buildData.indices.size());

        const VkDeviceSize vertexBufferSize = sizeof(TerrainVertex) * buildData.vertices.size();
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

        const VkDeviceSize indexBufferSize = sizeof(uint32_t) * buildData.indices.size();
        createBuffer(
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            mesh.indexBuffer,
            mesh.indexMemory);

        vkMapMemory(device_, mesh.indexMemory, 0, indexBufferSize, 0, &data);
        std::memcpy(data, buildData.indices.data(), static_cast<size_t>(indexBufferSize));
        vkUnmapMemory(device_, mesh.indexMemory);
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

    void Renderer::cleanupSwapchain()
    {
        for (VkFramebuffer framebuffer : framebuffers_)
        {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
        framebuffers_.clear();

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
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
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

    Renderer::Texture Renderer::createTexture(const std::string& path)
    {
        Texture texture;
        int channels = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &texture.width, &texture.height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            throw std::runtime_error("Failed to load texture: " + path);
        }

        Texture result = createTextureFromRgba(pixels, texture.width, texture.height);
        stbi_image_free(pixels);
        return result;
    }

    Renderer::Texture Renderer::createTextureFromRgba(const unsigned char* pixels, int width, int height)
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
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
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
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
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

    Renderer::Texture Renderer::createTextureArray(const std::vector<std::string>& paths)
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
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = destination;
        barrier.size = size;
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

    void Renderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera, Vec3 cameraPosition, std::string_view fpsText, bool debugTextVisible, VkBuffer screenshotBuffer, bool showPlayer, bool terrainWireframe)
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
        SpriteRect rect;
        if (projectSkyDirection(camera, aspect, {1.0f, 0.0f, 0.0f}, rect))
        {
            drawSprite(commandBuffer, sun_, rect);
        }
        if (projectSkyDirection(camera, aspect, {-1.0f, 0.0f, 0.0f}, rect))
        {
            drawSprite(commandBuffer, moon_, rect);
        }

        drawTerrain(commandBuffer, camera, cameraPosition, terrainWireframe);
        drawBlockSelection(commandBuffer, camera, cameraPosition);
        if (showPlayer)
        {
            drawPlayer(commandBuffer, camera, cameraPosition);
        }
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

        const float crosshairPixels = 32.0f;
        SpriteRect crosshairRect{};
        crosshairRect.halfWidth = crosshairPixels / static_cast<float>(swapchainExtent_.width);
        crosshairRect.halfHeight = crosshairPixels / static_cast<float>(swapchainExtent_.height);
        drawSprite(commandBuffer, crosshair_, crosshairRect);

        if (debugTextVisible)
        {
            updateDebugTextBatch(fpsText);
            drawTextBatch(commandBuffer, debugTextBatch_);
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

    void Renderer::drawTerrain(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, bool wireframe)
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
        const Mat4 view = viewMatrix(camera, cameraPosition);
        const Mat4 mvp = multiply(projection, view);
        const Frustum frustum = makeFrustum(camera, cameraPosition, aspect);

        TerrainPush push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = 0.0f;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe ? terrainWireframePipeline_ : terrainPipeline_);
        vkCmdPushConstants(commandBuffer, terrainPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);

        uint32_t visibleDrawCount = 0;
        uint32_t visibleFaceCount = 0;
        uint32_t visibleVertexCount = 0;
        for (const auto& entry : terrainChunks_)
        {
            const ChunkRenderData& chunk = entry.second;
            const float minX = static_cast<float>(chunk.chunkX * ChunkSizeX) - 0.5f;
            const float maxX = static_cast<float>(chunk.chunkX * ChunkSizeX + ChunkSizeX) - 0.5f;
            const float minZ = static_cast<float>(chunk.chunkZ * ChunkSizeZ) - 0.5f;
            const float maxZ = static_cast<float>(chunk.chunkZ * ChunkSizeZ + ChunkSizeZ) - 0.5f;
            for (size_t subchunkY = 0; subchunkY < chunk.rockSubchunks.size(); ++subchunkY)
            {
                const TerrainMesh& mesh = chunk.rockSubchunks[subchunkY];
                if (mesh.indexCount == 0)
                {
                    continue;
                }

                const float minY = static_cast<float>(subchunkY * SubchunkSize);
                const float maxY = minY + static_cast<float>(SubchunkSize);
                if (!aabbIntersectsFrustum(frustum, {minX, minY, minZ}, {maxX, maxY, maxZ}))
                {
                    continue;
                }

                drawTerrainMesh(commandBuffer, mesh, terrainTextureArray_);
                ++visibleDrawCount;
                visibleFaceCount += mesh.indexCount / 6;
                visibleVertexCount += mesh.vertexCount;
            }
        }

        if (visibleDrawCount != terrainDrawCount_ ||
            visibleFaceCount != terrainFaceCount_ ||
            visibleVertexCount != terrainVertexCount_)
        {
            terrainDrawCount_ = visibleDrawCount;
            terrainFaceCount_ = visibleFaceCount;
            terrainVertexCount_ = visibleVertexCount;
            terrainDrawText_ = "DRAWS: " + std::to_string(terrainDrawCount_);
            terrainFaceText_ = "FACES: " + std::to_string(terrainFaceCount_);
            terrainVertexText_ = "VERTICES: " + std::to_string(terrainVertexCount_);
            debugTextBatchDirty_ = true;
        }
    }

    const Renderer::BlockDefinition& Renderer::blockDefinition(uint16_t block) const
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
        constexpr double MaxInteractionDistance = 5.0;
        constexpr double Epsilon = 0.000001;

        Vec3 normalizedDirection = normalize(direction);
        if (normalizedDirection.x == 0.0f && normalizedDirection.y == 0.0f && normalizedDirection.z == 0.0f)
        {
            return false;
        }

        int blockX = blockCoordinateXz(origin.x);
        int blockY = blockCoordinateY(origin.y);
        int blockZ = blockCoordinateXz(origin.z);
        int previousBlockX = blockX;
        int previousBlockY = blockY;
        int previousBlockZ = blockZ;

        auto axisTMax = [](double originValue, double directionValue, int block, bool vertical) -> double
        {
            if (std::abs(directionValue) <= 0.0)
            {
                return std::numeric_limits<double>::infinity();
            }

            const double boundary = vertical
                ? (directionValue > 0.0 ? static_cast<double>(block + 1) : static_cast<double>(block))
                : (directionValue > 0.0 ? static_cast<double>(block) + 0.5 : static_cast<double>(block) - 0.5);
            return (boundary - originValue) / directionValue;
        };

        const int stepX = normalizedDirection.x > 0.0f ? 1 : (normalizedDirection.x < 0.0f ? -1 : 0);
        const int stepY = normalizedDirection.y > 0.0f ? 1 : (normalizedDirection.y < 0.0f ? -1 : 0);
        const int stepZ = normalizedDirection.z > 0.0f ? 1 : (normalizedDirection.z < 0.0f ? -1 : 0);
        double tMaxX = axisTMax(origin.x, normalizedDirection.x, blockX, false);
        double tMaxY = axisTMax(origin.y, normalizedDirection.y, blockY, true);
        double tMaxZ = axisTMax(origin.z, normalizedDirection.z, blockZ, false);
        const double tDeltaX = stepX == 0 ? std::numeric_limits<double>::infinity() : 1.0 / std::abs(static_cast<double>(normalizedDirection.x));
        const double tDeltaY = stepY == 0 ? std::numeric_limits<double>::infinity() : 1.0 / std::abs(static_cast<double>(normalizedDirection.y));
        const double tDeltaZ = stepZ == 0 ? std::numeric_limits<double>::infinity() : 1.0 / std::abs(static_cast<double>(normalizedDirection.z));

        double traveled = 0.0;
        while (traveled <= MaxInteractionDistance + Epsilon)
        {
            const uint16_t block = blockAtWorld(blockX, blockY, blockZ);
            if (block != BlockAir && blockDefinition(block).renderType != BlockRenderType::None)
            {
                hit.blockX = blockX;
                hit.blockY = blockY;
                hit.blockZ = blockZ;
                hit.previousBlockX = previousBlockX;
                hit.previousBlockY = previousBlockY;
                hit.previousBlockZ = previousBlockZ;
                return true;
            }

            previousBlockX = blockX;
            previousBlockY = blockY;
            previousBlockZ = blockZ;
            if (tMaxX <= tMaxY && tMaxX <= tMaxZ)
            {
                blockX += stepX;
                traveled = tMaxX;
                tMaxX += tDeltaX;
            }
            else if (tMaxY <= tMaxZ)
            {
                blockY += stepY;
                traveled = tMaxY;
                tMaxY += tDeltaY;
            }
            else
            {
                blockZ += stepZ;
                traveled = tMaxZ;
                tMaxZ += tDeltaZ;
            }
        }

        return false;
    }

    uint16_t Renderer::blockAtWorld(int x, int y, int z) const
    {
        if (y < 0 || y >= ChunkSizeY)
        {
            return BlockAir;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        const auto chunkIt = chunkData_.find(chunkKey(chunkX, chunkZ));
        if (chunkIt == chunkData_.end() || !chunkIt->second)
        {
            return BlockAir;
        }

        const int localX = positiveModulo(x, ChunkSizeX);
        const int localZ = positiveModulo(z, ChunkSizeZ);
        const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
        if (index >= chunkIt->second->blocks.size())
        {
            return BlockAir;
        }

        return chunkIt->second->blocks[index];
    }

    bool Renderer::terrainCellBlocksPlayer(int x, int y, int z) const
    {
        if (y < 0)
        {
            return true;
        }
        if (y >= ChunkSizeY)
        {
            return false;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        const auto chunkIt = chunkData_.find(chunkKey(chunkX, chunkZ));
        if (chunkIt == chunkData_.end() || !chunkIt->second)
        {
            return true;
        }

        const int localX = positiveModulo(x, ChunkSizeX);
        const int localZ = positiveModulo(z, ChunkSizeZ);
        const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
        if (index >= chunkIt->second->blocks.size())
        {
            return true;
        }

        return blockDefinition(chunkIt->second->blocks[index]).collision;
    }

    uint32_t Renderer::blockFaceTextureLayer(uint16_t block, int face) const
    {
        if (face < 0 || face >= 6 || static_cast<size_t>(block) >= blockTextureLayers_.size())
        {
            return 0;
        }

        return blockTextureLayers_[block].faces[static_cast<size_t>(face)];
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
        const Mat4 view = viewMatrix(camera, cameraPosition);
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

    void Renderer::drawTerrainMesh(VkCommandBuffer commandBuffer, const TerrainMesh& mesh, const Texture& texture) const
    {
        if (mesh.indexCount == 0)
        {
            return;
        }

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout_, 0, 1, &texture.descriptorSet, 0, nullptr);
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
        const Mat4 view = viewMatrix(camera, cameraPosition);
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
        addText(debugTextBatch_, dataQueueText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 176.0f, false);
        addText(debugTextBatch_, meshQueueText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 154.0f, false);
        addText(debugTextBatch_, dataDoneText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 132.0f, false);
        addText(debugTextBatch_, meshDoneText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 110.0f, false);
        addText(debugTextBatch_, uploadText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 88.0f, false);
        addText(debugTextBatch_, unloadText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 66.0f, false);
        addText(debugTextBatch_, retiredText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 44.0f, false);
        addText(debugTextBatch_, jobMainText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 22.0f, false);

        debugTextBatchDirty_ = false;
        debugTextBufferDirty_ = true;
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
        if (totalVertices > MaxTextVertices)
        {
            throw std::runtime_error("Debug text vertex buffer is too small: " + std::to_string(totalVertices));
        }

        const VkDeviceSize outlineSize = sizeof(TextVertex) * batch.outline.size();
        const VkDeviceSize fillSize = sizeof(TextVertex) * batch.fill.size();
        const VkDeviceSize fillOffset = outlineSize;

        if (debugTextBufferDirty_)
        {
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
        }

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
        const stbtt_bakedchar& baked = bakedChars_[static_cast<size_t>(character - 32)];

        const float x0 = x + baked.xoff;
        const float y0 = y + baked.yoff;
        const float x1 = x0 + static_cast<float>(baked.x1 - baked.x0);
        const float y1 = y0 + static_cast<float>(baked.y1 - baked.y0);

        Glyph glyph{};
        glyph.rect.centerX = ((x0 + x1) * 0.5f / static_cast<float>(swapchainExtent_.width)) * 2.0f - 1.0f;
        glyph.rect.centerY = ((y0 + y1) * 0.5f / static_cast<float>(swapchainExtent_.height)) * 2.0f - 1.0f;
        glyph.rect.halfWidth = (x1 - x0) / static_cast<float>(swapchainExtent_.width);
        glyph.rect.halfHeight = (y1 - y0) / static_cast<float>(swapchainExtent_.height);
        glyph.uv.x = static_cast<float>(baked.x0) / static_cast<float>(FontAtlasSize);
        glyph.uv.y = static_cast<float>(baked.y0) / static_cast<float>(FontAtlasSize);
        glyph.uv.width = static_cast<float>(baked.x1 - baked.x0) / static_cast<float>(FontAtlasSize);
        glyph.uv.height = static_cast<float>(baked.y1 - baked.y0) / static_cast<float>(FontAtlasSize);
        glyph.advance = baked.xadvance;
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

            width += bakedChars_[static_cast<size_t>(character - 32)].xadvance;
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
        rect.halfWidth = 0.08f;
        rect.halfHeight = 0.08f * aspect;
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
        terrainVertexText_ = "VERTICES: 9";
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
