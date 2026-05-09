#include "renderer/Renderer.h"

#include "camera/Camera.h"
#include "platform/Log.h"
#include "platform/RuntimePaths.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/SystemInterface.h>

#include <FastNoise/FastNoise.h>
#include <stb_image.h>
#include <stb_image_write.h>

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
        constexpr int MeshingBorder = 1;
        constexpr int MeshingSizeX = ChunkSizeX + MeshingBorder * 2;
        constexpr int MeshingSizeZ = ChunkSizeZ + MeshingBorder * 2;
        constexpr size_t MeshingBlockCount = MeshingSizeX * ChunkSizeY * MeshingSizeZ;
        constexpr int LoadGridUnitChunks = 16;
        constexpr int RegionSizeChunks = 16;
        constexpr uint32_t RegionSectorSize = 4096;
        constexpr size_t RegionChunkEntrySize = 16;
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
        constexpr double PeakProfilerStartupDelaySeconds = 5.0;
        constexpr float HeightLutNoiseMin = -2.0f;
        constexpr float HeightLutNoiseMax = 2.0f;
        constexpr uint32_t HeightLutVersion = 1;
        constexpr uint32_t HeightLutCount = 1024;
        constexpr double PerformanceSampleSeconds = 0.5;
        constexpr uint16_t BlockAir = 0;
        constexpr uint16_t BlockRock = 1;
        constexpr uint16_t BlockGrass = 2;
        constexpr uint16_t BlockDirt = 3;
        constexpr uint16_t BlockSand = 4;
        constexpr uint16_t BlockTrunk = 8;
        constexpr uint16_t BlockLeaves = 9;
        constexpr uint16_t BlockPlant = 10000;
        constexpr uint16_t BlockStoneProp = 20000;
        constexpr uint16_t BlockBranchProp = 20001;
        constexpr uint16_t BlockBedrock = 65535;
        constexpr uint16_t FluidNone = 0;
        constexpr uint16_t FluidWater = 1;
        constexpr uint16_t FluidFullAmount = 100;
        constexpr uint16_t FluidHeightStepAmount = 10;
        constexpr uint16_t FluidHeightLevels = 10;
        constexpr float FluidSurfaceMaxHeight = 0.8f;
        constexpr int FluidAmountBits = 7;
        constexpr uint16_t FluidAmountMask = (1u << FluidAmountBits) - 1u;
        constexpr float FluidMipDistanceScale = 0.0f;
        constexpr uint8_t ClimateMinByte = 0;
        constexpr uint8_t ClimateMaxByte = 255;
        constexpr size_t MaxBlockBreakParticles = 2048;
        constexpr size_t MaxDroppedItems = 1024;
        constexpr uint32_t BlockBreakParticleCount = 24;
        constexpr float BlockBreakParticleGravity = 22.0f;
        constexpr float BlockBreakParticleDrag = 0.92f;
        constexpr float DroppedItemGravity = 32.0f;
        constexpr float DroppedItemDrag = 0.94f;
        constexpr float DroppedItemSize = 0.68f;
        constexpr float DroppedItemThickness = 0.05f;
        constexpr float DroppedItemTickSeconds = 1.0f / 20.0f;
        constexpr float DroppedItemMaxFrameSeconds = 0.25f;
        constexpr float DroppedItemCollisionRadius = 0.22f;
        constexpr float DroppedItemWallBounce = 0.25f;
        constexpr float DroppedItemWallFriction = 0.65f;
        constexpr float DroppedItemPickupBaseSpeed = 7.0f;
        constexpr float DroppedItemPickupAcceleration = 256.0f;
        constexpr float DroppedItemPickupMaxSpeed = 52.0f;
        constexpr size_t MaxDroppedItemRenderQuads = MaxDroppedItems * 256u;
        constexpr uint32_t BlockDropSalt = 0xD90210A5u;
        constexpr uint32_t BedrockHeightSalt = 0xBEEFBEDu;
        constexpr uint32_t TopFaceRotationSalt = 0x51A7E001u;
        constexpr uint32_t PlantPlacementSalt = 0x9A7D3E21u;
        constexpr uint8_t PlantPlacementMax = 151;
        constexpr uint8_t StonePlacementMax = 159;
        constexpr uint8_t BranchPlacementMax = 167;
        constexpr uint8_t TreePlacementMin = 168;
        constexpr uint8_t TreePlacementMax = 170;
        constexpr float RandomBlockOffsetHalfRange = 0.2f;
        constexpr size_t DpmHeaderSize = sizeof(uint32_t);
        constexpr size_t DpmQuadFloatCount = 4u * 3u + 4u * 2u + 3u;
        constexpr size_t DpmQuadRenderFloatCount = 4u * 3u + 4u * 2u;
        constexpr size_t DpmQuadSize = sizeof(float) * DpmQuadFloatCount;
        constexpr uint32_t GlbJsonChunkType = 0x4E4F534Au;
        constexpr uint32_t GlbBinChunkType = 0x004E4942u;
        constexpr float TerrainPositionPackScale = 256.0f;
        constexpr float TerrainUvPackScale = 256.0f;
        constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
        constexpr const char* VersionText = "DOLBUTO 0.0.0.1";
        constexpr std::array<const char*, 1> DeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        constexpr const char* MemoryBudgetExtension = "VK_EXT_memory_budget";
        constexpr const char* PhysicalDeviceProperties2Extension = "VK_KHR_get_physical_device_properties2";

        class RmlGlfwSystemInterface final : public Rml::SystemInterface
        {
        public:
            explicit RmlGlfwSystemInterface(GLFWwindow* window)
                : window_(window)
            {
            }

            double GetElapsedTime() override
            {
                return glfwGetTime();
            }

            void SetClipboardText(const Rml::String& text) override
            {
                if (window_ != nullptr)
                {
                    glfwSetClipboardString(window_, text.c_str());
                }
            }

            void GetClipboardText(Rml::String& text) override
            {
                text.clear();
                if (window_ == nullptr)
                {
                    return;
                }

                const char* clipboard = glfwGetClipboardString(window_);
                if (clipboard != nullptr)
                {
                    text = clipboard;
                }
            }

        private:
            GLFWwindow* window_ = nullptr;
        };

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
            std::string name = "unknown";
            std::string renderType = "none";
            bool directional = false;
            bool collision = false;
            bool ao = false;
            std::string faceOcclusion = "none";
            bool sameBlockFaceCulling = false;
            std::string alphaMode = "opaque";
            float alphaCutoff = 0.5f;
            float mipDistanceScale = 1.0f;
            bool randomOffset = false;
            std::unordered_map<std::string, std::string> textures;
            std::string propModel;
            std::string propTexture;
            std::vector<std::string> dropItemKeys;
            std::vector<uint16_t> dropMins;
            std::vector<uint16_t> dropMaxes;
            std::vector<float> dropChances;
        };

        struct ParsedItemDefinition
        {
            uint16_t id = 0;
            std::string key = "none";
            std::string name = "None";
            uint16_t stackSize = 0;
            std::string texture = "none";
            std::string slotTexture = "none";
            std::string droppedTexture = "none";
            std::string heldTexture = "none";
            std::string droppedRender = "extruded_sprite";
            std::string heldRender = "extruded_sprite";
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

        std::optional<std::array<float, 2>> jsonFloat2Field(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t openPos = object.find('[', keyPos + token.size());
            const size_t closePos = object.find(']', openPos == std::string::npos ? keyPos + token.size() : openPos + 1);
            if (openPos == std::string::npos || closePos == std::string::npos)
            {
                return std::nullopt;
            }

            std::array<float, 2> values{};
            size_t cursor = openPos + 1;
            for (size_t i = 0; i < values.size(); ++i)
            {
                cursor = object.find_first_not_of(" \t\r\n,", cursor);
                if (cursor == std::string::npos || cursor >= closePos)
                {
                    return std::nullopt;
                }

                size_t valueEnd = cursor;
                while (valueEnd < closePos)
                {
                    const char c = object[valueEnd];
                    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
                    {
                        ++valueEnd;
                        continue;
                    }
                    break;
                }
                if (valueEnd == cursor)
                {
                    return std::nullopt;
                }

                try
                {
                    values[i] = std::stof(object.substr(cursor, valueEnd - cursor));
                }
                catch (...)
                {
                    return std::nullopt;
                }
                cursor = valueEnd;
            }

            return values;
        }

        std::optional<std::array<float, 3>> jsonFloat3Field(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t openPos = object.find('[', keyPos + token.size());
            const size_t closePos = object.find(']', openPos == std::string::npos ? keyPos + token.size() : openPos + 1);
            if (openPos == std::string::npos || closePos == std::string::npos)
            {
                return std::nullopt;
            }

            std::array<float, 3> values{};
            size_t cursor = openPos + 1;
            for (size_t i = 0; i < values.size(); ++i)
            {
                cursor = object.find_first_not_of(" \t\r\n,", cursor);
                if (cursor == std::string::npos || cursor >= closePos)
                {
                    return std::nullopt;
                }

                size_t valueEnd = cursor;
                while (valueEnd < closePos)
                {
                    const char c = object[valueEnd];
                    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
                    {
                        ++valueEnd;
                        continue;
                    }
                    break;
                }
                if (valueEnd == cursor)
                {
                    return std::nullopt;
                }

                try
                {
                    values[i] = std::stof(object.substr(cursor, valueEnd - cursor));
                }
                catch (...)
                {
                    return std::nullopt;
                }
                cursor = valueEnd;
            }

            return values;
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

        std::optional<std::string> jsonArrayField(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t openPos = object.find('[', keyPos + token.size());
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
                else if (c == '[')
                {
                    ++depth;
                }
                else if (c == ']')
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

        std::vector<std::string> jsonObjectMemberObjects(const std::string& object)
        {
            const size_t open = object.find('{');
            const size_t close = object.rfind('}');
            if (open == std::string::npos || close == std::string::npos || close <= open)
            {
                return {};
            }

            return jsonTopLevelObjects(object.substr(open + 1u, close - open - 1u));
        }

        std::optional<std::string> jsonTopLevelArrayField(const std::string& object, const std::string& key)
        {
            const size_t open = object.find('{');
            const size_t close = object.rfind('}');
            if (open == std::string::npos || close == std::string::npos || close <= open)
            {
                return std::nullopt;
            }

            const std::string token = "\"" + key + "\"";
            size_t cursor = open + 1u;
            int depth = 0;
            bool inString = false;
            bool escaped = false;
            while (cursor < close)
            {
                const char c = object[cursor];
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
                    ++cursor;
                    continue;
                }

                if (c == '"')
                {
                    if (depth == 0 && object.compare(cursor, token.size(), token) == 0)
                    {
                        const size_t colon = object.find(':', cursor + token.size());
                        if (colon == std::string::npos)
                        {
                            return std::nullopt;
                        }
                        const size_t arrayOpen = object.find_first_not_of(" \t\r\n", colon + 1u);
                        if (arrayOpen == std::string::npos || arrayOpen >= object.size() || object[arrayOpen] != '[')
                        {
                            return std::nullopt;
                        }

                        int arrayDepth = 0;
                        bool arrayInString = false;
                        bool arrayEscaped = false;
                        for (size_t i = arrayOpen; i < object.size(); ++i)
                        {
                            const char arrayChar = object[i];
                            if (arrayInString)
                            {
                                if (arrayEscaped)
                                {
                                    arrayEscaped = false;
                                }
                                else if (arrayChar == '\\')
                                {
                                    arrayEscaped = true;
                                }
                                else if (arrayChar == '"')
                                {
                                    arrayInString = false;
                                }
                                continue;
                            }
                            if (arrayChar == '"')
                            {
                                arrayInString = true;
                            }
                            else if (arrayChar == '[')
                            {
                                ++arrayDepth;
                            }
                            else if (arrayChar == ']')
                            {
                                --arrayDepth;
                                if (arrayDepth == 0)
                                {
                                    return object.substr(arrayOpen, i - arrayOpen + 1u);
                                }
                            }
                        }
                        return std::nullopt;
                    }
                    inString = true;
                }
                else if (c == '{' || c == '[')
                {
                    ++depth;
                }
                else if (c == '}' || c == ']')
                {
                    --depth;
                }
                ++cursor;
            }
            return std::nullopt;
        }

        std::vector<ParsedItemDefinition> parseItemDefinitions(const std::string& text)
        {
            std::vector<ParsedItemDefinition> definitions;

            for (const std::string& object : jsonTopLevelObjects(text))
            {
                const std::optional<int> id = jsonIntField(object, "id");
                if (!id.has_value() || *id < 0 || *id > std::numeric_limits<uint16_t>::max())
                {
                    continue;
                }

                ParsedItemDefinition definition{};
                definition.id = static_cast<uint16_t>(*id);
                if (const std::optional<std::string> key = jsonStringField(object, "key"); key.has_value())
                {
                    definition.key = *key;
                }
                if (const std::optional<std::string> name = jsonStringField(object, "name"); name.has_value())
                {
                    definition.name = *name;
                }
                if (const std::optional<int> stackSize = jsonIntField(object, "stackSize"); stackSize.has_value())
                {
                    definition.stackSize = static_cast<uint16_t>(std::clamp(*stackSize, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
                }
                if (const std::optional<std::string> texture = jsonStringField(object, "texture"); texture.has_value())
                {
                    definition.texture = *texture;
                }
                if (const std::optional<std::string> slotTexture = jsonStringField(object, "slotTexture"); slotTexture.has_value())
                {
                    definition.slotTexture = *slotTexture;
                }
                if (const std::optional<std::string> render = jsonObjectField(object, "render"); render.has_value())
                {
                    if (const std::optional<std::string> type = jsonStringField(*render, "type"); type.has_value())
                    {
                        definition.droppedRender = *type;
                        definition.heldRender = *type;
                    }
                    if (const std::optional<std::string> texture = jsonStringField(*render, "texture"); texture.has_value())
                    {
                        definition.droppedTexture = *texture;
                        definition.heldTexture = *texture;
                    }
                    if (const std::optional<std::string> dropped = jsonStringField(*render, "dropped"); dropped.has_value())
                    {
                        definition.droppedRender = *dropped;
                    }
                    if (const std::optional<std::string> held = jsonStringField(*render, "held"); held.has_value())
                    {
                        definition.heldRender = *held;
                    }
                }
                if (const std::optional<std::string> droppedRender = jsonObjectField(object, "droppedRender"); droppedRender.has_value())
                {
                    if (const std::optional<std::string> type = jsonStringField(*droppedRender, "type"); type.has_value())
                    {
                        definition.droppedRender = *type;
                    }
                    if (const std::optional<std::string> texture = jsonStringField(*droppedRender, "texture"); texture.has_value())
                    {
                        definition.droppedTexture = *texture;
                    }
                }
                if (const std::optional<std::string> heldRender = jsonObjectField(object, "heldRender"); heldRender.has_value())
                {
                    if (const std::optional<std::string> type = jsonStringField(*heldRender, "type"); type.has_value())
                    {
                        definition.heldRender = *type;
                    }
                    if (const std::optional<std::string> texture = jsonStringField(*heldRender, "texture"); texture.has_value())
                    {
                        definition.heldTexture = *texture;
                    }
                }

                definitions.push_back(std::move(definition));
            }

            return definitions;
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
                if (const std::optional<std::string> name = jsonStringField(object, "name"); name.has_value())
                {
                    definition.name = *name;
                }
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
                if (const std::optional<bool> randomOffset = jsonBoolField(object, "randomOffset"); randomOffset.has_value())
                {
                    definition.randomOffset = *randomOffset;
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
                if (const std::optional<std::string> prop = jsonObjectField(object, "prop"); prop.has_value())
                {
                    if (const std::optional<std::string> model = jsonStringField(*prop, "model"); model.has_value())
                    {
                        definition.propModel = *model;
                    }
                    if (const std::optional<std::string> texture = jsonStringField(*prop, "texture"); texture.has_value())
                    {
                        definition.propTexture = *texture;
                    }
                }
                if (const std::optional<std::string> drops = jsonArrayField(object, "drops"); drops.has_value())
                {
                    for (const std::string& dropObject : jsonTopLevelObjects(*drops))
                    {
                        const std::optional<std::string> item = jsonStringField(dropObject, "item");
                        if (!item.has_value() || item->empty())
                        {
                            continue;
                        }

                        const int minCount = jsonIntField(dropObject, "min").value_or(1);
                        const int maxCount = jsonIntField(dropObject, "max").value_or(minCount);
                        const float chance = jsonFloatField(dropObject, "chance").value_or(1.0f);
                        definition.dropItemKeys.push_back(*item);
                        definition.dropMins.push_back(static_cast<uint16_t>(std::clamp(minCount, 0, static_cast<int>(std::numeric_limits<uint16_t>::max()))));
                        definition.dropMaxes.push_back(static_cast<uint16_t>(std::clamp(maxCount, 0, static_cast<int>(std::numeric_limits<uint16_t>::max()))));
                        definition.dropChances.push_back(std::clamp(chance, 0.0f, 1.0f));
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

        int positiveModulo(int value, int divisor)
        {
            int result = value % divisor;
            return result < 0 ? result + divisor : result;
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

        int bedrockHeightAt(int worldX, int worldZ)
        {
            return 1 + static_cast<int>(worldRandom8(worldX, 0, worldZ, BedrockHeightSalt) & 3u);
        }

        constexpr uint16_t packFluid(uint16_t id, uint16_t amount)
        {
            return static_cast<uint16_t>((id << FluidAmountBits) | amount);
        }

        constexpr uint16_t fluidId(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid >> FluidAmountBits);
        }

        constexpr uint16_t fluidAmount(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid & FluidAmountMask);
        }

        constexpr float fluidSurfaceHeight(uint16_t amount)
        {
            const uint16_t clampedAmount = amount > FluidFullAmount ? FluidFullAmount : amount;
            if (clampedAmount == 0)
            {
                return 0.0f;
            }
            const uint16_t level = static_cast<uint16_t>((clampedAmount + FluidHeightStepAmount - 1u) / FluidHeightStepAmount);
            return (static_cast<float>(level) / static_cast<float>(FluidHeightLevels)) * FluidSurfaceMaxHeight;
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

        uint16_t baseTerrainBlockForColumn(int worldX, int y, int worldZ, int height)
        {
            if (y < 0 || y >= height)
            {
                return BlockAir;
            }
            if (y < bedrockHeightAt(worldX, worldZ))
            {
                return BlockBedrock;
            }
            return BlockRock;
        }

        uint16_t generatedTerrainBlockForColumn(int worldX, int y, int worldZ, int height, int seaLevel)
        {
            const uint16_t baseBlock = baseTerrainBlockForColumn(worldX, y, worldZ, height);
            if (baseBlock != BlockRock)
            {
                return baseBlock;
            }

            const bool waterAbove = height <= seaLevel;
            if (y == height - 1)
            {
                return waterAbove ? BlockSand : BlockGrass;
            }
            if (y >= height - 5)
            {
                return waterAbove ? BlockSand : BlockDirt;
            }
            return BlockRock;
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

        uint64_t storageChunkKey(int chunkX, int chunkZ)
        {
            return chunkKey(wrapChunkCoordinate(chunkX), wrapChunkCoordinate(chunkZ));
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

        void writeU8(std::vector<uint8_t>& bytes, uint8_t value)
        {
            bytes.push_back(value);
        }

        void writeU16(std::vector<uint8_t>& bytes, uint16_t value)
        {
            bytes.push_back(static_cast<uint8_t>(value & 0xFFu));
            bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
        }

        void writeU32(std::vector<uint8_t>& bytes, uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
            {
                bytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
            }
        }

        void writeU64(std::vector<uint8_t>& bytes, uint64_t value)
        {
            for (int i = 0; i < 8; ++i)
            {
                bytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
            }
        }

        uint8_t readU8(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            if (offset >= bytes.size())
            {
                throw std::runtime_error("Chunk payload read overflow.");
            }
            return bytes[offset++];
        }

        uint16_t readU16(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            if (offset + 2 > bytes.size())
            {
                throw std::runtime_error("Chunk payload read overflow.");
            }
            const uint16_t value = static_cast<uint16_t>(bytes[offset]) |
                static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8u);
            offset += 2;
            return value;
        }

        uint32_t readU32(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            if (offset + 4 > bytes.size())
            {
                throw std::runtime_error("Chunk payload read overflow.");
            }
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                value |= static_cast<uint32_t>(bytes[offset + static_cast<size_t>(i)]) << (i * 8);
            }
            offset += 4;
            return value;
        }

        uint64_t readU64(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            if (offset + 8 > bytes.size())
            {
                throw std::runtime_error("Chunk payload read overflow.");
            }
            uint64_t value = 0;
            for (int i = 0; i < 8; ++i)
            {
                value |= static_cast<uint64_t>(bytes[offset + static_cast<size_t>(i)]) << (i * 8);
            }
            offset += 8;
            return value;
        }

        void writeU32At(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
            {
                bytes[offset + static_cast<size_t>(i)] = static_cast<uint8_t>((value >> (i * 8)) & 0xFFu);
            }
        }

        uint32_t readU32At(const std::vector<uint8_t>& bytes, size_t offset)
        {
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                value |= static_cast<uint32_t>(bytes[offset + static_cast<size_t>(i)]) << (i * 8);
            }
            return value;
        }

        void writeF32(std::vector<uint8_t>& bytes, float value)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            writeU32(bytes, bits);
        }

        float readF32At(const std::vector<uint8_t>& bytes, size_t offset)
        {
            float value = 0.0f;
            if (offset + sizeof(value) <= bytes.size())
            {
                std::memcpy(&value, bytes.data() + offset, sizeof(value));
            }
            return value;
        }

        std::optional<std::string> jsonStringAt(const std::string& text, size_t& cursor)
        {
            cursor = text.find('"', cursor);
            if (cursor == std::string::npos)
            {
                return std::nullopt;
            }

            std::string value;
            bool escaped = false;
            for (size_t i = cursor + 1; i < text.size(); ++i)
            {
                const char c = text[i];
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
                    cursor = i + 1;
                    return value;
                }
                value.push_back(c);
            }
            return std::nullopt;
        }

        std::vector<float> jsonFloatArrayValues(const std::string& array)
        {
            std::vector<float> values;
            const size_t openPos = array.find('[');
            const size_t closePos = array.rfind(']');
            if (openPos == std::string::npos || closePos == std::string::npos || closePos <= openPos)
            {
                return values;
            }

            size_t cursor = openPos + 1;
            while (cursor < closePos)
            {
                cursor = array.find_first_not_of(" \t\r\n,", cursor);
                if (cursor == std::string::npos || cursor >= closePos)
                {
                    break;
                }

                size_t valueEnd = cursor;
                while (valueEnd < closePos)
                {
                    const char c = array[valueEnd];
                    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
                    {
                        ++valueEnd;
                        continue;
                    }
                    break;
                }
                if (valueEnd == cursor)
                {
                    break;
                }

                try
                {
                    values.push_back(std::stof(array.substr(cursor, valueEnd - cursor)));
                }
                catch (...)
                {
                    values.clear();
                    return values;
                }
                cursor = valueEnd;
            }
            return values;
        }

        std::vector<std::string> jsonStringArrayValues(const std::string& array)
        {
            std::vector<std::string> values;
            const size_t openPos = array.find('[');
            const size_t closePos = array.rfind(']');
            if (openPos == std::string::npos || closePos == std::string::npos || closePos <= openPos)
            {
                return values;
            }

            size_t cursor = openPos + 1;
            while (cursor < closePos)
            {
                cursor = array.find_first_not_of(" \t\r\n,", cursor);
                if (cursor == std::string::npos || cursor >= closePos)
                {
                    break;
                }
                std::optional<std::string> value = jsonStringAt(array, cursor);
                if (!value)
                {
                    break;
                }
                values.push_back(*value);
            }
            return values;
        }

        std::optional<std::string> jsonArrayAt(const std::string& text, size_t openPos, size_t& next)
        {
            if (openPos == std::string::npos || openPos >= text.size() || text[openPos] != '[')
            {
                return std::nullopt;
            }

            int depth = 0;
            bool inString = false;
            bool escaped = false;
            for (size_t i = openPos; i < text.size(); ++i)
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
                else if (c == '[')
                {
                    ++depth;
                }
                else if (c == ']')
                {
                    --depth;
                    if (depth == 0)
                    {
                        next = i + 1;
                        return text.substr(openPos, i - openPos + 1);
                    }
                }
            }
            return std::nullopt;
        }

        std::unordered_map<std::string, std::array<float, 3>> jsonFloat3Map(const std::string& object)
        {
            std::unordered_map<std::string, std::array<float, 3>> values;
            size_t cursor = 0;
            while (cursor < object.size())
            {
                std::optional<std::string> key = jsonStringAt(object, cursor);
                if (!key)
                {
                    break;
                }
                const size_t colon = object.find(':', cursor);
                const size_t open = object.find('[', colon == std::string::npos ? cursor : colon + 1);
                size_t next = 0;
                std::optional<std::string> array = jsonArrayAt(object, open, next);
                if (!array)
                {
                    break;
                }
                const std::vector<float> parsed = jsonFloatArrayValues(*array);
                if (parsed.size() >= 3)
                {
                    values[*key] = {parsed[0], parsed[1], parsed[2]};
                }
                cursor = next;
            }
            return values;
        }

        std::unordered_map<std::string, std::array<float, 2>> jsonFloat2Map(const std::string& object)
        {
            std::unordered_map<std::string, std::array<float, 2>> values;
            size_t cursor = 0;
            while (cursor < object.size())
            {
                std::optional<std::string> key = jsonStringAt(object, cursor);
                if (!key)
                {
                    break;
                }
                const size_t colon = object.find(':', cursor);
                const size_t open = object.find('[', colon == std::string::npos ? cursor : colon + 1);
                size_t next = 0;
                std::optional<std::string> array = jsonArrayAt(object, open, next);
                if (!array)
                {
                    break;
                }
                const std::vector<float> parsed = jsonFloatArrayValues(*array);
                if (parsed.size() >= 2)
                {
                    values[*key] = {parsed[0], parsed[1]};
                }
                cursor = next;
            }
            return values;
        }

        struct PropVertex
        {
            std::array<float, 3> position{};
            std::array<float, 2> uv{};
            std::array<float, 3> normal{};
        };

        struct PropMeshData
        {
            std::vector<PropVertex> vertices;
            std::vector<uint32_t> indices;
        };

        struct PropQuad
        {
            std::array<PropVertex, 4> vertices{};
            std::array<float, 3> normal{};
        };

        struct GlbAccessor
        {
            int bufferView = -1;
            int byteOffset = 0;
            int componentType = 0;
            int count = 0;
            std::string type;
        };

        struct GlbBufferView
        {
            int byteOffset = 0;
            int byteLength = 0;
            int byteStride = 0;
        };

        struct GlbNode
        {
            int mesh = -1;
            std::vector<int> children;
            std::array<float, 3> translation{0.0f, 0.0f, 0.0f};
            std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
        };

        struct GlbPrimitive
        {
            int position = -1;
            int normal = -1;
            int uv = -1;
            int indices = -1;
        };

        struct GlbMesh
        {
            std::vector<GlbPrimitive> primitives;
        };

        std::array<float, 3> glbPositionToBlockLocal(std::array<float, 3> value)
        {
            return {
                (value[0] + 8.0f) / 16.0f,
                value[1] / 16.0f,
                (value[2] + 8.0f) / 16.0f};
        }

        std::array<float, 3> rotateByQuaternion(std::array<float, 3> value, std::array<float, 4> q)
        {
            const std::array<float, 3> u{q[0], q[1], q[2]};
            const float s = q[3];
            const std::array<float, 3> cross1{
                u[1] * value[2] - u[2] * value[1],
                u[2] * value[0] - u[0] * value[2],
                u[0] * value[1] - u[1] * value[0]};
            const std::array<float, 3> cross2{
                u[1] * cross1[2] - u[2] * cross1[1],
                u[2] * cross1[0] - u[0] * cross1[2],
                u[0] * cross1[1] - u[1] * cross1[0]};
            return {
                value[0] + 2.0f * (s * cross1[0] + cross2[0]),
                value[1] + 2.0f * (s * cross1[1] + cross2[1]),
                value[2] + 2.0f * (s * cross1[2] + cross2[2])};
        }

        std::array<float, 3> transformPoint(std::array<float, 3> value, const std::vector<int>& chain, const std::vector<GlbNode>& nodes)
        {
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                const GlbNode& node = nodes[static_cast<size_t>(*it)];
                value = rotateByQuaternion(value, node.rotation);
                value[0] += node.translation[0];
                value[1] += node.translation[1];
                value[2] += node.translation[2];
            }
            return value;
        }

        std::optional<std::string> jsonObjectAt(const std::vector<std::string>& objects, int index)
        {
            if (index < 0 || static_cast<size_t>(index) >= objects.size())
            {
                return std::nullopt;
            }
            return objects[static_cast<size_t>(index)];
        }

        std::vector<uint8_t> glbChunk(const std::vector<uint8_t>& bytes, uint32_t expectedType)
        {
            if (bytes.size() < 12 || bytes[0] != 'g' || bytes[1] != 'l' || bytes[2] != 'T' || bytes[3] != 'F')
            {
                return {};
            }
            size_t offset = 12;
            while (offset + 8 <= bytes.size())
            {
                const uint32_t chunkLength = readU32At(bytes, offset);
                const uint32_t chunkType = readU32At(bytes, offset + 4u);
                offset += 8u;
                if (offset + chunkLength > bytes.size())
                {
                    return {};
                }
                if (chunkType == expectedType)
                {
                    return std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunkLength));
                }
                offset += chunkLength;
            }
            return {};
        }

        GlbAccessor parseGlbAccessor(const std::string& object)
        {
            GlbAccessor accessor{};
            accessor.bufferView = jsonIntField(object, "bufferView").value_or(-1);
            accessor.byteOffset = jsonIntField(object, "byteOffset").value_or(0);
            accessor.componentType = jsonIntField(object, "componentType").value_or(0);
            accessor.count = jsonIntField(object, "count").value_or(0);
            accessor.type = jsonStringField(object, "type").value_or("");
            return accessor;
        }

        GlbBufferView parseGlbBufferView(const std::string& object)
        {
            GlbBufferView view{};
            view.byteOffset = jsonIntField(object, "byteOffset").value_or(0);
            view.byteLength = jsonIntField(object, "byteLength").value_or(0);
            view.byteStride = jsonIntField(object, "byteStride").value_or(0);
            return view;
        }

        GlbNode parseGlbNode(const std::string& object)
        {
            GlbNode node{};
            node.mesh = jsonIntField(object, "mesh").value_or(-1);
            node.translation = jsonFloat3Field(object, "translation").value_or(node.translation);
            if (const std::optional<std::string> rotation = jsonArrayField(object, "rotation"); rotation.has_value())
            {
                const std::vector<float> values = jsonFloatArrayValues(*rotation);
                if (values.size() >= 4)
                {
                    node.rotation = {values[0], values[1], values[2], values[3]};
                }
            }
            if (const std::optional<std::string> children = jsonArrayField(object, "children"); children.has_value())
            {
                const std::vector<float> values = jsonFloatArrayValues(*children);
                for (float value : values)
                {
                    node.children.push_back(static_cast<int>(std::lround(value)));
                }
            }
            return node;
        }

        GlbMesh parseGlbMesh(const std::string& object)
        {
            GlbMesh mesh{};
            const std::optional<std::string> primitives = jsonArrayField(object, "primitives");
            if (!primitives)
            {
                return mesh;
            }
            for (const std::string& primitiveObject : jsonTopLevelObjects(*primitives))
            {
                GlbPrimitive primitive{};
                if (jsonIntField(primitiveObject, "mode").value_or(4) != 4)
                {
                    continue;
                }
                primitive.indices = jsonIntField(primitiveObject, "indices").value_or(-1);
                if (const std::optional<std::string> attributes = jsonObjectField(primitiveObject, "attributes"); attributes.has_value())
                {
                    primitive.position = jsonIntField(*attributes, "POSITION").value_or(-1);
                    primitive.normal = jsonIntField(*attributes, "NORMAL").value_or(-1);
                    primitive.uv = jsonIntField(*attributes, "TEXCOORD_0").value_or(-1);
                }
                if (primitive.position >= 0 && primitive.uv >= 0 && primitive.indices >= 0)
                {
                    mesh.primitives.push_back(primitive);
                }
            }
            return mesh;
        }

        std::array<float, 3> readGlbVec3(const std::vector<uint8_t>& bin, const std::vector<GlbAccessor>& accessors, const std::vector<GlbBufferView>& views, int accessorIndex, int elementIndex)
        {
            const GlbAccessor& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const GlbBufferView& view = views[static_cast<size_t>(accessor.bufferView)];
            const int stride = view.byteStride > 0 ? view.byteStride : 12;
            const size_t offset = static_cast<size_t>(view.byteOffset + accessor.byteOffset + elementIndex * stride);
            return {readF32At(bin, offset), readF32At(bin, offset + 4u), readF32At(bin, offset + 8u)};
        }

        std::array<float, 2> readGlbVec2(const std::vector<uint8_t>& bin, const std::vector<GlbAccessor>& accessors, const std::vector<GlbBufferView>& views, int accessorIndex, int elementIndex)
        {
            const GlbAccessor& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const GlbBufferView& view = views[static_cast<size_t>(accessor.bufferView)];
            const int stride = view.byteStride > 0 ? view.byteStride : 8;
            const size_t offset = static_cast<size_t>(view.byteOffset + accessor.byteOffset + elementIndex * stride);
            return {readF32At(bin, offset), readF32At(bin, offset + 4u)};
        }

        uint32_t readGlbIndex(const std::vector<uint8_t>& bin, const std::vector<GlbAccessor>& accessors, const std::vector<GlbBufferView>& views, int accessorIndex, int elementIndex)
        {
            const GlbAccessor& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const GlbBufferView& view = views[static_cast<size_t>(accessor.bufferView)];
            const size_t offset = static_cast<size_t>(view.byteOffset + accessor.byteOffset);
            if (accessor.componentType == 5125)
            {
                return readU32At(bin, offset + static_cast<size_t>(elementIndex) * 4u);
            }
            if (accessor.componentType == 5123)
            {
                size_t readOffset = offset + static_cast<size_t>(elementIndex) * 2u;
                return readOffset + 1u < bin.size() ? static_cast<uint32_t>(bin[readOffset]) | (static_cast<uint32_t>(bin[readOffset + 1u]) << 8u) : 0u;
            }
            return offset + static_cast<size_t>(elementIndex) < bin.size() ? bin[offset + static_cast<size_t>(elementIndex)] : 0u;
        }

        std::array<float, 3> cross3(std::array<float, 3> a, std::array<float, 3> b)
        {
            return {
                a[1] * b[2] - a[2] * b[1],
                a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0]};
        }

        std::array<float, 3> subtract3(std::array<float, 3> a, std::array<float, 3> b)
        {
            return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
        }

        float lengthSquared3(std::array<float, 3> value)
        {
            return value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
        }

        std::array<float, 3> normalize3(std::array<float, 3> value)
        {
            const float length = std::sqrt(lengthSquared3(value));
            if (length <= 0.000001f)
            {
                return {0.0f, 1.0f, 0.0f};
            }
            return {value[0] / length, value[1] / length, value[2] / length};
        }

        float quadOrderError(const std::array<PropVertex, 4>& vertices, const std::array<int, 4>& order)
        {
            const std::array<float, 3>& a = vertices[static_cast<size_t>(order[0])].position;
            const std::array<float, 3>& b = vertices[static_cast<size_t>(order[1])].position;
            const std::array<float, 3>& c = vertices[static_cast<size_t>(order[2])].position;
            const std::array<float, 3>& d = vertices[static_cast<size_t>(order[3])].position;
            const std::array<float, 3> predicted{b[0] + d[0] - a[0], b[1] + d[1] - a[1], b[2] + d[2] - a[2]};
            return lengthSquared3(subtract3(predicted, c));
        }

        std::optional<PropQuad> mergeTrianglePairToQuad(const PropMeshData& mesh, size_t indexOffset)
        {
            if (indexOffset + 5u >= mesh.indices.size())
            {
                return std::nullopt;
            }

            std::array<PropVertex, 6> triangleVertices{};
            for (size_t i = 0; i < triangleVertices.size(); ++i)
            {
                const uint32_t index = mesh.indices[indexOffset + i];
                if (static_cast<size_t>(index) >= mesh.vertices.size())
                {
                    return std::nullopt;
                }
                triangleVertices[i] = mesh.vertices[static_cast<size_t>(index)];
            }

            std::array<PropVertex, 4> uniqueVertices{};
            size_t uniqueCount = 0;
            auto samePositionAndUv = [](const PropVertex& a, const PropVertex& b)
            {
                return lengthSquared3(subtract3(a.position, b.position)) <= 0.0000001f &&
                    std::abs(a.uv[0] - b.uv[0]) <= 0.0001f &&
                    std::abs(a.uv[1] - b.uv[1]) <= 0.0001f;
            };
            for (const PropVertex& vertex : triangleVertices)
            {
                bool exists = false;
                for (size_t i = 0; i < uniqueCount; ++i)
                {
                    if (samePositionAndUv(uniqueVertices[i], vertex))
                    {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                {
                    if (uniqueCount >= uniqueVertices.size())
                    {
                        return std::nullopt;
                    }
                    uniqueVertices[uniqueCount++] = vertex;
                }
            }
            if (uniqueCount != 4u)
            {
                return std::nullopt;
            }

            std::array<int, 4> bestOrder{0, 1, 2, 3};
            float bestError = std::numeric_limits<float>::max();
            std::array<int, 4> order{0, 1, 2, 3};
            do
            {
                const std::array<float, 3> edgeU = subtract3(uniqueVertices[static_cast<size_t>(order[1])].position, uniqueVertices[static_cast<size_t>(order[0])].position);
                const std::array<float, 3> edgeV = subtract3(uniqueVertices[static_cast<size_t>(order[3])].position, uniqueVertices[static_cast<size_t>(order[0])].position);
                if (lengthSquared3(edgeU) <= 0.0000001f || lengthSquared3(edgeV) <= 0.0000001f || lengthSquared3(cross3(edgeU, edgeV)) <= 0.0000001f)
                {
                    continue;
                }
                const float error = quadOrderError(uniqueVertices, order);
                if (error < bestError)
                {
                    bestError = error;
                    bestOrder = order;
                }
            } while (std::next_permutation(order.begin(), order.end()));

            if (bestError > 0.000001f)
            {
                return std::nullopt;
            }

            PropQuad quad{};
            for (size_t i = 0; i < quad.vertices.size(); ++i)
            {
                quad.vertices[i] = uniqueVertices[static_cast<size_t>(bestOrder[i])];
            }
            quad.normal = normalize3(cross3(
                subtract3(quad.vertices[1].position, quad.vertices[0].position),
                subtract3(quad.vertices[3].position, quad.vertices[0].position)));
            return quad;
        }

        std::vector<PropQuad> convertTrianglesToQuads(const PropMeshData& mesh)
        {
            std::vector<PropQuad> quads;
            quads.reserve(mesh.indices.size() / 6u);
            for (size_t offset = 0; offset + 5u < mesh.indices.size(); offset += 6u)
            {
                if (std::optional<PropQuad> quad = mergeTrianglePairToQuad(mesh, offset); quad.has_value())
                {
                    quads.push_back(*quad);
                }
            }
            return quads;
        }

        bool writeDpm(const std::filesystem::path& path, const std::vector<PropQuad>& quads)
        {
            if (quads.empty())
            {
                return false;
            }
            std::vector<uint8_t> bytes;
            bytes.reserve(DpmHeaderSize + quads.size() * DpmQuadSize);
            writeU32(bytes, static_cast<uint32_t>(quads.size()));
            for (const PropQuad& quad : quads)
            {
                for (const PropVertex& vertex : quad.vertices)
                {
                    for (float value : vertex.position) { writeF32(bytes, value); }
                }
                for (const PropVertex& vertex : quad.vertices)
                {
                    for (float value : vertex.uv) { writeF32(bytes, value); }
                }
                for (float value : quad.normal) { writeF32(bytes, value); }
            }
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                return false;
            }
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return static_cast<bool>(file);
        }

        bool dpmLooksValid(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                return false;
            }
            const auto fileSize = static_cast<size_t>(file.tellg());
            if (fileSize < DpmHeaderSize)
            {
                return false;
            }
            std::vector<uint8_t> header(DpmHeaderSize);
            file.seekg(0);
            file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
            if (!file)
            {
                return false;
            }
            const uint32_t quadCount = readU32At(header, 0);
            return fileSize == DpmHeaderSize + static_cast<size_t>(quadCount) * DpmQuadSize;
        }

        PropMeshData convertGlbToDpmMesh(const std::filesystem::path& glbPath)
        {
            const std::vector<char> file = readFile(glbPath.string());
            const std::vector<uint8_t> bytes(file.begin(), file.end());
            const std::vector<uint8_t> jsonBytes = glbChunk(bytes, GlbJsonChunkType);
            const std::vector<uint8_t> bin = glbChunk(bytes, GlbBinChunkType);
            if (jsonBytes.empty() || bin.empty())
            {
                return {};
            }
            std::string json(jsonBytes.begin(), jsonBytes.end());
            while (!json.empty() && (json.back() == '\0' || json.back() == ' '))
            {
                json.pop_back();
            }

            std::vector<GlbAccessor> accessors;
            std::vector<GlbBufferView> views;
            std::vector<GlbNode> nodes;
            std::vector<GlbMesh> meshes;
            for (const std::string& object : jsonTopLevelObjects(jsonTopLevelArrayField(json, "accessors").value_or("[]")))
            {
                accessors.push_back(parseGlbAccessor(object));
            }
            for (const std::string& object : jsonTopLevelObjects(jsonTopLevelArrayField(json, "bufferViews").value_or("[]")))
            {
                views.push_back(parseGlbBufferView(object));
            }
            for (const std::string& object : jsonTopLevelObjects(jsonTopLevelArrayField(json, "nodes").value_or("[]")))
            {
                nodes.push_back(parseGlbNode(object));
            }
            for (const std::string& object : jsonTopLevelObjects(jsonTopLevelArrayField(json, "meshes").value_or("[]")))
            {
                meshes.push_back(parseGlbMesh(object));
            }

            PropMeshData mesh;
            std::vector<bool> hasParent(nodes.size(), false);
            for (const GlbNode& node : nodes)
            {
                for (int child : node.children)
                {
                    if (child >= 0 && static_cast<size_t>(child) < hasParent.size())
                    {
                        hasParent[static_cast<size_t>(child)] = true;
                    }
                }
            }

            auto visitNode = [&](auto&& self, int nodeIndex, std::vector<int>& chain) -> void
            {
                if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= nodes.size())
                {
                    return;
                }
                chain.push_back(nodeIndex);
                const GlbNode& node = nodes[static_cast<size_t>(nodeIndex)];
                if (node.mesh >= 0 && static_cast<size_t>(node.mesh) < meshes.size())
                {
                    for (const GlbPrimitive& primitive : meshes[static_cast<size_t>(node.mesh)].primitives)
                    {
                        const GlbAccessor& positionAccessor = accessors[static_cast<size_t>(primitive.position)];
                        const uint32_t vertexBase = static_cast<uint32_t>(mesh.vertices.size());
                        for (int i = 0; i < positionAccessor.count; ++i)
                        {
                            PropVertex vertex{};
                            vertex.position = glbPositionToBlockLocal(transformPoint(readGlbVec3(bin, accessors, views, primitive.position, i), chain, nodes));
                            vertex.uv = readGlbVec2(bin, accessors, views, primitive.uv, i);
                            vertex.normal = primitive.normal >= 0 ? rotateByQuaternion(readGlbVec3(bin, accessors, views, primitive.normal, i), node.rotation) : std::array<float, 3>{0.0f, 1.0f, 0.0f};
                            mesh.vertices.push_back(vertex);
                        }
                        const GlbAccessor& indexAccessor = accessors[static_cast<size_t>(primitive.indices)];
                        for (int i = 0; i < indexAccessor.count; ++i)
                        {
                            mesh.indices.push_back(vertexBase + readGlbIndex(bin, accessors, views, primitive.indices, i));
                        }
                    }
                }
                for (int child : node.children)
                {
                    self(self, child, chain);
                }
                chain.pop_back();
            };

            std::vector<int> chain;
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                if (!hasParent[i])
                {
                    visitNode(visitNode, static_cast<int>(i), chain);
                }
            }
            return mesh;
        }

        bool convertGlbToDpm(const std::filesystem::path& glbPath, const std::filesystem::path& dpmPath)
        {
            try
            {
                const PropMeshData mesh = convertGlbToDpmMesh(glbPath);
                const std::vector<PropQuad> quads = convertTrianglesToQuads(mesh);
                if (!writeDpm(dpmPath, quads))
                {
                    return false;
                }
                return dpmLooksValid(dpmPath);
            }
            catch (...)
            {
                return false;
            }
        }

        void ensurePropModelBinary(const std::filesystem::path& modelDirectory, const std::string& modelName)
        {
            if (modelName.empty())
            {
                return;
            }
            const std::filesystem::path dpmPath = modelDirectory / (modelName + ".dpm");
            const std::filesystem::path glbPath = modelDirectory / (modelName + ".glb");
            bool needsConvert = !std::filesystem::exists(dpmPath) || !dpmLooksValid(dpmPath);
            if (!needsConvert && std::filesystem::exists(glbPath))
            {
                try
                {
                    needsConvert = std::filesystem::last_write_time(glbPath) > std::filesystem::last_write_time(dpmPath);
                }
                catch (...)
                {
                    needsConvert = false;
                }
            }
            if (!needsConvert)
            {
                return;
            }
            if (!std::filesystem::exists(glbPath))
            {
                log::warn("Prop model dpm is missing and glb was not found: " + modelName);
                return;
            }
            log::info("Converting prop model: " + glbPath.string() + " -> " + dpmPath.string());
            if (!convertGlbToDpm(glbPath, dpmPath))
            {
                log::warn("Failed to convert prop model: " + glbPath.string());
            }
        }

        PropMeshData loadDpmMesh(const std::filesystem::path& dpmPath)
        {
            std::ifstream file(dpmPath, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                return {};
            }
            const auto fileSize = static_cast<size_t>(file.tellg());
            std::vector<uint8_t> bytes(fileSize);
            file.seekg(0);
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file || bytes.size() < DpmHeaderSize)
            {
                return {};
            }
            const uint32_t quadCount = readU32At(bytes, 0);
            if (bytes.size() != DpmHeaderSize + static_cast<size_t>(quadCount) * DpmQuadSize)
            {
                return {};
            }
            PropMeshData mesh;
            mesh.vertices.reserve(static_cast<size_t>(quadCount) * 4u);
            mesh.indices.reserve(static_cast<size_t>(quadCount) * 4u);
            size_t offset = DpmHeaderSize;
            for (uint32_t quad = 0; quad < quadCount; ++quad)
            {
                std::array<PropVertex, 4> vertices{};
                for (PropVertex& vertex : vertices)
                {
                    for (float& value : vertex.position) { value = readF32At(bytes, offset); offset += sizeof(float); }
                }
                for (PropVertex& vertex : vertices)
                {
                    for (float& value : vertex.uv) { value = readF32At(bytes, offset); offset += sizeof(float); }
                }
                std::array<float, 3> normal{};
                for (float& value : normal) { value = readF32At(bytes, offset); offset += sizeof(float); }

                const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
                for (PropVertex& vertex : vertices)
                {
                    vertex.normal = normal;
                    mesh.vertices.push_back(vertex);
                }
                mesh.indices.push_back(base + 0u);
                mesh.indices.push_back(base + 1u);
                mesh.indices.push_back(base + 2u);
                mesh.indices.push_back(base + 3u);
            }
            return mesh;
        }

        std::vector<uint8_t> lz4EncodeLiteralBlock(const std::vector<uint8_t>& raw)
        {
            std::vector<uint8_t> encoded;
            encoded.reserve(raw.size() + raw.size() / 255 + 16);
            const size_t literalLength = raw.size();
            const uint8_t tokenLiteral = static_cast<uint8_t>(std::min<size_t>(literalLength, 15u));
            encoded.push_back(static_cast<uint8_t>(tokenLiteral << 4u));
            if (literalLength >= 15)
            {
                size_t remaining = literalLength - 15;
                while (remaining >= 255)
                {
                    encoded.push_back(255);
                    remaining -= 255;
                }
                encoded.push_back(static_cast<uint8_t>(remaining));
            }
            encoded.insert(encoded.end(), raw.begin(), raw.end());
            return encoded;
        }

        std::vector<uint8_t> lz4DecodeBlock(const std::vector<uint8_t>& encoded, size_t rawSize)
        {
            std::vector<uint8_t> decoded;
            decoded.reserve(rawSize);
            size_t offset = 0;
            while (offset < encoded.size() && decoded.size() < rawSize)
            {
                const uint8_t token = encoded[offset++];
                size_t literalLength = token >> 4u;
                if (literalLength == 15)
                {
                    uint8_t lengthByte = 0;
                    do
                    {
                        if (offset >= encoded.size())
                        {
                            throw std::runtime_error("Invalid LZ4 literal length.");
                        }
                        lengthByte = encoded[offset++];
                        literalLength += lengthByte;
                    } while (lengthByte == 255);
                }

                if (offset + literalLength > encoded.size())
                {
                    throw std::runtime_error("Invalid LZ4 literal data.");
                }
                decoded.insert(decoded.end(), encoded.begin() + static_cast<std::ptrdiff_t>(offset), encoded.begin() + static_cast<std::ptrdiff_t>(offset + literalLength));
                offset += literalLength;
                if (decoded.size() >= rawSize)
                {
                    break;
                }

                if (offset + 2 > encoded.size())
                {
                    throw std::runtime_error("Invalid LZ4 match offset.");
                }
                const size_t matchOffset = static_cast<size_t>(encoded[offset]) |
                    (static_cast<size_t>(encoded[offset + 1]) << 8u);
                offset += 2;
                size_t matchLength = token & 0x0Fu;
                if (matchLength == 15)
                {
                    uint8_t lengthByte = 0;
                    do
                    {
                        if (offset >= encoded.size())
                        {
                            throw std::runtime_error("Invalid LZ4 match length.");
                        }
                        lengthByte = encoded[offset++];
                        matchLength += lengthByte;
                    } while (lengthByte == 255);
                }
                matchLength += 4;
                if (matchOffset == 0 || matchOffset > decoded.size())
                {
                    throw std::runtime_error("Invalid LZ4 match distance.");
                }
                for (size_t i = 0; i < matchLength; ++i)
                {
                    decoded.push_back(decoded[decoded.size() - matchOffset]);
                }
            }

            if (decoded.size() != rawSize)
            {
                throw std::runtime_error("Invalid LZ4 decoded size.");
            }
            return decoded;
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
        createCommandBuffers();
        createSyncObjects();
    }

    Renderer::~Renderer()
    {
        stopTerrainWorkers();
        enqueueSaveAllRuntimeChunks();
        stopSaveWorker();
        vkDeviceWaitIdle(device_);
        shutdownRmlUi();

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
        bool worldUpdateEnabled,
        bool gameSceneRenderEnabled,
        uint64_t worldTicks)
    {
        const auto frameStart = std::chrono::steady_clock::now();
        frameChunkUpdateMs_ = 0.0;
        frameJobMainMs_ = 0.0;
        frameUploadMs_ = 0.0;
        frameUnloadMs_ = 0.0;
        frameRetireMs_ = 0.0;
        frameSaveEnqueueMs_ = 0.0;
        frameEnsureRuntimeMs_ = 0.0;
        frameWantRenderMs_ = 0.0;
        frameRenderDetachMs_ = 0.0;
        frameUnloadScanMs_ = 0.0;
        frameWantEnsureMs_ = 0.0;
        frameWantInsertMs_ = 0.0;
        frameWantReadyMs_ = 0.0;
        frameWantDependMs_ = 0.0;
        frameWantMeshReadyMs_ = 0.0;
        frameWantMeshDependMs_ = 0.0;
        frameEnsureKeyMs_ = 0.0;
        frameEnsureMarkMs_ = 0.0;
        frameEnsureFindMs_ = 0.0;
        frameEnsureLoadMs_ = 0.0;
        frameEnsureCreateMs_ = 0.0;
        frameEnsureDataTouchMs_ = 0.0;

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
        if (worldUpdateEnabled)
        {
            const auto chunkUpdateStart = std::chrono::steady_clock::now();
            updateLoadedChunks(playerPosition);
            const auto chunkUpdateEnd = std::chrono::steady_clock::now();
            frameChunkUpdateMs_ += std::chrono::duration<double, std::milli>(chunkUpdateEnd - chunkUpdateStart).count();
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

        recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, camera, cameraPositionFloat, playerPositionFloat, fpsText, debugTextVisible, screenshotBuffer, showPlayer, terrainWireframe, climateOverlayMode, menuOverlayMode, gameSceneRenderEnabled, worldTicks);

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
        updatePeakProfiler(std::chrono::duration<double, std::milli>(cpuEnd - frameStart).count());
        currentFrame_ = (currentFrame_ + 1) % MaxFramesInFlight;
    }

    void Renderer::setFramebufferResized()
    {
        framebufferResized_ = true;
    }

    void Renderer::resetPeakProfiler()
    {
        peakFrameMs_ = 0.0;
        peakChunkUpdateMs_ = 0.0;
        peakJobMainMs_ = 0.0;
        peakUploadMs_ = 0.0;
        peakUnloadMs_ = 0.0;
        peakRetireMs_ = 0.0;
        peakSaveEnqueueMs_ = 0.0;
        peakEnsureRuntimeMs_ = 0.0;
        peakWantRenderMs_ = 0.0;
        peakRenderDetachMs_ = 0.0;
        peakUnloadScanMs_ = 0.0;
        peakWantEnsureMs_ = 0.0;
        peakWantInsertMs_ = 0.0;
        peakWantReadyMs_ = 0.0;
        peakWantDependMs_ = 0.0;
        peakWantMeshReadyMs_ = 0.0;
        peakWantMeshDependMs_ = 0.0;
        peakEnsureKeyMs_ = 0.0;
        peakEnsureMarkMs_ = 0.0;
        peakEnsureFindMs_ = 0.0;
        peakEnsureLoadMs_ = 0.0;
        peakEnsureCreateMs_ = 0.0;
        peakEnsureDataTouchMs_ = 0.0;
        if (peakProfilerSamplingStarted_)
        {
            peakProfilerStatusText_ = "PEAK SAMPLE: ON";
        }
        updatePeakProfilerText();
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

        const uint16_t destroyedBlock = placeRock ? BlockAir : blockAtWorld(hit.blockX, hit.blockY, hit.blockZ);
        const bool changed = placeRock
            ? setBlockAtWorld(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ, BlockRock)
            : setBlockAtWorld(hit.blockX, hit.blockY, hit.blockZ, BlockAir);
        if (changed)
        {
            const int changedX = placeRock ? hit.previousBlockX : hit.blockX;
            const int changedY = placeRock ? hit.previousBlockY : hit.blockY;
            const int changedZ = placeRock ? hit.previousBlockZ : hit.blockZ;
            if (!placeRock && destroyedBlock != BlockAir)
            {
                spawnBlockBreakParticles(changedX, changedY, changedZ, destroyedBlock);
                spawnBlockDrops(changedX, changedY, changedZ, destroyedBlock);
            }
            rebuildEditedChunkMeshes(changedX, changedY, changedZ);
        }
        return changed;
    }

    bool Renderer::pickupDroppedItemInView(DVec3 origin, Vec3 direction)
    {
        size_t itemIndex = 0;
        if (!raycastDroppedItem(origin, direction, itemIndex))
        {
            return false;
        }

        DroppedItem& item = droppedItems_[itemIndex];
        item.collecting = true;
        item.collectAge = 0.0f;
        item.grounded = false;
        item.velocity = {};
        item.previousPosition = item.position;
        item.renderSpinX = 8.0f;
        item.renderSpin = 8.0f;
        item.renderSpinZ = 8.0f;
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

        addItemToInventoryRange(stack, 0, playerInventorySlots_.size());
        updateInventoryUi();
        return stack.count;
    }

    uint16_t Renderer::addItemToInventoryRange(ItemStack& stack, size_t begin, size_t end)
    {
        if (stack.itemId == 0 || stack.count == 0 || static_cast<size_t>(stack.itemId) >= itemDefinitions_.size())
        {
            return stack.count;
        }

        end = std::min(end, playerInventorySlots_.size());
        if (begin >= end)
        {
            return stack.count;
        }

        const uint16_t maxStack = itemDefinitions_[stack.itemId].stackSize;
        if (maxStack == 0)
        {
            return stack.count;
        }

        for (size_t i = begin; i < end && stack.count > 0; ++i)
        {
            ItemStack& slot = playerInventorySlots_[i];
            if (!inventoryStackCanMerge(slot, stack))
            {
                continue;
            }

            const uint16_t available = static_cast<uint16_t>(maxStack - slot.count);
            const uint16_t moved = std::min(available, stack.count);
            slot.count = static_cast<uint16_t>(slot.count + moved);
            stack.count = static_cast<uint16_t>(stack.count - moved);
        }

        for (size_t i = begin; i < end && stack.count > 0; ++i)
        {
            ItemStack& slot = playerInventorySlots_[i];
            if (slot.itemId != 0 || slot.count != 0)
            {
                continue;
            }

            const uint16_t moved = std::min(maxStack, stack.count);
            slot.itemId = stack.itemId;
            slot.count = moved;
            stack.count = static_cast<uint16_t>(stack.count - moved);
        }

        return stack.count;
    }

    bool Renderer::inventoryStackCanMerge(const ItemStack& slot, const ItemStack& stack) const
    {
        if (slot.itemId == 0 || stack.itemId == 0 || slot.itemId != stack.itemId || static_cast<size_t>(slot.itemId) >= itemDefinitions_.size())
        {
            return false;
        }
        return slot.count < itemDefinitions_[slot.itemId].stackSize;
    }

    bool Renderer::blockIntersectsPlayerCollider(int x, int y, int z, uint16_t block, DVec3 playerPosition) const
    {
        if (!blockDefinition(block).collision)
        {
            return false;
        }

        constexpr double HalfWidth = 0.3;
        constexpr double Height = 1.75;
        constexpr double Epsilon = 0.000001;

        const double minX = playerPosition.x - HalfWidth;
        const double maxX = playerPosition.x + HalfWidth;
        const double minY = playerPosition.y;
        const double maxY = playerPosition.y + Height;
        const double minZ = playerPosition.z - HalfWidth;
        const double maxZ = playerPosition.z + HalfWidth;

        return x >= blockCoordinateXz(minX) &&
            x <= blockCoordinateXz(maxX - Epsilon) &&
            y >= blockCoordinateY(minY) &&
            y <= blockCoordinateY(maxY - Epsilon) &&
            z >= blockCoordinateXz(minZ) &&
            z <= blockCoordinateXz(maxZ - Epsilon);
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
        const auto chunkIt = runtimeChunks_.find(chunkKey(chunkX, chunkZ));
        if (chunkIt != runtimeChunks_.end() && chunkIt->second.data)
        {
            const ChunkData& data = *chunkIt->second.data;
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
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &itemPipeline_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create item pipeline.");
        }

        vkDestroyShaderModule(device_, fragShader, nullptr);
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
        const std::vector<ParsedBlockDefinition> blockDefinitions = parseBlockDefinitions(blockDefinitionText);

        const std::vector<char> itemDefinitionData = readFile((assetDir / "data" / "items.json").string());
        const std::string itemDefinitionText(itemDefinitionData.begin(), itemDefinitionData.end());
        const std::vector<ParsedItemDefinition> itemDefinitions = parseItemDefinitions(itemDefinitionText);

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
        for (const ParsedItemDefinition& definition : itemDefinitions)
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
        for (const ParsedBlockDefinition& definition : blockDefinitions)
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

        const std::filesystem::path propModelDirectory = assetDir / "textures" / "block" / "model";
        std::unordered_set<std::string> checkedPropModels;
        for (const ParsedBlockDefinition& definition : blockDefinitions)
        {
            if (definition.renderType == "prop" && !definition.propModel.empty() && checkedPropModels.insert(definition.propModel).second)
            {
                ensurePropModelBinary(propModelDirectory, definition.propModel);
            }
        }
        propMeshesByBlock_.clear();
        for (const ParsedBlockDefinition& definition : blockDefinitions)
        {
            if (definition.renderType != "prop" || definition.propModel.empty())
            {
                continue;
            }

            const std::filesystem::path dpmPath = propModelDirectory / (definition.propModel + ".dpm");
            PropMeshData loadedMesh = loadDpmMesh(dpmPath);
            if (loadedMesh.vertices.empty() || loadedMesh.indices.empty())
            {
                log::warn("Prop model dpm could not be loaded: " + dpmPath.string());
                continue;
            }
            PropMesh mesh{};
            mesh.quads.reserve((loadedMesh.vertices.size() / 4u) * DpmQuadRenderFloatCount);
            for (size_t vertexOffset = 0; vertexOffset + 3u < loadedMesh.vertices.size(); vertexOffset += 4u)
            {
                for (size_t vertexIndex = 0; vertexIndex < 4u; ++vertexIndex)
                {
                    const PropVertex& vertex = loadedMesh.vertices[vertexOffset + vertexIndex];
                    mesh.quads.push_back(vertex.position[0]);
                    mesh.quads.push_back(vertex.position[1]);
                    mesh.quads.push_back(vertex.position[2]);
                }
                for (size_t vertexIndex = 0; vertexIndex < 4u; ++vertexIndex)
                {
                    const PropVertex& vertex = loadedMesh.vertices[vertexOffset + vertexIndex];
                    mesh.quads.push_back(vertex.uv[0]);
                    mesh.quads.push_back(vertex.uv[1]);
                }
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
        createBuffer(
            sizeof(TerrainVertex) * MaxDroppedItemRenderQuads * 4u,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            droppedItemVertexBuffer_,
            droppedItemVertexMemory_);
        createBuffer(
            sizeof(uint32_t) * MaxDroppedItemRenderQuads * 6u,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            droppedItemIndexBuffer_,
            droppedItemIndexMemory_);
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

    void Renderer::loadWorldConfig()
    {
        loadGridScale_ = DefaultLoadGridScale;
        terrainWorkerCount_ = DefaultTerrainWorkerCount;
        maxTerrainUploadChunksPerFrame_ = DefaultMaxTerrainUploadChunksPerFrame;
        maxTerrainUnloadChunksPerFrame_ = DefaultMaxTerrainUnloadChunksPerFrame;
        maxTerrainRetiredDestroyPerFrame_ = DefaultMaxTerrainRetiredDestroyPerFrame;
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
        temperatureNoiseStrength_ = DefaultTemperatureNoiseStrength;
        temperatureNoiseFeatureScale_ = DefaultTemperatureNoiseFeatureScale;
        temperatureNoiseOctaveCount_ = DefaultTemperatureNoiseOctaveCount;
        temperatureNoiseLacunarity_ = DefaultTemperatureNoiseLacunarity;
        temperatureNoiseGain_ = DefaultTemperatureNoiseGain;
        temperatureNoiseSimplexScale_ = DefaultTemperatureNoiseSimplexScale;
        precipitationNoiseFeatureScale_ = DefaultPrecipitationNoiseFeatureScale;
        precipitationNoiseOctaveCount_ = DefaultPrecipitationNoiseOctaveCount;
        precipitationNoiseLacunarity_ = DefaultPrecipitationNoiseLacunarity;
        precipitationNoiseGain_ = DefaultPrecipitationNoiseGain;
        precipitationNoiseSimplexScale_ = DefaultPrecipitationNoiseSimplexScale;
        seaLevel_ = DefaultSeaLevel;

        const std::filesystem::path path = configDirectory() / "world.json";
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
        const std::string climate = jsonObjectField(text, "climate").value_or("{}");
        const std::string temperature = jsonObjectField(climate, "temperature").value_or("{}");
        const std::string precipitation = jsonObjectField(climate, "precipitation").value_or("{}");
        if (const std::optional<int> value = jsonIntField(terrain, "seaLevel"); value.has_value())
        {
            seaLevel_ = std::clamp(*value, 0, ChunkSizeY - 1);
        }

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
        if (const std::optional<int> value = jsonIntField(chunkLoad, "maxRetiredChunksDestroyedPerFrame"); value.has_value())
        {
            maxTerrainRetiredDestroyPerFrame_ = std::clamp(*value, 1, 64);
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
        if (const std::optional<float> value = jsonFloatField(temperature, "noiseStrength"); value.has_value() && *value >= 0.0f)
        {
            temperatureNoiseStrength_ = *value;
        }
        if (const std::optional<float> value = jsonFloatField(temperature, "noiseFeatureScale"); value.has_value() && *value > 0.0f)
        {
            temperatureNoiseFeatureScale_ = *value;
        }
        if (const std::optional<int> value = jsonIntField(temperature, "noiseOctaveCount"); value.has_value())
        {
            temperatureNoiseOctaveCount_ = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(temperature, "noiseLacunarity"); value.has_value() && *value > 0.0f)
        {
            temperatureNoiseLacunarity_ = *value;
        }
        if (const std::optional<float> value = jsonFloatField(temperature, "noiseGain"); value.has_value() && *value >= 0.0f)
        {
            temperatureNoiseGain_ = *value;
        }
        if (const std::optional<float> value = jsonFloatField(temperature, "noiseSimplexScale"); value.has_value() && *value > 0.0f)
        {
            temperatureNoiseSimplexScale_ = *value;
        }
        if (const std::optional<float> value = jsonFloatField(precipitation, "featureScale"); value.has_value() && *value > 0.0f)
        {
            precipitationNoiseFeatureScale_ = *value;
        }
        if (const std::optional<int> value = jsonIntField(precipitation, "octaveCount"); value.has_value())
        {
            precipitationNoiseOctaveCount_ = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(precipitation, "lacunarity"); value.has_value() && *value > 0.0f)
        {
            precipitationNoiseLacunarity_ = *value;
        }
        if (const std::optional<float> value = jsonFloatField(precipitation, "gain"); value.has_value() && *value >= 0.0f)
        {
            precipitationNoiseGain_ = *value;
        }
        if (const std::optional<float> value = jsonFloatField(precipitation, "simplexScale"); value.has_value() && *value > 0.0f)
        {
            precipitationNoiseSimplexScale_ = *value;
        }
    }

    void Renderer::loadRenderConfig()
    {
        fluidWaterAlpha_ = DefaultFluidWaterAlpha;

        const std::filesystem::path path = configDirectory() / "render.json";
        std::ifstream file(path);
        if (!file.is_open())
        {
            return;
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        const std::string text = contents.str();
        const std::string fluid = jsonObjectField(text, "fluid").value_or("{}");
        const std::string water = jsonObjectField(fluid, "water").value_or("{}");

        if (const std::optional<float> value = jsonFloatField(water, "alpha"); value.has_value())
        {
            fluidWaterAlpha_ = std::clamp(*value, 0.0f, 1.0f);
        }
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

        desiredTerrainChunks_.clear();
        desiredFeatureChunks_.clear();
        desiredRenderChunks_.clear();
        desiredTerrainChunks_.reserve(static_cast<size_t>(loadedChunkDiameter_ + 4) * static_cast<size_t>(loadedChunkDiameter_ + 4));
        desiredFeatureChunks_.reserve(static_cast<size_t>(loadedChunkDiameter_ + 2) * static_cast<size_t>(loadedChunkDiameter_ + 2));
        desiredRenderChunks_.reserve(static_cast<size_t>(loadedChunkDiameter_) * loadedChunkDiameter_);
        const size_t runtimeCapacity = static_cast<size_t>(loadedChunkDiameter_ + 4) * static_cast<size_t>(loadedChunkDiameter_ + 4);
        const size_t featureCapacity = static_cast<size_t>(loadedChunkDiameter_ + 2) * static_cast<size_t>(loadedChunkDiameter_ + 2);
        const size_t renderCapacity = static_cast<size_t>(loadedChunkDiameter_) * static_cast<size_t>(loadedChunkDiameter_);
        runtimeChunks_.reserve(runtimeCapacity + 256u);
        terrainChunks_.reserve(renderCapacity + 256u);
        pendingUnloadSet_.reserve(runtimeCapacity + 256u);
        requestedChunkJobs_.reserve(featureCapacity + 256u);
        requestedMeshJobs_.reserve(renderCapacity + 256u);
        requestedChunkJobs_.clear();
        requestedMeshJobs_.clear();

        const auto gridStart = std::chrono::steady_clock::now();
        const int renderMin = -(loadedChunkDiameter_ / 2 - 1);
        const int renderMax = loadedChunkDiameter_ / 2;
        const int runtimeKeepMin = renderMin - 2;
        const int runtimeKeepMax = renderMax + 2;

        {
            std::lock_guard<std::mutex> lock(terrainJobMutex_);
            terrainFeatureJobs_.clear();
            terrainFinalizeJobs_.clear();
            terrainMeshJobs_.clear();
            completedChunkMeshes_.clear();
        }

        for (auto& entry : runtimeChunks_)
        {
            entry.second.bestPriority = UINT32_MAX;
        }

        const auto ensureStart = std::chrono::steady_clock::now();
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
        const auto ensureEnd = std::chrono::steady_clock::now();
        frameEnsureRuntimeMs_ += std::chrono::duration<double, std::milli>(ensureEnd - ensureStart).count();

        auto distanceToCenterGroupSquared = [](const ChunkOffset& offset)
        {
            const int dx = offset.x < 0 ? -offset.x : (offset.x > 1 ? offset.x - 1 : 0);
            const int dz = offset.z < 0 ? -offset.z : (offset.z > 1 ? offset.z - 1 : 0);
            return static_cast<uint32_t>(dx * dx + dz * dz);
        };

        const auto wantRenderStart = std::chrono::steady_clock::now();
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
        const auto wantRenderEnd = std::chrono::steady_clock::now();
        frameWantRenderMs_ += std::chrono::duration<double, std::milli>(wantRenderEnd - wantRenderStart).count();
        const auto gridEnd = std::chrono::steady_clock::now();

        const auto renderDetachStart = std::chrono::steady_clock::now();
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
        const auto renderDetachEnd = std::chrono::steady_clock::now();
        frameRenderDetachMs_ += std::chrono::duration<double, std::milli>(renderDetachEnd - renderDetachStart).count();

        const auto unloadScanStart = std::chrono::steady_clock::now();
        for (const auto& entry : runtimeChunks_)
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
        const auto unloadScanEnd = std::chrono::steady_clock::now();
        frameUnloadScanMs_ += std::chrono::duration<double, std::milli>(unloadScanEnd - unloadScanStart).count();

        const auto updateEnd = std::chrono::steady_clock::now();
        chunkUpdateProfileText_ = formatProfileMs("UPDATE TOTAL", std::chrono::duration<double, std::milli>(updateEnd - updateStart).count());
        gridScanProfileText_ = formatProfileMs("GRID SCAN", std::chrono::duration<double, std::milli>(gridEnd - gridStart).count());
        newChunksProfileText_ = "NEW CHUNKS: " + std::to_string(requestedChunkJobs_.size());
        metadataBuildProfileText_ = formatProfileMs("META BUILD", 0.0);
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
            terrainFeatureJobs_.clear();
            terrainFinalizeJobs_.clear();
            terrainMeshJobs_.clear();
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

        std::deque<CompletedChunkData> completedData;
        std::deque<std::shared_ptr<ChunkData>> completedMerged;
        {
            std::lock_guard<std::mutex> lock(terrainJobMutex_);
            completedData = std::move(completedChunkData_);
            completedMerged = std::move(completedMergedChunks_);
            completedChunkMeshes_.clear();
        }

        for (CompletedChunkData& completed : completedData)
        {
            if (!completed.chunk)
            {
                continue;
            }

            SaveChunkSnapshot snapshot{};
            snapshot.chunkX = completed.chunk->chunkX;
            snapshot.chunkZ = completed.chunk->chunkZ;
            snapshot.genState = ChunkGenState::Featuring;
            snapshot.revision = completed.chunk->revision;
            snapshot.hasData = true;
            snapshot.chunkData = completed.chunk;
            enqueueSaveSnapshot(std::move(snapshot));
        }

        for (const std::shared_ptr<ChunkData>& chunk : completedMerged)
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

    void Renderer::startSaveWorker()
    {
        stopSaveWorker_ = false;
        saveWorker_ = std::thread(&Renderer::saveWorkerLoop, this);
    }

    void Renderer::stopSaveWorker()
    {
        {
            std::lock_guard<std::mutex> lock(saveJobMutex_);
            stopSaveWorker_ = true;
        }
        saveJobCondition_.notify_all();

        if (saveWorker_.joinable())
        {
            saveWorker_.join();
        }
    }

    void Renderer::saveWorkerLoop()
    {
        for (;;)
        {
            SaveChunkSnapshot snapshot{};
            {
                std::unique_lock<std::mutex> lock(saveJobMutex_);
                saveJobCondition_.wait(lock, [this]
                {
                    return stopSaveWorker_ || !saveJobs_.empty();
                });

                if (saveJobs_.empty())
                {
                    if (stopSaveWorker_)
                    {
                        return;
                    }
                    continue;
                }

                snapshot = std::move(saveJobs_.front());
                saveJobs_.pop_front();
            }

            try
            {
                const bool savedChunkData = snapshot.hasData;
                saveChunkSnapshot(snapshot);
                const uint64_t storageKey = storageChunkKey(snapshot.chunkX, snapshot.chunkZ);
                const uint64_t runtimeKey = chunkKey(snapshot.chunkX, snapshot.chunkZ);
                {
                    std::lock_guard<std::mutex> lock(saveJobMutex_);
                    const auto pendingIt = pendingSaveSnapshots_.find(storageKey);
                    if (pendingIt != pendingSaveSnapshots_.end() &&
                        pendingIt->second.hasData == snapshot.hasData &&
                        pendingIt->second.revision == snapshot.revision &&
                        pendingIt->second.genState == snapshot.genState)
                    {
                        pendingSaveSnapshots_.erase(pendingIt);
                    }
                }
                if (savedChunkData)
                {
                    auto runtimeIt = runtimeChunks_.find(runtimeKey);
                    if (runtimeIt != runtimeChunks_.end() &&
                        runtimeIt->second.data &&
                        runtimeIt->second.data->revision == snapshot.revision)
                    {
                        runtimeIt->second.hasSavedBacking = true;
                        runtimeIt->second.dataDirtyForSave = false;
                    }
                    saveChunkDoneCount_.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    saveFeatureDoneCount_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            catch (...)
            {
                saveFailedCount_.fetch_add(1, std::memory_order_relaxed);
            }
        }
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
                    return stopTerrainWorkers_ ||
                        !terrainMeshJobs_.empty() ||
                        !terrainFinalizeJobs_.empty() ||
                        !terrainFeatureJobs_.empty();
                });

                if (stopTerrainWorkers_)
                {
                    return;
                }

                auto stageRank = [](TerrainJob::Type type)
                {
                    switch (type)
                    {
                    case TerrainJob::Type::BuildChunkMesh:
                        return 0;
                    case TerrainJob::Type::FinalizeFeatures:
                        return 1;
                    case TerrainJob::Type::BuildFeaturing:
                        return 2;
                    }
                    return 3;
                };

                auto jobLess = [&](const TerrainJob& left, const TerrainJob& right)
                {
                    if (left.priority != right.priority)
                    {
                        return left.priority < right.priority;
                    }
                    const int leftStage = stageRank(left.type);
                    const int rightStage = stageRank(right.type);
                    if (leftStage != rightStage)
                    {
                        return leftStage < rightStage;
                    }
                    return left.sequence < right.sequence;
                };

                auto bestQueue = &terrainFeatureJobs_;
                auto bestIt = terrainFeatureJobs_.begin();
                bool hasBest = bestIt != terrainFeatureJobs_.end();
                auto considerQueue = [&](std::deque<TerrainJob>& jobs)
                {
                    for (auto it = jobs.begin(); it != jobs.end(); ++it)
                    {
                        if (!hasBest || jobLess(*it, *bestIt))
                        {
                            bestQueue = &jobs;
                            bestIt = it;
                            hasBest = true;
                        }
                    }
                };

                considerQueue(terrainFinalizeJobs_);
                considerQueue(terrainMeshJobs_);

                if (hasBest)
                {
                    job = std::move(*bestIt);
                    bestQueue->erase(bestIt);
                }
            }

            if (job.generation != terrainGeneration_.load())
            {
                continue;
            }

            if (job.type == TerrainJob::Type::BuildFeaturing)
            {
                std::shared_ptr<ChunkData> chunk = buildChunkData(job.chunkX, job.chunkZ);
                chunk->generation = job.generation;
                chunk->revision = 0;
                const std::array<int, Renderer::ChunkColumnCount> heights = buildChunkHeightmap(job.chunkX, job.chunkZ);
                std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingFeatureSlots = buildTreeFeatures(chunk, heights);
                std::lock_guard<std::mutex> lock(terrainJobMutex_);
                completedChunkData_.push_back(CompletedChunkData{std::move(chunk), std::move(outgoingFeatureSlots)});
            }
            else if (job.type == TerrainJob::Type::FinalizeFeatures && job.chunk)
            {
                applyFeatureWrites(job.chunk, job.incomingFeatureSlots);
                std::lock_guard<std::mutex> lock(terrainJobMutex_);
                completedMergedChunks_.push_back(std::move(job.chunk));
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
                    std::lock_guard<std::mutex> lock(terrainJobMutex_);
                    completedChunkMeshes_.push_back(std::move(mesh));
                    continue;
                }
                CompletedChunkMesh mesh = buildChunkMesh(job.meshChunks, job.generation);
                std::lock_guard<std::mutex> lock(terrainJobMutex_);
                completedChunkMeshes_.push_back(std::move(mesh));
            }
        }
    }

    void Renderer::enqueueTerrainJob(TerrainJob job)
    {
        {
            std::lock_guard<std::mutex> lock(terrainJobMutex_);
            job.sequence = ++terrainJobSequence_;
            if (job.type == TerrainJob::Type::BuildFeaturing)
            {
                terrainFeatureJobs_.push_back(std::move(job));
            }
            else if (job.type == TerrainJob::Type::FinalizeFeatures)
            {
                terrainFinalizeJobs_.push_back(std::move(job));
            }
            else
            {
                terrainMeshJobs_.push_back(std::move(job));
            }
        }
        terrainJobCondition_.notify_one();
    }

    void Renderer::processCompletedTerrainJobs()
    {
        const auto buildStart = std::chrono::steady_clock::now();
        const uint64_t generation = terrainGeneration_.load();
        std::vector<CompletedChunkData> completedChunks;
        std::vector<std::shared_ptr<ChunkData>> completedMergedChunks;
        std::vector<CompletedChunkMesh> completedMeshes;
        size_t queuedFeatureJobCount = 0;
        size_t queuedFinalizeJobCount = 0;
        size_t queuedMeshJobCount = 0;
        size_t queuedDataDoneCount = 0;
        size_t queuedFinalizeDoneCount = 0;
        size_t queuedMeshDoneCount = 0;
        uint32_t uploadedChunkCount = 0;
        double uploadMs = 0.0;
        {
            std::lock_guard<std::mutex> lock(terrainJobMutex_);
            queuedFeatureJobCount = terrainFeatureJobs_.size();
            queuedFinalizeJobCount = terrainFinalizeJobs_.size();
            queuedMeshJobCount = terrainMeshJobs_.size();
            queuedDataDoneCount = completedChunkData_.size();
            queuedFinalizeDoneCount = completedMergedChunks_.size();
            queuedMeshDoneCount = completedChunkMeshes_.size();
            while (!completedChunkData_.empty())
            {
                completedChunks.push_back(std::move(completedChunkData_.front()));
                completedChunkData_.pop_front();
            }
            while (!completedMergedChunks_.empty())
            {
                completedMergedChunks.push_back(std::move(completedMergedChunks_.front()));
                completedMergedChunks_.pop_front();
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
                if (frontMesh.generation != generation || desiredRenderChunks_.find(key) == desiredRenderChunks_.end())
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
            RuntimeChunk& runtimeChunk = runtimeChunks_[key];
            runtimeChunk.chunkX = chunk->chunkX;
            runtimeChunk.chunkZ = chunk->chunkZ;
            runtimeChunk.data = chunk;
            runtimeChunk.outgoingFeatureSlots = std::move(completed.outgoingFeatureSlots);
            runtimeChunk.genState = ChunkGenState::Featuring;
            runtimeChunk.buildQueuedTicket = 0;
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
            RuntimeChunk& runtimeChunk = runtimeChunks_[key];
            runtimeChunk.chunkX = chunk->chunkX;
            runtimeChunk.chunkZ = chunk->chunkZ;
            runtimeChunk.data = chunk;
            runtimeChunk.incomingFeatureSlots = {};
            runtimeChunk.incomingFeatureMask = 0;
            runtimeChunk.finalizeQueuedTicket = 0;
            runtimeChunk.genState = ChunkGenState::Full;
            tryQueueMeshesAround(chunk->chunkX, chunk->chunkZ);
        }

        for (CompletedChunkMesh& mesh : completedMeshes)
        {
            const uint64_t key = chunkKey(mesh.chunkX, mesh.chunkZ);
            requestedMeshJobs_.erase(key);
            auto runtimeIt = runtimeChunks_.find(key);
            if (runtimeIt != runtimeChunks_.end())
            {
                runtimeIt->second.meshQueuedTicket = 0;
            }

            if (mesh.generation != generation || desiredRenderChunks_.find(key) == desiredRenderChunks_.end())
            {
                continue;
            }
            if (runtimeIt == runtimeChunks_.end() || !runtimeIt->second.data || mesh.revision != runtimeIt->second.data->revision)
            {
                tryQueueMeshIfReady(mesh.chunkX, mesh.chunkZ);
                continue;
            }

            const auto uploadStart = std::chrono::steady_clock::now();
            ChunkRenderData& renderData = terrainChunks_[key];
            retiredTerrainChunks_.push_back(RetiredChunkRenderData{
                static_cast<uint32_t>(MaxFramesInFlight + 1),
                std::move(renderData)});
            renderData = {};
            renderData.revision = mesh.revision;
            renderData.chunkX = mesh.chunkX;
            renderData.chunkZ = mesh.chunkZ;
            createChunkTerrainBuffers(mesh.rockSubchunks, renderData.rockSubchunks);
            createChunkTerrainBuffers(mesh.fluidSubchunks, renderData.fluidSubchunks);
            runtimeIt->second.genState = ChunkGenState::Meshed;
            const auto uploadEnd = std::chrono::steady_clock::now();
            uploadMs += std::chrono::duration<double, std::milli>(uploadEnd - uploadStart).count();
        }

        if (!completedChunks.empty() || !completedMergedChunks.empty() || !completedMeshes.empty())
        {
            updateTerrainStats();
        }
        const auto retireStart = std::chrono::steady_clock::now();
        processRetiredTerrainChunks();
        const auto retireEnd = std::chrono::steady_clock::now();
        const uint32_t unloadedChunkCount = processPendingTerrainUnloads();
        const auto unloadEnd = std::chrono::steady_clock::now();
        const size_t retiredChunkCount = retiredTerrainChunks_.size();

        const auto buildEnd = std::chrono::steady_clock::now();
        frameUploadMs_ += uploadMs;
        frameRetireMs_ += std::chrono::duration<double, std::milli>(retireEnd - retireStart).count();
        frameUnloadMs_ += std::chrono::duration<double, std::milli>(unloadEnd - retireEnd).count();
        frameJobMainMs_ += std::chrono::duration<double, std::milli>(buildEnd - buildStart).count();
        const std::chrono::duration<double> terrainDebugElapsed = buildEnd - terrainDebugSampleTime_;
        if (terrainDebugSampleTime_ == std::chrono::steady_clock::time_point{} || terrainDebugElapsed.count() >= 0.05)
        {
            terrainDebugSampleTime_ = buildEnd;
            size_t emptyCount = 0;
            size_t featuringCount = 0;
            size_t fullCount = 0;
            size_t meshedCount = 0;
            for (const auto& entry : runtimeChunks_)
            {
                if (desiredTerrainChunks_.find(entry.first) == desiredTerrainChunks_.end())
                {
                    continue;
                }

                switch (entry.second.genState)
                {
                case ChunkGenState::Empty:
                    ++emptyCount;
                    break;
                case ChunkGenState::Featuring:
                    ++featuringCount;
                    break;
                case ChunkGenState::Full:
                    ++fullCount;
                    break;
                case ChunkGenState::Meshed:
                    ++meshedCount;
                    break;
                }
            }

            size_t renderMissingCount = 0;
            for (uint64_t key : desiredRenderChunks_)
            {
                if (terrainChunks_.find(key) == terrainChunks_.end())
                {
                    ++renderMissingCount;
                }
            }

            size_t saveQueueCount = 0;
            size_t pendingSaveCount = 0;
            {
                std::lock_guard<std::mutex> lock(saveJobMutex_);
                saveQueueCount = saveJobs_.size();
                pendingSaveCount = pendingSaveSnapshots_.size();
            }

            dataQueueText_ = "WANT RENDER/FEATURE/DATA: " +
                std::to_string(desiredRenderChunks_.size()) + " / " +
                std::to_string(desiredFeatureChunks_.size()) + " / " +
                std::to_string(desiredTerrainChunks_.size());
            finalizeQueueText_ = "STATE EMPTY/FEATURING/FULL/MESHED: " +
                std::to_string(emptyCount) + " / " +
                std::to_string(featuringCount) + " / " +
                std::to_string(fullCount) + " / " +
                std::to_string(meshedCount);
            meshQueueText_ = "RENDER MISS: " + std::to_string(renderMissingCount);
            dataDoneText_ = "QUEUE BUILD/FINALIZE/MESH: " +
                std::to_string(queuedFeatureJobCount) + " / " +
                std::to_string(queuedFinalizeJobCount) + " / " +
                std::to_string(queuedMeshJobCount);
            meshDoneText_ = "DONE BUILD/FINALIZE/MESH: " +
                std::to_string(queuedDataDoneCount) + " / " +
                std::to_string(queuedFinalizeDoneCount) + " / " +
                std::to_string(queuedMeshDoneCount);
            saveQueueText_ = "SAVE QUEUE/CACHE: " +
                std::to_string(saveQueueCount) + " / " +
                std::to_string(pendingSaveCount);
            saveDoneText_ = "SAVE CHUNK/FEATURE/FAILED: " +
                std::to_string(saveChunkDoneCount_.load(std::memory_order_relaxed)) + " / " +
                std::to_string(saveFeatureDoneCount_.load(std::memory_order_relaxed)) + " / " +
                std::to_string(saveFailedCount_.load(std::memory_order_relaxed));
            loadText_ = "LOAD PENDING/REGION/MISS: " +
                std::to_string(loadPendingHitCount_.load(std::memory_order_relaxed)) + " / " +
                std::to_string(loadRegionHitCount_.load(std::memory_order_relaxed)) + " / " +
                std::to_string(loadMissCount_.load(std::memory_order_relaxed));
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
                    static_cast<uint32_t>(MaxFramesInFlight + 1),
                    std::move(renderIt->second)});
                terrainChunks_.erase(renderIt);
            }
            auto runtimeIt = runtimeChunks_.find(key);
            if (runtimeIt != runtimeChunks_.end())
            {
                enqueueSaveSnapshot(makeSaveSnapshot(runtimeIt->second));
            }
            runtimeChunks_.erase(key);
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

    Renderer::RuntimeChunk& Renderer::ensureRuntimeChunk(int chunkX, int chunkZ, uint64_t generation)
    {
        const auto keyStart = std::chrono::steady_clock::now();
        const uint64_t key = chunkKey(chunkX, chunkZ);
        const auto keyEnd = std::chrono::steady_clock::now();
        frameEnsureKeyMs_ += std::chrono::duration<double, std::milli>(keyEnd - keyStart).count();

        const auto markStart = std::chrono::steady_clock::now();
        desiredTerrainChunks_.insert(key);
        pendingUnloadSet_.erase(key);
        const auto markEnd = std::chrono::steady_clock::now();
        frameEnsureMarkMs_ += std::chrono::duration<double, std::milli>(markEnd - markStart).count();

        const auto findStart = std::chrono::steady_clock::now();
        auto chunkIt = runtimeChunks_.find(key);
        const auto findEnd = std::chrono::steady_clock::now();
        frameEnsureFindMs_ += std::chrono::duration<double, std::milli>(findEnd - findStart).count();
        if (chunkIt == runtimeChunks_.end())
        {
            const auto loadStart = std::chrono::steady_clock::now();
            std::optional<SaveChunkSnapshot> snapshot = loadChunkSnapshot(chunkX, chunkZ);
            const auto loadEnd = std::chrono::steady_clock::now();
            frameEnsureLoadMs_ += std::chrono::duration<double, std::milli>(loadEnd - loadStart).count();

            const auto createStart = std::chrono::steady_clock::now();
            if (snapshot)
            {
                chunkIt = runtimeChunks_.emplace(key, runtimeChunkFromSnapshot(*snapshot, generation)).first;
            }
            else
            {
                RuntimeChunk chunk{};
                chunk.chunkX = chunkX;
                chunk.chunkZ = chunkZ;
                chunkIt = runtimeChunks_.emplace(key, std::move(chunk)).first;
            }
            const auto createEnd = std::chrono::steady_clock::now();
            frameEnsureCreateMs_ += std::chrono::duration<double, std::milli>(createEnd - createStart).count();
        }

        RuntimeChunk& chunk = chunkIt->second;
        const auto touchStart = std::chrono::steady_clock::now();
        chunk.chunkX = chunkX;
        chunk.chunkZ = chunkZ;
        if (chunk.data)
        {
            chunk.data->generation = generation;
        }
        const auto touchEnd = std::chrono::steady_clock::now();
        frameEnsureDataTouchMs_ += std::chrono::duration<double, std::milli>(touchEnd - touchStart).count();
        return chunk;
    }

    void Renderer::wantRender(int chunkX, int chunkZ, uint32_t priority)
    {
        const uint64_t generation = terrainGeneration_.load();
        const uint64_t key = chunkKey(chunkX, chunkZ);
        const auto ensureStart = std::chrono::steady_clock::now();
        RuntimeChunk& chunk = ensureRuntimeChunk(chunkX, chunkZ, generation);
        const auto ensureEnd = std::chrono::steady_clock::now();
        frameWantEnsureMs_ += std::chrono::duration<double, std::milli>(ensureEnd - ensureStart).count();

        const auto insertStart = std::chrono::steady_clock::now();
        desiredRenderChunks_.insert(key);
        chunk.renderTicket = generation;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);
        const auto insertEnd = std::chrono::steady_clock::now();
        frameWantInsertMs_ += std::chrono::duration<double, std::milli>(insertEnd - insertStart).count();

        const auto readyStart = std::chrono::steady_clock::now();
        if (chunkMeshReady(key))
        {
            const auto readyEnd = std::chrono::steady_clock::now();
            frameWantReadyMs_ += std::chrono::duration<double, std::milli>(readyEnd - readyStart).count();
            return;
        }
        const auto readyEnd = std::chrono::steady_clock::now();
        frameWantReadyMs_ += std::chrono::duration<double, std::milli>(readyEnd - readyStart).count();

        const auto dependStart = std::chrono::steady_clock::now();
        wantMesh(chunkX, chunkZ, priority);
        const auto dependEnd = std::chrono::steady_clock::now();
        frameWantDependMs_ += std::chrono::duration<double, std::milli>(dependEnd - dependStart).count();
    }

    void Renderer::wantMesh(int chunkX, int chunkZ, uint32_t priority)
    {
        const uint64_t generation = terrainGeneration_.load();
        const uint64_t key = chunkKey(chunkX, chunkZ);
        RuntimeChunk& chunk = ensureRuntimeChunk(chunkX, chunkZ, generation);
        chunk.meshTicket = generation;
        chunk.bestPriority = std::min(chunk.bestPriority, priority);

        const auto readyStart = std::chrono::steady_clock::now();
        if (chunkMeshReady(key))
        {
            const auto readyEnd = std::chrono::steady_clock::now();
            frameWantMeshReadyMs_ += std::chrono::duration<double, std::milli>(readyEnd - readyStart).count();
            return;
        }
        const auto readyEnd = std::chrono::steady_clock::now();
        frameWantMeshReadyMs_ += std::chrono::duration<double, std::milli>(readyEnd - readyStart).count();

        const auto dependStart = std::chrono::steady_clock::now();
        wantFull(chunkX, chunkZ, priority);
        tryQueueMeshIfReady(chunkX, chunkZ);
        const auto dependEnd = std::chrono::steady_clock::now();
        frameWantMeshDependMs_ += std::chrono::duration<double, std::milli>(dependEnd - dependStart).count();
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

        RuntimeChunk& current = runtimeChunks_[key];
        if (current.genState == ChunkGenState::Featuring)
        {
            publishFeatureSlots(current);
        }
        else if (current.genState == ChunkGenState::Full || current.genState == ChunkGenState::Meshed)
        {
            publishFeatureSlots(current);
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

    Renderer::SaveChunkSnapshot Renderer::makeSaveSnapshot(const RuntimeChunk& chunk) const
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
        const auto enqueueStart = std::chrono::steady_clock::now();
        const auto recordEnqueueMs = [&]()
        {
            const auto enqueueEnd = std::chrono::steady_clock::now();
            frameSaveEnqueueMs_ += std::chrono::duration<double, std::milli>(enqueueEnd - enqueueStart).count();
        };

        const uint64_t key = storageChunkKey(snapshot.chunkX, snapshot.chunkZ);
        if (snapshot.genState == ChunkGenState::Meshed)
        {
            snapshot.genState = ChunkGenState::Full;
        }

        const auto hasIncomingFeatureSlots = [](const SaveChunkSnapshot& value)
        {
            for (const FeatureWriteListPtr& slot : value.incomingFeatureSlots)
            {
                if (slot && !slot->empty())
                {
                    return true;
                }
            }
            return false;
        };

        if (snapshot.hasData && !snapshot.forceSave)
        {
            recordEnqueueMs();
            return;
        }
        if (!snapshot.hasData && snapshot.incomingFeatureMask == 0 && !hasIncomingFeatureSlots(snapshot))
        {
            recordEnqueueMs();
            return;
        }

        if (snapshot.genState == ChunkGenState::Full)
        {
            snapshot.incomingFeatureMask = 0;
            snapshot.incomingFeatureSlots = {};
            if (snapshot.hasData)
            {
                std::lock_guard<std::mutex> lock(savedChunkMutex_);
                const auto savedIt = savedCleanRevisions_.find(key);
                if (savedIt != savedCleanRevisions_.end() && savedIt->second == snapshot.revision)
                {
                    recordEnqueueMs();
                    return;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(saveJobMutex_);
            SaveChunkSnapshot& pending = pendingSaveSnapshots_[key];
            if (snapshot.hasData)
            {
                const SaveChunkSnapshot previous = pending;
                if (snapshot.hasSavedBacking && !snapshot.forceSave && previous.hasData &&
                    previous.genState == ChunkGenState::Full &&
                    (snapshot.genState != ChunkGenState::Full || previous.revision > snapshot.revision))
                {
                    recordEnqueueMs();
                    return;
                }
                pending = snapshot;
                if (pending.genState != ChunkGenState::Full)
                {
                    pending.incomingFeatureMask |= previous.incomingFeatureMask;
                    for (size_t slot = 0; slot < FeatureNeighborCount; ++slot)
                    {
                        if (!pending.incomingFeatureSlots[slot])
                        {
                            pending.incomingFeatureSlots[slot] = previous.incomingFeatureSlots[slot];
                        }
                    }
                }
            }
            else
            {
                if (pending.chunkX == 0 && pending.chunkZ == 0 && key != storageChunkKey(0, 0))
                {
                    pending.chunkX = snapshot.chunkX;
                    pending.chunkZ = snapshot.chunkZ;
                }
                pending.incomingFeatureMask |= snapshot.incomingFeatureMask;
                for (size_t slot = 0; slot < FeatureNeighborCount; ++slot)
                {
                    if (snapshot.incomingFeatureSlots[slot])
                    {
                        pending.incomingFeatureSlots[slot] = snapshot.incomingFeatureSlots[slot];
                    }
                }
            }
            saveJobs_.push_back(std::move(snapshot));
        }
        saveJobCondition_.notify_one();
        recordEnqueueMs();
    }

    void Renderer::enqueueSaveAllRuntimeChunks()
    {
        for (const auto& entry : runtimeChunks_)
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
        droppedItems_.clear();
        playerInventorySlots_.fill(ItemStack{});
        inventoryCursorStack_ = {};
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
        enqueueSaveAllRuntimeChunks();
        stopSaveWorker();
        vkDeviceWaitIdle(device_);
        destroyAllTerrainChunks();
        {
            std::lock_guard<std::mutex> lock(savedChunkMutex_);
            savedCleanRevisions_.clear();
            pendingSaveSnapshots_.clear();
        }
        {
            std::lock_guard<std::mutex> lock(regionHeaderCacheMutex_);
            regionHeaderCache_.clear();
        }
        terrainLoadRequested_ = false;
        blockBreakParticles_.clear();
        droppedItems_.clear();
        playerInventorySlots_.fill(ItemStack{});
        inventoryCursorStack_ = {};
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

    void Renderer::saveChunkSnapshot(const SaveChunkSnapshot& snapshot)
    {
        const auto chunkHasIncomingFeatureSlots = [](const SaveChunkSnapshot& value)
        {
            for (const FeatureWriteListPtr& slot : value.incomingFeatureSlots)
            {
                if (slot && !slot->empty())
                {
                    return true;
                }
            }
            return false;
        };

        if (!snapshot.hasData && snapshot.incomingFeatureMask == 0 && !chunkHasIncomingFeatureSlots(snapshot))
        {
            return;
        }

        auto serializePayload = [&](const SaveChunkSnapshot& value)
        {
            std::vector<uint8_t> payload;
            writeU8(payload, static_cast<uint8_t>(value.genState));
            writeU8(payload, value.incomingFeatureMask);
            for (const FeatureWriteListPtr& slot : value.incomingFeatureSlots)
            {
                const size_t count = slot ? slot->size() : 0;
                writeU16(payload, static_cast<uint16_t>(std::min<size_t>(count, std::numeric_limits<uint16_t>::max())));
            }

            const std::vector<uint16_t>* blocks = nullptr;
            if (value.chunkData && !value.chunkData->blocks.empty())
            {
                blocks = &value.chunkData->blocks;
            }
            else if (!value.blocks.empty())
            {
                blocks = &value.blocks;
            }

            const std::vector<uint16_t>* fluids = nullptr;
            if (value.chunkData && !value.chunkData->fluids.empty())
            {
                fluids = &value.chunkData->fluids;
            }
            else if (!value.fluids.empty())
            {
                fluids = &value.fluids;
            }

            const std::array<uint8_t, ChunkColumnCount>* temperature = value.chunkData ? &value.chunkData->temperature : &value.temperature;
            const std::array<uint8_t, ChunkColumnCount>* precipitation = value.chunkData ? &value.chunkData->precipitation : &value.precipitation;

            auto writeRuns = [&](const std::vector<uint16_t>* values)
            {
                if (!value.hasData || !values || values->empty())
                {
                    writeU32(payload, 0);
                    return;
                }

                const size_t runCountOffset = payload.size();
                writeU32(payload, 0);
                uint32_t runCount = 0;
                uint32_t current = (*values)[0];
                uint32_t count = 1;
                for (size_t i = 1; i < values->size(); ++i)
                {
                    const uint32_t item = (*values)[i];
                    if (item == current && count < std::numeric_limits<uint32_t>::max())
                    {
                        ++count;
                        continue;
                    }
                    writeU32(payload, current);
                    writeU32(payload, count);
                    ++runCount;
                    current = item;
                    count = 1;
                }
                writeU32(payload, current);
                writeU32(payload, count);
                ++runCount;
                writeU32At(payload, runCountOffset, runCount);
            };

            writeRuns(blocks);
            writeRuns(fluids);
            if (value.hasData)
            {
                payload.insert(payload.end(), temperature->begin(), temperature->end());
                payload.insert(payload.end(), precipitation->begin(), precipitation->end());
            }

            for (const FeatureWriteListPtr& slot : value.incomingFeatureSlots)
            {
                if (!slot)
                {
                    continue;
                }

                size_t written = 0;
                for (const FeatureWrite& write : *slot)
                {
                    if (written >= std::numeric_limits<uint16_t>::max())
                    {
                        break;
                    }
                    writeU8(payload, static_cast<uint8_t>(std::clamp(write.localX, 0, ChunkSizeX - 1)));
                    writeU8(payload, static_cast<uint8_t>(std::clamp(write.localZ, 0, ChunkSizeZ - 1)));
                    writeU16(payload, static_cast<uint16_t>(std::clamp(write.y, 0, ChunkSizeY - 1)));
                    writeU32(payload, write.block);
                    ++written;
                }
            }
            writeU64(payload, value.revision);
            return payload;
        };

        auto deserializePayload = [&](const std::vector<uint8_t>& payload, int chunkX, int chunkZ) -> std::optional<SaveChunkSnapshot>
        {
            try
            {
                SaveChunkSnapshot value{};
                value.chunkX = chunkX;
                value.chunkZ = chunkZ;
                size_t offset = 0;
                value.genState = static_cast<ChunkGenState>(readU8(payload, offset));
                value.incomingFeatureMask = readU8(payload, offset);
                std::array<uint16_t, FeatureNeighborCount> featureCounts{};
                for (uint16_t& count : featureCounts)
                {
                    count = readU16(payload, offset);
                }

                const uint32_t blockRunCount = readU32(payload, offset);
                if (blockRunCount > 0)
                {
                    value.hasData = true;
                    value.blocks.reserve(ChunkBlockCount);
                    uint64_t totalCount = 0;
                    for (uint32_t run = 0; run < blockRunCount; ++run)
                    {
                        const uint16_t block = static_cast<uint16_t>(readU32(payload, offset) & 0xFFFFu);
                        const uint32_t count = readU32(payload, offset);
                        totalCount += count;
                        if (totalCount > ChunkBlockCount)
                        {
                            return std::nullopt;
                        }
                        value.blocks.insert(value.blocks.end(), count, block);
                    }
                    if (value.blocks.size() != ChunkBlockCount)
                    {
                        return std::nullopt;
                    }
                }

                const uint32_t fluidRunCount = readU32(payload, offset);
                if (fluidRunCount > 0)
                {
                    value.hasData = true;
                    value.fluids.reserve(ChunkBlockCount);
                    uint64_t totalCount = 0;
                    for (uint32_t run = 0; run < fluidRunCount; ++run)
                    {
                        const uint16_t fluid = static_cast<uint16_t>(readU32(payload, offset) & 0xFFFFu);
                        const uint32_t count = readU32(payload, offset);
                        totalCount += count;
                        if (totalCount > ChunkBlockCount)
                        {
                            return std::nullopt;
                        }
                        value.fluids.insert(value.fluids.end(), count, fluid);
                    }
                    if (value.fluids.size() != ChunkBlockCount)
                    {
                        return std::nullopt;
                    }
                }

                if (value.hasData)
                {
                    for (uint8_t& item : value.temperature)
                    {
                        item = readU8(payload, offset);
                    }
                    for (uint8_t& item : value.precipitation)
                    {
                        item = readU8(payload, offset);
                    }
                }

                for (size_t slot = 0; slot < FeatureNeighborCount; ++slot)
                {
                    if (featureCounts[slot] == 0)
                    {
                        continue;
                    }
                    auto writes = std::make_shared<FeatureWriteList>();
                    writes->reserve(featureCounts[slot]);
                    for (uint16_t i = 0; i < featureCounts[slot]; ++i)
                    {
                        FeatureWrite write{};
                        write.localX = readU8(payload, offset);
                        write.localZ = readU8(payload, offset);
                        write.y = readU16(payload, offset);
                        write.block = static_cast<uint16_t>(readU32(payload, offset) & 0xFFFFu);
                        writes->push_back(write);
                    }
                    value.incomingFeatureSlots[slot] = std::move(writes);
                }
                if (offset + 8 <= payload.size())
                {
                    value.revision = readU64(payload, offset);
                }
                return value;
            }
            catch (...)
            {
                return std::nullopt;
            }
        };

        auto applySavedIncomingFeatureSlots = [&](SaveChunkSnapshot& value)
        {
            if (!value.hasData)
            {
                return false;
            }

            bool changed = false;
            for (const FeatureWriteListPtr& writes : value.incomingFeatureSlots)
            {
                if (!writes)
                {
                    continue;
                }
                for (const FeatureWrite& write : *writes)
                {
                    if (write.block != BlockLeaves ||
                        write.localX < 0 || write.localX >= ChunkSizeX ||
                        write.localZ < 0 || write.localZ >= ChunkSizeZ ||
                        write.y < 0 || write.y >= ChunkSizeY)
                    {
                        continue;
                    }

                    const size_t index = static_cast<size_t>((write.y * ChunkSizeZ + write.localZ) * ChunkSizeX + write.localX);
                    uint16_t& existing = value.blocks[index];
                    if (existing == BlockAir || existing == BlockPlant)
                    {
                        existing = BlockLeaves;
                        changed = true;
                    }
                }
            }
            return changed;
        };

        const int storageChunkX = wrapChunkCoordinate(snapshot.chunkX);
        const int storageChunkZ = wrapChunkCoordinate(snapshot.chunkZ);
        const int regionX = storageChunkX / RegionSizeChunks;
        const int regionZ = storageChunkZ / RegionSizeChunks;
        const int localX = storageChunkX % RegionSizeChunks;
        const int localZ = storageChunkZ % RegionSizeChunks;
        const size_t entryIndex = static_cast<size_t>(localZ * RegionSizeChunks + localX);
        const std::filesystem::path regionDirectory = activeWorldDirectory_ / "regions";
        std::filesystem::create_directories(regionDirectory);
        const std::filesystem::path regionPath = regionDirectory / ("r." + std::to_string(regionX) + "." + std::to_string(regionZ) + ".region");

        if (!std::filesystem::exists(regionPath))
        {
            std::ofstream createFile(regionPath, std::ios::binary);
            std::vector<uint8_t> emptyHeader(RegionSectorSize, 0);
            createFile.write(reinterpret_cast<const char*>(emptyHeader.data()), static_cast<std::streamsize>(emptyHeader.size()));
        }

        std::fstream file(regionPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open())
        {
            return;
        }

        std::vector<uint8_t> header(RegionSectorSize, 0);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));

        const size_t entryOffset = entryIndex * RegionChunkEntrySize;
        const uint32_t existingOffsetSector = readU32At(header, entryOffset);
        const uint32_t existingSectorCount = readU32At(header, entryOffset + 4);
        const uint32_t existingStoredSize = readU32At(header, entryOffset + 8);
        const uint32_t existingRawSize = readU32At(header, entryOffset + 12);

        std::optional<SaveChunkSnapshot> existingSnapshot;
        if (existingOffsetSector != 0 && existingSectorCount != 0 && existingStoredSize != 0 && existingRawSize != 0)
        {
            std::vector<uint8_t> stored(existingStoredSize);
            file.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(existingOffsetSector) * RegionSectorSize));
            file.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size()));
            try
            {
                existingSnapshot = deserializePayload(lz4DecodeBlock(stored, existingRawSize), snapshot.chunkX, snapshot.chunkZ);
            }
            catch (...)
            {
                existingSnapshot.reset();
            }
        }

        SaveChunkSnapshot merged = existingSnapshot.value_or(SaveChunkSnapshot{});
        if (!existingSnapshot)
        {
            merged.chunkX = snapshot.chunkX;
            merged.chunkZ = snapshot.chunkZ;
            merged.genState = ChunkGenState::Empty;
        }

        if (snapshot.hasData)
        {
            const std::optional<SaveChunkSnapshot> previous = existingSnapshot;
            if (snapshot.hasSavedBacking && !snapshot.forceSave && previous && previous->hasData &&
                previous->genState == ChunkGenState::Full &&
                (snapshot.genState != ChunkGenState::Full || previous->revision > snapshot.revision))
            {
                return;
            }
            merged = snapshot;
            if (merged.genState == ChunkGenState::Meshed)
            {
                merged.genState = ChunkGenState::Full;
            }
            if (merged.genState == ChunkGenState::Full)
            {
                merged.incomingFeatureMask = 0;
                merged.incomingFeatureSlots = {};
            }
            else if (previous)
            {
                merged.incomingFeatureMask |= previous->incomingFeatureMask;
                for (size_t slot = 0; slot < FeatureNeighborCount; ++slot)
                {
                    if (!merged.incomingFeatureSlots[slot])
                    {
                        merged.incomingFeatureSlots[slot] = previous->incomingFeatureSlots[slot];
                    }
                }
            }
        }
        else
        {
            if (merged.genState != ChunkGenState::Full)
            {
                merged.incomingFeatureMask |= snapshot.incomingFeatureMask;
            }
            for (size_t slot = 0; slot < FeatureNeighborCount; ++slot)
            {
                if (!snapshot.incomingFeatureSlots[slot])
                {
                    continue;
                }
                if (merged.genState == ChunkGenState::Full && merged.hasData)
                {
                    std::array<FeatureWriteListPtr, FeatureNeighborCount> singleSlot{};
                    singleSlot[slot] = snapshot.incomingFeatureSlots[slot];
                    merged.incomingFeatureSlots = singleSlot;
                    if (applySavedIncomingFeatureSlots(merged))
                    {
                        merged.revision += 1;
                    }
                    merged.incomingFeatureSlots = {};
                    merged.incomingFeatureMask = 0;
                }
                else
                {
                    merged.incomingFeatureSlots[slot] = snapshot.incomingFeatureSlots[slot];
                    merged.incomingFeatureMask |= static_cast<uint8_t>(1u << static_cast<uint32_t>(slot));
                }
            }
        }

        if (merged.genState == ChunkGenState::Meshed)
        {
            merged.genState = ChunkGenState::Full;
        }
        if (merged.genState == ChunkGenState::Full)
        {
            merged.incomingFeatureSlots = {};
            merged.incomingFeatureMask = 0;
        }

        const std::vector<uint8_t> rawPayload = serializePayload(merged);
        const std::vector<uint8_t> storedPayload = lz4EncodeLiteralBlock(rawPayload);
        const uint32_t storedSize = static_cast<uint32_t>(storedPayload.size());
        const uint32_t rawSize = static_cast<uint32_t>(rawPayload.size());
        const uint32_t sectorCount = (storedSize + RegionSectorSize - 1u) / RegionSectorSize;

        file.seekp(0, std::ios::end);
        std::streamoff endOffset = file.tellp();
        const uint32_t paddingBefore = static_cast<uint32_t>((RegionSectorSize - (static_cast<uint64_t>(endOffset) % RegionSectorSize)) % RegionSectorSize);
        if (paddingBefore > 0)
        {
            std::vector<uint8_t> padding(paddingBefore, 0);
            file.write(reinterpret_cast<const char*>(padding.data()), static_cast<std::streamsize>(padding.size()));
            endOffset += paddingBefore;
        }

        const uint32_t offsetSector = static_cast<uint32_t>(static_cast<uint64_t>(endOffset) / RegionSectorSize);
        file.write(reinterpret_cast<const char*>(storedPayload.data()), static_cast<std::streamsize>(storedPayload.size()));
        const uint32_t paddingAfter = sectorCount * RegionSectorSize - storedSize;
        if (paddingAfter > 0)
        {
            std::vector<uint8_t> padding(paddingAfter, 0);
            file.write(reinterpret_cast<const char*>(padding.data()), static_cast<std::streamsize>(padding.size()));
        }

        writeU32At(header, entryOffset, offsetSector);
        writeU32At(header, entryOffset + 4, sectorCount);
        writeU32At(header, entryOffset + 8, storedSize);
        writeU32At(header, entryOffset + 12, rawSize);
        file.seekp(0);
        file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));

        {
            std::lock_guard<std::mutex> lock(regionHeaderCacheMutex_);
            RegionHeaderCache& cache = regionHeaderCache_[chunkKey(regionX, regionZ)];
            cache.exists = true;
            cache.entries[entryIndex] = RegionChunkEntry{
                offsetSector,
                sectorCount,
                storedSize,
                rawSize};
        }

        if (merged.genState == ChunkGenState::Full && merged.hasData)
        {
            std::lock_guard<std::mutex> lock(savedChunkMutex_);
            savedCleanRevisions_[storageChunkKey(merged.chunkX, merged.chunkZ)] = merged.revision;
        }
    }

    std::optional<Renderer::SaveChunkSnapshot> Renderer::loadChunkSnapshot(int chunkX, int chunkZ)
    {
        const uint64_t key = storageChunkKey(chunkX, chunkZ);
        auto loadMiss = [this]() -> std::optional<SaveChunkSnapshot>
        {
            loadMissCount_.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        };

        {
            std::lock_guard<std::mutex> lock(saveJobMutex_);
            const auto pendingIt = pendingSaveSnapshots_.find(key);
            if (pendingIt != pendingSaveSnapshots_.end())
            {
                loadPendingHitCount_.fetch_add(1, std::memory_order_relaxed);
                SaveChunkSnapshot snapshot = pendingIt->second;
                snapshot.chunkX = chunkX;
                snapshot.chunkZ = chunkZ;
                return snapshot;
            }
        }

        const int storageChunkX = wrapChunkCoordinate(chunkX);
        const int storageChunkZ = wrapChunkCoordinate(chunkZ);
        const int regionX = storageChunkX / RegionSizeChunks;
        const int regionZ = storageChunkZ / RegionSizeChunks;
        const int localX = storageChunkX % RegionSizeChunks;
        const int localZ = storageChunkZ % RegionSizeChunks;
        const size_t entryIndex = static_cast<size_t>(localZ * RegionSizeChunks + localX);
        const std::filesystem::path regionPath = activeWorldDirectory_ /
            "regions" /
            ("r." + std::to_string(regionX) + "." + std::to_string(regionZ) + ".region");

        const uint64_t regionCacheKey = chunkKey(regionX, regionZ);
        RegionChunkEntry entry{};
        bool regionExists = false;
        bool regionCached = false;
        {
            std::lock_guard<std::mutex> lock(regionHeaderCacheMutex_);
            const auto cacheIt = regionHeaderCache_.find(regionCacheKey);
            if (cacheIt != regionHeaderCache_.end())
            {
                regionCached = true;
                regionExists = cacheIt->second.exists;
                if (regionExists)
                {
                    entry = cacheIt->second.entries[entryIndex];
                }
            }
        }

        if (!regionCached)
        {
            RegionHeaderCache cache{};
            std::ifstream headerFile(regionPath, std::ios::binary);
            if (headerFile.is_open())
            {
                std::vector<uint8_t> header(RegionSectorSize, 0);
                headerFile.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
                if (headerFile)
                {
                    cache.exists = true;
                    for (size_t i = 0; i < cache.entries.size(); ++i)
                    {
                        const size_t entryOffset = i * RegionChunkEntrySize;
                        cache.entries[i] = RegionChunkEntry{
                            readU32At(header, entryOffset),
                            readU32At(header, entryOffset + 4),
                            readU32At(header, entryOffset + 8),
                            readU32At(header, entryOffset + 12)};
                    }
                    entry = cache.entries[entryIndex];
                    regionExists = true;
                }
            }

            {
                std::lock_guard<std::mutex> lock(regionHeaderCacheMutex_);
                const auto cacheIt = regionHeaderCache_.find(regionCacheKey);
                if (cacheIt == regionHeaderCache_.end())
                {
                    regionHeaderCache_.emplace(regionCacheKey, cache);
                }
                else if (!cacheIt->second.exists && cache.exists)
                {
                    cacheIt->second = cache;
                }
            }
        }

        if (!regionExists ||
            entry.offsetSector == 0 ||
            entry.sectorCount == 0 ||
            entry.storedSize == 0 ||
            entry.rawSize == 0)
        {
            return loadMiss();
        }

        std::ifstream file(regionPath, std::ios::binary);
        if (!file.is_open())
        {
            return loadMiss();
        }

        std::vector<uint8_t> stored(entry.storedSize);
        file.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(entry.offsetSector) * RegionSectorSize));
        file.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size()));
        if (!file)
        {
            return loadMiss();
        }

        std::vector<uint8_t> payload;
        try
        {
            payload = lz4DecodeBlock(stored, entry.rawSize);
        }
        catch (...)
        {
            return loadMiss();
        }

        try
        {
            SaveChunkSnapshot snapshot{};
            snapshot.chunkX = chunkX;
            snapshot.chunkZ = chunkZ;
            size_t offset = 0;
            snapshot.genState = static_cast<ChunkGenState>(readU8(payload, offset));
            snapshot.incomingFeatureMask = readU8(payload, offset);
            std::array<uint16_t, FeatureNeighborCount> featureCounts{};
            for (uint16_t& count : featureCounts)
            {
                count = readU16(payload, offset);
            }

            const uint32_t blockRunCount = readU32(payload, offset);
            if (blockRunCount > 0)
            {
                snapshot.hasData = true;
                snapshot.blocks.reserve(ChunkBlockCount);
                uint64_t totalCount = 0;
                for (uint32_t run = 0; run < blockRunCount; ++run)
                {
                    const uint16_t block = static_cast<uint16_t>(readU32(payload, offset) & 0xFFFFu);
                    const uint32_t count = readU32(payload, offset);
                    totalCount += count;
                    if (totalCount > ChunkBlockCount)
                    {
                        return loadMiss();
                    }
                    snapshot.blocks.insert(snapshot.blocks.end(), count, block);
                }
                if (snapshot.blocks.size() != ChunkBlockCount)
                {
                    return loadMiss();
                }
            }

            const uint32_t fluidRunCount = readU32(payload, offset);
            if (fluidRunCount > 0)
            {
                snapshot.hasData = true;
                snapshot.fluids.reserve(ChunkBlockCount);
                uint64_t totalCount = 0;
                for (uint32_t run = 0; run < fluidRunCount; ++run)
                {
                    const uint16_t fluid = static_cast<uint16_t>(readU32(payload, offset) & 0xFFFFu);
                    const uint32_t count = readU32(payload, offset);
                    totalCount += count;
                    if (totalCount > ChunkBlockCount)
                    {
                        return loadMiss();
                    }
                    snapshot.fluids.insert(snapshot.fluids.end(), count, fluid);
                }
                if (snapshot.fluids.size() != ChunkBlockCount)
                {
                    return loadMiss();
                }
            }

            if (snapshot.hasData)
            {
                for (uint8_t& item : snapshot.temperature)
                {
                    item = readU8(payload, offset);
                }
                for (uint8_t& item : snapshot.precipitation)
                {
                    item = readU8(payload, offset);
                }
            }

            for (size_t slot = 0; slot < FeatureNeighborCount; ++slot)
            {
                if (featureCounts[slot] == 0)
                {
                    continue;
                }

                auto writes = std::make_shared<FeatureWriteList>();
                writes->reserve(featureCounts[slot]);
                for (uint16_t i = 0; i < featureCounts[slot]; ++i)
                {
                    FeatureWrite write{};
                    write.localX = readU8(payload, offset);
                    write.localZ = readU8(payload, offset);
                    write.y = readU16(payload, offset);
                    write.block = static_cast<uint16_t>(readU32(payload, offset) & 0xFFFFu);
                    writes->push_back(write);
                }
                snapshot.incomingFeatureSlots[slot] = std::move(writes);
            }
            if (offset + 8 <= payload.size())
            {
                snapshot.revision = readU64(payload, offset);
            }

            if (snapshot.genState == ChunkGenState::Full && snapshot.hasData)
            {
                std::lock_guard<std::mutex> lock(savedChunkMutex_);
                savedCleanRevisions_[key] = snapshot.revision;
            }
            loadRegionHitCount_.fetch_add(1, std::memory_order_relaxed);
            return snapshot;
        }
        catch (...)
        {
            return loadMiss();
        }
    }

    Renderer::RuntimeChunk Renderer::runtimeChunkFromSnapshot(const SaveChunkSnapshot& snapshot, uint64_t generation)
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
            data->generation = generation;
            data->revision = snapshot.revision;
            data->chunkX = snapshot.chunkX;
            data->chunkZ = snapshot.chunkZ;
            data->emptySubchunks.fill(true);
            for (int subchunkY = 0; subchunkY < SubchunksPerChunk; ++subchunkY)
            {
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
                            if (data->blocks[index] != BlockAir)
                            {
                                empty = false;
                                break;
                            }
                        }
                    }
                }
                data->emptySubchunks[static_cast<size_t>(subchunkY)] = empty;
            }
            chunk.data = std::move(data);
        }

        if (chunk.genState == ChunkGenState::Full)
        {
            std::lock_guard<std::mutex> lock(savedChunkMutex_);
            savedCleanRevisions_[storageChunkKey(chunk.chunkX, chunk.chunkZ)] = chunk.data ? chunk.data->revision : 0;
        }
        return chunk;
    }

    std::shared_ptr<Renderer::ChunkData> Renderer::buildChunkData(int chunkX, int chunkZ) const
    {
        auto chunk = std::make_shared<ChunkData>();
        chunk->chunkX = chunkX;
        chunk->chunkZ = chunkZ;
        chunk->blocks.assign(ChunkBlockCount, BlockAir);
        chunk->fluids.assign(ChunkBlockCount, FluidNone);
        chunk->emptySubchunks.fill(true);
        populateChunkClimate(*chunk);

        std::array<int, Renderer::ChunkColumnCount> heights = buildChunkHeightmap(chunkX, chunkZ);
        int maxHeight = 0;
        for (int height : heights)
        {
            maxHeight = std::max(maxHeight, height);
        }

        const int solidHeightLimit = std::min(maxHeight, ChunkSizeY);
        const int filledSubchunks = std::min(SubchunksPerChunk, (solidHeightLimit + SubchunkSize - 1) / SubchunkSize);
        for (int subchunkY = 0; subchunkY < filledSubchunks; ++subchunkY)
        {
            chunk->emptySubchunks[static_cast<size_t>(subchunkY)] = false;
        }

        constexpr size_t BlocksPerLayer = ChunkSizeX * ChunkSizeZ;
        const int worldXStart = chunkX * ChunkSizeX;
        const int worldZStart = chunkZ * ChunkSizeZ;
        for (int y = 0; y < solidHeightLimit; ++y)
        {
            uint16_t* layer = chunk->blocks.data() + static_cast<size_t>(y) * BlocksPerLayer;
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                    layer[column] = baseTerrainBlockForColumn(worldXStart + localX, y, worldZStart + localZ, heights[column]);
                }
            }
        }

        constexpr uint16_t FullWater = packFluid(FluidWater, FluidFullAmount);
        const int seaY = std::clamp(seaLevel_, 0, ChunkSizeY - 1);
        for (int y = 0; y <= seaY; ++y)
        {
            uint16_t* fluidLayer = chunk->fluids.data() + static_cast<size_t>(y) * BlocksPerLayer;
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                    if (y >= heights[column])
                    {
                        fluidLayer[column] = FullWater;
                    }
                }
            }
        }

        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            for (int localX = 0; localX < ChunkSizeX; ++localX)
            {
                const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                const int height = heights[column];
                const int surfaceY = height - 1;
                if (surfaceY < 0 || surfaceY >= ChunkSizeY)
                {
                    continue;
                }

                const size_t surfaceIndex = static_cast<size_t>((surfaceY * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                if (surfaceIndex >= chunk->blocks.size() || chunk->blocks[surfaceIndex] == BlockBedrock)
                {
                    continue;
                }

                const bool waterAbove = height >= 0 && height < ChunkSizeY &&
                    chunk->fluids[static_cast<size_t>((height * ChunkSizeZ + localZ) * ChunkSizeX + localX)] != FluidNone;
                const uint16_t surfaceBlock = waterAbove ? BlockSand : BlockGrass;
                const uint16_t subsurfaceBlock = waterAbove ? BlockSand : BlockDirt;
                chunk->blocks[surfaceIndex] = surfaceBlock;

                const int bedrockHeight = bedrockHeightAt(worldXStart + localX, worldZStart + localZ);
                const int subsurfaceStartY = std::max(bedrockHeight, height - 5);
                for (int y = subsurfaceStartY; y < surfaceY; ++y)
                {
                    if (y < 0 || y >= ChunkSizeY)
                    {
                        continue;
                    }
                    const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                    if (index < chunk->blocks.size() && chunk->blocks[index] != BlockBedrock)
                    {
                        chunk->blocks[index] = subsurfaceBlock;
                    }
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
                    chunk->blocks[topIndex] == BlockGrass)
                {
                    const uint8_t placement = worldRandom8(worldXStart + localX, height, worldZStart + localZ, PlantPlacementSalt);
                    uint16_t placedBlock = BlockAir;
                    if (placement <= PlantPlacementMax)
                    {
                        placedBlock = BlockPlant;
                    }
                    else if (placement <= StonePlacementMax)
                    {
                        placedBlock = BlockStoneProp;
                    }
                    else if (placement <= BranchPlacementMax)
                    {
                        placedBlock = BlockBranchProp;
                    }

                    if (placedBlock != BlockAir)
                    {
                        chunk->blocks[plantIndex] = placedBlock;
                        chunk->emptySubchunks[static_cast<size_t>(height / SubchunkSize)] = false;
                    }
                }
            }
        }

        return chunk;
    }

    std::array<Renderer::FeatureWriteListPtr, Renderer::FeatureNeighborCount> Renderer::buildTreeFeatures(const std::shared_ptr<Renderer::ChunkData>& chunk, const std::array<int, Renderer::ChunkColumnCount>& heights) const
    {
        std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingSlots{};
        for (FeatureWriteListPtr& slot : outgoingSlots)
        {
            slot = std::make_shared<FeatureWriteList>();
        }

        auto outgoingSlotForTarget = [&](int targetChunkX, int targetChunkZ) -> FeatureWriteList*
        {
            const int offsetX = targetChunkX - chunk->chunkX;
            const int offsetZ = targetChunkZ - chunk->chunkZ;
            const std::optional<size_t> slotIndex = featureNeighborIndex(offsetX, offsetZ);
            if (!slotIndex)
            {
                return nullptr;
            }
            return outgoingSlots[*slotIndex].get();
        };

        auto canPlaceTrunk = [](uint16_t existing)
        {
            return existing == BlockAir || existing == BlockPlant || existing == BlockLeaves;
        };

        auto canPlaceLeaves = [](uint16_t existing)
        {
            return existing == BlockAir || existing == BlockPlant;
        };

        auto setInternalBlock = [&](int localX, int y, int localZ, uint16_t block)
        {
            if (localX < 0 || localX >= ChunkSizeX || localZ < 0 || localZ >= ChunkSizeZ || y < 0 || y >= ChunkSizeY)
            {
                return;
            }

            const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
            uint16_t& existing = chunk->blocks[index];
            const bool canPlace = block == BlockTrunk ? canPlaceTrunk(existing) : canPlaceLeaves(existing);
            if (canPlace)
            {
                existing = block;
                chunk->emptySubchunks[static_cast<size_t>(y / SubchunkSize)] = false;
            }
        };

        auto emitLeaves = [&](int worldX, int y, int worldZ)
        {
            if (y < 0 || y >= ChunkSizeY)
            {
                return;
            }

            const int targetChunkX = floorDiv(worldX, ChunkSizeX);
            const int targetChunkZ = floorDiv(worldZ, ChunkSizeZ);
            if (targetChunkX == chunk->chunkX && targetChunkZ == chunk->chunkZ)
            {
                setInternalBlock(positiveModulo(worldX, ChunkSizeX), y, positiveModulo(worldZ, ChunkSizeZ), BlockLeaves);
                return;
            }

            FeatureWriteList* writes = outgoingSlotForTarget(targetChunkX, targetChunkZ);
            if (writes == nullptr)
            {
                return;
            }

            writes->push_back(FeatureWrite{
                positiveModulo(worldX, ChunkSizeX),
                y,
                positiveModulo(worldZ, ChunkSizeZ),
                BlockLeaves});
        };

        const int worldXStart = chunk->chunkX * ChunkSizeX;
        const int worldZStart = chunk->chunkZ * ChunkSizeZ;
        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            for (int localX = 0; localX < ChunkSizeX; ++localX)
            {
                const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                const int height = std::clamp(heights[column], 0, ChunkSizeY);
                if (height <= 0 || height + 5 >= ChunkSizeY)
                {
                    continue;
                }

                const int worldX = worldXStart + localX;
                const int worldZ = worldZStart + localZ;
                const uint8_t vegetationRandom = worldRandom8(worldX, height, worldZ, PlantPlacementSalt);
                if (vegetationRandom < TreePlacementMin || vegetationRandom > TreePlacementMax)
                {
                    continue;
                }

                const size_t topIndex = static_cast<size_t>(((height - 1) * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                if (topIndex >= chunk->blocks.size() || chunk->blocks[topIndex] != BlockGrass)
                {
                    continue;
                }

                for (int y = height; y <= height + 3; ++y)
                {
                    setInternalBlock(localX, y, localZ, BlockTrunk);
                }

                for (int y = height + 2; y <= height + 3; ++y)
                {
                    for (int dz = -2; dz <= 2; ++dz)
                    {
                        for (int dx = -2; dx <= 2; ++dx)
                        {
                            if (std::abs(dx) == 2 && std::abs(dz) == 2)
                            {
                                continue;
                            }
                            emitLeaves(worldX + dx, y, worldZ + dz);
                        }
                    }
                }

                for (int dz = -1; dz <= 1; ++dz)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        emitLeaves(worldX + dx, height + 4, worldZ + dz);
                    }
                }
            }
        }

        return outgoingSlots;
    }

    bool Renderer::applyFeatureWrites(const std::shared_ptr<Renderer::ChunkData>& chunk, const std::array<FeatureWriteListPtr, FeatureNeighborCount>& incomingFeatureSlots) const
    {
        bool changed = false;
        for (const FeatureWriteListPtr& writes : incomingFeatureSlots)
        {
            if (!writes)
            {
                continue;
            }

            for (const FeatureWrite& write : *writes)
            {
                if (write.block != BlockLeaves ||
                    write.localX < 0 || write.localX >= ChunkSizeX ||
                    write.localZ < 0 || write.localZ >= ChunkSizeZ ||
                    write.y < 0 || write.y >= ChunkSizeY)
                {
                    continue;
                }

                const size_t index = static_cast<size_t>((write.y * ChunkSizeZ + write.localZ) * ChunkSizeX + write.localX);
                uint16_t& existing = chunk->blocks[index];
                if (existing == BlockAir || existing == BlockPlant)
                {
                    existing = BlockLeaves;
                    chunk->emptySubchunks[static_cast<size_t>(write.y / SubchunkSize)] = false;
                    changed = true;
                }
            }
        }

        if (changed)
        {
            ++chunk->revision;
        }
        return changed;
    }

    void Renderer::acceptFeatureSlot(int targetChunkX, int targetChunkZ, size_t sourceSlot, FeatureWriteListPtr writes)
    {
        if (sourceSlot >= FeatureNeighborCount || !writes)
        {
            return;
        }

        const uint64_t targetKey = chunkKey(targetChunkX, targetChunkZ);
        auto existingTarget = runtimeChunks_.find(targetKey);
        if (existingTarget == runtimeChunks_.end() && desiredTerrainChunks_.find(targetKey) == desiredTerrainChunks_.end())
        {
            return;
        }

        RuntimeChunk& target = existingTarget != runtimeChunks_.end()
            ? existingTarget->second
            : runtimeChunks_[targetKey];
        target.chunkX = targetChunkX;
        target.chunkZ = targetChunkZ;

        if ((target.genState == ChunkGenState::Full || target.genState == ChunkGenState::Meshed) && target.data)
        {
            std::array<FeatureWriteListPtr, FeatureNeighborCount> singleSlot{};
            singleSlot[sourceSlot] = std::move(writes);
            if (applyFeatureWrites(target.data, singleSlot))
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
            const std::array<int, Renderer::ChunkColumnCount> heights = buildChunkHeightmap(sourceChunk.chunkX, sourceChunk.chunkZ);
            sourceChunk.outgoingFeatureSlots = buildTreeFeatures(sourceChunk.data, heights);
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
        auto chunkIt = runtimeChunks_.find(key);
        if (chunkIt == runtimeChunks_.end())
        {
            return;
        }

        RuntimeChunk& chunk = chunkIt->second;
        const uint64_t generation = terrainGeneration_.load();
        if (chunk.fullTicket != generation ||
            chunk.genState != ChunkGenState::Featuring ||
            !chunk.data ||
            chunk.finalizeQueuedTicket == generation)
        {
            return;
        }

        if ((chunk.incomingFeatureMask & AllFeatureSourcesMask) != AllFeatureSourcesMask)
        {
            return;
        }

        TerrainJob job{};
        job.type = TerrainJob::Type::FinalizeFeatures;
        job.generation = generation;
        job.priority = chunk.bestPriority;
        job.chunkX = chunk.chunkX;
        job.chunkZ = chunk.chunkZ;
        job.chunk = chunk.data;
        job.incomingFeatureSlots = chunk.incomingFeatureSlots;
        chunk.finalizeQueuedTicket = generation;
        enqueueTerrainJob(std::move(job));
    }

    void Renderer::tryQueueMeshIfReady(int chunkX, int chunkZ)
    {
        const uint64_t key = chunkKey(chunkX, chunkZ);
        auto targetIt = runtimeChunks_.find(key);
        const uint64_t generation = terrainGeneration_.load();
        if (targetIt == runtimeChunks_.end() ||
            desiredRenderChunks_.find(key) == desiredRenderChunks_.end() ||
            targetIt->second.meshTicket != generation ||
            requestedMeshJobs_.find(key) != requestedMeshJobs_.end() ||
            targetIt->second.meshQueuedTicket == generation ||
            (targetIt->second.genState != ChunkGenState::Full && targetIt->second.genState != ChunkGenState::Meshed))
        {
            return;
        }

        std::array<std::shared_ptr<ChunkData>, 9> chunks{};
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const auto chunkIt = runtimeChunks_.find(chunkKey(chunkX + dx, chunkZ + dz));
                if (chunkIt == runtimeChunks_.end() || !chunkIt->second.data ||
                    chunkIt->second.genState == ChunkGenState::Empty)
                {
                    return;
                }
                chunks[static_cast<size_t>((dz + 1) * 3 + (dx + 1))] = chunkIt->second.data;
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
        job.priority = targetIt->second.bestPriority;
        job.chunkX = chunkX;
        job.chunkZ = chunkZ;
        job.meshChunks = chunks;
        requestedMeshJobs_.insert(key);
        targetIt->second.meshQueuedTicket = generation;
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
        if (y < 0 || y >= ChunkSizeY)
        {
            return false;
        }

        const int chunkX = floorDiv(x, ChunkSizeX);
        const int chunkZ = floorDiv(z, ChunkSizeZ);
        const uint64_t key = chunkKey(chunkX, chunkZ);
        auto chunkIt = runtimeChunks_.find(key);
        if (chunkIt == runtimeChunks_.end() || !chunkIt->second.data ||
            (chunkIt->second.genState != ChunkGenState::Full && chunkIt->second.genState != ChunkGenState::Meshed))
        {
            return false;
        }

        const std::shared_ptr<ChunkData>& chunk = chunkIt->second.data;
        const int localX = positiveModulo(x, ChunkSizeX);
        const int localZ = positiveModulo(z, ChunkSizeZ);
        const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
        if (index >= chunk->blocks.size() || chunk->blocks[index] == block)
        {
            return false;
        }

        chunk->blocks[index] = block;
        ++chunk->revision;
        chunkIt->second.dataDirtyForSave = true;
        updateChunkEmptySubchunk(chunk, y / SubchunkSize);
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
        auto chunkIt = runtimeChunks_.find(key);
        if (chunkIt == runtimeChunks_.end() || !chunkIt->second.data || desiredTerrainChunks_.find(key) == desiredTerrainChunks_.end())
        {
            return;
        }

        const uint64_t generation = terrainGeneration_.load();
        chunkIt->second.data->generation = generation;
        const uint64_t revision = chunkIt->second.data->revision;
        TerrainBuildData mesh = buildEditedSubchunkMesh(chunkIt->second.data, subchunkY);

        requestedMeshJobs_.erase(key);
        chunkIt->second.meshQueuedTicket = 0;
        ChunkRenderData& renderData = terrainChunks_[key];
        renderData.revision = chunkIt->second.data->revision;
        renderData.chunkX = chunkX;
        renderData.chunkZ = chunkZ;
        if (chunkIt->second.data->revision != revision || chunkIt->second.data->generation != generation)
        {
            return;
        }

        TerrainMesh& targetMesh = renderData.rockSubchunks[static_cast<size_t>(subchunkY)];
        if (targetMesh.vertexBuffer != VK_NULL_HANDLE || targetMesh.indexBuffer != VK_NULL_HANDLE)
        {
            ChunkRenderData retired{};
            retired.rockSubchunks[static_cast<size_t>(subchunkY)] = std::move(targetMesh);
            targetMesh = {};
            retiredTerrainChunks_.push_back(RetiredChunkRenderData{
                static_cast<uint32_t>(MaxFramesInFlight + 1),
                std::move(retired)});
        }
        createTerrainBuffer(mesh, targetMesh);
        renderData.revision = chunkIt->second.data->revision;
        chunkIt->second.genState = ChunkGenState::Meshed;
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
                    meshingBlocks[meshingIndex(meshX, y, meshZ)] = generatedTerrainBlockForColumn(worldX, y, worldZ, height, seaLevel_);
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

            const PropMesh& mesh = meshIt->second;
            const uint32_t textureLayer = blockFaceTextureLayer(block, 0);
            const float mipDistanceScale = blockDefinition(block).mipDistanceScale;
            const std::array<float, 2> offset = randomBlockOffset(block, x, y, z);
            const float originX = static_cast<float>(x) - 0.5f + offset[0];
            const float originY = static_cast<float>(y);
            const float originZ = static_cast<float>(z) - 0.5f + offset[1];
            const uint8_t rotation = topFaceRotation(block, x, y, z);

            for (size_t offset = 0; offset + DpmQuadRenderFloatCount <= mesh.quads.size(); offset += DpmQuadRenderFloatCount)
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

    Renderer::TerrainBuildData Renderer::buildFluidSubchunkMesh(const std::array<std::shared_ptr<Renderer::ChunkData>, 9>& chunks, int subchunkY) const
    {
        TerrainBuildData result{};
        if (subchunkY < 0 || subchunkY >= SubchunksPerChunk || !chunks[4])
        {
            return result;
        }

        const std::shared_ptr<ChunkData>& chunk = chunks[4];
        if (chunk->fluids.size() != ChunkBlockCount)
        {
            return result;
        }

        auto sampleChunk = [&](int localX, int localZ, int& sampleX, int& sampleZ) -> const std::shared_ptr<ChunkData>&
        {
            int chunkOffsetX = 0;
            int chunkOffsetZ = 0;
            sampleX = localX;
            sampleZ = localZ;
            if (sampleX < 0)
            {
                chunkOffsetX = -1;
                sampleX += ChunkSizeX;
            }
            else if (sampleX >= ChunkSizeX)
            {
                chunkOffsetX = 1;
                sampleX -= ChunkSizeX;
            }

            if (sampleZ < 0)
            {
                chunkOffsetZ = -1;
                sampleZ += ChunkSizeZ;
            }
            else if (sampleZ >= ChunkSizeZ)
            {
                chunkOffsetZ = 1;
                sampleZ -= ChunkSizeZ;
            }

            static const std::shared_ptr<ChunkData> EmptyChunk;
            if (sampleX < 0 || sampleX >= ChunkSizeX || sampleZ < 0 || sampleZ >= ChunkSizeZ)
            {
                return EmptyChunk;
            }
            return chunks[static_cast<size_t>((chunkOffsetZ + 1) * 3 + (chunkOffsetX + 1))];
        };

        auto blockAt = [&](int localX, int y, int localZ) -> uint16_t
        {
            if (y < 0 || y >= ChunkSizeY)
            {
                return BlockAir;
            }

            int sampleX = localX;
            int sampleZ = localZ;
            const std::shared_ptr<ChunkData>& sample = sampleChunk(localX, localZ, sampleX, sampleZ);
            if (!sample || sample->blocks.size() != ChunkBlockCount)
            {
                return BlockAir;
            }

            return sample->blocks[static_cast<size_t>((y * ChunkSizeZ + sampleZ) * ChunkSizeX + sampleX)];
        };

        auto fluidAt = [&](int localX, int y, int localZ) -> uint16_t
        {
            if (y < 0 || y >= ChunkSizeY)
            {
                return FluidNone;
            }

            int sampleX = localX;
            int sampleZ = localZ;
            const std::shared_ptr<ChunkData>& sample = sampleChunk(localX, localZ, sampleX, sampleZ);
            if (!sample || sample->fluids.size() != ChunkBlockCount)
            {
                return FluidNone;
            }

            return sample->fluids[static_cast<size_t>((y * ChunkSizeZ + sampleZ) * ChunkSizeX + sampleX)];
        };

        auto blockOccludesFluid = [&](uint16_t block)
        {
            return block != BlockAir && blockDefinition(block).faceOcclusion == BlockFaceOcclusion::Opaque;
        };

        auto fluidRenderHeight = [&](int localX, int y, int localZ, uint16_t fluid)
        {
            if (fluidId(fluid) != FluidWater)
            {
                return 0.0f;
            }
            if (fluidId(fluidAt(localX, y + 1, localZ)) == FluidWater)
            {
                return 1.0f;
            }
            return fluidSurfaceHeight(fluidAmount(fluid));
        };

        auto appendQuad = [&](TerrainVertex a, TerrainVertex b, TerrainVertex c, TerrainVertex d)
        {
            a.textureLayer = 0.0f;
            b.textureLayer = 0.0f;
            c.textureLayer = 0.0f;
            d.textureLayer = 0.0f;
            a.mipDistanceScale = FluidMipDistanceScale;
            b.mipDistanceScale = FluidMipDistanceScale;
            c.mipDistanceScale = FluidMipDistanceScale;
            d.mipDistanceScale = FluidMipDistanceScale;

            const uint32_t baseIndex = static_cast<uint32_t>(result.vertices.size());
            result.vertices.push_back(a);
            result.vertices.push_back(b);
            result.vertices.push_back(c);
            result.vertices.push_back(d);
            result.indices.push_back(baseIndex);
            result.indices.push_back(baseIndex + 1);
            result.indices.push_back(baseIndex + 2);
            result.indices.push_back(baseIndex);
            result.indices.push_back(baseIndex + 2);
            result.indices.push_back(baseIndex + 3);
        };

        auto appendFluidFace = [&](int localX, int y, int localZ, int face, float sideBottom, float sideTop)
        {
            const int worldX = chunk->chunkX * ChunkSizeX + localX;
            const int worldZ = chunk->chunkZ * ChunkSizeZ + localZ;
            const float x0 = static_cast<float>(worldX) - 0.5f;
            const float x1 = static_cast<float>(worldX) + 0.5f;
            const float y0 = static_cast<float>(y) + sideBottom;
            const float y1 = static_cast<float>(y) + sideTop;
            const float z0 = static_cast<float>(worldZ) - 0.5f;
            const float z1 = static_cast<float>(worldZ) + 0.5f;

            if (face == 0)
            {
                appendQuad({x0, y1, z0, 0.0f, 0.0f, 1.0f}, {x0, y1, z1, 0.0f, 1.0f, 1.0f}, {x1, y1, z1, 1.0f, 1.0f, 1.0f}, {x1, y1, z0, 1.0f, 0.0f, 1.0f});
            }
            else if (face == 1)
            {
                appendQuad({x0, y0, z1, 0.0f, 0.0f, 1.0f}, {x0, y0, z0, 0.0f, 1.0f, 1.0f}, {x1, y0, z0, 1.0f, 1.0f, 1.0f}, {x1, y0, z1, 1.0f, 0.0f, 1.0f});
            }
            else if (face == 2)
            {
                appendQuad({x1, y0, z0, 0.0f, 1.0f, 1.0f}, {x1, y1, z0, 0.0f, 0.0f, 1.0f}, {x1, y1, z1, 1.0f, 0.0f, 1.0f}, {x1, y0, z1, 1.0f, 1.0f, 1.0f});
            }
            else if (face == 3)
            {
                appendQuad({x0, y0, z1, 0.0f, 1.0f, 1.0f}, {x0, y1, z1, 0.0f, 0.0f, 1.0f}, {x0, y1, z0, 1.0f, 0.0f, 1.0f}, {x0, y0, z0, 1.0f, 1.0f, 1.0f});
            }
            else if (face == 4)
            {
                appendQuad({x1, y0, z1, 0.0f, 1.0f, 1.0f}, {x1, y1, z1, 0.0f, 0.0f, 1.0f}, {x0, y1, z1, 1.0f, 0.0f, 1.0f}, {x0, y0, z1, 1.0f, 1.0f, 1.0f});
            }
            else
            {
                appendQuad({x0, y0, z0, 0.0f, 1.0f, 1.0f}, {x0, y1, z0, 0.0f, 0.0f, 1.0f}, {x1, y1, z0, 1.0f, 0.0f, 1.0f}, {x1, y0, z0, 1.0f, 1.0f, 1.0f});
            }
        };

        result.vertices.reserve(256);
        result.indices.reserve(384);

        const int yStart = subchunkY * SubchunkSize;
        const int yEnd = yStart + SubchunkSize;
        for (int y = yStart; y < yEnd; ++y)
        {
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const uint16_t fluid = fluidAt(localX, y, localZ);
                    const uint16_t id = fluidId(fluid);
                    const uint16_t amount = fluidAmount(fluid);
                    if (id != FluidWater || amount == 0 || blockOccludesFluid(blockAt(localX, y, localZ)))
                    {
                        continue;
                    }

                    const float height = fluidRenderHeight(localX, y, localZ, fluid);
                    if (fluidId(fluidAt(localX, y + 1, localZ)) != FluidWater && !blockOccludesFluid(blockAt(localX, y + 1, localZ)))
                    {
                        appendFluidFace(localX, y, localZ, 0, 0.0f, height);
                    }
                    if (fluidId(fluidAt(localX, y - 1, localZ)) != FluidWater && !blockOccludesFluid(blockAt(localX, y - 1, localZ)))
                    {
                        appendFluidFace(localX, y, localZ, 1, 0.0f, height);
                    }

                    const std::array<std::array<int, 3>, 4> sideOffsets = {{{1, 0, 2}, {-1, 0, 3}, {0, 1, 4}, {0, -1, 5}}};
                    for (const std::array<int, 3>& side : sideOffsets)
                    {
                        const int neighborX = localX + side[0];
                        const int neighborZ = localZ + side[1];
                        if (blockOccludesFluid(blockAt(neighborX, y, neighborZ)))
                        {
                            continue;
                        }

                        const uint16_t neighborFluid = fluidAt(neighborX, y, neighborZ);
                        const float neighborHeight = fluidRenderHeight(neighborX, y, neighborZ, neighborFluid);
                        if (neighborHeight >= height)
                        {
                            continue;
                        }
                        appendFluidFace(localX, y, localZ, side[2], neighborHeight, height);
                    }
                }
            }
        }

        return result;
    }

    Renderer::CompletedChunkMesh Renderer::buildChunkMesh(const std::array<std::shared_ptr<Renderer::ChunkData>, 9>& chunks, uint64_t generation) const
    {
        const std::shared_ptr<ChunkData>& chunk = chunks[4];
        CompletedChunkMesh result{};
        result.generation = generation;
        result.revision = chunk->revision;
        result.chunkX = chunk->chunkX;
        result.chunkZ = chunk->chunkZ;

        auto blockAt = [&](int localX, int y, int localZ) -> uint16_t
        {
            if (y < 0 || y >= ChunkSizeY)
            {
                return BlockAir;
            }

            int chunkOffsetX = 0;
            int chunkOffsetZ = 0;
            int sampleX = localX;
            int sampleZ = localZ;
            if (sampleX < 0)
            {
                chunkOffsetX = -1;
                sampleX += ChunkSizeX;
            }
            else if (sampleX >= ChunkSizeX)
            {
                chunkOffsetX = 1;
                sampleX -= ChunkSizeX;
            }

            if (sampleZ < 0)
            {
                chunkOffsetZ = -1;
                sampleZ += ChunkSizeZ;
            }
            else if (sampleZ >= ChunkSizeZ)
            {
                chunkOffsetZ = 1;
                sampleZ -= ChunkSizeZ;
            }

            if (sampleX < 0 || sampleX >= ChunkSizeX || sampleZ < 0 || sampleZ >= ChunkSizeZ)
            {
                return BlockAir;
            }

            const std::shared_ptr<ChunkData>& sampleChunk = chunks[static_cast<size_t>((chunkOffsetZ + 1) * 3 + (chunkOffsetX + 1))];
            if (!sampleChunk)
            {
                return BlockAir;
            }

            const size_t index = static_cast<size_t>((y * ChunkSizeZ + sampleZ) * ChunkSizeX + sampleX);
            return sampleChunk->blocks[index];
        };

        for (int subchunkY = 0; subchunkY < SubchunksPerChunk; ++subchunkY)
        {
            result.rockSubchunks[static_cast<size_t>(subchunkY)] = buildSubchunkMesh(chunk, subchunkY, blockAt);
            result.fluidSubchunks[static_cast<size_t>(subchunkY)] = buildFluidSubchunkMesh(chunks, subchunkY);
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
        auto chunkIt = runtimeChunks_.find(key);
        if (chunkIt == runtimeChunks_.end() || !chunkIt->second.data || renderIt->second.revision != chunkIt->second.data->revision)
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
        for (TerrainMesh& mesh : chunk.rockSubchunks)
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
        runtimeChunks_.clear();
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
                    terrainSeed(101));
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
                    terrainSeed(202));
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
                    terrainSeed(303));
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
                    terrainSeed(404));

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
            terrainSeed());

        convertNoiseToHeights(heightLut_, noise, heights);
        return heights;
    }

    Renderer::PackedTerrainQuad Renderer::packTerrainQuad(const TerrainVertex& a, const TerrainVertex& b, const TerrainVertex& c, const TerrainVertex& d) const
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

    std::vector<Renderer::PackedTerrainQuad> Renderer::buildPackedTerrainQuads(const TerrainBuildData& buildData) const
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

    void Renderer::initializeRmlUi()
    {
        rmlSystemInterface_ = std::make_unique<RmlGlfwSystemInterface>(window_);
        Rml::SetSystemInterface(rmlSystemInterface_.get());
        Rml::SetRenderInterface(this);
        if (!Rml::Initialise())
        {
            log::warn("RmlUi initialization failed.");
            Rml::SetSystemInterface(nullptr);
            rmlSystemInterface_.reset();
            return;
        }

        rmlInitialized_ = true;
        const std::filesystem::path assetDir = assetDirectory();
        const std::filesystem::path fontPath = assetDir / "fonts" / "VCR_OSD_MONO.ttf";
        if (!Rml::LoadFontFace(fontPath.string(), true))
        {
            log::warn("RmlUi font load failed: " + fontPath.string());
        }

        rmlContext_ = Rml::CreateContext("main", Rml::Vector2i(static_cast<int>(swapchainExtent_.width), static_cast<int>(swapchainExtent_.height)), this);
        if (rmlContext_ == nullptr)
        {
            log::warn("RmlUi context creation failed.");
            return;
        }

        const std::filesystem::path uiDir = assetDir / "ui";
        rmlLobbyDocument_ = rmlContext_->LoadDocument((uiDir / "lobby.rml").string());
        rmlWorldSelectDocument_ = rmlContext_->LoadDocument((uiDir / "world_select.rml").string());
        rmlWorldCreateDocument_ = rmlContext_->LoadDocument((uiDir / "world_create.rml").string());
        rmlHudDocument_ = rmlContext_->LoadDocument((uiDir / "hud.rml").string());
        rmlInventoryDocument_ = rmlContext_->LoadDocument((uiDir / "inventory.rml").string());
        rmlPauseDocument_ = rmlContext_->LoadDocument((uiDir / "pause.rml").string());

        attachRmlUiEvents(rmlLobbyDocument_);
        attachRmlUiEvents(rmlWorldSelectDocument_);
        attachRmlUiEvents(rmlWorldCreateDocument_);
        attachRmlUiEvents(rmlHudDocument_);
        attachRmlUiEvents(rmlInventoryDocument_);
        attachRmlUiEvents(rmlPauseDocument_);

        updateHotbarScopeClass();
        updateInventoryUi();
        updateInventoryDebugSlots();
        setRmlUiDocument(0);
    }

    void Renderer::shutdownRmlUi()
    {
        if (!rmlInitialized_)
        {
            return;
        }

        if (rmlLobbyDocument_ != nullptr)
        {
            rmlLobbyDocument_->Close();
            rmlLobbyDocument_ = nullptr;
        }
        if (rmlWorldSelectDocument_ != nullptr)
        {
            rmlWorldSelectDocument_->Close();
            rmlWorldSelectDocument_ = nullptr;
        }
        if (rmlWorldCreateDocument_ != nullptr)
        {
            rmlWorldCreateDocument_->Close();
            rmlWorldCreateDocument_ = nullptr;
        }
        if (rmlHudDocument_ != nullptr)
        {
            rmlHudDocument_->Close();
            rmlHudDocument_ = nullptr;
        }
        if (rmlInventoryDocument_ != nullptr)
        {
            rmlInventoryDocument_->Close();
            rmlInventoryDocument_ = nullptr;
        }
        if (rmlPauseDocument_ != nullptr)
        {
            rmlPauseDocument_->Close();
            rmlPauseDocument_ = nullptr;
        }
        Rml::RemoveContext("main");
        rmlContext_ = nullptr;
        Rml::Shutdown();
        Rml::SetSystemInterface(nullptr);
        rmlSystemInterface_.reset();
        rmlInitialized_ = false;
        activeRmlMenuOverlayMode_ = -1;
    }

    void Renderer::attachRmlUiEvents(Rml::ElementDocument* document)
    {
        if (document == nullptr)
        {
            return;
        }

        constexpr std::array<const char*, 8> ButtonIds = {
            "start",
            "exit",
            "new-world",
            "create-world",
            "back-to-lobby",
            "back-to-world-select",
            "resume",
            "exit-to-lobby"
        };
        for (const char* id : ButtonIds)
        {
            if (Rml::Element* element = document->GetElementById(id))
            {
                element->AddEventListener("click", this);
            }
        }
    }

    void Renderer::setRmlUiDocument(int menuOverlayMode)
    {
        if (activeRmlMenuOverlayMode_ == menuOverlayMode)
        {
            return;
        }

        if (activeRmlMenuOverlayMode_ == 5 && menuOverlayMode != 5)
        {
            closeInventoryInteraction();
        }

        activeRmlMenuOverlayMode_ = menuOverlayMode;
        if (rmlLobbyDocument_ != nullptr)
        {
            menuOverlayMode == 1 ? rmlLobbyDocument_->Show() : rmlLobbyDocument_->Hide();
        }
        if (rmlWorldSelectDocument_ != nullptr)
        {
            menuOverlayMode == 3 ? rmlWorldSelectDocument_->Show() : rmlWorldSelectDocument_->Hide();
        }
        if (rmlWorldCreateDocument_ != nullptr)
        {
            menuOverlayMode == 4 ? rmlWorldCreateDocument_->Show() : rmlWorldCreateDocument_->Hide();
        }
        if (rmlHudDocument_ != nullptr)
        {
            (menuOverlayMode == 0 || menuOverlayMode == 5) ? rmlHudDocument_->Show() : rmlHudDocument_->Hide();
        }
        if (rmlInventoryDocument_ != nullptr)
        {
            menuOverlayMode == 5 ? rmlInventoryDocument_->Show() : rmlInventoryDocument_->Hide();
        }
        if (rmlPauseDocument_ != nullptr)
        {
            menuOverlayMode == 2 ? rmlPauseDocument_->Show() : rmlPauseDocument_->Hide();
        }
    }

    bool Renderer::renderRmlUi(VkCommandBuffer commandBuffer, int menuOverlayMode)
    {
        if (!rmlInitialized_ || rmlContext_ == nullptr)
        {
            return false;
        }

        setRmlUiDocument(menuOverlayMode);
        rmlContext_->SetDimensions(Rml::Vector2i(static_cast<int>(swapchainExtent_.width), static_cast<int>(swapchainExtent_.height)));
        rmlContext_->Update();
        rmlUiVertexOffset_ = 0;
        rmlUiIndexOffset_ = 0;
        rmlCommandBuffer_ = commandBuffer;
        rmlContext_->Render();
        rmlCommandBuffer_ = VK_NULL_HANDLE;
        return true;
    }

    void Renderer::uiMouseMove(double x, double y)
    {
        rmlMouseX_ = x;
        rmlMouseY_ = y;
        if (rmlContext_ != nullptr)
        {
            rmlContext_->ProcessMouseMove(static_cast<int>(std::round(x)), static_cast<int>(std::round(y)), currentRmlKeyModifiers());
        }
        if (activeRmlMenuOverlayMode_ == 5)
        {
            updateItemTooltipUi();
            updateInventoryCursorUi();
        }
    }

    void Renderer::uiMouseButton(int button, bool pressed, int modifiers)
    {
        if (rmlContext_ == nullptr)
        {
            return;
        }

        int rmlButton = 0;
        if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            rmlButton = 1;
        }
        else if (button == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            rmlButton = 2;
        }

        if (pressed)
        {
            rmlContext_->ProcessMouseButtonDown(rmlButton, rmlKeyModifiersFromGlfw(modifiers));
            if (activeRmlMenuOverlayMode_ == 5 && (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT))
            {
                if (const std::optional<size_t> slot = inventorySlotAt(rmlMouseX_, rmlMouseY_); slot.has_value())
                {
                    handleInventorySlotClick(*slot, button, modifiers);
                }
            }
        }
        else
        {
            rmlContext_->ProcessMouseButtonUp(rmlButton, rmlKeyModifiersFromGlfw(modifiers));
        }
    }

    void Renderer::uiMouseWheel(double yOffset)
    {
        if (rmlContext_ != nullptr)
        {
            rmlContext_->ProcessMouseWheel(static_cast<float>(-yOffset), currentRmlKeyModifiers());
        }
    }

    void Renderer::uiTextInput(unsigned int codepoint)
    {
        if (rmlContext_ != nullptr)
        {
            rmlContext_->ProcessTextInput(static_cast<Rml::Character>(codepoint));
        }
    }

    void Renderer::uiKey(int key, bool pressed, int modifiers)
    {
        if (rmlContext_ == nullptr)
        {
            return;
        }

        const Rml::Input::KeyIdentifier identifier = rmlKeyFromGlfw(key);
        if (identifier == Rml::Input::KI_UNKNOWN)
        {
            return;
        }

        if (pressed)
        {
            if (activeRmlMenuOverlayMode_ == 5 && ((key >= GLFW_KEY_0 && key <= GLFW_KEY_9)))
            {
                handleInventoryHotbarSwapKey(key);
            }
            rmlContext_->ProcessKeyDown(identifier, rmlKeyModifiersFromGlfw(modifiers));
        }
        else
        {
            rmlContext_->ProcessKeyUp(identifier, rmlKeyModifiersFromGlfw(modifiers));
        }
    }

    std::optional<std::string> Renderer::consumeUiAction()
    {
        std::optional<std::string> action = std::move(pendingUiAction_);
        pendingUiAction_.reset();
        return action;
    }

    bool Renderer::rmlUiAvailable() const
    {
        return rmlInitialized_ && rmlContext_ != nullptr;
    }

    void Renderer::setHotbarSelectedSlot(int slot)
    {
        hotbarSelectedSlot_ = std::clamp(slot, 0, 9);
        updateHotbarScopeClass();
    }

    void Renderer::updateHotbarScopeClass()
    {
        if (rmlHudDocument_ == nullptr)
        {
            return;
        }

        Rml::Element* scope = rmlHudDocument_->GetElementById("hotbar-scope");
        if (scope == nullptr)
        {
            return;
        }

        scope->SetAttribute("class", Rml::String("hotbar-scope hotbar-slot-") + std::to_string(hotbarSelectedSlot_));
    }

    std::string Renderer::inventoryDebugSlotRml(size_t slotIndex, bool inventorySlot) const
    {
        int sourceX = 0;
        int sourceY = 0;
        if (inventorySlot)
        {
            constexpr int Padding = 4;
            constexpr int Step = 17;
            constexpr int HotbarY = 77;
            const int col = static_cast<int>(slotIndex % 10u);
            sourceX = Padding + col * Step;
            if (slotIndex < 10u)
            {
                sourceY = HotbarY;
            }
            else
            {
                const int group = static_cast<int>(slotIndex / 10u);
                sourceY = Padding + (4 - group) * Step;
            }
        }
        else
        {
            constexpr int HotbarStartX = 3;
            constexpr int HotbarStartY = 3;
            constexpr int Step = 17;
            sourceX = HotbarStartX + static_cast<int>(slotIndex) * Step;
            sourceY = HotbarStartY;
        }

        constexpr int Scale = 4;
        const int hotbarOffsetX = inventorySlot ? 0 : 4;
        const int hotbarOffsetY = inventorySlot ? 0 : 4;
        const int left = sourceX * Scale + hotbarOffsetX;
        const int top = sourceY * Scale + hotbarOffsetY;

        std::string rml;
        rml += "<div class=\"slot-debug\" style=\"left: " + std::to_string(left) + "px; top: " + std::to_string(top) + "px;\">";
        rml += std::to_string(slotIndex);
        rml += "</div>";
        return rml;
    }

    void Renderer::updateInventoryDebugSlots()
    {
        if (rmlHudDocument_ != nullptr)
        {
            if (Rml::Element* hotbarItems = rmlHudDocument_->GetElementById("hotbar-debug-slots"))
            {
                hotbarItems->SetAttribute("class", inventoryDebugSlotsVisible_ ? "hotbar-items" : "hotbar-items ui-hidden");
                std::string rml;
                if (inventoryDebugSlotsVisible_)
                {
                    for (size_t i = 0; i < 10u; ++i)
                    {
                        rml += inventoryDebugSlotRml(i, false);
                    }
                }
                hotbarItems->SetInnerRML(rml);
            }
        }

        if (rmlInventoryDocument_ != nullptr)
        {
            if (Rml::Element* inventoryItems = rmlInventoryDocument_->GetElementById("inventory-debug-slots"))
            {
                inventoryItems->SetAttribute("class", inventoryDebugSlotsVisible_ ? "inventory-items" : "inventory-items ui-hidden");
                std::string rml;
                if (inventoryDebugSlotsVisible_)
                {
                    for (size_t i = 0; i < 50u; ++i)
                    {
                        rml += inventoryDebugSlotRml(i, true);
                    }
                }
                inventoryItems->SetInnerRML(rml);
            }
        }
    }

    std::optional<size_t> Renderer::inventorySlotAt(double x, double y) const
    {
        if (swapchainExtent_.width == 0 || swapchainExtent_.height == 0)
        {
            return std::nullopt;
        }

        const int panelLeft = static_cast<int>(swapchainExtent_.width) / 2 - 354;
        const int panelTop = static_cast<int>(swapchainExtent_.height) - 388;
        const int localX = static_cast<int>(std::floor(x)) - panelLeft;
        const int localY = static_cast<int>(std::floor(y)) - panelTop;
        if (localX < 0 || localY < 0 || localX >= 708 || localY >= 388)
        {
            return std::nullopt;
        }

        constexpr int Scale = 4;
        constexpr int SlotSize = 64;
        constexpr int Padding = 4;
        constexpr int Step = 17;
        constexpr int HotbarY = 77;
        for (size_t slotIndex = 0; slotIndex < playerInventorySlots_.size(); ++slotIndex)
        {
            const int col = static_cast<int>(slotIndex % 10u);
            const int sourceX = Padding + col * Step;
            int sourceY = HotbarY;
            if (slotIndex >= 10u)
            {
                const int group = static_cast<int>(slotIndex / 10u);
                sourceY = Padding + (4 - group) * Step;
            }

            const int left = sourceX * Scale;
            const int top = sourceY * Scale;
            if (localX >= left && localX < left + SlotSize && localY >= top && localY < top + SlotSize)
            {
                return slotIndex;
            }
        }

        return std::nullopt;
    }

    void Renderer::handleInventorySlotClick(size_t slotIndex, int button, int modifiers)
    {
        if (slotIndex >= playerInventorySlots_.size())
        {
            return;
        }

        ItemStack& slot = playerInventorySlots_[slotIndex];
        const bool shift = (modifiers & GLFW_MOD_SHIFT) != 0;
        if (shift && inventoryCursorStack_.itemId == 0 && slot.itemId != 0 && slot.count != 0)
        {
            ItemStack moving = slot;
            slot = {};
            if (slotIndex < 10u)
            {
                addItemToInventoryRange(moving, 10, playerInventorySlots_.size());
            }
            else
            {
                addItemToInventoryRange(moving, 0, 10);
            }
            slot = moving.count == 0 ? ItemStack{} : moving;
            updateInventoryUi();
            updateItemTooltipUi();
            return;
        }

        if (button == GLFW_MOUSE_BUTTON_LEFT)
        {
            if (inventoryCursorStack_.itemId == 0 || inventoryCursorStack_.count == 0)
            {
                inventoryCursorStack_ = slot;
                slot = {};
            }
            else if (slot.itemId == 0 || slot.count == 0)
            {
                slot = inventoryCursorStack_;
                inventoryCursorStack_ = {};
            }
            else if (inventoryStackCanMerge(slot, inventoryCursorStack_))
            {
                const uint16_t maxStack = itemDefinitions_[slot.itemId].stackSize;
                const uint16_t moved = std::min(static_cast<uint16_t>(maxStack - slot.count), inventoryCursorStack_.count);
                slot.count = static_cast<uint16_t>(slot.count + moved);
                inventoryCursorStack_.count = static_cast<uint16_t>(inventoryCursorStack_.count - moved);
                if (inventoryCursorStack_.count == 0)
                {
                    inventoryCursorStack_ = {};
                }
            }
            else
            {
                std::swap(slot, inventoryCursorStack_);
            }
        }
        else if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            if (inventoryCursorStack_.itemId == 0 || inventoryCursorStack_.count == 0)
            {
                if (slot.itemId != 0 && slot.count != 0)
                {
                    const uint16_t taken = static_cast<uint16_t>((slot.count + 1u) / 2u);
                    inventoryCursorStack_.itemId = slot.itemId;
                    inventoryCursorStack_.count = taken;
                    slot.count = static_cast<uint16_t>(slot.count - taken);
                    if (slot.count == 0)
                    {
                        slot = {};
                    }
                }
            }
            else if (slot.itemId == 0 || slot.count == 0)
            {
                slot.itemId = inventoryCursorStack_.itemId;
                slot.count = 1;
                inventoryCursorStack_.count = static_cast<uint16_t>(inventoryCursorStack_.count - 1u);
                if (inventoryCursorStack_.count == 0)
                {
                    inventoryCursorStack_ = {};
                }
            }
            else if (inventoryStackCanMerge(slot, inventoryCursorStack_))
            {
                slot.count = static_cast<uint16_t>(slot.count + 1u);
                inventoryCursorStack_.count = static_cast<uint16_t>(inventoryCursorStack_.count - 1u);
                if (inventoryCursorStack_.count == 0)
                {
                    inventoryCursorStack_ = {};
                }
            }
        }

        updateInventoryUi();
        updateItemTooltipUi();
    }

    void Renderer::handleInventoryHotbarSwapKey(int key)
    {
        if (inventoryCursorStack_.itemId != 0 || inventoryCursorStack_.count != 0)
        {
            return;
        }

        int hotbarSlot = -1;
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9)
        {
            hotbarSlot = key - GLFW_KEY_1;
        }
        else if (key == GLFW_KEY_0)
        {
            hotbarSlot = 9;
        }
        if (hotbarSlot < 0)
        {
            return;
        }

        const std::optional<size_t> hoveredSlot = inventorySlotAt(rmlMouseX_, rmlMouseY_);
        if (!hoveredSlot.has_value() || *hoveredSlot == static_cast<size_t>(hotbarSlot))
        {
            return;
        }

        std::swap(playerInventorySlots_[*hoveredSlot], playerInventorySlots_[static_cast<size_t>(hotbarSlot)]);
        updateInventoryUi();
        updateItemTooltipUi();
    }

    void Renderer::closeInventoryInteraction()
    {
        if (inventoryCursorStack_.itemId != 0 && inventoryCursorStack_.count != 0)
        {
            const uint16_t remaining = addItemToPlayerInventory(inventoryCursorStack_);
            if (remaining == 0)
            {
                inventoryCursorStack_ = {};
            }
            else
            {
                inventoryCursorStack_.count = remaining;
            }
        }
        updateItemTooltipUi();
        updateInventoryCursorUi();
    }

    std::string Renderer::itemStackContentRml(const ItemStack& stack, int itemLeft, int itemTop) const
    {
        if (stack.itemId == 0 || stack.count == 0 || static_cast<size_t>(stack.itemId) >= itemDefinitions_.size())
        {
            return {};
        }

        const ItemDefinition& definition = itemDefinitions_[stack.itemId];
        if (definition.slotTexture.empty() || definition.slotTexture == "none")
        {
            return {};
        }

        constexpr int SlotSize = 64;
        constexpr int CountBoxWidth = 48;
        constexpr int CountRightInset = 2;
        const int countLeft = itemLeft + SlotSize - CountBoxWidth - CountRightInset;
        const int countTop = itemTop + 40;
        const std::string src = "../textures/item/" + definition.slotTexture + ".png";

        std::string rml;
        rml += "<img class=\"slot-item\" src=\"" + escapeRml(src) + "\" style=\"left: " + std::to_string(itemLeft) + "px; top: " + std::to_string(itemTop) + "px;\"/>";
        if (stack.count > 1)
        {
            rml += "<div class=\"slot-count\" style=\"left: " + std::to_string(countLeft) + "px; top: " + std::to_string(countTop) + "px;\">";
            rml += std::to_string(stack.count);
            rml += "</div>";
        }
        return rml;
    }

    std::string Renderer::itemTooltipRml(const ItemStack& stack) const
    {
        if (stack.itemId == 0 || stack.count == 0 || static_cast<size_t>(stack.itemId) >= itemDefinitions_.size())
        {
            return {};
        }

        const ItemDefinition& definition = itemDefinitions_[stack.itemId];
        if (definition.key.empty() || definition.key == "none")
        {
            return {};
        }

        auto renderTypeText = [](ItemRenderType type)
        {
            switch (type)
            {
            case ItemRenderType::ExtrudedSprite:
                return "extruded_sprite";
            }
            return "unknown";
        };

        std::string rml;
        rml += "<div class=\"item-tooltip-title\">" + escapeRml(definition.name) + "</div>";
        auto line = [&](std::string_view key, const std::string& value)
        {
            rml += "<div class=\"item-tooltip-line\">" + std::string(key) + ": " + escapeRml(value) + "</div>";
        };
        line("ID", std::to_string(stack.itemId));
        line("KEY", definition.key);
        line("COUNT", std::to_string(stack.count) + " / " + std::to_string(definition.stackSize));
        line("STACK_SIZE", std::to_string(definition.stackSize));
        line("SLOT_TEXTURE", definition.slotTexture);
        line("DROPPED_RENDER", renderTypeText(definition.droppedRender));
        line("DROPPED_TEXTURE", definition.droppedTexture);
        line("HELD_RENDER", renderTypeText(definition.heldRender));
        line("HELD_TEXTURE", definition.heldTexture);
        return rml;
    }

    std::string Renderer::itemSlotImageRml(size_t slotIndex, bool inventorySlot) const
    {
        if (slotIndex >= playerInventorySlots_.size())
        {
            return {};
        }

        int sourceX = 0;
        int sourceY = 0;
        if (inventorySlot)
        {
            constexpr int Padding = 4;
            constexpr int Step = 17;
            constexpr int HotbarY = 77;
            const int col = static_cast<int>(slotIndex % 10u);
            sourceX = Padding + col * Step;
            if (slotIndex < 10u)
            {
                sourceY = HotbarY;
            }
            else
            {
                const int group = static_cast<int>(slotIndex / 10u);
                sourceY = Padding + (4 - group) * Step;
            }
        }
        else
        {
            constexpr int HotbarStartX = 3;
            constexpr int HotbarStartY = 3;
            constexpr int Step = 17;
            sourceX = HotbarStartX + static_cast<int>(slotIndex) * Step;
            sourceY = HotbarStartY;
        }

        constexpr int Scale = 4;
        const int hotbarOffsetX = inventorySlot ? 0 : 4;
        const int hotbarOffsetY = inventorySlot ? 0 : 4;
        const int left = sourceX * Scale + hotbarOffsetX;
        const int top = sourceY * Scale + hotbarOffsetY;

        std::string rml;
        if (!inventorySlot)
        {
            rml += "<div class=\"hotbar-slot-background\" style=\"left: " + std::to_string(left) + "px; top: " + std::to_string(top) + "px;\"></div>";
        }
        else
        {
            rml += "<div class=\"inventory-slot-cell\" style=\"left: " + std::to_string(left) + "px; top: " + std::to_string(top) + "px;\">";
        }

        const ItemStack& stack = playerInventorySlots_[slotIndex];
        const int itemLeft = inventorySlot ? 0 : left;
        const int itemTop = inventorySlot ? 0 : top;
        rml += itemStackContentRml(stack, itemLeft, itemTop);
        if (inventorySlot)
        {
            rml += "</div>";
        }
        return rml;
    }

    void Renderer::updateInventoryUi()
    {
        if (rmlHudDocument_ != nullptr)
        {
            if (Rml::Element* hotbarItems = rmlHudDocument_->GetElementById("hotbar-items"))
            {
                std::string rml;
                for (size_t i = 0; i < 10u; ++i)
                {
                    rml += itemSlotImageRml(i, false);
                }
                hotbarItems->SetInnerRML(rml);
            }
        }

        if (rmlInventoryDocument_ != nullptr)
        {
            if (Rml::Element* inventoryItems = rmlInventoryDocument_->GetElementById("inventory-items"))
            {
                std::string rml;
                for (size_t i = 0; i < playerInventorySlots_.size(); ++i)
                {
                    rml += itemSlotImageRml(i, true);
                }
                inventoryItems->SetInnerRML(rml);
            }
        }
        updateInventoryCursorUi();
    }

    void Renderer::updateInventoryCursorUi()
    {
        if (rmlInventoryDocument_ == nullptr)
        {
            return;
        }

        Rml::Element* cursor = rmlInventoryDocument_->GetElementById("inventory-cursor-item");
        if (cursor == nullptr)
        {
            return;
        }

        if (inventoryCursorStack_.itemId == 0 || inventoryCursorStack_.count == 0)
        {
            cursor->SetAttribute("class", "cursor-item-layer ui-hidden");
            cursor->SetInnerRML("");
            return;
        }

        cursor->SetAttribute("class", "cursor-item-layer");
        const int left = static_cast<int>(std::round(rmlMouseX_)) - 32;
        const int top = static_cast<int>(std::round(rmlMouseY_)) - 32;
        cursor->SetInnerRML(itemStackContentRml(inventoryCursorStack_, left, top));
    }

    void Renderer::updateItemTooltipUi()
    {
        if (rmlInventoryDocument_ == nullptr)
        {
            return;
        }

        Rml::Element* tooltip = rmlInventoryDocument_->GetElementById("item-tooltip");
        if (tooltip == nullptr)
        {
            return;
        }

        if (inventoryCursorStack_.itemId != 0 && inventoryCursorStack_.count != 0)
        {
            tooltip->SetAttribute("class", "item-tooltip ui-hidden");
            tooltip->SetInnerRML("");
            return;
        }

        const std::optional<size_t> hoveredSlot = inventorySlotAt(rmlMouseX_, rmlMouseY_);
        if (!hoveredSlot.has_value())
        {
            tooltip->SetAttribute("class", "item-tooltip ui-hidden");
            tooltip->SetInnerRML("");
            return;
        }

        const ItemStack& stack = playerInventorySlots_[*hoveredSlot];
        const std::string rml = itemTooltipRml(stack);
        if (rml.empty())
        {
            tooltip->SetAttribute("class", "item-tooltip ui-hidden");
            tooltip->SetInnerRML("");
            return;
        }

        constexpr int Padding = 10;
        constexpr int TitleHeight = 22;
        constexpr int TitleMargin = 4;
        constexpr int LineHeight = 18;
        constexpr int LineCount = 9;
        constexpr int MinWidth = 180;
        constexpr int MaxWidth = 520;
        constexpr int TitleCharWidth = 12;
        constexpr int LineCharWidth = 8;
        constexpr int Height = Padding * 2 + TitleHeight + TitleMargin + LineHeight * LineCount;
        constexpr int MouseOffset = 16;
        auto renderTypeText = [](ItemRenderType type)
        {
            switch (type)
            {
            case ItemRenderType::ExtrudedSprite:
                return "extruded_sprite";
            }
            return "unknown";
        };
        auto lineText = [](std::string_view key, const std::string& value)
        {
            return std::string(key) + ": " + value;
        };

        const ItemDefinition& definition = itemDefinitions_[stack.itemId];
        int contentWidth = static_cast<int>(definition.name.size()) * TitleCharWidth;
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("ID", std::to_string(stack.itemId)).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("KEY", definition.key).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("COUNT", std::to_string(stack.count) + " / " + std::to_string(definition.stackSize)).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("STACK_SIZE", std::to_string(definition.stackSize)).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("SLOT_TEXTURE", definition.slotTexture).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("DROPPED_RENDER", renderTypeText(definition.droppedRender)).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("DROPPED_TEXTURE", definition.droppedTexture).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("HELD_RENDER", renderTypeText(definition.heldRender)).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("HELD_TEXTURE", definition.heldTexture).size()) * LineCharWidth);
        const int Width = std::clamp(contentWidth + Padding * 2, MinWidth, MaxWidth);

        int left = static_cast<int>(std::round(rmlMouseX_)) + MouseOffset;
        int top = static_cast<int>(std::round(rmlMouseY_)) + MouseOffset;
        const int screenWidth = static_cast<int>(swapchainExtent_.width);
        const int screenHeight = static_cast<int>(swapchainExtent_.height);
        if (left + Width > screenWidth)
        {
            left = static_cast<int>(std::round(rmlMouseX_)) - Width - MouseOffset;
        }
        if (top + Height > screenHeight)
        {
            top = static_cast<int>(std::round(rmlMouseY_)) - Height - MouseOffset;
        }
        left = std::clamp(left, 0, std::max(0, screenWidth - Width));
        top = std::clamp(top, 0, std::max(0, screenHeight - Height));

        tooltip->SetAttribute("class", "item-tooltip");
        tooltip->SetAttribute("style", "left: " + std::to_string(left) + "px; top: " + std::to_string(top) + "px; width: " + std::to_string(Width) + "px; height: " + std::to_string(Height) + "px;");
        tooltip->SetInnerRML(rml);
    }

    void Renderer::setWorldList(const std::vector<WorldListItem>& worlds)
    {
        if (rmlWorldSelectDocument_ == nullptr)
        {
            return;
        }

        Rml::Element* list = rmlWorldSelectDocument_->GetElementById("world-list");
        if (list == nullptr)
        {
            return;
        }

        std::string rml;
        if (worlds.empty())
        {
            rml =
                "<div class=\"world-row large\">"
                "<div class=\"world-name\">No worlds yet</div>"
                "<div class=\"world-meta\">Create a new world to begin</div>"
                "</div>";
        }
        else
        {
            for (size_t i = 0; i < worlds.size(); ++i)
            {
                rml += "<div id=\"world-open-" + std::to_string(i) + "\" class=\"world-row large\">";
                rml += "<div class=\"world-name\">" + escapeRml(worlds[i].name) + "</div>";
                rml += "<div class=\"world-meta\">CREATED " + escapeRml(worlds[i].createdText) + " / LAST " + escapeRml(worlds[i].lastPlayedText) + "</div>";
                rml += "</div>";
            }
        }

        list->SetInnerRML(rml);
        for (size_t i = 0; i < worlds.size(); ++i)
        {
            if (Rml::Element* element = rmlWorldSelectDocument_->GetElementById("world-open-" + std::to_string(i)))
            {
                element->AddEventListener("dblclick", this);
            }
        }
    }

    std::string Renderer::uiInputValue(std::string_view id) const
    {
        if (rmlContext_ == nullptr)
        {
            return {};
        }

        for (Rml::ElementDocument* document : {rmlLobbyDocument_, rmlWorldSelectDocument_, rmlWorldCreateDocument_, rmlHudDocument_, rmlInventoryDocument_, rmlPauseDocument_})
        {
            if (document == nullptr)
            {
                continue;
            }
            Rml::Element* element = document->GetElementById(std::string(id));
            if (element == nullptr)
            {
                continue;
            }
            if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(element))
            {
                return input->GetValue();
            }
            return element->GetAttribute<Rml::String>("value", "");
        }

        return {};
    }

    int Renderer::terrainSeed(int offset) const
    {
        return TerrainNoiseSeed + activeWorldSeedSalt_ + offset;
    }

    int Renderer::temperatureSeed() const
    {
        return TemperatureNoiseSeed + activeWorldSeedSalt_;
    }

    int Renderer::precipitationSeed() const
    {
        return PrecipitationNoiseSeed + activeWorldSeedSalt_;
    }

    std::string Renderer::escapeRml(std::string_view text)
    {
        std::string escaped;
        escaped.reserve(text.size());
        for (const char c : text)
        {
            switch (c)
            {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            default: escaped.push_back(c); break;
            }
        }
        return escaped;
    }

    Rml::Input::KeyIdentifier Renderer::rmlKeyFromGlfw(int key) const
    {
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        {
            return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + (key - GLFW_KEY_A));
        }
        if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
        {
            return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_0 + (key - GLFW_KEY_0));
        }

        switch (key)
        {
        case GLFW_KEY_SPACE: return Rml::Input::KI_SPACE;
        case GLFW_KEY_BACKSPACE: return Rml::Input::KI_BACK;
        case GLFW_KEY_TAB: return Rml::Input::KI_TAB;
        case GLFW_KEY_ENTER: return Rml::Input::KI_RETURN;
        case GLFW_KEY_ESCAPE: return Rml::Input::KI_ESCAPE;
        case GLFW_KEY_LEFT: return Rml::Input::KI_LEFT;
        case GLFW_KEY_RIGHT: return Rml::Input::KI_RIGHT;
        case GLFW_KEY_UP: return Rml::Input::KI_UP;
        case GLFW_KEY_DOWN: return Rml::Input::KI_DOWN;
        case GLFW_KEY_DELETE: return Rml::Input::KI_DELETE;
        case GLFW_KEY_HOME: return Rml::Input::KI_HOME;
        case GLFW_KEY_END: return Rml::Input::KI_END;
        case GLFW_KEY_MINUS: return Rml::Input::KI_OEM_MINUS;
        case GLFW_KEY_EQUAL: return Rml::Input::KI_OEM_PLUS;
        case GLFW_KEY_COMMA: return Rml::Input::KI_OEM_COMMA;
        case GLFW_KEY_PERIOD: return Rml::Input::KI_OEM_PERIOD;
        default: return Rml::Input::KI_UNKNOWN;
        }
    }

    int Renderer::rmlKeyModifiersFromGlfw(int modifiers) const
    {
        int rmlModifiers = 0;
        if ((modifiers & GLFW_MOD_CONTROL) != 0)
        {
            rmlModifiers |= Rml::Input::KM_CTRL;
        }
        if ((modifiers & GLFW_MOD_SHIFT) != 0)
        {
            rmlModifiers |= Rml::Input::KM_SHIFT;
        }
        if ((modifiers & GLFW_MOD_ALT) != 0)
        {
            rmlModifiers |= Rml::Input::KM_ALT;
        }
        if ((modifiers & GLFW_MOD_SUPER) != 0)
        {
            rmlModifiers |= Rml::Input::KM_META;
        }
        if ((modifiers & GLFW_MOD_CAPS_LOCK) != 0)
        {
            rmlModifiers |= Rml::Input::KM_CAPSLOCK;
        }
        if ((modifiers & GLFW_MOD_NUM_LOCK) != 0)
        {
            rmlModifiers |= Rml::Input::KM_NUMLOCK;
        }
        return rmlModifiers;
    }

    int Renderer::currentRmlKeyModifiers() const
    {
        if (window_ == nullptr)
        {
            return 0;
        }

        int modifiers = 0;
        if (glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
        {
            modifiers |= GLFW_MOD_CONTROL;
        }
        if (glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
        {
            modifiers |= GLFW_MOD_SHIFT;
        }
        if (glfwGetKey(window_, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
        {
            modifiers |= GLFW_MOD_ALT;
        }
        if (glfwGetKey(window_, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS)
        {
            modifiers |= GLFW_MOD_SUPER;
        }
        return rmlKeyModifiersFromGlfw(modifiers);
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

    Rml::CompiledGeometryHandle Renderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
    {
        auto* geometry = new UiGeometry();
        geometry->vertices.reserve(vertices.size());
        geometry->indices.reserve(indices.size());

        for (const Rml::Vertex& vertex : vertices)
        {
            UiVertex out{};
            out.x = vertex.position.x;
            out.y = vertex.position.y;
            out.r = static_cast<float>(vertex.colour.red) / 255.0f;
            out.g = static_cast<float>(vertex.colour.green) / 255.0f;
            out.b = static_cast<float>(vertex.colour.blue) / 255.0f;
            out.a = static_cast<float>(vertex.colour.alpha) / 255.0f;
            out.u = vertex.tex_coord.x;
            out.v = vertex.tex_coord.y;
            geometry->vertices.push_back(out);
        }
        for (int index : indices)
        {
            geometry->indices.push_back(static_cast<uint32_t>(index));
        }

        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
    }

    void Renderer::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle textureHandle)
    {
        if (rmlCommandBuffer_ == VK_NULL_HANDLE || uiPipeline_ == VK_NULL_HANDLE || handle == 0)
        {
            return;
        }

        const auto* geometry = reinterpret_cast<const UiGeometry*>(handle);
        if (geometry->vertices.empty() || geometry->indices.empty() ||
            rmlUiVertexOffset_ + geometry->vertices.size() > MaxUiVertices ||
            rmlUiIndexOffset_ + geometry->indices.size() > MaxUiIndices)
        {
            return;
        }

        const VkDeviceSize vertexBytes = sizeof(UiVertex) * geometry->vertices.size();
        const VkDeviceSize vertexBufferOffset = sizeof(UiVertex) * rmlUiVertexOffset_;
        void* vertexData = nullptr;
        vkMapMemory(device_, uiVertexMemory_, vertexBufferOffset, vertexBytes, 0, &vertexData);
        std::memcpy(vertexData, geometry->vertices.data(), static_cast<size_t>(vertexBytes));
        vkUnmapMemory(device_, uiVertexMemory_);

        const VkDeviceSize indexBytes = sizeof(uint32_t) * geometry->indices.size();
        const VkDeviceSize indexBufferOffset = sizeof(uint32_t) * rmlUiIndexOffset_;
        void* indexData = nullptr;
        vkMapMemory(device_, uiIndexMemory_, indexBufferOffset, indexBytes, 0, &indexData);
        std::memcpy(indexData, geometry->indices.data(), static_cast<size_t>(indexBytes));
        vkUnmapMemory(device_, uiIndexMemory_);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapchainExtent_.width);
        viewport.height = static_cast<float>(swapchainExtent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapchainExtent_;
        if (rmlScissorEnabled_)
        {
            scissor = rmlScissor_;
        }

        const Texture* texture = textureHandle == 0 ? &white_ : reinterpret_cast<const Texture*>(textureHandle);
        const UiPush push{
            static_cast<float>(swapchainExtent_.width),
            static_cast<float>(swapchainExtent_.height),
            translation.x,
            translation.y
        };

        vkCmdBindPipeline(rmlCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline_);
        vkCmdSetViewport(rmlCommandBuffer_, 0, 1, &viewport);
        vkCmdSetScissor(rmlCommandBuffer_, 0, 1, &scissor);
        vkCmdBindVertexBuffers(rmlCommandBuffer_, 0, 1, &uiVertexBuffer_, &vertexBufferOffset);
        vkCmdBindIndexBuffer(rmlCommandBuffer_, uiIndexBuffer_, indexBufferOffset, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(rmlCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipelineLayout_, 0, 1, &texture->descriptorSet, 0, nullptr);
        vkCmdPushConstants(rmlCommandBuffer_, uiPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(UiPush), &push);
        vkCmdDrawIndexed(rmlCommandBuffer_, static_cast<uint32_t>(geometry->indices.size()), 1, 0, 0, 0);

        rmlUiVertexOffset_ += geometry->vertices.size();
        rmlUiIndexOffset_ += geometry->indices.size();
    }

    void Renderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
    {
        delete reinterpret_cast<UiGeometry*>(geometry);
    }

    Rml::TextureHandle Renderer::LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source)
    {
        std::filesystem::path texturePath(source);
        if (texturePath.is_relative())
        {
            texturePath = (assetDirectory() / "ui" / texturePath).lexically_normal();
        }
        if (!std::filesystem::exists(texturePath))
        {
            std::string normalized = texturePath.generic_string();
            const std::string marker = "/textures/";
            const size_t markerPosition = normalized.find(marker);
            if (markerPosition != std::string::npos)
            {
                const std::string textureTail = normalized.substr(markerPosition + marker.size());
                const std::filesystem::path remappedPath = assetDirectory() / "textures" / std::filesystem::path(textureTail);
                if (std::filesystem::exists(remappedPath))
                {
                    texturePath = remappedPath;
                }
            }
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(texturePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            log::warn("RmlUi texture load failed: " + texturePath.string());
            return 0;
        }

        const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        std::vector<unsigned char> premultiplied(pixelCount * 4u);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const unsigned char alpha = pixels[i * 4u + 3u];
            premultiplied[i * 4u + 0u] = static_cast<unsigned char>((static_cast<uint32_t>(pixels[i * 4u + 0u]) * alpha) / 255u);
            premultiplied[i * 4u + 1u] = static_cast<unsigned char>((static_cast<uint32_t>(pixels[i * 4u + 1u]) * alpha) / 255u);
            premultiplied[i * 4u + 2u] = static_cast<unsigned char>((static_cast<uint32_t>(pixels[i * 4u + 2u]) * alpha) / 255u);
            premultiplied[i * 4u + 3u] = alpha;
        }
        stbi_image_free(pixels);

        Texture texture = createTextureFromRgba(premultiplied.data(), width, height, VK_FORMAT_R8G8B8A8_SRGB);
        textureDimensions = Rml::Vector2i(width, height);
        return reinterpret_cast<Rml::TextureHandle>(new Texture(texture));
    }

    Rml::TextureHandle Renderer::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions)
    {
        Texture texture = createTextureFromRgba(
            reinterpret_cast<const unsigned char*>(source.data()),
            sourceDimensions.x,
            sourceDimensions.y,
            VK_FORMAT_R8G8B8A8_UNORM);
        return reinterpret_cast<Rml::TextureHandle>(new Texture(texture));
    }

    void Renderer::ReleaseTexture(Rml::TextureHandle textureHandle)
    {
        if (textureHandle == 0)
        {
            return;
        }

        auto* texture = reinterpret_cast<Texture*>(textureHandle);
        destroyTexture(*texture);
        delete texture;
    }

    void Renderer::EnableScissorRegion(bool enable)
    {
        rmlScissorEnabled_ = enable;
    }

    void Renderer::SetScissorRegion(Rml::Rectanglei region)
    {
        const int left = std::max(region.Left(), 0);
        const int top = std::max(region.Top(), 0);
        const int right = std::min(region.Right(), static_cast<int>(swapchainExtent_.width));
        const int bottom = std::min(region.Bottom(), static_cast<int>(swapchainExtent_.height));

        rmlScissor_.offset = {left, top};
        rmlScissor_.extent = {
            static_cast<uint32_t>(std::max(right - left, 0)),
            static_cast<uint32_t>(std::max(bottom - top, 0))
        };
    }

    void Renderer::ProcessEvent(Rml::Event& event)
    {
        Rml::Element* target = event.GetCurrentElement();
        if (target == nullptr)
        {
            target = event.GetTargetElement();
        }
        if (target == nullptr)
        {
            return;
        }

        pendingUiAction_ = target->GetId();
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

    void Renderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera, Vec3 cameraPosition, Vec3 playerPosition, std::string_view fpsText, bool debugTextVisible, VkBuffer screenshotBuffer, bool showPlayer, bool terrainWireframe, int climateOverlayMode, int menuOverlayMode, bool gameSceneRenderEnabled, uint64_t worldTicks)
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

        if (gameSceneRenderEnabled && menuOverlayMode == 0)
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
        if (!renderRmlUi(commandBuffer, menuOverlayMode))
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
        const Mat4 view = viewMatrix(camera, cameraPosition);
        const Mat4 mvp = multiply(projection, view);
        const Frustum frustum = makeFrustum(camera, cameraPosition, aspect);

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
                const float minX = static_cast<float>(chunk.chunkX * ChunkSizeX) - 0.5f;
                const float maxX = static_cast<float>(chunk.chunkX * ChunkSizeX + ChunkSizeX) - 0.5f;
                const float minZ = static_cast<float>(chunk.chunkZ * ChunkSizeZ) - 0.5f;
                const float maxZ = static_cast<float>(chunk.chunkZ * ChunkSizeZ + ChunkSizeZ) - 0.5f;
                for (size_t subchunkY = 0; subchunkY < chunk.fluidSubchunks.size(); ++subchunkY)
                {
                    const TerrainMesh& mesh = chunk.fluidSubchunks[subchunkY];
                    if (mesh.indexCount == 0)
                    {
                        continue;
                    }

                    const float minY = static_cast<float>(subchunkY * SubchunkSize);
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
        constexpr double MaxInteractionDistance = 8.0;
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
        const auto chunkIt = runtimeChunks_.find(chunkKey(chunkX, chunkZ));
        if (chunkIt == runtimeChunks_.end() || !chunkIt->second.data ||
            (chunkIt->second.genState != ChunkGenState::Full && chunkIt->second.genState != ChunkGenState::Meshed))
        {
            return BlockAir;
        }

        const int localX = positiveModulo(x, ChunkSizeX);
        const int localZ = positiveModulo(z, ChunkSizeZ);
        const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
        if (index >= chunkIt->second.data->blocks.size())
        {
            return BlockAir;
        }

        return chunkIt->second.data->blocks[index];
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
        const auto chunkIt = runtimeChunks_.find(chunkKey(chunkX, chunkZ));
        if (chunkIt == runtimeChunks_.end() || !chunkIt->second.data ||
            (chunkIt->second.genState != ChunkGenState::Full && chunkIt->second.genState != ChunkGenState::Meshed))
        {
            return true;
        }

        const int localX = positiveModulo(x, ChunkSizeX);
        const int localZ = positiveModulo(z, ChunkSizeZ);
        const size_t index = static_cast<size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
        if (index >= chunkIt->second.data->blocks.size())
        {
            return true;
        }

        return blockDefinition(chunkIt->second.data->blocks[index]).collision;
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
        if (blockBreakParticles_.empty() || particlePipeline_ == VK_NULL_HANDLE || particleVertexBuffer_ == VK_NULL_HANDLE || particleIndexBuffer_ == VK_NULL_HANDLE)
        {
            return;
        }

        const size_t particleCount = std::min(blockBreakParticles_.size(), MaxBlockBreakParticles);
        std::vector<TerrainVertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(particleCount * 4u);
        indices.reserve(particleCount * 6u);

        const Vec3 cameraRight = camera.right();
        const Vec3 right{-cameraRight.x, -cameraRight.y, -cameraRight.z};
        const Vec3 forward = camera.forward();
        const Vec3 terrainForward{forward.x, -forward.y, forward.z};
        const Vec3 up = normalize(cross(terrainForward, right));
        for (size_t i = 0; i < particleCount; ++i)
        {
            const BlockBreakParticle& particle = blockBreakParticles_[i];
            const float half = particle.size * 0.5f;
            const Vec3 rightOffset{right.x * half, right.y * half, right.z * half};
            const Vec3 upOffset{up.x * half, up.y * half, up.z * half};
            const float layer = static_cast<float>(particle.textureLayer);
            const float ao = std::clamp(1.0f - particle.age / particle.lifetime * 0.25f, 0.75f, 1.0f);
            const uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

            vertices.push_back({particle.position.x - rightOffset.x - upOffset.x, particle.position.y - rightOffset.y - upOffset.y, particle.position.z - rightOffset.z - upOffset.z, particle.u0, particle.v1, ao, layer, 1.0f});
            vertices.push_back({particle.position.x - rightOffset.x + upOffset.x, particle.position.y - rightOffset.y + upOffset.y, particle.position.z - rightOffset.z + upOffset.z, particle.u0, particle.v0, ao, layer, 1.0f});
            vertices.push_back({particle.position.x + rightOffset.x + upOffset.x, particle.position.y + rightOffset.y + upOffset.y, particle.position.z + rightOffset.z + upOffset.z, particle.u1, particle.v0, ao, layer, 1.0f});
            vertices.push_back({particle.position.x + rightOffset.x - upOffset.x, particle.position.y + rightOffset.y - upOffset.y, particle.position.z + rightOffset.z - upOffset.z, particle.u1, particle.v1, ao, layer, 1.0f});

            indices.push_back(baseIndex);
            indices.push_back(baseIndex + 1u);
            indices.push_back(baseIndex + 2u);
            indices.push_back(baseIndex);
            indices.push_back(baseIndex + 2u);
            indices.push_back(baseIndex + 3u);
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
        const Mat4 view = viewMatrix(camera, cameraPosition);
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

    void Renderer::spawnBlockDrops(int x, int y, int z, uint16_t block)
    {
        const BlockDefinition& definition = blockDefinition(block);
        if (definition.drops.empty())
        {
            return;
        }

        auto unitRandom = [](uint32_t hash)
        {
            return static_cast<float>(hash) / static_cast<float>(std::numeric_limits<uint32_t>::max());
        };
        static thread_local std::mt19937 runtimeDropRandom{std::random_device{}()};
        auto randomRange = [&](float minValue, float maxValue)
        {
            std::uniform_real_distribution<float> distribution(minValue, maxValue);
            return distribution(runtimeDropRandom);
        };

        for (size_t dropIndex = 0; dropIndex < definition.drops.size(); ++dropIndex)
        {
            const BlockDrop& drop = definition.drops[dropIndex];
            const uint32_t hash = worldRandomHash(x, y, z, BlockDropSalt + static_cast<uint32_t>(dropIndex) * 0x9E3779B9u);
            if (unitRandom(hash) > drop.chance)
            {
                continue;
            }

            const uint16_t range = static_cast<uint16_t>(drop.max - drop.min + 1u);
            const uint16_t count = static_cast<uint16_t>(drop.min + (range > 0 ? hash % range : 0u));
            if (drop.itemId == 0 || count == 0 || static_cast<size_t>(drop.itemId) >= itemDefinitions_.size())
            {
                continue;
            }

            if (droppedItems_.size() >= MaxDroppedItems)
            {
                droppedItems_.erase(droppedItems_.begin());
            }

            DroppedItem item{};
            item.position = {
                static_cast<float>(x) + randomRange(-0.18f, 0.18f),
                static_cast<float>(y) + 0.5f + randomRange(-0.08f, 0.12f),
                static_cast<float>(z) + randomRange(-0.18f, 0.18f)
            };
            item.previousPosition = item.position;
            item.velocity = {
                randomRange(-1.5f, 1.5f),
                randomRange(2.0f, 3.5f),
                randomRange(-1.5f, 1.5f)
            };
            item.stack.itemId = drop.itemId;
            item.stack.count = count;
            item.renderRotationX = randomRange(0.0f, 6.2831853f);
            item.renderRotation = randomRange(0.0f, 6.2831853f);
            item.renderRotationZ = randomRange(0.0f, 6.2831853f);
            item.renderSpinX = randomRange(-8.0f, 8.0f);
            item.renderSpin = randomRange(-8.0f, 8.0f);
            item.renderSpinZ = randomRange(-8.0f, 8.0f);
            if (std::abs(item.renderSpinX) < 2.0f)
            {
                item.renderSpinX = item.renderSpinX < 0.0f ? -2.0f : 2.0f;
            }
            if (std::abs(item.renderSpin) < 2.0f)
            {
                item.renderSpin = item.renderSpin < 0.0f ? -2.0f : 2.0f;
            }
            if (std::abs(item.renderSpinZ) < 2.0f)
            {
                item.renderSpinZ = item.renderSpinZ < 0.0f ? -2.0f : 2.0f;
            }
            droppedItems_.push_back(item);
        }
    }

    bool Renderer::raycastDroppedItem(DVec3 origin, Vec3 direction, size_t& itemIndex) const
    {
        constexpr double MaxInteractionDistance = 8.0;
        constexpr double Epsilon = 0.000001;

        const Vec3 normalizedDirection = normalize(direction);
        if (normalizedDirection.x == 0.0f && normalizedDirection.y == 0.0f && normalizedDirection.z == 0.0f)
        {
            return false;
        }

        auto rayIntersectsAabb = [&](const DroppedItem& item, double& hitDistance)
        {
            const double halfWidth = static_cast<double>(DroppedItemSize) * 0.5;
            const double minX = static_cast<double>(item.position.x) - halfWidth;
            const double maxX = static_cast<double>(item.position.x) + halfWidth;
            const double minY = static_cast<double>(item.position.y);
            const double maxY = static_cast<double>(item.position.y) + static_cast<double>(DroppedItemThickness);
            const double minZ = static_cast<double>(item.position.z) - halfWidth;
            const double maxZ = static_cast<double>(item.position.z) + halfWidth;

            double tMin = 0.0;
            double tMax = MaxInteractionDistance;
            auto testAxis = [&](double axisOrigin, double axisDirection, double axisMin, double axisMax)
            {
                if (std::abs(axisDirection) < Epsilon)
                {
                    return axisOrigin >= axisMin && axisOrigin <= axisMax;
                }

                double t0 = (axisMin - axisOrigin) / axisDirection;
                double t1 = (axisMax - axisOrigin) / axisDirection;
                if (t0 > t1)
                {
                    std::swap(t0, t1);
                }
                tMin = std::max(tMin, t0);
                tMax = std::min(tMax, t1);
                return tMin <= tMax;
            };

            if (!testAxis(origin.x, normalizedDirection.x, minX, maxX) ||
                !testAxis(origin.y, normalizedDirection.y, minY, maxY) ||
                !testAxis(origin.z, normalizedDirection.z, minZ, maxZ))
            {
                return false;
            }

            hitDistance = tMin;
            return hitDistance >= 0.0 && hitDistance <= MaxInteractionDistance;
        };

        bool found = false;
        double bestDistance = MaxInteractionDistance;
        for (size_t i = 0; i < droppedItems_.size(); ++i)
        {
            const DroppedItem& item = droppedItems_[i];
            if (item.stack.itemId == 0 || item.stack.count == 0 || item.collecting)
            {
                continue;
            }

            double hitDistance = 0.0;
            if (rayIntersectsAabb(item, hitDistance) && hitDistance <= bestDistance)
            {
                bestDistance = hitDistance;
                itemIndex = i;
                found = true;
            }
        }

        return found;
    }

    bool Renderer::droppedItemTouchesPlayerCollider(const DroppedItem& item, Vec3 playerPosition) const
    {
        constexpr float PlayerHalfWidth = 0.3f;
        constexpr float PlayerHeight = 1.75f;
        const float itemHalfWidth = DroppedItemSize * 0.5f;

        return item.position.x + itemHalfWidth >= playerPosition.x - PlayerHalfWidth &&
            item.position.x - itemHalfWidth <= playerPosition.x + PlayerHalfWidth &&
            item.position.y + DroppedItemThickness >= playerPosition.y &&
            item.position.y <= playerPosition.y + PlayerHeight &&
            item.position.z + itemHalfWidth >= playerPosition.z - PlayerHalfWidth &&
            item.position.z - itemHalfWidth <= playerPosition.z + PlayerHalfWidth;
    }

    void Renderer::updateDroppedItems(Vec3 playerPosition)
    {
        const double now = glfwGetTime();
        if (lastDroppedItemUpdateTime_ <= 0.0)
        {
            lastDroppedItemUpdateTime_ = now;
            droppedItemRenderAlpha_ = 0.0f;
            return;
        }

        const float frameDt = static_cast<float>(std::clamp(now - lastDroppedItemUpdateTime_, 0.0, static_cast<double>(DroppedItemMaxFrameSeconds)));
        lastDroppedItemUpdateTime_ = now;
        if (frameDt <= 0.0f)
        {
            droppedItemRenderAlpha_ = 0.0f;
            return;
        }
        if (droppedItems_.empty())
        {
            droppedItemTickAccumulator_ = 0.0f;
            droppedItemRenderAlpha_ = 0.0f;
            return;
        }

        for (DroppedItem& item : droppedItems_)
        {
            if (!item.grounded || item.collecting)
            {
                item.renderRotationX += item.renderSpinX * frameDt;
                item.renderRotation += item.renderSpin * frameDt;
                item.renderRotationZ += item.renderSpinZ * frameDt;
            }
            else
            {
                item.renderRotationX = 0.0f;
                item.renderRotationZ = 0.0f;
            }
        }

        droppedItemTickAccumulator_ += frameDt;
        while (droppedItemTickAccumulator_ >= DroppedItemTickSeconds)
        {
            updateDroppedItemsTick(playerPosition, DroppedItemTickSeconds);
            droppedItemTickAccumulator_ -= DroppedItemTickSeconds;
        }
        droppedItemRenderAlpha_ = std::clamp(droppedItemTickAccumulator_ / DroppedItemTickSeconds, 0.0f, 1.0f);
    }

    void Renderer::updateDroppedItemsTick(Vec3 playerPosition, float dt)
    {
        if (dt <= 0.0f || droppedItems_.empty())
        {
            return;
        }

        constexpr float GroundProbeEpsilon = 0.01f;
        constexpr float WallProbeHeight = 0.08f;
        const float drag = std::pow(DroppedItemDrag, dt * 60.0f);

        auto solidAt = [&](float x, float y, float z)
        {
            return terrainCellBlocksPlayer(blockCoordinateXz(x), blockCoordinateY(y), blockCoordinateXz(z));
        };
        auto supportedByGround = [&](const DroppedItem& item)
        {
            return solidAt(item.position.x, item.position.y - GroundProbeEpsilon, item.position.z);
        };
        auto sideBlocked = [&](float x, float y, float z)
        {
            return solidAt(x, y + WallProbeHeight, z);
        };

        for (size_t i = 0; i < droppedItems_.size();)
        {
            DroppedItem& item = droppedItems_[i];
            item.previousPosition = item.position;
            item.age += dt;
            if (item.collecting)
            {
                item.collectAge += dt;
                const Vec3 target{
                    playerPosition.x,
                    playerPosition.y + 0.875f,
                    playerPosition.z
                };
                const Vec3 toTarget{
                    target.x - item.position.x,
                    target.y - item.position.y,
                    target.z - item.position.z
                };
                const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z);
                const float speed = std::min(
                    DroppedItemPickupMaxSpeed,
                    DroppedItemPickupBaseSpeed + item.collectAge * DroppedItemPickupAcceleration);
                const float travel = speed * dt;
                if (distance <= travel || droppedItemTouchesPlayerCollider(item, playerPosition))
                {
                    const uint16_t remaining = addItemToPlayerInventory(item.stack);
                    if (remaining == 0)
                    {
                        droppedItems_.erase(droppedItems_.begin() + static_cast<std::ptrdiff_t>(i));
                        continue;
                    }

                    item.stack.count = remaining;
                    item.collecting = false;
                    item.collectAge = 0.0f;
                    item.velocity = {};
                    item.renderSpinX = 5.0f;
                    item.renderSpin = 5.0f;
                    item.renderSpinZ = 5.0f;
                    ++i;
                    continue;
                }

                const float scale = travel / std::max(distance, 0.0001f);
                item.position.x += toTarget.x * scale;
                item.position.y += toTarget.y * scale;
                item.position.z += toTarget.z * scale;
                item.velocity = {};
                ++i;
                continue;
            }

            if (item.grounded)
            {
                if (supportedByGround(item))
                {
                    item.velocity = {};
                    item.renderSpinX = 0.0f;
                    item.renderSpin = 0.0f;
                    item.renderSpinZ = 0.0f;
                    ++i;
                    continue;
                }

                item.grounded = false;
                item.velocity.y = std::min(item.velocity.y, 0.0f);
                if (item.renderSpinX == 0.0f)
                {
                    item.renderSpinX = 5.0f;
                }
                if (item.renderSpin == 0.0f)
                {
                    item.renderSpin = 5.0f;
                }
                if (item.renderSpinZ == 0.0f)
                {
                    item.renderSpinZ = 5.0f;
                }
            }

            item.velocity.x *= drag;
            item.velocity.z *= drag;
            item.velocity.y -= DroppedItemGravity * dt;

            if (item.velocity.x != 0.0f)
            {
                const float nextX = item.position.x + item.velocity.x * dt;
                const float probeX = nextX + (item.velocity.x > 0.0f ? DroppedItemCollisionRadius : -DroppedItemCollisionRadius);
                if (sideBlocked(probeX, item.position.y, item.position.z))
                {
                    item.velocity.x = -item.velocity.x * DroppedItemWallBounce;
                    item.velocity.z *= DroppedItemWallFriction;
                }
                else
                {
                    item.position.x = nextX;
                }
            }

            if (item.velocity.z != 0.0f)
            {
                const float nextZ = item.position.z + item.velocity.z * dt;
                const float probeZ = nextZ + (item.velocity.z > 0.0f ? DroppedItemCollisionRadius : -DroppedItemCollisionRadius);
                if (sideBlocked(item.position.x, item.position.y, probeZ))
                {
                    item.velocity.z = -item.velocity.z * DroppedItemWallBounce;
                    item.velocity.x *= DroppedItemWallFriction;
                }
                else
                {
                    item.position.z = nextZ;
                }
            }

            const float currentY = item.position.y;
            const float nextY = currentY + item.velocity.y * dt;
            bool landed = false;
            if (item.velocity.y < 0.0f)
            {
                const int startY = blockCoordinateY(currentY - GroundProbeEpsilon);
                const int endY = blockCoordinateY(nextY - GroundProbeEpsilon);
                for (int groundY = startY; groundY >= endY; --groundY)
                {
                    if (!terrainCellBlocksPlayer(blockCoordinateXz(item.position.x), groundY, blockCoordinateXz(item.position.z)))
                    {
                        continue;
                    }

                    item.position.y = static_cast<float>(groundY + 1);
                    item.velocity = {};
                    item.renderRotationX = 0.0f;
                    item.renderRotation = std::fmod(item.renderRotation, 6.2831853f);
                    if (item.renderRotation < 0.0f)
                    {
                        item.renderRotation += 6.2831853f;
                    }
                    item.renderRotationZ = 0.0f;
                    item.renderSpinX = 0.0f;
                    item.renderSpin = 0.0f;
                    item.renderSpinZ = 0.0f;
                    item.grounded = true;
                    landed = true;
                    break;
                }
            }

            if (!landed)
            {
                item.position.y = nextY;
            }
            ++i;
        }
    }

    Renderer::ItemSpriteMesh Renderer::buildItemSpriteMesh(const std::filesystem::path& path) const
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* loadedPixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (loadedPixels == nullptr)
        {
            throw std::runtime_error("Failed to load item sprite mesh texture: " + path.string());
        }

        ItemSpriteMesh mesh{};
        auto alphaAt = [&](int x, int y)
        {
            if (x < 0 || x >= width || y < 0 || y >= height)
            {
                return 0u;
            }
            const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u + 3u;
            return static_cast<unsigned int>(loadedPixels[index]);
        };
        auto opaqueAt = [&](int x, int y)
        {
            return alphaAt(x, y) >= 128u;
        };
        auto addQuad = [&](std::array<Vec3, 4> positions, std::array<std::array<float, 2>, 4> uvs, float ao)
        {
            ItemSpriteQuad quad{};
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

    void Renderer::drawDroppedItems(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, Vec3 playerPosition)
    {
        updateDroppedItems(playerPosition);
        if (droppedItems_.empty() ||
            itemPipeline_ == VK_NULL_HANDLE ||
            droppedItemVertexBuffer_ == VK_NULL_HANDLE ||
            droppedItemIndexBuffer_ == VK_NULL_HANDLE ||
            itemTextureArray_.descriptorSet == VK_NULL_HANDLE)
        {
            return;
        }

        const size_t itemCount = std::min(droppedItems_.size(), MaxDroppedItems);
        std::vector<TerrainVertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(std::min<size_t>(itemCount * 64u * 4u, MaxDroppedItemRenderQuads * 4u));
        indices.reserve(std::min<size_t>(itemCount * 64u * 6u, MaxDroppedItemRenderQuads * 6u));

        auto appendQuad = [&](const std::array<Vec3, 4>& positions, const std::array<std::array<float, 2>, 4>& uvs, float layer, float ao)
        {
            if (indices.size() / 6u >= MaxDroppedItemRenderQuads)
            {
                return;
            }

            const uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
            for (size_t vertexIndex = 0; vertexIndex < positions.size(); ++vertexIndex)
            {
                vertices.push_back({
                    positions[vertexIndex].x,
                    positions[vertexIndex].y,
                    positions[vertexIndex].z,
                    uvs[vertexIndex][0],
                    uvs[vertexIndex][1],
                    ao,
                    layer,
                    1.0f
                });
            }
            indices.push_back(baseIndex);
            indices.push_back(baseIndex + 1u);
            indices.push_back(baseIndex + 2u);
            indices.push_back(baseIndex);
            indices.push_back(baseIndex + 2u);
            indices.push_back(baseIndex + 3u);
        };

        for (size_t i = 0; i < itemCount; ++i)
        {
            const DroppedItem& item = droppedItems_[i];
            if (item.stack.itemId == 0 || item.stack.count == 0 || static_cast<size_t>(item.stack.itemId) >= itemDefinitions_.size())
            {
                continue;
            }

            const ItemDefinition& definition = itemDefinitions_[item.stack.itemId];
            if (definition.droppedRender != ItemRenderType::ExtrudedSprite)
            {
                continue;
            }
            if (static_cast<size_t>(item.stack.itemId) >= itemSpriteMeshes_.size() || itemSpriteMeshes_[item.stack.itemId].quads.empty())
            {
                continue;
            }

            const Vec3 interpolatedPosition{
                item.previousPosition.x + (item.position.x - item.previousPosition.x) * droppedItemRenderAlpha_,
                item.previousPosition.y + (item.position.y - item.previousPosition.y) * droppedItemRenderAlpha_,
                item.previousPosition.z + (item.position.z - item.previousPosition.z) * droppedItemRenderAlpha_
            };
            const float cosX = std::cos(item.renderRotationX);
            const float sinX = std::sin(item.renderRotationX);
            const float cosY = std::cos(item.renderRotation);
            const float sinY = std::sin(item.renderRotation);
            const float cosZ = std::cos(item.renderRotationZ);
            const float sinZ = std::sin(item.renderRotationZ);
            const Vec3 center{
                interpolatedPosition.x,
                interpolatedPosition.y + DroppedItemThickness * 0.5f,
                interpolatedPosition.z
            };
            const float layer = static_cast<float>(definition.droppedTextureLayer);
            auto transformLocal = [&](const Vec3& local)
            {
                Vec3 value{
                    local.x * DroppedItemSize,
                    local.y * DroppedItemThickness,
                    local.z * DroppedItemSize
                };

                value = {
                    value.x,
                    value.y * cosX - value.z * sinX,
                    value.y * sinX + value.z * cosX
                };
                value = {
                    value.x * cosZ - value.y * sinZ,
                    value.x * sinZ + value.y * cosZ,
                    value.z
                };
                value = {
                    value.x * cosY - value.z * sinY,
                    value.y,
                    value.x * sinY + value.z * cosY
                };
                return Vec3{center.x + value.x, center.y + value.y, center.z + value.z};
            };

            for (const ItemSpriteQuad& quad : itemSpriteMeshes_[item.stack.itemId].quads)
            {
                std::array<Vec3, 4> worldPositions{};
                for (size_t vertexIndex = 0; vertexIndex < quad.positions.size(); ++vertexIndex)
                {
                    worldPositions[vertexIndex] = transformLocal(quad.positions[vertexIndex]);
                }
                appendQuad(worldPositions, quad.uvs, layer, quad.ao);
            }
        }

        if (indices.empty())
        {
            return;
        }

        const VkDeviceSize vertexBytes = sizeof(TerrainVertex) * vertices.size();
        const VkDeviceSize indexBytes = sizeof(uint32_t) * indices.size();
        void* vertexData = nullptr;
        vkMapMemory(device_, droppedItemVertexMemory_, 0, vertexBytes, 0, &vertexData);
        std::memcpy(vertexData, vertices.data(), static_cast<size_t>(vertexBytes));
        vkUnmapMemory(device_, droppedItemVertexMemory_);

        void* indexData = nullptr;
        vkMapMemory(device_, droppedItemIndexMemory_, 0, indexBytes, 0, &indexData);
        std::memcpy(indexData, indices.data(), static_cast<size_t>(indexBytes));
        vkUnmapMemory(device_, droppedItemIndexMemory_);

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
        push.cameraPosition[3] = static_cast<float>(glfwGetTime());

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, itemPipeline_);
        vkCmdPushConstants(commandBuffer, particlePipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipelineLayout_, 0, 1, &itemTextureArray_.descriptorSet, 0, nullptr);
        const VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &droppedItemVertexBuffer_, &vertexOffset);
        vkCmdBindIndexBuffer(commandBuffer, droppedItemIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);
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

    std::array<float, Renderer::ChunkColumnCount> Renderer::buildChunkTileableClimateNoise(
        int chunkX,
        int chunkZ,
        float featureScale,
        float simplexScale,
        int octaveCount,
        float lacunarity,
        float gain,
        int seed) const
    {
        std::array<float, Renderer::ChunkColumnCount> noise{};
        auto generator = terrainNoiseGenerator(simplexScale, octaveCount, lacunarity, gain);
        if (!generator)
        {
            return noise;
        }

        constexpr float TwoPi = 6.28318530718f;
        const float angleScale = TwoPi / static_cast<float>(TerrainTilePeriod);
        const float radius = static_cast<float>(TerrainTilePeriod) / (TwoPi * featureScale);
        std::array<float, ChunkColumnCount> xPositions{};
        std::array<float, ChunkColumnCount> yPositions{};
        std::array<float, ChunkColumnCount> zPositions{};
        std::array<float, ChunkColumnCount> wPositions{};

        const int worldXStart = chunkX * ChunkSizeX;
        const int worldZStart = chunkZ * ChunkSizeZ;
        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            const float zAngle = static_cast<float>(positiveModulo(worldZStart + localZ, TerrainTilePeriod)) * angleScale;
            const float zCos = std::cos(zAngle) * radius;
            const float zSin = std::sin(zAngle) * radius;
            for (int localX = 0; localX < ChunkSizeX; ++localX)
            {
                const float xAngle = static_cast<float>(positiveModulo(worldXStart + localX, TerrainTilePeriod)) * angleScale;
                const size_t index = static_cast<size_t>(localZ * ChunkSizeX + localX);
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
        const std::array<float, ChunkColumnCount> temperatureNoise = buildChunkTileableClimateNoise(
            chunk.chunkX,
            chunk.chunkZ,
            temperatureNoiseFeatureScale_,
            temperatureNoiseSimplexScale_,
            temperatureNoiseOctaveCount_,
            temperatureNoiseLacunarity_,
            temperatureNoiseGain_,
            temperatureSeed());
        const std::array<float, ChunkColumnCount> precipitationNoise = buildChunkTileableClimateNoise(
            chunk.chunkX,
            chunk.chunkZ,
            precipitationNoiseFeatureScale_,
            precipitationNoiseSimplexScale_,
            precipitationNoiseOctaveCount_,
            precipitationNoiseLacunarity_,
            precipitationNoiseGain_,
            precipitationSeed());

        const int worldZStart = chunk.chunkZ * ChunkSizeZ;
        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            const int wrappedZ = wrapBlockCoordinate(worldZStart + localZ);
            for (int localX = 0; localX < ChunkSizeX; ++localX)
            {
                const size_t column = static_cast<size_t>(localZ * ChunkSizeX + localX);
                chunk.temperature[column] = encodeClimateValue(temperatureAtWrapped(wrappedZ, temperatureNoise[column]));
                chunk.precipitation[column] = encodeClimateValue(precipitationAtNoise(precipitationNoise[column]));
            }
        }
    }

    float Renderer::baseTemperatureAtWrappedZ(int wrappedZ) const
    {
        const float normalizedZ = static_cast<float>(positiveModulo(wrappedZ, WorldSizeBlocks)) / static_cast<float>(WorldSizeBlocks);
        return std::clamp(1.0f - std::abs(normalizedZ * 2.0f - 1.0f), 0.0f, 1.0f);
    }

    float Renderer::temperatureAtWrapped(int wrappedZ, float noise) const
    {
        const float base = baseTemperatureAtWrappedZ(wrappedZ);
        const float midLatitudeMask = 1.0f - std::abs(base * 2.0f - 1.0f);
        return std::clamp(base + noise * temperatureNoiseStrength_ * midLatitudeMask, 0.0f, 1.0f);
    }

    float Renderer::precipitationAtNoise(float noise) const
    {
        return std::clamp(noise * 0.5f + 0.5f, 0.0f, 1.0f);
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
        addText(debugTextBatch_, peakProfilerStatusText_, rightX, static_cast<float>(swapchainExtent_.height) - 242.0f, true);
        addText(debugTextBatch_, peakFrameText_, rightX, static_cast<float>(swapchainExtent_.height) - 220.0f, true);
        addText(debugTextBatch_, peakUpdateText_, rightX, static_cast<float>(swapchainExtent_.height) - 198.0f, true);
        addText(debugTextBatch_, peakEnsureRuntimeText_, rightX, static_cast<float>(swapchainExtent_.height) - 176.0f, true);
        addText(debugTextBatch_, peakWantRenderText_, rightX, static_cast<float>(swapchainExtent_.height) - 154.0f, true);
        addText(debugTextBatch_, peakEnsureKeyText_, rightX, static_cast<float>(swapchainExtent_.height) - 132.0f, true);
        addText(debugTextBatch_, peakEnsureMarkText_, rightX, static_cast<float>(swapchainExtent_.height) - 110.0f, true);
        addText(debugTextBatch_, peakEnsureFindText_, rightX, static_cast<float>(swapchainExtent_.height) - 88.0f, true);
        addText(debugTextBatch_, peakEnsureLoadText_, rightX, static_cast<float>(swapchainExtent_.height) - 66.0f, true);
        addText(debugTextBatch_, peakEnsureCreateText_, rightX, static_cast<float>(swapchainExtent_.height) - 44.0f, true);
        addText(debugTextBatch_, peakEnsureDataTouchText_, rightX, static_cast<float>(swapchainExtent_.height) - 22.0f, true);
        addText(debugTextBatch_, dataQueueText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 264.0f, false);
        addText(debugTextBatch_, finalizeQueueText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 242.0f, false);
        addText(debugTextBatch_, meshQueueText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 220.0f, false);
        addText(debugTextBatch_, dataDoneText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 198.0f, false);
        addText(debugTextBatch_, meshDoneText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 176.0f, false);
        addText(debugTextBatch_, saveQueueText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 154.0f, false);
        addText(debugTextBatch_, saveDoneText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 132.0f, false);
        addText(debugTextBatch_, loadText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 110.0f, false);
        addText(debugTextBatch_, uploadText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 88.0f, false);
        addText(debugTextBatch_, unloadText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 66.0f, false);
        addText(debugTextBatch_, retiredText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 44.0f, false);
        addText(debugTextBatch_, jobMainText_, 12.0f, static_cast<float>(swapchainExtent_.height) - 22.0f, false);

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

    void Renderer::updatePeakProfiler(double frameMs)
    {
        const auto now = std::chrono::steady_clock::now();
        if (peakProfilerStartTime_ == std::chrono::steady_clock::time_point{})
        {
            peakProfilerStartTime_ = now;
            peakProfilerStatusText_ = "PEAK SAMPLE: WAIT 5S";
            debugTextBatchDirty_ = true;
            return;
        }

        const std::chrono::duration<double> elapsed = now - peakProfilerStartTime_;
        if (!peakProfilerSamplingStarted_)
        {
            if (elapsed.count() < PeakProfilerStartupDelaySeconds)
            {
                return;
            }

            peakProfilerSamplingStarted_ = true;
            peakProfilerStatusText_ = "PEAK SAMPLE: ON";
            updatePeakProfilerText();
        }

        bool changed = false;
        const auto updatePeak = [&](double value, double& peak)
        {
            if (value > peak)
            {
                peak = value;
                changed = true;
            }
        };

        updatePeak(frameMs, peakFrameMs_);
        updatePeak(frameChunkUpdateMs_, peakChunkUpdateMs_);
        updatePeak(frameJobMainMs_, peakJobMainMs_);
        updatePeak(frameUploadMs_, peakUploadMs_);
        updatePeak(frameUnloadMs_, peakUnloadMs_);
        updatePeak(frameRetireMs_, peakRetireMs_);
        updatePeak(frameSaveEnqueueMs_, peakSaveEnqueueMs_);
        updatePeak(frameEnsureRuntimeMs_, peakEnsureRuntimeMs_);
        updatePeak(frameWantRenderMs_, peakWantRenderMs_);
        updatePeak(frameWantEnsureMs_, peakWantEnsureMs_);
        updatePeak(frameWantInsertMs_, peakWantInsertMs_);
        updatePeak(frameWantReadyMs_, peakWantReadyMs_);
        updatePeak(frameWantDependMs_, peakWantDependMs_);
        updatePeak(frameWantMeshReadyMs_, peakWantMeshReadyMs_);
        updatePeak(frameWantMeshDependMs_, peakWantMeshDependMs_);
        updatePeak(frameEnsureKeyMs_, peakEnsureKeyMs_);
        updatePeak(frameEnsureMarkMs_, peakEnsureMarkMs_);
        updatePeak(frameEnsureFindMs_, peakEnsureFindMs_);
        updatePeak(frameEnsureLoadMs_, peakEnsureLoadMs_);
        updatePeak(frameEnsureCreateMs_, peakEnsureCreateMs_);
        updatePeak(frameEnsureDataTouchMs_, peakEnsureDataTouchMs_);

        if (changed)
        {
            updatePeakProfilerText();
        }
    }

    void Renderer::updatePeakProfilerText()
    {
        peakFrameText_ = formatProfileMs("PEAK FRAME", peakFrameMs_);
        peakUpdateText_ = formatProfileMs("PEAK UPDATE", peakChunkUpdateMs_);
        peakEnsureRuntimeText_ = formatProfileMs("PEAK ENSURE RUNTIME", peakEnsureRuntimeMs_);
        peakWantRenderText_ = formatProfileMs("PEAK WANT RENDER", peakWantRenderMs_);
        peakEnsureKeyText_ = formatProfileMs("PEAK ENSURE KEY", peakEnsureKeyMs_);
        peakEnsureMarkText_ = formatProfileMs("PEAK ENSURE MARK", peakEnsureMarkMs_);
        peakEnsureFindText_ = formatProfileMs("PEAK ENSURE FIND", peakEnsureFindMs_);
        peakEnsureLoadText_ = formatProfileMs("PEAK ENSURE LOAD", peakEnsureLoadMs_);
        peakEnsureCreateText_ = formatProfileMs("PEAK ENSURE CREATE", peakEnsureCreateMs_);
        peakEnsureDataTouchText_ = formatProfileMs("PEAK ENSURE DATA TOUCH", peakEnsureDataTouchMs_);
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
