#include "renderer/Renderer.h"

#include "camera/Camera.h"

#include <stb_image.h>
#include <stb_truetype.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <ctime>
#include <filesystem>
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
        constexpr int DefaultLoadRadius = 5;
        constexpr int TerrainMinHeight = 120;
        constexpr int TerrainBaseHeight = 130;
        constexpr int TerrainMaxHeight = 140;
        constexpr float TerrainRenderScale = 0.75f;
        constexpr double PerformanceSampleSeconds = 0.5;
        constexpr uint32_t SubchunkEmpty = 1u;
        constexpr uint32_t SubchunkUniform = 2u;
        constexpr uint32_t SubchunkDense = 4u;
        constexpr uint16_t BlockAir = 0;
        constexpr uint16_t BlockRock = 1;
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

        int parseLoadRadius(const std::string& text, int fallback)
        {
            const std::string key = "\"loadRadius\"";
            const size_t keyPos = text.find(key);
            if (keyPos == std::string::npos)
            {
                return fallback;
            }

            const size_t colonPos = text.find(':', keyPos + key.size());
            if (colonPos == std::string::npos)
            {
                return fallback;
            }

            const size_t valueStart = text.find_first_not_of(" \t\r\n", colonPos + 1);
            if (valueStart == std::string::npos)
            {
                return fallback;
            }

            size_t valueEnd = valueStart;
            if (text[valueEnd] == '-')
            {
                ++valueEnd;
            }
            while (valueEnd < text.size() && text[valueEnd] >= '0' && text[valueEnd] <= '9')
            {
                ++valueEnd;
            }
            if (valueEnd == valueStart || (valueEnd == valueStart + 1 && text[valueStart] == '-'))
            {
                return fallback;
            }

            try
            {
                return std::max(0, std::stoi(text.substr(valueStart, valueEnd - valueStart)));
            }
            catch (...)
            {
                return fallback;
            }
        }

        int chunkCoordinate(float worldCoordinate)
        {
            const int blockCoordinate = static_cast<int>(std::floor(worldCoordinate + 0.5f));
            return static_cast<int>(std::floor(static_cast<float>(blockCoordinate) / static_cast<float>(ChunkSizeX)));
        }

        uint32_t packCell(uint16_t blockId, uint16_t fluidId = 0)
        {
            return static_cast<uint32_t>(blockId) | (static_cast<uint32_t>(fluidId) << 16u);
        }

        int terrainHeight(int worldX, int worldZ)
        {
            const double waveX = std::sin(static_cast<double>(worldX) * 0.05) * 5.0;
            const double waveZ = std::cos(static_cast<double>(worldZ) * 0.05) * 5.0;
            return std::clamp(
                static_cast<int>(std::lround(static_cast<double>(TerrainBaseHeight) + waveX + waveZ)),
                TerrainMinHeight,
                TerrainMaxHeight);
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

        uint32_t mipLevelCount(int width, int height)
        {
            return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1u;
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
        createTerrainRenderResources();
        createDescriptorSetLayout();
        createRaymarchDescriptorSetLayout();
        createPipeline();
        createTerrainPipeline();
        createRaymarchPipeline();
        createFramebuffers();
        createCommandPool();
        createPerformanceQueries();
        createSampler();
        createTerrainUpscaleSampler();
        createDescriptorPool();
        createTerrainUpscaleDescriptors();
        createTextures();
        createFont();
        createTextVertexBuffer();
        createPlayerMesh();
        loadWorldConfig();
        createTerrainMesh();
        createCommandBuffers();
        createSyncObjects();
    }

    Renderer::~Renderer()
    {
        vkDeviceWaitIdle(device_);

        cleanupSwapchain();
        destroyTexture(grassTop_);
        destroyTexture(grassSide_);
        destroyTexture(rock_);
        destroyTexture(blockTextureArray_);
        destroyTexture(playerTexture_);
        destroyTexture(font_);
        destroyTexture(crosshair_);
        destroyTexture(moon_);
        destroyTexture(sun_);

        destroyTerrainMesh(grassTopTerrain_);
        destroyTerrainMesh(grassSideTerrain_);
        destroyTerrainMesh(rockTerrain_);
        destroyTerrainMesh(playerMesh_);
        if (raymarchSubchunkBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, raymarchSubchunkBuffer_, nullptr);
        }
        if (raymarchSubchunkMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, raymarchSubchunkMemory_, nullptr);
        }
        if (raymarchBlockBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, raymarchBlockBuffer_, nullptr);
        }
        if (raymarchBlockMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, raymarchBlockMemory_, nullptr);
        }
        if (textVertexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, textVertexBuffer_, nullptr);
        }
        if (textVertexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, textVertexMemory_, nullptr);
        }

        if (descriptorPool_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        }
        if (terrainUpscaleSampler_ != VK_NULL_HANDLE)
        {
            vkDestroySampler(device_, terrainUpscaleSampler_, nullptr);
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
        if (raymarchPipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, raymarchPipeline_, nullptr);
        }
        if (terrainPipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, terrainPipeline_, nullptr);
        }
        if (raymarchPipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, raymarchPipelineLayout_, nullptr);
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
        if (raymarchDescriptorSetLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device_, raymarchDescriptorSetLayout_, nullptr);
        }
        if (renderPass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
        }
        if (terrainRenderPass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device_, terrainRenderPass_, nullptr);
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
        Vec3 cameraPosition,
        std::string_view fpsText,
        bool debugTextVisible,
        bool screenshotRequested,
        bool showPlayer,
        Vec3 playerPosition,
        float playerYaw)
    {
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
            updatePlayerMesh(playerPosition, playerYaw);
        }

        recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, camera, cameraPosition, fpsText, debugTextVisible, screenshotBuffer, showPlayer);

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

        VkAttachmentDescription terrainColorAttachment{};
        terrainColorAttachment.format = swapchainImageFormat_;
        terrainColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        terrainColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        terrainColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        terrainColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        terrainColorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference terrainColorRef{};
        terrainColorRef.attachment = 0;
        terrainColorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription terrainDepthAttachment{};
        terrainDepthAttachment.format = DepthFormat;
        terrainDepthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        terrainDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        terrainDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        terrainDepthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        terrainDepthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        terrainDepthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        terrainDepthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference terrainDepthRef{};
        terrainDepthRef.attachment = 1;
        terrainDepthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription terrainSubpass{};
        terrainSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        terrainSubpass.colorAttachmentCount = 1;
        terrainSubpass.pColorAttachments = &terrainColorRef;
        terrainSubpass.pDepthStencilAttachment = &terrainDepthRef;

        std::array<VkSubpassDependency, 2> terrainDependencies{};
        terrainDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        terrainDependencies[0].dstSubpass = 0;
        terrainDependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        terrainDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        terrainDependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        terrainDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        terrainDependencies[1].srcSubpass = 0;
        terrainDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        terrainDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        terrainDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        terrainDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        terrainDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        std::array<VkAttachmentDescription, 2> terrainAttachments = {terrainColorAttachment, terrainDepthAttachment};

        VkRenderPassCreateInfo terrainCreateInfo{};
        terrainCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        terrainCreateInfo.attachmentCount = static_cast<uint32_t>(terrainAttachments.size());
        terrainCreateInfo.pAttachments = terrainAttachments.data();
        terrainCreateInfo.subpassCount = 1;
        terrainCreateInfo.pSubpasses = &terrainSubpass;
        terrainCreateInfo.dependencyCount = static_cast<uint32_t>(terrainDependencies.size());
        terrainCreateInfo.pDependencies = terrainDependencies.data();

        if (vkCreateRenderPass(device_, &terrainCreateInfo, nullptr, &terrainRenderPass_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain render pass.");
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

    void Renderer::createTerrainRenderResources()
    {
        terrainRenderExtent_.width = std::max(1u, static_cast<uint32_t>(static_cast<float>(swapchainExtent_.width) * TerrainRenderScale));
        terrainRenderExtent_.height = std::max(1u, static_cast<uint32_t>(static_cast<float>(swapchainExtent_.height) * TerrainRenderScale));

        terrainColorImages_.resize(MaxFramesInFlight);
        terrainColorMemories_.resize(MaxFramesInFlight);
        terrainColorImageViews_.resize(MaxFramesInFlight);
        terrainDepthImages_.resize(MaxFramesInFlight);
        terrainDepthMemories_.resize(MaxFramesInFlight);
        terrainDepthImageViews_.resize(MaxFramesInFlight);
        terrainFramebuffers_.resize(MaxFramesInFlight);

        for (size_t i = 0; i < terrainColorImages_.size(); ++i)
        {
            VkImageCreateInfo colorInfo{};
            colorInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            colorInfo.imageType = VK_IMAGE_TYPE_2D;
            colorInfo.extent = {terrainRenderExtent_.width, terrainRenderExtent_.height, 1};
            colorInfo.mipLevels = 1;
            colorInfo.arrayLayers = 1;
            colorInfo.format = swapchainImageFormat_;
            colorInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            colorInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            colorInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateImage(device_, &colorInfo, nullptr, &terrainColorImages_[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create terrain color image.");
            }

            VkMemoryRequirements colorRequirements{};
            vkGetImageMemoryRequirements(device_, terrainColorImages_[i], &colorRequirements);

            VkMemoryAllocateInfo colorAlloc{};
            colorAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            colorAlloc.allocationSize = colorRequirements.size;
            colorAlloc.memoryTypeIndex = findMemoryType(colorRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (vkAllocateMemory(device_, &colorAlloc, nullptr, &terrainColorMemories_[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to allocate terrain color image memory.");
            }

            vkBindImageMemory(device_, terrainColorImages_[i], terrainColorMemories_[i], 0);

            VkImageViewCreateInfo colorView{};
            colorView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            colorView.image = terrainColorImages_[i];
            colorView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            colorView.format = swapchainImageFormat_;
            colorView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            colorView.subresourceRange.levelCount = 1;
            colorView.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_, &colorView, nullptr, &terrainColorImageViews_[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create terrain color image view.");
            }

            VkImageCreateInfo depthInfo{};
            depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            depthInfo.imageType = VK_IMAGE_TYPE_2D;
            depthInfo.extent = {terrainRenderExtent_.width, terrainRenderExtent_.height, 1};
            depthInfo.mipLevels = 1;
            depthInfo.arrayLayers = 1;
            depthInfo.format = DepthFormat;
            depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            depthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateImage(device_, &depthInfo, nullptr, &terrainDepthImages_[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create terrain depth image.");
            }

            VkMemoryRequirements depthRequirements{};
            vkGetImageMemoryRequirements(device_, terrainDepthImages_[i], &depthRequirements);

            VkMemoryAllocateInfo depthAlloc{};
            depthAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            depthAlloc.allocationSize = depthRequirements.size;
            depthAlloc.memoryTypeIndex = findMemoryType(depthRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (vkAllocateMemory(device_, &depthAlloc, nullptr, &terrainDepthMemories_[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to allocate terrain depth image memory.");
            }

            vkBindImageMemory(device_, terrainDepthImages_[i], terrainDepthMemories_[i], 0);

            VkImageViewCreateInfo depthView{};
            depthView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            depthView.image = terrainDepthImages_[i];
            depthView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            depthView.format = DepthFormat;
            depthView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            depthView.subresourceRange.levelCount = 1;
            depthView.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_, &depthView, nullptr, &terrainDepthImageViews_[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create terrain depth image view.");
            }

            std::array<VkImageView, 2> attachments = {terrainColorImageViews_[i], terrainDepthImageViews_[i]};

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = terrainRenderPass_;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = terrainRenderExtent_.width;
            framebufferInfo.height = terrainRenderExtent_.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &terrainFramebuffers_[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create terrain framebuffer.");
            }
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

    void Renderer::createRaymarchDescriptorSetLayout()
    {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorCount = 1;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorCount = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorCount = 1;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        createInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device_, &createInfo, nullptr, &raymarchDescriptorSetLayout_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create raymarch descriptor set layout.");
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

        std::array<VkVertexInputAttributeDescription, 2> attributes{};
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(TerrainVertex, x);
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[1].offset = offsetof(TerrainVertex, u);

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
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(TerrainPush);
        static_assert(sizeof(TerrainPush) == sizeof(float) * 16);

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

    void Renderer::createRaymarchPipeline()
    {
        VkShaderModule vertShader = createShaderModule(std::string(DOLBUTO_SHADER_DIR) + "/raymarch.vert.spv");
        VkShaderModule fragShader = createShaderModule(std::string(DOLBUTO_SHADER_DIR) + "/raymarch.frag.spv");

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
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(RaymarchPush);
        static_assert(sizeof(RaymarchPush) == sizeof(float) * 24);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &raymarchDescriptorSetLayout_;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &raymarchPipelineLayout_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create raymarch pipeline layout.");
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
        pipelineInfo.layout = raymarchPipelineLayout_;
        pipelineInfo.renderPass = terrainRenderPass_;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &raymarchPipeline_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create raymarch pipeline.");
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

    void Renderer::createTerrainUpscaleSampler()
    {
        VkSamplerCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        createInfo.magFilter = VK_FILTER_LINEAR;
        createInfo.minFilter = VK_FILTER_LINEAR;
        createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        createInfo.minLod = 0.0f;
        createInfo.maxLod = 0.0f;

        if (vkCreateSampler(device_, &createInfo, nullptr, &terrainUpscaleSampler_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain upscale sampler.");
        }
    }

    void Renderer::createDescriptorPool()
    {
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = 11;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[1].descriptorCount = 2;

        VkDescriptorPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        createInfo.pPoolSizes = poolSizes.data();
        createInfo.maxSets = 11;

        if (vkCreateDescriptorPool(device_, &createInfo, nullptr, &descriptorPool_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor pool.");
        }
    }

    void Renderer::createTerrainUpscaleDescriptors()
    {
        if (terrainUpscaleDescriptorSets_.empty())
        {
            terrainUpscaleDescriptorSets_.resize(MaxFramesInFlight);
            std::vector<VkDescriptorSetLayout> layouts(MaxFramesInFlight, descriptorSetLayout_);

            VkDescriptorSetAllocateInfo setInfo{};
            setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            setInfo.descriptorPool = descriptorPool_;
            setInfo.descriptorSetCount = static_cast<uint32_t>(terrainUpscaleDescriptorSets_.size());
            setInfo.pSetLayouts = layouts.data();

            if (vkAllocateDescriptorSets(device_, &setInfo, terrainUpscaleDescriptorSets_.data()) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to allocate terrain upscale descriptor sets.");
            }
        }

        for (size_t i = 0; i < terrainUpscaleDescriptorSets_.size(); ++i)
        {
            VkDescriptorImageInfo imageDescriptor{};
            imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageDescriptor.imageView = terrainColorImageViews_[i];
            imageDescriptor.sampler = terrainUpscaleSampler_;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = terrainUpscaleDescriptorSets_[i];
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageDescriptor;
            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
    }

    void Renderer::createTextures()
    {
        const std::string blockTextureDir = std::string(DOLBUTO_ASSET_DIR) + "/textures/block/";
        sun_ = createTexture(std::string(DOLBUTO_ASSET_DIR) + "/textures/sky/Sun.png");
        moon_ = createTexture(std::string(DOLBUTO_ASSET_DIR) + "/textures/sky/Moon.png");
        crosshair_ = createTexture(std::string(DOLBUTO_ASSET_DIR) + "/textures/ui/Crosshair.png");
        playerTexture_ = createTexture(std::string(DOLBUTO_ASSET_DIR) + "/textures/character/Character.png");
        rock_ = createTexture(blockTextureDir + "rock.png");
        grassSide_ = createTexture(blockTextureDir + "grass_side.png");
        grassTop_ = createTexture(blockTextureDir + "grass_top.png");
        blockTextureArray_ = createTextureArray({
            blockTextureDir + "rock.png",
            blockTextureDir + "grass_top.png",
            blockTextureDir + "grass_bottom.png",
            blockTextureDir + "grass_side.png",
            blockTextureDir + "dirt.png",
            blockTextureDir + "sand.png",
            blockTextureDir + "sandstone_side.png",
            blockTextureDir + "sandstone_topbottom.png",
            blockTextureDir + "mud.png",
            blockTextureDir + "clay.png",
            blockTextureDir + "trunk_side.png",
            blockTextureDir + "trunk_topbottom.png"
        });
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
        const size_t indicesOffset = verticesOffset + static_cast<size_t>(vertexCount) * sizeof(TerrainVertex);
        const size_t expectedSize = indicesOffset + static_cast<size_t>(indexCount) * sizeof(uint32_t);
        if (meshData.size() < expectedSize)
        {
            throw std::runtime_error("Incomplete player mesh file.");
        }

        static_assert(sizeof(TerrainVertex) == sizeof(float) * 5);
        std::vector<TerrainVertex> sourceVertices(vertexCount);
        std::memcpy(sourceVertices.data(), meshData.data() + verticesOffset, sourceVertices.size() * sizeof(TerrainVertex));

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
        loadRadius_ = DefaultLoadRadius;

        const std::filesystem::path path = std::filesystem::path(DOLBUTO_CONFIG_DIR) / "world.json";
        std::ifstream file(path);
        if (!file.is_open())
        {
            return;
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        loadRadius_ = parseLoadRadius(contents.str(), DefaultLoadRadius);
    }

    void Renderer::updateLoadedChunks(Vec3 playerPosition)
    {
        const int centerChunkX = chunkCoordinate(playerPosition.x);
        const int centerChunkZ = chunkCoordinate(playerPosition.z);
        if (centerChunkX == loadedCenterChunkX_ && centerChunkZ == loadedCenterChunkZ_)
        {
            return;
        }

        loadedCenterChunkX_ = centerChunkX;
        loadedCenterChunkZ_ = centerChunkZ;
        raymarchWorldMinX_ = (loadedCenterChunkX_ - loadRadius_) * ChunkSizeX;
        raymarchWorldMinZ_ = (loadedCenterChunkZ_ - loadRadius_) * ChunkSizeZ;
        createTerrainMesh();
    }

    void Renderer::createTerrainMesh()
    {
        loadedChunkDiameter_ = loadRadius_ * 2 + 1;
        raymarchWorldMinX_ = (loadedCenterChunkX_ - loadRadius_) * ChunkSizeX;
        raymarchWorldMinZ_ = (loadedCenterChunkZ_ - loadRadius_) * ChunkSizeZ;
        raymarchWorldWidth_ = loadedChunkDiameter_ * ChunkSizeX;
        raymarchWorldDepth_ = loadedChunkDiameter_ * ChunkSizeZ;

        const uint32_t airCell = packCell(BlockAir);
        const uint32_t rockCell = packCell(BlockRock);
        raymarchSubchunks_.clear();
        raymarchCells_.clear();
        raymarchSubchunks_.reserve(static_cast<size_t>(loadedChunkDiameter_) * loadedChunkDiameter_ * SubchunksPerChunk);
        raymarchCells_.reserve(static_cast<size_t>(loadedChunkDiameter_) * loadedChunkDiameter_ * SubchunkSize * ChunkSizeX * ChunkSizeZ);

        for (int chunkZ = 0; chunkZ < loadedChunkDiameter_; ++chunkZ)
        {
            for (int chunkX = 0; chunkX < loadedChunkDiameter_; ++chunkX)
            {
                std::array<int, ChunkSizeX * ChunkSizeZ> columnHeights{};
                int chunkMinHeight = TerrainMaxHeight;
                int chunkMaxHeight = TerrainMinHeight;
                for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                {
                    for (int localX = 0; localX < ChunkSizeX; ++localX)
                    {
                        const int worldX = raymarchWorldMinX_ + chunkX * ChunkSizeX + localX;
                        const int worldZ = raymarchWorldMinZ_ + chunkZ * ChunkSizeZ + localZ;
                        const int height = terrainHeight(worldX, worldZ);
                        columnHeights[localZ * ChunkSizeX + localX] = height;
                        chunkMinHeight = std::min(chunkMinHeight, height);
                        chunkMaxHeight = std::max(chunkMaxHeight, height);
                    }
                }

                for (int subchunkY = 0; subchunkY < SubchunksPerChunk; ++subchunkY)
                {
                    const int yStart = subchunkY * SubchunkSize;
                    const int yEnd = yStart + SubchunkSize;
                    RaymarchSubchunk subchunk{};
                    if (yEnd <= chunkMinHeight)
                    {
                        subchunk.flags = SubchunkUniform;
                        subchunk.uniformCell = rockCell;
                    }
                    else if (yStart >= chunkMaxHeight)
                    {
                        subchunk.flags = SubchunkEmpty;
                        subchunk.uniformCell = airCell;
                    }
                    else
                    {
                        subchunk.flags = SubchunkDense;
                        subchunk.cellOffset = static_cast<uint32_t>(raymarchCells_.size());
                        for (int localY = 0; localY < SubchunkSize; ++localY)
                        {
                            const int worldY = yStart + localY;
                            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                            {
                                for (int localX = 0; localX < ChunkSizeX; ++localX)
                                {
                                    const int height = columnHeights[localZ * ChunkSizeX + localX];
                                    raymarchCells_.push_back(worldY < height ? rockCell : airCell);
                                }
                            }
                        }
                    }

                    raymarchSubchunks_.push_back(subchunk);
                }
            }
        }

        terrainDrawCount_ = 0;
        terrainFaceCount_ = 0;
        terrainVertexCount_ = 0;
        terrainDrawText_ = "DRAWS: 2";
        terrainFaceText_ = "FACES: 0";
        terrainVertexText_ = "VERTICES: 9";
        debugTextBatchDirty_ = true;
        createRaymarchBlockBuffer();
        return;

#if 0
        const int blockCountX = TestChunkCountX * ChunkSizeX;
        const int blockCountZ = TestChunkCountZ * ChunkSizeZ;
        const size_t blockCount = static_cast<size_t>(blockCountX) * ChunkSizeY * blockCountZ;

        std::random_device randomDevice;
        std::mt19937 random(randomDevice());
        std::uniform_int_distribution<int> airDistribution(0, 1);
        std::uniform_int_distribution<int> blockIdDistribution(RandomBlockMin, RandomBlockMax);

        raymarchBlocks_.assign(blockCount, BlockAir);
        if (TestRandomVoxelStress)
        {
            for (int chunkZ = 0; chunkZ < TestChunkCountZ; ++chunkZ)
            {
                for (int chunkX = 0; chunkX < TestChunkCountX; ++chunkX)
                {
                    for (int z = 0; z < ChunkSizeZ; ++z)
                    {
                        for (int y = 0; y < ChunkSizeY; ++y)
                        {
                            for (int x = 0; x < ChunkSizeX; ++x)
                            {
                                const int worldX = chunkX * ChunkSizeX + x;
                                const int worldZ = chunkZ * ChunkSizeZ + z;
                                const uint32_t blockId = airDistribution(random) == 0
                                    ? BlockAir
                                    : static_cast<uint32_t>(blockIdDistribution(random));
                                raymarchBlocks_[(static_cast<size_t>(y) * blockCountZ + worldZ) * blockCountX + worldX] = blockId;
                            }
                        }
                    }
                }
            }

            terrainDrawCount_ = 0;
            terrainFaceCount_ = 0;
            terrainVertexCount_ = 0;
            terrainDrawText_ = "DRAWS: 0";
            terrainFaceText_ = "FACES: 0";
            terrainVertexText_ = "VERTICES: 0";
            debugTextBatchDirty_ = true;
            createRaymarchBlockBuffer();
            return;
        }

        std::uniform_int_distribution<int> heightDistribution(TestChunkMinHeight, TestChunkMaxHeight);
        std::vector<TestChunk> chunks(TestChunkCountX * TestChunkCountZ);
        for (int chunkZ = 0; chunkZ < TestChunkCountZ; ++chunkZ)
        {
            for (int chunkX = 0; chunkX < TestChunkCountX; ++chunkX)
            {
                TestChunk& chunk = chunks[chunkZ * TestChunkCountX + chunkX];
                for (int z = 0; z < ChunkSizeZ; ++z)
                {
                    for (int x = 0; x < ChunkSizeX; ++x)
                    {
                        const int height = heightDistribution(random);
                        const int worldX = chunkX * ChunkSizeX + x;
                        const int worldZ = chunkZ * ChunkSizeZ + z;
                        for (int y = 0; y < height; ++y)
                        {
                            chunk.at(x, y, z) = RandomBlockMin;
                            raymarchBlocks_[(static_cast<size_t>(y) * blockCountZ + worldZ) * blockCountX + worldX] = RandomBlockMin;
                        }
                    }
                }
            }
        }

        auto blockAt = [&](int x, int y, int z) -> uint16_t
        {
            if (x < 0 || x >= blockCountX || y < 0 || y >= ChunkSizeY || z < 0 || z >= blockCountZ)
            {
                return BlockAir;
            }

            const int chunkX = x / ChunkSizeX;
            const int chunkZ = z / ChunkSizeZ;
            const int localX = x % ChunkSizeX;
            const int localZ = z % ChunkSizeZ;
            return chunks[chunkZ * TestChunkCountX + chunkX].at(localX, y, localZ);
        };

        auto appendFace = [](TerrainBuildData& buildData, int x, int y, int z, int face, int width, int height)
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

        auto emitGreedy = [](std::vector<uint8_t>& mask, int maskWidth, int maskHeight, auto emit)
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
                    while (x + width < maskWidth && mask[y * maskWidth + x + width] != 0)
                    {
                        ++width;
                    }

                    int height = 1;
                    bool canGrow = true;
                    while (y + height < maskHeight && canGrow)
                    {
                        for (int offset = 0; offset < width; ++offset)
                        {
                            if (mask[(y + height) * maskWidth + x + offset] == 0)
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

        TerrainBuildData rockBuildData;
        TerrainBuildData grassSideBuildData;
        TerrainBuildData grassTopBuildData;
        rockBuildData.vertices.reserve(static_cast<size_t>(blockCountX) * blockCountZ * 4);
        rockBuildData.indices.reserve(static_cast<size_t>(blockCountX) * blockCountZ * 6);
        grassSideBuildData.vertices.reserve(static_cast<size_t>(blockCountX + blockCountZ) * 16);
        grassSideBuildData.indices.reserve(static_cast<size_t>(blockCountX + blockCountZ) * 24);
        grassTopBuildData.vertices.reserve(static_cast<size_t>(blockCountX) * blockCountZ * 4);
        grassTopBuildData.indices.reserve(static_cast<size_t>(blockCountX) * blockCountZ * 6);

        std::vector<uint8_t> mask(SubchunkSize * SubchunkSize);
        for (int chunkZ = 0; chunkZ < TestChunkCountZ; ++chunkZ)
        {
            for (int chunkX = 0; chunkX < TestChunkCountX; ++chunkX)
            {
                for (int subchunkY = 0; subchunkY < SubchunksPerChunk; ++subchunkY)
                {
                    const int worldXStart = chunkX * ChunkSizeX;
                    const int worldYStart = subchunkY * SubchunkSize;
                    const int worldZStart = chunkZ * ChunkSizeZ;

                    for (int localY = 0; localY < SubchunkSize; ++localY)
                    {
                        const int y = worldYStart + localY;
                        std::fill(mask.begin(), mask.end(), 0);
                        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                        {
                            for (int localX = 0; localX < ChunkSizeX; ++localX)
                            {
                                const int x = worldXStart + localX;
                                const int z = worldZStart + localZ;
                                mask[localZ * ChunkSizeX + localX] = blockAt(x, y, z) != BlockAir && blockAt(x, y + 1, z) == BlockAir ? 1 : 0;
                            }
                        }
                        emitGreedy(mask, ChunkSizeX, ChunkSizeZ, [&](int localX, int localZ, int width, int height)
                        {
                            appendFace(rockBuildData, worldXStart + localX, y, worldZStart + localZ, 0, width, height);
                        });

                        std::fill(mask.begin(), mask.end(), 0);
                        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                        {
                            for (int localX = 0; localX < ChunkSizeX; ++localX)
                            {
                                const int x = worldXStart + localX;
                                const int z = worldZStart + localZ;
                                mask[localZ * ChunkSizeX + localX] = blockAt(x, y, z) != BlockAir && blockAt(x, y - 1, z) == BlockAir ? 1 : 0;
                            }
                        }
                        emitGreedy(mask, ChunkSizeX, ChunkSizeZ, [&](int localX, int localZ, int width, int height)
                        {
                            appendFace(rockBuildData, worldXStart + localX, y, worldZStart + localZ, 1, width, height);
                        });
                    }

                    for (int localX = 0; localX < ChunkSizeX; ++localX)
                    {
                        const int x = worldXStart + localX;
                        std::fill(mask.begin(), mask.end(), 0);
                        for (int localY = 0; localY < SubchunkSize; ++localY)
                        {
                            const int y = worldYStart + localY;
                            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                            {
                                const int z = worldZStart + localZ;
                                mask[localY * ChunkSizeZ + localZ] = blockAt(x, y, z) != BlockAir && blockAt(x + 1, y, z) == BlockAir ? 1 : 0;
                            }
                        }
                        emitGreedy(mask, ChunkSizeZ, SubchunkSize, [&](int localZ, int localY, int width, int height)
                        {
                            appendFace(rockBuildData, x, worldYStart + localY, worldZStart + localZ, 2, width, height);
                        });

                        std::fill(mask.begin(), mask.end(), 0);
                        for (int localY = 0; localY < SubchunkSize; ++localY)
                        {
                            const int y = worldYStart + localY;
                            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                            {
                                const int z = worldZStart + localZ;
                                mask[localY * ChunkSizeZ + localZ] = blockAt(x, y, z) != BlockAir && blockAt(x - 1, y, z) == BlockAir ? 1 : 0;
                            }
                        }
                        emitGreedy(mask, ChunkSizeZ, SubchunkSize, [&](int localZ, int localY, int width, int height)
                        {
                            appendFace(rockBuildData, x, worldYStart + localY, worldZStart + localZ, 3, width, height);
                        });
                    }

                    for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                    {
                        const int z = worldZStart + localZ;
                        std::fill(mask.begin(), mask.end(), 0);
                        for (int localY = 0; localY < SubchunkSize; ++localY)
                        {
                            const int y = worldYStart + localY;
                            for (int localX = 0; localX < ChunkSizeX; ++localX)
                            {
                                const int x = worldXStart + localX;
                                mask[localY * ChunkSizeX + localX] = blockAt(x, y, z) != BlockAir && blockAt(x, y, z + 1) == BlockAir ? 1 : 0;
                            }
                        }
                        emitGreedy(mask, ChunkSizeX, SubchunkSize, [&](int localX, int localY, int width, int height)
                        {
                            appendFace(rockBuildData, worldXStart + localX, worldYStart + localY, z, 4, width, height);
                        });

                        std::fill(mask.begin(), mask.end(), 0);
                        for (int localY = 0; localY < SubchunkSize; ++localY)
                        {
                            const int y = worldYStart + localY;
                            for (int localX = 0; localX < ChunkSizeX; ++localX)
                            {
                                const int x = worldXStart + localX;
                                mask[localY * ChunkSizeX + localX] = blockAt(x, y, z) != BlockAir && blockAt(x, y, z - 1) == BlockAir ? 1 : 0;
                            }
                        }
                        emitGreedy(mask, ChunkSizeX, SubchunkSize, [&](int localX, int localY, int width, int height)
                        {
                            appendFace(rockBuildData, worldXStart + localX, worldYStart + localY, z, 5, width, height);
                        });
                    }
                }
            }
        }

        createTerrainBuffer(rockBuildData, rockTerrain_);
        createTerrainBuffer(grassSideBuildData, grassSideTerrain_);
        createTerrainBuffer(grassTopBuildData, grassTopTerrain_);

        terrainDrawCount_ = 0;
        terrainVertexCount_ = rockTerrain_.vertexCount + grassSideTerrain_.vertexCount + grassTopTerrain_.vertexCount;
        terrainFaceCount_ = (rockTerrain_.indexCount + grassSideTerrain_.indexCount + grassTopTerrain_.indexCount) / 6;
        if (rockTerrain_.indexCount > 0)
        {
            ++terrainDrawCount_;
        }
        if (grassSideTerrain_.indexCount > 0)
        {
            ++terrainDrawCount_;
        }
        if (grassTopTerrain_.indexCount > 0)
        {
            ++terrainDrawCount_;
        }

        terrainDrawText_ = "DRAWS: " + std::to_string(terrainDrawCount_);
        terrainFaceText_ = "FACES: " + std::to_string(terrainFaceCount_);
        terrainVertexText_ = "VERTICES: " + std::to_string(terrainVertexCount_);
        debugTextBatchDirty_ = true;

        createRaymarchBlockBuffer();
#endif
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

    void Renderer::createRaymarchBlockBuffer()
    {
        if (raymarchSubchunks_.empty())
        {
            return;
        }

        if (raymarchSubchunkBuffer_ != VK_NULL_HANDLE || raymarchBlockBuffer_ != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(device_);
        }
        if (raymarchSubchunkBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, raymarchSubchunkBuffer_, nullptr);
            raymarchSubchunkBuffer_ = VK_NULL_HANDLE;
        }
        if (raymarchSubchunkMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, raymarchSubchunkMemory_, nullptr);
            raymarchSubchunkMemory_ = VK_NULL_HANDLE;
        }
        if (raymarchBlockBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_, raymarchBlockBuffer_, nullptr);
            raymarchBlockBuffer_ = VK_NULL_HANDLE;
        }
        if (raymarchBlockMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, raymarchBlockMemory_, nullptr);
            raymarchBlockMemory_ = VK_NULL_HANDLE;
        }

        const VkDeviceSize subchunkBufferSize = sizeof(RaymarchSubchunk) * raymarchSubchunks_.size();
        const VkDeviceSize cellBufferSize = sizeof(uint32_t) * std::max<size_t>(raymarchCells_.size(), 1);
        raymarchBlockBufferSize_ = subchunkBufferSize + cellBufferSize;
        {
            std::ostringstream text;
            text << "BUFFER: " << std::fixed << std::setprecision(1) << (static_cast<double>(raymarchBlockBufferSize_) / (1024.0 * 1024.0)) << "MB";
            bufferText_ = text.str();
            debugTextBatchDirty_ = true;
        }
        createDeviceLocalBuffer(
            raymarchSubchunks_.data(),
            subchunkBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            raymarchSubchunkBuffer_,
            raymarchSubchunkMemory_);

        const uint32_t emptyCell = 0;
        const void* cellSource = raymarchCells_.empty() ? static_cast<const void*>(&emptyCell) : static_cast<const void*>(raymarchCells_.data());
        createDeviceLocalBuffer(
            cellSource,
            cellBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            raymarchBlockBuffer_,
            raymarchBlockMemory_,
            &raymarchBlockMemoryTypeIndex_);
        {
            VkPhysicalDeviceMemoryProperties memoryProperties{};
            vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
            const VkMemoryPropertyFlags flags = raymarchBlockMemoryTypeIndex_ < memoryProperties.memoryTypeCount
                ? memoryProperties.memoryTypes[raymarchBlockMemoryTypeIndex_].propertyFlags
                : 0;

            blockHeapText_ = "BLOCK HEAP:";
            blockHeapText_ += (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0 ? " LOCAL" : "";
            blockHeapText_ += (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 ? " HOST" : "";
            blockHeapText_ += (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0 ? " COHERENT" : "";
            if (blockHeapText_ == "BLOCK HEAP:")
            {
                blockHeapText_ += " UNKNOWN";
            }
            debugTextBatchDirty_ = true;
        }

        if (raymarchDescriptorSet_ == VK_NULL_HANDLE)
        {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool_;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &raymarchDescriptorSetLayout_;

            if (vkAllocateDescriptorSets(device_, &allocInfo, &raymarchDescriptorSet_) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to allocate raymarch descriptor set.");
            }
        }

        VkDescriptorBufferInfo subchunkBufferInfo{};
        subchunkBufferInfo.buffer = raymarchSubchunkBuffer_;
        subchunkBufferInfo.offset = 0;
        subchunkBufferInfo.range = subchunkBufferSize;

        VkDescriptorBufferInfo cellBufferInfo{};
        cellBufferInfo.buffer = raymarchBlockBuffer_;
        cellBufferInfo.offset = 0;
        cellBufferInfo.range = cellBufferSize;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = blockTextureArray_.view;
        imageInfo.sampler = sampler_;

        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = raymarchDescriptorSet_;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].pBufferInfo = &subchunkBufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = raymarchDescriptorSet_;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &cellBufferInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = raymarchDescriptorSet_;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
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
        cleanupTerrainRenderResources();

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
        createTerrainRenderResources();
        createFramebuffers();
        createTerrainUpscaleDescriptors();
    }

    void Renderer::cleanupTerrainRenderResources()
    {
        for (VkFramebuffer framebuffer : terrainFramebuffers_)
        {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
        terrainFramebuffers_.clear();

        for (VkImageView view : terrainDepthImageViews_)
        {
            vkDestroyImageView(device_, view, nullptr);
        }
        terrainDepthImageViews_.clear();

        for (VkImage image : terrainDepthImages_)
        {
            vkDestroyImage(device_, image, nullptr);
        }
        terrainDepthImages_.clear();

        for (VkDeviceMemory memory : terrainDepthMemories_)
        {
            vkFreeMemory(device_, memory, nullptr);
        }
        terrainDepthMemories_.clear();

        for (VkImageView view : terrainColorImageViews_)
        {
            vkDestroyImageView(device_, view, nullptr);
        }
        terrainColorImageViews_.clear();

        for (VkImage image : terrainColorImages_)
        {
            vkDestroyImage(device_, image, nullptr);
        }
        terrainColorImages_.clear();

        for (VkDeviceMemory memory : terrainColorMemories_)
        {
            vkFreeMemory(device_, memory, nullptr);
        }
        terrainColorMemories_.clear();
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
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
        transitionImageLayout(texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, texture.image, static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height));
        transitionImageLayout(texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingMemory, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
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
            throw std::runtime_error("Texture array must contain at least one layer.");
        }

        Texture texture;
        std::vector<unsigned char> pixels;
        int expectedWidth = 0;
        int expectedHeight = 0;
        for (const std::string& path : paths)
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_uc* layerPixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (layerPixels == nullptr)
            {
                throw std::runtime_error("Failed to load texture array layer: " + path);
            }

            if (expectedWidth == 0)
            {
                expectedWidth = width;
                expectedHeight = height;
            }
            else if (width != expectedWidth || height != expectedHeight)
            {
                stbi_image_free(layerPixels);
                throw std::runtime_error("Texture array layers must have matching dimensions.");
            }

            const size_t layerSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
            const size_t oldSize = pixels.size();
            pixels.resize(oldSize + layerSize);
            std::memcpy(pixels.data() + oldSize, layerPixels, layerSize);
            stbi_image_free(layerPixels);
        }

        texture.width = expectedWidth;
        texture.height = expectedHeight;
        texture.mipLevels = mipLevelCount(texture.width, texture.height);
        const uint32_t layerCount = static_cast<uint32_t>(paths.size());
        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(texture.width) * static_cast<VkDeviceSize>(texture.height) * 4u * layerCount;

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory);

        void* data = nullptr;
        vkMapMemory(device_, stagingMemory, 0, imageSize, 0, &data);
        std::memcpy(data, pixels.data(), static_cast<size_t>(imageSize));
        vkUnmapMemory(device_, stagingMemory);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height), 1};
        imageInfo.mipLevels = texture.mipLevels;
        imageInfo.arrayLayers = layerCount;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
        transitionImageLayout(texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layerCount);
        copyBufferToImageArray(stagingBuffer, texture.image, static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height), layerCount);
        generateTextureArrayMipmaps(texture.image, VK_FORMAT_R8G8B8A8_SRGB, static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height), layerCount, texture.mipLevels);

        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingMemory, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = texture.mipLevels;
        viewInfo.subresourceRange.layerCount = layerCount;

        if (vkCreateImageView(device_, &viewInfo, nullptr, &texture.view) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create texture array image view.");
        }

        return texture;
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

    void Renderer::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        endSingleTimeCommands(commandBuffer);
    }

    void Renderer::copyBufferToImageArray(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layers) const
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        std::vector<VkBufferImageCopy> regions;
        regions.reserve(layers);
        const VkDeviceSize layerSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            VkBufferImageCopy region{};
            region.bufferOffset = layerSize * layer;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.baseArrayLayer = layer;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {width, height, 1};
            regions.push_back(region);
        }

        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), regions.data());
        endSingleTimeCommands(commandBuffer);
    }

    void Renderer::generateTextureArrayMipmaps(VkImage image, VkFormat format, uint32_t width, uint32_t height, uint32_t layers, uint32_t mipLevels) const
    {
        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &formatProperties);
        if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) == 0)
        {
            throw std::runtime_error("Texture array format does not support linear blit mipmaps.");
        }

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = layers;
        barrier.subresourceRange.levelCount = 1;

        int32_t mipWidth = static_cast<int32_t>(width);
        int32_t mipHeight = static_cast<int32_t>(height);
        for (uint32_t level = 1; level < mipLevels; ++level)
        {
            barrier.subresourceRange.baseMipLevel = level - 1;
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
            blit.srcSubresource.mipLevel = level - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = layers;

            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = level;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = layers;

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

    void Renderer::transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) const
    {
        transitionImageLayout(image, oldLayout, newLayout, 1);
    }

    void Renderer::transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layers) const
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
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = layers;

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

    void Renderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera, Vec3 cameraPosition, std::string_view fpsText, bool debugTextVisible, VkBuffer screenshotBuffer, bool showPlayer)
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
        VkClearValue transparentColor{};
        transparentColor.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        std::array<VkClearValue, 2> terrainClearValues = {transparentColor, clearDepth};

        VkRenderPassBeginInfo terrainPassInfo{};
        terrainPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        terrainPassInfo.renderPass = terrainRenderPass_;
        terrainPassInfo.framebuffer = terrainFramebuffers_[currentFrame_];
        terrainPassInfo.renderArea.offset = {0, 0};
        terrainPassInfo.renderArea.extent = terrainRenderExtent_;
        terrainPassInfo.clearValueCount = static_cast<uint32_t>(terrainClearValues.size());
        terrainPassInfo.pClearValues = terrainClearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &terrainPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        drawRaymarchTerrain(commandBuffer, camera, cameraPosition, terrainRenderExtent_);
        vkCmdEndRenderPass(commandBuffer);

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

        drawTerrainUpscale(commandBuffer);
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

    void Renderer::drawRaymarchTerrain(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, VkExtent2D renderExtent) const
    {
        if (raymarchDescriptorSet_ == VK_NULL_HANDLE)
        {
            return;
        }

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(renderExtent.width);
        viewport.height = static_cast<float>(renderExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = renderExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        const Vec3 cameraRight = camera.right();
        const Vec3 right{-cameraRight.x, -cameraRight.y, -cameraRight.z};
        const Vec3 cameraForward = camera.forward();
        const Vec3 forward{cameraForward.x, -cameraForward.y, cameraForward.z};
        const Vec3 up = normalize(cross(forward, right));
        const float aspect = static_cast<float>(renderExtent.width) / static_cast<float>(renderExtent.height);
        const float tanHalfFov = std::tan(FieldOfViewRadians * 0.5f);

        RaymarchPush push{};
        push.data[0] = cameraPosition.x;
        push.data[1] = cameraPosition.y;
        push.data[2] = cameraPosition.z;
        push.data[3] = tanHalfFov;
        push.data[4] = right.x;
        push.data[5] = right.y;
        push.data[6] = right.z;
        push.data[7] = aspect;
        push.data[8] = up.x;
        push.data[9] = up.y;
        push.data[10] = up.z;
        push.data[11] = 0.1f;
        push.data[12] = forward.x;
        push.data[13] = forward.y;
        push.data[14] = forward.z;
        push.data[15] = 1000.0f;
        push.data[16] = static_cast<float>(renderExtent.width);
        push.data[17] = static_cast<float>(renderExtent.height);
        push.data[18] = static_cast<float>(raymarchWorldWidth_);
        push.data[19] = static_cast<float>(raymarchWorldDepth_);
        push.data[20] = static_cast<float>(raymarchWorldMinX_);
        push.data[21] = static_cast<float>(raymarchWorldMinZ_);
        push.data[22] = static_cast<float>(ChunkSizeY);
        push.data[23] = 0.0f;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, raymarchPipeline_);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, raymarchPipelineLayout_, 0, 1, &raymarchDescriptorSet_, 0, nullptr);
        vkCmdPushConstants(commandBuffer, raymarchPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(RaymarchPush), &push);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }

    void Renderer::drawTerrain(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, bool wireframe) const
    {
        if (rockTerrain_.indexCount == 0 && grassSideTerrain_.indexCount == 0 && grassTopTerrain_.indexCount == 0)
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
        const Mat4 projection = perspective(FieldOfViewRadians, aspect, 0.1f, 1000.0f);
        const Mat4 view = viewMatrix(camera, cameraPosition);
        const Mat4 mvp = multiply(projection, view);

        TerrainPush push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe ? terrainWireframePipeline_ : terrainPipeline_);
        vkCmdPushConstants(commandBuffer, terrainPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TerrainPush), &push);
        drawTerrainMesh(commandBuffer, rockTerrain_, rock_);
        drawTerrainMesh(commandBuffer, grassSideTerrain_, grassSide_);
        drawTerrainMesh(commandBuffer, grassTopTerrain_, grassTop_);
    }

    void Renderer::drawPlayer(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition) const
    {
        const float aspect = static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);
        const Mat4 projection = perspective(FieldOfViewRadians, aspect, 0.1f, 1000.0f);
        const Mat4 view = viewMatrix(camera, cameraPosition);
        const Mat4 mvp = multiply(projection, view);

        TerrainPush push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, playerPipeline_);
        vkCmdPushConstants(commandBuffer, terrainPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TerrainPush), &push);
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

    void Renderer::drawTerrainUpscale(VkCommandBuffer commandBuffer) const
    {
        if (terrainUpscaleDescriptorSets_.empty())
        {
            return;
        }

        SpriteRect rect{};
        rect.halfWidth = 1.0f;
        rect.halfHeight = 1.0f;
        drawSpriteDescriptor(commandBuffer, terrainUpscaleDescriptorSets_[currentFrame_], rect, {0.0f, 1.0f, 1.0f, -1.0f});
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
        addText(debugTextBatch_, terrainDrawText_, rightX, 156.0f, true);
        addText(debugTextBatch_, terrainFaceText_, rightX, 178.0f, true);
        addText(debugTextBatch_, terrainVertexText_, rightX, 200.0f, true);
        addText(debugTextBatch_, cpuFrameText_, rightX, 222.0f, true);
        addText(debugTextBatch_, gpuFrameText_, rightX, 244.0f, true);
        addText(debugTextBatch_, blockHeapText_, rightX, 266.0f, true);
        addText(debugTextBatch_, bufferText_, rightX, 288.0f, true);
        addText(debugTextBatch_, vramText_, rightX, 310.0f, true);

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
