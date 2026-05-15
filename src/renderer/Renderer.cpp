#include "renderer/Renderer.h"

#include "camera/Camera.h"
#include "config/ConfigLoaders.h"
#include "platform/Log.h"
#include "platform/RuntimePaths.h"
#include "renderer/RendererTerrainMeshBridge.h"

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
        constexpr float DefaultPrecipitationNoiseFeatureScale = 4096.0f;
        constexpr int DefaultPrecipitationNoiseOctaveCount = 3;
        constexpr float DefaultPrecipitationNoiseLacunarity = 2.0f;
        constexpr float DefaultPrecipitationNoiseGain = 0.5f;
        constexpr float DefaultPrecipitationNoiseSimplexScale = 1.0f;
        constexpr float DefaultFluidWaterAlpha = 0.8f;
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
        constexpr int FluidAmountBits = 7;
        constexpr uint16_t FluidAmountMask = (1u << FluidAmountBits) - 1u;
        constexpr uint8_t PlantPlacementMax = 151;
        constexpr uint8_t StonePlacementMax = 159;
        constexpr uint8_t BranchPlacementMax = 167;
        constexpr uint8_t TreePlacementMin = 168;
        constexpr uint8_t TreePlacementMax = 170;
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

        constexpr uint16_t fluidId(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid >> FluidAmountBits);
        }

        constexpr uint16_t fluidAmount(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid & FluidAmountMask);
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

    }

    bool Renderer::QueueFamilyIndices::complete() const
    {
        return graphics != UINT32_MAX && present != UINT32_MAX;
    }

    Renderer::Renderer(GLFWwindow* window)
        : window_(window),
        terrainSceneRuntime_(clientWorldRuntime_),
        sceneLifecycle_(clientWorldRuntime_, terrainSceneRuntime_, gameplayRuntime_),
        worldRuntime_(clientWorldRuntime_.worldRuntime),
        gpuResources_(&physicalDevice_, &device_, &graphicsQueue_, &commandPool_, &descriptorPool_, &descriptorSetLayout_, &sampler_),
        terrainRenderPath_(&device_, &descriptorPool_, &terrainVertexDescriptorSetLayout_, &gpuResources_),
        textRenderPath_(&device_, &gpuResources_),
        playerMeshRenderPath_(&device_, &gpuResources_),
        particleRenderPath_(&device_, &gpuResources_),
        droppedItemRenderPath_(&device_, &gpuResources_)
    {
        gameplayRuntime_.setContext(&worldRuntime_, &content_.itemDefinitions());
        uiBridge_.setContext(&ui_, &gameplayRuntime_, &content_.itemDefinitions());

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
        createTextRenderPath();
        createUiBuffers();
        createRenderPathBuffers();
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
        unloadGameScene();
        vkDeviceWaitIdle(device_);
        shutdownRmlUi();
        shutdownAudio();

        cleanupSwapchain();
        rendererAssets_.destroy(gpuResources_);

        destroyAllTerrainChunks();
        playerMeshRenderPath_.destroy();
        textRenderPath_.destroy();
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
        particleRenderPath_.destroy();
        droppedItemRenderPath_.destroy();
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

    void Renderer::drawFrame(const RendererFrame& frame)
    {
        const Vec3 cameraPositionFloat = toVec3(frame.cameraPosition);
        const Vec3 playerPositionFloat = toVec3(frame.playerPosition);

        updateAudioListener(frame.camera, cameraPositionFloat);
        updateMusicPlayback(frame.menuOverlayMode, frame.gameSceneRenderEnabled);
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
        if (frame.worldUpdateEnabled)
        {
            updateLoadedChunks(frame.playerPosition);
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
        if (frame.screenshotRequested)
        {
            gpuResources_.createBuffer(
                screenshotSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                screenshotBuffer,
                screenshotMemory);
        }

        if (frame.showPlayer)
        {
            updatePlayerMesh(playerPositionFloat, frame.playerYaw);
        }
        ensureClimateOverlayTexture(frame.climateOverlayMode);

        recordCommandBuffer(
            commandBuffers_[currentFrame_],
            imageIndex,
            frame.camera,
            cameraPositionFloat,
            playerPositionFloat,
            frame.fpsText,
            frame.debugTextVisible,
            screenshotBuffer,
            frame.showPlayer,
            frame.terrainWireframe,
            frame.climateOverlayMode,
            frame.menuOverlayMode,
            frame.hudVisible,
            frame.gameSceneRenderEnabled,
            frame.worldTicks);

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
        return gameplayRuntime_.playerColliderIntersectsTerrain(
            playerPosition,
            [this](int x, int y, int z) { return terrainCellBlocksPlayer(x, y, z); });
    }

    bool Renderer::editBlockInView(DVec3 origin, Vec3 direction, bool placeRock, DVec3 playerPosition)
    {
        return applyBlockEditResult(
            gameplayRuntime_.editBlockInView(
                origin,
                direction,
                placeRock,
                BlockRock,
                playerPosition,
                [this](int x, int y, int z) { return blockAtWorld(x, y, z); },
                [this](uint16_t block) -> const BlockDefinition& { return blockDefinition(block); },
                [this](int x, int y, int z, uint16_t block) { return setBlockAtWorld(x, y, z, block); },
                [this](RuntimeChunk& chunk)
                {
                    markRuntimeChunkDataDirty(chunk);
                }));
    }

    void Renderer::updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, DVec3, float deltaSeconds)
    {
        const gameplay::BlockBreakingUpdate update = gameplayRuntime_.updateBlockBreaking(
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
        return gameplayRuntime_.pickupDroppedItemInView(
            origin,
            direction,
            [this](RuntimeChunk& chunk)
            {
                markRuntimeChunkDataDirty(chunk);
            });
    }

    bool Renderer::dropSelectedHotbarItem(bool wholeStack, DVec3 playerPosition, Vec3 direction)
    {
        const bool dropped = gameplayRuntime_.dropSelectedHotbarItem(
            wholeStack,
            playerPosition,
            direction,
            [this](RuntimeChunk& chunk)
            {
                markRuntimeChunkDataDirty(chunk);
            });
        if (!dropped)
        {
            return false;
        }
        updateInventoryUi();
        return true;
    }

    std::array<ItemStack, gameplay::PlayerInventory::SlotCount> Renderer::inventorySnapshot() const
    {
        return gameplayRuntime_.inventorySnapshot();
    }

    void Renderer::setInventorySnapshot(const std::array<ItemStack, gameplay::PlayerInventory::SlotCount>& slots)
    {
        gameplayRuntime_.setInventorySnapshot(slots);
        updateInventoryUi();
        uiBridge_.updateInventoryDebugSlots();
        uiBridge_.updateInventoryCursorUi();
        uiBridge_.updateItemTooltipUi(swapchainExtent_.width, swapchainExtent_.height);
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
        const world::ClimateSystem climate = climateSystem();
        if (chunk != nullptr && chunk->data)
        {
            const ChunkData& data = *chunk->data;
            temperature = world::ClimateSystem::decodeClimateValue(data.temperature[column]);
            precipitation = world::ClimateSystem::decodeClimateValue(data.precipitation[column]);
        }
        else
        {
            const int wrappedX = wrapBlockCoordinate(blockX);
            const int wrappedZ = wrapBlockCoordinate(blockZ);
            const world::TerrainBuilderConfig config = terrainBuilderConfig();
            const float temperatureNoise = climate.sampleTileableNoise(
                wrappedX,
                wrappedZ,
                config.temperatureNoiseFeatureScale,
                config.temperatureNoiseSimplexScale,
                config.temperatureNoiseOctaveCount,
                config.temperatureNoiseLacunarity,
                config.temperatureNoiseGain,
                climate.temperatureSeed());
            const float precipitationNoise = climate.sampleTileableNoise(
                wrappedX,
                wrappedZ,
                config.precipitationNoiseFeatureScale,
                config.precipitationNoiseSimplexScale,
                config.precipitationNoiseOctaveCount,
                config.precipitationNoiseLacunarity,
                config.precipitationNoiseGain,
                climate.precipitationSeed());
            temperature = climate.temperatureAtWrapped(wrappedZ, temperatureNoise);
            precipitation = climate.precipitationAtNoise(precipitationNoise);
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
        const std::string cpuText = readCpuName();

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
        const std::string gpuText = properties.deviceName;
        const std::string vulkanText = "VULKAN: " + formatVersion(properties.apiVersion);
        const std::string driverText = "DRIVER: " + formatVersion(properties.driverVersion);
        debugOverlayText_.setHardwareInfo(cpuText, gpuText, vulkanText, driverText);
        timestampPeriod_ = properties.limits.timestampPeriod;
        log::info("CPU: " + cpuText);
        log::info("GPU: " + gpuText);
        log::info(vulkanText);
        log::info(driverText);
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
        allocInfo.memoryTypeIndex = gpuResources_.findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

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
        bindingDescription.stride = sizeof(TextRenderPath::TextVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attributes{};
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[0].offset = offsetof(TextRenderPath::TextVertex, x);
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[1].offset = offsetof(TextRenderPath::TextVertex, u);

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
        pushRange.size = sizeof(SpriteRenderPath::Push);
        static_assert(sizeof(SpriteRenderPath::Push) == sizeof(float) * 12);

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
        itemBindings[0].stride = sizeof(DroppedItemRenderPath::ItemLocalVertex);
        itemBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        itemBindings[1].binding = 1;
        itemBindings[1].stride = sizeof(DroppedItemRenderPath::Instance);
        itemBindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        std::array<VkVertexInputAttributeDescription, 5> itemAttributes{};
        itemAttributes[0].binding = 0;
        itemAttributes[0].location = 0;
        itemAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        itemAttributes[0].offset = offsetof(DroppedItemRenderPath::ItemLocalVertex, x);
        itemAttributes[1].binding = 0;
        itemAttributes[1].location = 1;
        itemAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        itemAttributes[1].offset = offsetof(DroppedItemRenderPath::ItemLocalVertex, u);
        itemAttributes[2].binding = 0;
        itemAttributes[2].location = 2;
        itemAttributes[2].format = VK_FORMAT_R32_SFLOAT;
        itemAttributes[2].offset = offsetof(DroppedItemRenderPath::ItemLocalVertex, ao);
        itemAttributes[3].binding = 1;
        itemAttributes[3].location = 3;
        itemAttributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        itemAttributes[3].offset = offsetof(DroppedItemRenderPath::Instance, centerX);
        itemAttributes[4].binding = 1;
        itemAttributes[4].location = 4;
        itemAttributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        itemAttributes[4].offset = offsetof(DroppedItemRenderPath::Instance, rotationY);

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
            debugOverlayText_.setGpuUnavailable();
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
            debugOverlayText_.setGpuUnavailable();
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
            sceneColorTargets_.push_back(gpuResources_.createRenderTargetTexture(
                swapchainExtent_,
                swapchainImageFormat_,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
            sceneDepthTargets_.push_back(gpuResources_.createRenderTargetTexture(
                swapchainExtent_,
                DepthFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL));
        }
    }

    void Renderer::createTextures()
    {
        const std::filesystem::path assetDir = assetDirectory();
        content_ = game::ClientContent::load(assetDir);
        rendererAssets_ = RendererAssetStore::load(assetDir, content_, gpuResources_);
    }

    void Renderer::createTextRenderPath()
    {
        textRenderPath_.loadFont(assetDirectory() / "fonts" / "VCR_OSD_MONO.ttf", rendererAssets_.font);
        textRenderPath_.createBuffers();
    }
    void Renderer::createUiBuffers()
    {
        gpuResources_.createBuffer(
            sizeof(UiVertex) * MaxUiVertices,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            uiVertexBuffer_,
            uiVertexMemory_);
        gpuResources_.createBuffer(
            sizeof(uint32_t) * MaxUiIndices,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            uiIndexBuffer_,
            uiIndexMemory_);
    }

    void Renderer::createRenderPathBuffers()
    {
        particleRenderPath_.createBuffers();
        droppedItemRenderPath_.createBuffers(rendererAssets_.itemSpriteMeshes);
    }

    void Renderer::createSelectionLineBuffer()
    {
        constexpr VkDeviceSize BufferSize = sizeof(LineVertex) * 24u;
        gpuResources_.createBuffer(
            BufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            selectionLineVertexBuffer_,
            selectionLineVertexMemory_);
    }

    void Renderer::createPlayerMesh()
    {
        playerMeshRenderPath_.loadFromFile(assetDirectory() / "textures" / "character" / "Character.mesh");
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
        requestTerrainLoad(centerGroupChunkX, centerGroupChunkZ);
    }

    void Renderer::requestTerrainLoad(int centerGroupChunkX, int centerGroupChunkZ)
    {
        const game::ClientTerrainSceneRuntime::TerrainLoadResult loadResult = terrainSceneRuntime_.requestTerrainLoad(
            centerGroupChunkX,
            centerGroupChunkZ,
            loadGridScale_,
            LoadGridUnitChunks,
            terrainBuilderConfig(),
            [this](uint64_t key)
            {
                return chunkMeshReady(key);
            });

        if (!loadResult.requested)
        {
            return;
        }

        gameplayRuntime_.reserveDroppedItemTracking(loadResult.droppedItemTrackingCapacity);
        terrainRenderPath_.reserve(loadResult.terrainRenderCapacity);
        terrainRenderPath_.retireChunksNotIn(*loadResult.desiredRenderChunks, static_cast<uint32_t>(MaxFramesInFlight + 1));

        updateTerrainStats();
        debugOverlayText_.markDirty();
    }

    world::TerrainJobResult Renderer::processRenderTerrainMeshJob(TerrainJob job)
    {
        world::TerrainJobResult result{};
        if (job.meshChunks[4])
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
                result.completedChunkMesh = RendererTerrainMeshBridge(content_, rendererAssets_).buildChunkMesh(job.meshChunks, job.generation);
            }
        }
        return result;
    }

    void Renderer::markRuntimeChunkDataDirty(RuntimeChunk& chunk)
    {
        world::WorldRuntime::markDataDirty(chunk);
    }

    void Renderer::processCompletedTerrainJobs()
    {
        auto completionResult = terrainSceneRuntime_.processCompletedTerrainJobs(
            static_cast<uint32_t>(maxTerrainUploadChunksPerFrame_),
            terrainBuilderConfig(),
            [this](uint64_t key)
            {
                return chunkMeshReady(key);
            },
            [this](WorldEntity& entity)
            {
                gameplayRuntime_.normalizeLoadedEntity(entity);
            });

        for (uint64_t key : completionResult.refreshDroppedItemChunkKeys)
        {
            gameplayRuntime_.refreshDroppedItemChunkTracking(key);
        }

        for (CompletedChunkMesh& mesh : completionResult.meshesToInstall)
        {
            const uint64_t key = chunkKey(mesh.chunkX, mesh.chunkZ);
            terrainRenderPath_.installCompletedMesh(key, mesh, static_cast<uint32_t>(MaxFramesInFlight + 1));
            worldRuntime_.markMeshed(key);
        }

        if (completionResult.terrainStatsDirty)
        {
            updateTerrainStats();
        }
        processRetiredTerrainChunks();
        processPendingTerrainUnloads();
    }

    uint32_t Renderer::processPendingTerrainUnloads()
    {
        uint32_t unloadedCount = 0;
        while (unloadedCount < static_cast<uint32_t>(maxTerrainUnloadChunksPerFrame_))
        {
            const std::optional<game::ClientTerrainSceneRuntime::PendingTerrainUnload> pendingUnload =
                terrainSceneRuntime_.processNextPendingTerrainUnload();
            if (!pendingUnload)
            {
                break;
            }

            const uint64_t key = pendingUnload->key;
            terrainRenderPath_.retireAndErase(key, static_cast<uint32_t>(MaxFramesInFlight + 1));
            gameplayRuntime_.removeDroppedItemChunkTracking(key);
            ++unloadedCount;
        }

        if (unloadedCount > 0)
        {
            updateTerrainStats();
            debugOverlayText_.markDirty();
        }

        return unloadedCount;
    }

    void Renderer::processRetiredTerrainChunks()
    {
        terrainRenderPath_.processRetired(static_cast<uint32_t>(maxTerrainRetiredDestroyPerFrame_));
    }

    void Renderer::loadGameScene(const std::filesystem::path& worldDirectory, uint64_t worldSeed)
    {
        game::ClientSceneLifecycle::LoadHooks hooks{};
        hooks.clearParticles = [this](double timestamp)
        {
            particleRenderPath_.clear(timestamp);
        };
        hooks.resetBlockBreaking = [this]
        {
            resetBlockBreaking();
        };
        hooks.refreshInventoryUi = [this]
        {
            updateInventoryUi();
        };
        hooks.refreshInventoryDebugSlots = [this]
        {
            uiBridge_.updateInventoryDebugSlots();
        };
        hooks.resetClimateOverlays = [this]
        {
            climateTemperatureOverlayReady_ = false;
            climatePrecipitationOverlayReady_ = false;
        };
        hooks.processRenderMeshJob = [this](TerrainJob job)
        {
            return processRenderTerrainMeshJob(std::move(job));
        };

        sceneLifecycle_.loadGameScene(
            worldDirectory,
            worldSeed,
            terrainWorkerCount_,
            terrainBuilderConfig(),
            glfwGetTime(),
            hooks);
    }

    void Renderer::unloadGameScene()
    {
        game::ClientSceneLifecycle::UnloadHooks hooks{};
        hooks.waitForRendererIdle = [this]
        {
            vkDeviceWaitIdle(device_);
        };
        hooks.destroyTerrainRenderData = [this]
        {
            destroyAllTerrainChunks();
        };
        hooks.clearParticles = [this]
        {
            particleRenderPath_.clear();
        };
        hooks.resetBlockBreaking = [this]
        {
            resetBlockBreaking();
        };
        hooks.refreshInventoryUi = [this]
        {
            updateInventoryUi();
        };
        hooks.refreshInventoryDebugSlots = [this]
        {
            uiBridge_.updateInventoryDebugSlots();
        };
        hooks.updateTerrainStats = [this]
        {
            updateTerrainStats();
        };
        hooks.markDebugDirty = [this]
        {
            debugOverlayText_.markDirty();
        };

        sceneLifecycle_.unloadGameScene(hooks);
    }

    world::TerrainBuilderConfig Renderer::terrainBuilderConfig() const
    {
        world::TerrainBuilderConfig config{};
        config.heightLut = heightLut_;
        config.activeWorldSeedSalt = clientWorldRuntime_.activeWorldSeedSalt;
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

    bool Renderer::setBlockAtWorld(int x, int y, int z, uint16_t block)
    {
        return worldRuntime_.setBlockAtWorld(x, y, z, block);
    }

    void Renderer::updateChunkEmptySubchunk(const std::shared_ptr<ChunkData>& chunk, int subchunkY)
    {
        world::WorldRuntime::updateChunkEmptySubchunk(chunk, subchunkY);
    }

    void Renderer::rebuildSubchunkMeshNow(int chunkX, int chunkZ, int subchunkY)
    {
        if (subchunkY < 0 || subchunkY >= SubchunksPerChunk)
        {
            return;
        }

        const uint64_t key = chunkKey(chunkX, chunkZ);
        RuntimeChunk* chunk = worldRuntime_.find(key);
        if (chunk == nullptr || !chunk->data || !clientWorldRuntime_.isTerrainDesired(key))
        {
            return;
        }

        const uint64_t generation = terrainSceneRuntime_.terrainGeneration();
        chunk->data->generation = generation;
        const uint64_t revision = chunk->data->revision;
        TerrainBuildData mesh = RendererTerrainMeshBridge(content_, rendererAssets_).buildEditedSubchunkMesh(
            chunk->data,
            subchunkY,
            [this](int x, int y, int z)
            {
                return blockAtWorld(x, y, z);
            });

        clientWorldRuntime_.clearRequestedMeshJob(key);
        if (chunk->data->revision != revision || chunk->data->generation != generation)
        {
            return;
        }

        terrainRenderPath_.replaceEditedSolidSubchunk(
            key,
            chunkX,
            chunkZ,
            chunk->data->revision,
            subchunkY,
            mesh,
            static_cast<uint32_t>(MaxFramesInFlight + 1));
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
        debugOverlayText_.markDirty();
    }

    bool Renderer::chunkMeshReady(uint64_t key) const
    {
        return terrainRenderPath_.chunkMeshReady(key, worldRuntime_.find(key));
    }

    void Renderer::destroyAllTerrainChunks()
    {
        terrainRenderPath_.destroyAll();
        clientWorldRuntime_.resetSceneRuntime();
        gameplayRuntime_.resetDroppedItemTracking();
    }

    void Renderer::updateTerrainStats()
    {
        const TerrainRenderPath::Stats stats = terrainRenderPath_.rebuildStats();
        terrainDrawCount_ = stats.drawCount;
        terrainFaceCount_ = stats.faceCount;
        terrainVertexCount_ = stats.vertexCount;
        debugOverlayText_.setTerrainStats(terrainDrawCount_, terrainFaceCount_, terrainVertexCount_);
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
            gpuResources_.destroyTexture(texture);
        }
        sceneColorTargets_.clear();
        for (Texture& texture : sceneDepthTargets_)
        {
            gpuResources_.destroyTexture(texture);
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
            screenPresentation_.drawSkySprites(
                commandBuffer,
                camera,
                swapchainExtent_,
                worldTicks,
                rendererAssets_,
                spriteRenderPath_,
                pipelineLayout_,
                textRenderPath_.vertexBuffer());

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
        if (gameSceneRenderEnabled)
        {
            screenPresentation_.drawSceneComposite(
                commandBuffer,
                sceneColorTargets_[imageIndex],
                swapchainExtent_,
                rendererAssets_,
                spriteRenderPath_,
                pipelineLayout_,
                textRenderPath_.vertexBuffer(),
                climateOverlayMode);
        }

        if (gameSceneRenderEnabled && menuOverlayMode == 0 && hudVisible)
        {
            screenPresentation_.drawCrosshair(
                commandBuffer,
                swapchainExtent_,
                rendererAssets_,
                spriteRenderPath_,
                pipelineLayout_,
                textRenderPath_.vertexBuffer());
        }

        if (debugTextVisible)
        {
            updateDebugTextBatch(fpsText);
            screenPresentation_.drawDebugText(
                commandBuffer,
                debugOverlayText_.batch(),
                rendererAssets_,
                textRenderPath_,
                swapchainExtent_,
                pipelineLayout_);
        }
        if (!renderRmlUi(commandBuffer, menuOverlayMode, hudVisible))
        {
            screenPresentation_.drawMenuOverlay(
                commandBuffer,
                menuOverlayMode,
                rendererAssets_,
                spriteRenderPath_,
                textRenderPath_,
                swapchainExtent_,
                pipelineLayout_,
                textRenderPath_.vertexBuffer());
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
        playerMeshRenderPath_.update(playerPosition, playerYaw);
    }

    void Renderer::drawTerrain(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, bool wireframe, bool drawBlocks, bool drawFluids, uint32_t sceneImageIndex)
    {
        if (terrainRenderPath_.empty())
        {
            return;
        }
        (void)sceneImageIndex;

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
        const Vec3 cameraRight = camera.right();
        const Vec3 terrainRight{-cameraRight.x, -cameraRight.y, -cameraRight.z};
        const Vec3 forward = camera.forward();
        const Vec3 terrainForward{forward.x, -forward.y, forward.z};
        const Vec3 terrainUp = normalize(cross(terrainForward, terrainRight));
        const float tanHalfVertical = std::tan(FieldOfViewRadians * 0.5f);
        const TerrainRenderPath::View terrainView{
            cameraPosition,
            {},
            terrainRight,
            terrainUp,
            terrainForward,
            tanHalfVertical,
            tanHalfVertical * aspect,
            TerrainNearPlane,
            TerrainFarPlane
        };

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
        auto addVisibleStats = [&](const TerrainRenderPath::Stats& stats)
        {
            visibleDrawCount += stats.drawCount;
            visibleFaceCount += stats.faceCount;
            visibleVertexCount += stats.vertexCount;
        };

        if (drawBlocks)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe ? terrainWireframePipeline_ : terrainPipeline_);
            vkCmdPushConstants(commandBuffer, terrainPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout_, 0, 1, &rendererAssets_.terrainTextureArray.descriptorSet, 0, nullptr);
            addVisibleStats(terrainRenderPath_.drawSolid(commandBuffer, terrainPipelineLayout_, terrainView));
        }

        if (drawFluids)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, fluidPipeline_);
            vkCmdPushConstants(commandBuffer, terrainPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush), &push);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout_, 0, 1, &rendererAssets_.fluidTextureArray.descriptorSet, 0, nullptr);
            addVisibleStats(terrainRenderPath_.drawFluids(commandBuffer, terrainPipelineLayout_, terrainView));
        }

        if (drawBlocks && !drawFluids && (visibleDrawCount != terrainDrawCount_ ||
            visibleFaceCount != terrainFaceCount_ ||
            visibleVertexCount != terrainVertexCount_))
        {
            terrainRenderPath_.setVisibleStats(visibleDrawCount, visibleFaceCount, visibleVertexCount);
            terrainDrawCount_ = visibleDrawCount;
            terrainFaceCount_ = visibleFaceCount;
            terrainVertexCount_ = visibleVertexCount;
            debugOverlayText_.setTerrainStats(terrainDrawCount_, terrainFaceCount_, terrainVertexCount_);
        }
    }

    const BlockDefinition& Renderer::blockDefinition(uint16_t block) const
    {
        static const BlockDefinition fallback{};
        if (static_cast<size_t>(block) >= content_.blockDefinitions().size())
        {
            return fallback;
        }
        return content_.blockDefinitions()[block];
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

    bool Renderer::applyBlockEditResult(const gameplay::BlockEditResult& result)
    {
        if (!result.changed)
        {
            return false;
        }

        if (result.type == gameplay::BlockEditType::Break)
        {
            spawnBlockBreakParticles(result.hit.blockX, result.hit.blockY, result.hit.blockZ, result.block);
            playBlockBreakSound(result.hit.blockX, result.hit.blockY, result.hit.blockZ);
            rebuildEditedChunkMeshes(result.hit.blockX, result.hit.blockY, result.hit.blockZ);
            return true;
        }

        if (result.type == gameplay::BlockEditType::Place)
        {
            rebuildEditedChunkMeshes(result.hit.previousBlockX, result.hit.previousBlockY, result.hit.previousBlockZ);
            playBlockPlaceSound(result.hit.previousBlockX, result.hit.previousBlockY, result.hit.previousBlockZ);
            return true;
        }

        return false;
    }

    bool Renderer::breakBlockAtHit(const BlockRaycastHit& hit)
    {
        return applyBlockEditResult(
            gameplayRuntime_.breakBlockAtHit(
                hit,
                [this](int x, int y, int z) { return blockAtWorld(x, y, z); },
                [this](uint16_t block) -> const BlockDefinition& { return blockDefinition(block); },
                [this](int x, int y, int z, uint16_t block) { return setBlockAtWorld(x, y, z, block); },
                [this](RuntimeChunk& chunk)
                {
                    markRuntimeChunkDataDirty(chunk);
                }));
    }

    void Renderer::resetBlockBreaking()
    {
        gameplayRuntime_.resetBlockBreaking();
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
        if (face < 0 || face >= 6 || static_cast<size_t>(block) >= content_.blockTextureLayers().size())
        {
            return 0;
        }

        return content_.blockTextureLayers()[block].faces[static_cast<size_t>(face)];
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
        playerMeshRenderPath_.draw(commandBuffer, terrainPipelineLayout_, rendererAssets_.playerTexture);
    }

    void Renderer::spawnBlockBreakParticles(int x, int y, int z, uint16_t block)
    {
        const BlockDefinition& definition = blockDefinition(block);
        if (definition.renderType == BlockRenderType::None)
        {
            return;
        }

        particleRenderPath_.spawnBlockBreak(x, y, z, block, blockFaceTextureLayer(block, 0));
    }

    void Renderer::spawnBlockMiningParticle(const BlockRaycastHit& hit, uint16_t block)
    {
        const BlockDefinition& definition = blockDefinition(block);
        if (definition.renderType == BlockRenderType::None)
        {
            return;
        }

        particleRenderPath_.spawnMiningParticle(
            ParticleRenderPath::MiningHit{
                hit.blockX,
                hit.blockY,
                hit.blockZ,
                hit.previousBlockX,
                hit.previousBlockY,
                hit.previousBlockZ
            },
            blockFaceTextureLayerForHit(block, hit));
    }

    void Renderer::drawBlockBreakParticles(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition)
    {
        const gameplay::BlockBreakingState& blockBreaking = gameplayRuntime_.blockBreakingState();
        const bool drawBreakingOverlay =
            blockBreaking.active &&
            blockBreaking.progress > 0.0f &&
            blockBreaking.progress < 1.0f &&
            blockDefinition(blockBreaking.block).renderType == BlockRenderType::Cube;

        const float aspect = static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);
        const Mat4 projection = perspective(FieldOfViewRadians, aspect, TerrainNearPlane, TerrainFarPlane);
        const Mat4 view = viewMatrix(camera, {});
        const Mat4 mvp = multiply(projection, view);

        ParticleRenderPath::PushConstants push{};
        const double now = glfwGetTime();
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.cameraPosition[0] = cameraPosition.x;
        push.cameraPosition[1] = cameraPosition.y;
        push.cameraPosition[2] = cameraPosition.z;
        push.cameraPosition[3] = static_cast<float>(now);

        ParticleRenderPath::BreakingOverlay overlay{};
        if (drawBreakingOverlay)
        {
            overlay.active = true;
            overlay.x = blockBreaking.x;
            overlay.y = blockBreaking.y;
            overlay.z = blockBreaking.z;
            overlay.progress = blockBreaking.progress;
            overlay.textureLayers = content_.blockBreakingTextureLayers().data();
            overlay.textureLayerCount = content_.blockBreakingTextureLayers().size();
        }

        particleRenderPath_.draw(
            commandBuffer,
            camera,
            swapchainExtent_,
            particlePipeline_,
            particlePipelineLayout_,
            rendererAssets_.terrainTextureArray,
            push,
            overlay,
            now,
            [this](int x, int y, int z)
            {
                return terrainCellBlocksPlayer(x, y, z);
            });
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

    void Renderer::ensureClimateOverlayTexture(int mode)
    {
        if (mode == 1 && !climateTemperatureOverlayReady_)
        {
            const std::vector<unsigned char> pixels = ClimateOverlayTextureBuilder::buildPixels(mode, climateSystem());
            rendererAssets_.climateTemperatureOverlay = gpuResources_.createTextureFromRgba(pixels.data(), ClimateOverlayTextureBuilder::OverlaySize, ClimateOverlayTextureBuilder::OverlaySize, VK_FORMAT_R8G8B8A8_SRGB);
            climateTemperatureOverlayReady_ = true;
        }
        else if (mode == 2 && !climatePrecipitationOverlayReady_)
        {
            const std::vector<unsigned char> pixels = ClimateOverlayTextureBuilder::buildPixels(mode, climateSystem());
            rendererAssets_.climatePrecipitationOverlay = gpuResources_.createTextureFromRgba(pixels.data(), ClimateOverlayTextureBuilder::OverlaySize, ClimateOverlayTextureBuilder::OverlaySize, VK_FORMAT_R8G8B8A8_SRGB);
            climatePrecipitationOverlayReady_ = true;
        }
    }

    world::ClimateSystem Renderer::climateSystem() const
    {
        return world::ClimateSystem(terrainBuilderConfig());
    }

    void Renderer::populateChunkClimate(ChunkData& chunk) const
    {
        climateSystem().populateChunkClimate(chunk);
    }

    void Renderer::updateDebugTextBatch(std::string_view fpsText)
    {
        debugOverlayText_.buildBatch(textRenderPath_, fpsText, VersionText, swapchainExtent_);
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
        debugOverlayText_.setFrameTimings(
            accumulatedCpuFrameMs_ / sampleCount,
            accumulatedGpuFrameMs_ / sampleCount,
            timestampSupported_);
        updateVramText();

        accumulatedCpuFrameMs_ = 0.0;
        accumulatedGpuFrameMs_ = 0.0;
        performanceSampleCount_ = 0;
        performanceSampleStart_ = now;
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
        debugOverlayText_.setTerrainStats(2, 0, 9);
    }

    void Renderer::updateVramText()
    {
        if (localMemoryHeapIndex_ == UINT32_MAX)
        {
            debugOverlayText_.setVramText("VRAM: N/A");
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
                    debugOverlayText_.setVramText("VRAM: " + std::to_string(static_cast<uint64_t>(localMemoryHeapSize_ / (1024u * 1024u))) + "MB");
                    return;
                }
                getMemoryProperties2Khr(physicalDevice_, reinterpret_cast<VkPhysicalDeviceMemoryProperties2KHR*>(&properties));
            }

            const uint64_t usedMb = static_cast<uint64_t>(budget.heapUsage[localMemoryHeapIndex_] / (1024u * 1024u));
            const uint64_t budgetMb = static_cast<uint64_t>(budget.heapBudget[localMemoryHeapIndex_] / (1024u * 1024u));
            debugOverlayText_.setVramText("VRAM: " + std::to_string(usedMb) + " / " + std::to_string(budgetMb) + "MB");
            return;
        }

        debugOverlayText_.setVramText("VRAM: " + std::to_string(static_cast<uint64_t>(localMemoryHeapSize_ / (1024u * 1024u))) + "MB");
    }
}
