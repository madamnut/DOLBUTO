#pragma once

#include "camera/Camera.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stb_truetype.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dolbuto
{
    class Renderer
    {
    public:
        explicit Renderer(GLFWwindow* window);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        void drawFrame(
            const Camera& camera,
            DVec3 cameraPosition,
            std::string_view fpsText,
            bool debugTextVisible,
            bool screenshotRequested,
            bool showPlayer,
            DVec3 playerPosition,
            float playerYaw,
            bool terrainWireframe);
        void setFramebufferResized();

    private:
        static constexpr std::size_t ChunkColumnCount = 16u * 16u;
        static constexpr std::size_t ChunkBlockCount = 16u * 512u * 16u;
        static constexpr std::size_t SubchunkCount = 512u / 16u;

        struct QueueFamilyIndices
        {
            uint32_t graphics = UINT32_MAX;
            uint32_t present = UINT32_MAX;

            bool complete() const;
        };

        struct Texture
        {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
            int width = 0;
            int height = 0;
            uint32_t mipLevels = 1;
        };

        struct SpriteRect
        {
            float centerX = 0.0f;
            float centerY = 0.0f;
            float halfWidth = 0.0f;
            float halfHeight = 0.0f;
        };

        struct UvRect
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 1.0f;
            float height = 1.0f;
        };

        struct Color
        {
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;
            float a = 1.0f;
        };

        struct SpritePush
        {
            float data[12]{};
        };

        struct TextPush
        {
            float data[12]{};
        };

        struct Glyph
        {
            SpriteRect rect;
            UvRect uv;
            float advance = 0.0f;
        };

        struct TextVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float u = 0.0f;
            float v = 0.0f;
        };

        struct TextBatch
        {
            std::vector<TextVertex> outline;
            std::vector<TextVertex> fill;
        };

        struct TerrainVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float u = 0.0f;
            float v = 0.0f;
        };

        struct TerrainPush
        {
            float mvp[16]{};
        };

        struct BufferUploadRegion
        {
            const void* source = nullptr;
            VkDeviceSize size = 0;
            VkDeviceSize destinationOffset = 0;
        };

        struct TerrainMesh
        {
            VkBuffer vertexBuffer = VK_NULL_HANDLE;
            VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
            VkBuffer indexBuffer = VK_NULL_HANDLE;
            VkDeviceMemory indexMemory = VK_NULL_HANDLE;
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;
        };

        struct TerrainBuildData
        {
            std::vector<TerrainVertex> vertices;
            std::vector<uint32_t> indices;
        };

        struct ChunkOffset
        {
            int x = 0;
            int z = 0;
        };

        struct ChunkData
        {
            uint64_t generation = 0;
            int chunkX = 0;
            int chunkZ = 0;
            std::vector<uint16_t> blocks;
            std::array<bool, SubchunkCount> emptySubchunks{};
        };

        struct TerrainJob
        {
            enum class Type
            {
                BuildChunkData,
                BuildChunkMesh
            };

            Type type = Type::BuildChunkData;
            uint64_t generation = 0;
            int chunkX = 0;
            int chunkZ = 0;
            std::shared_ptr<ChunkData> chunk;
        };

        struct CompletedChunkMesh
        {
            uint64_t generation = 0;
            int chunkX = 0;
            int chunkZ = 0;
            std::array<TerrainBuildData, SubchunkCount> rockSubchunks;
        };

        struct ChunkRenderData
        {
            int chunkX = 0;
            int chunkZ = 0;
            std::array<TerrainMesh, SubchunkCount> rockSubchunks;
        };

        struct RetiredChunkRenderData
        {
            uint32_t framesLeft = 0;
            ChunkRenderData chunk;
        };

        void createInstance();
        void createSurface();
        void pickPhysicalDevice();
        void collectHardwareInfo();
        void createDevice();
        void createSwapchain();
        void createImageViews();
        void createRenderPass();
        void createDepthResources();
        void createDescriptorSetLayout();
        void createPipeline();
        void createTerrainPipeline();
        void createFramebuffers();
        void createCommandPool();
        void createSampler();
        void createDescriptorPool();
        void createPerformanceQueries();
        void createTextures();
        void createFont();
        void createTextVertexBuffer();
        void createPlayerMesh();
        void loadWorldConfig();
        void loadHeightLut();
        void updateLoadedChunks(DVec3 playerPosition);
        void requestTerrainLoad(int centerGroupChunkX, int centerGroupChunkZ);
        void rebuildLoadOrderIfNeeded();
        void startTerrainWorkers();
        void stopTerrainWorkers();
        void terrainWorkerLoop();
        void enqueueTerrainJob(TerrainJob job);
        void processCompletedTerrainJobs();
        uint32_t processPendingTerrainUnloads();
        void processRetiredTerrainChunks();
        std::shared_ptr<ChunkData> buildChunkData(int chunkX, int chunkZ) const;
        TerrainBuildData buildSubchunkMesh(const std::shared_ptr<ChunkData>& chunk, int subchunkY) const;
        CompletedChunkMesh buildChunkMesh(const std::shared_ptr<ChunkData>& chunk, uint64_t generation) const;
        bool chunkMeshReady(uint64_t key) const;
        void destroyChunkRenderData(ChunkRenderData& chunk);
        void destroyAllTerrainChunks();
        void updateTerrainStats();
        std::array<int, ChunkColumnCount> buildChunkHeightmap(int chunkX, int chunkZ) const;
        void createTerrainBuffer(const TerrainBuildData& buildData, TerrainMesh& mesh);
        void createCommandBuffers();
        void createSyncObjects();

        void cleanupSwapchain();
        void recreateSwapchain();
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
        bool isDeviceSuitable(VkPhysicalDevice device) const;
        VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
        VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const;
        VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;

        VkShaderModule createShaderModule(const std::string& path) const;
        Texture createTexture(const std::string& path);
        Texture createTextureFromRgba(const unsigned char* pixels, int width, int height);
        void destroyTexture(Texture& texture);
        void destroyTerrainMesh(TerrainMesh& mesh);

        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
        void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory, uint32_t* memoryTypeIndex = nullptr) const;
        void createDeviceLocalBuffer(const void* source, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory, uint32_t* memoryTypeIndex = nullptr) const;
        void copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) const;
        void uploadBufferRegions(VkBuffer destination, const std::vector<BufferUploadRegion>& regions) const;
        void uploadBufferData(VkBuffer destination, const void* source, VkDeviceSize size, VkDeviceSize destinationOffset = 0) const;
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
        void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) const;
        VkCommandBuffer beginSingleTimeCommands() const;
        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

        void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera, Vec3 cameraPosition, std::string_view fpsText, bool debugTextVisible, VkBuffer screenshotBuffer, bool showPlayer, bool terrainWireframe);
        void copySwapchainImageToBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, VkBuffer buffer) const;
        void saveScreenshot(VkDeviceMemory memory, VkDeviceSize size) const;
        void updatePlayerMesh(Vec3 playerPosition, float playerYaw);
        void drawTerrain(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, bool wireframe);
        void drawTerrainMesh(VkCommandBuffer commandBuffer, const TerrainMesh& mesh, const Texture& texture) const;
        void drawPlayer(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition) const;
        void drawSprite(VkCommandBuffer commandBuffer, const Texture& texture, SpriteRect rect, UvRect uv = {}, Color color = {}) const;
        void drawSpriteDescriptor(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, SpriteRect rect, UvRect uv = {}, Color color = {}) const;
        std::string_view resolutionText();
        void updateDebugTextBatch(std::string_view fpsText);
        void updatePerformanceText(double cpuFrameMs);
        void addText(TextBatch& batch, std::string_view text, float x, float y, bool alignRight) const;
        void addTextPass(std::vector<TextVertex>& vertices, std::string_view text, float x, float y, bool alignRight, float offsetX, float offsetY) const;
        void appendGlyphQuad(std::vector<TextVertex>& vertices, const Glyph& glyph) const;
        void drawTextBatch(VkCommandBuffer commandBuffer, const TextBatch& batch);
        void drawTextVertices(VkCommandBuffer commandBuffer, const std::vector<TextVertex>& vertices, Color color, VkDeviceSize bufferOffset) const;
        Glyph makeGlyph(char character, float x, float y) const;
        float measureText(std::string_view text) const;
        bool projectSkyDirection(const Camera& camera, float aspect, const std::array<float, 3>& direction, SpriteRect& rect) const;
        std::string readCpuName() const;
        std::string formatVersion(uint32_t version) const;
        void updateTerrainDebugText();
        void updateVramText();

        GLFWwindow* window_ = nullptr;

        VkInstance instance_ = VK_NULL_HANDLE;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        std::string cpuText_;
        std::string gpuText_;
        std::string vulkanText_;
        std::string driverText_;
        std::string resolutionText_;
        std::string terrainDrawText_;
        std::string terrainFaceText_;
        std::string terrainVertexText_;
        std::string cpuFrameText_ = "CPU: ---.---MS";
        std::string gpuFrameText_ = "GPU: ---.---MS";
        std::string vramText_ = "VRAM: 0MB";
        std::string dataQueueText_ = "DATA QUEUE: 0000";
        std::string meshQueueText_ = "MESH QUEUE: 0000";
        std::string dataDoneText_ = "DATA DONE: 0000";
        std::string meshDoneText_ = "MESH DONE: 0000";
        std::string uploadText_ = "UPLOAD: 0000 / 8";
        std::string unloadText_ = "UNLOAD: 0000 / 16";
        std::string retiredText_ = "RETIRED: 0000";
        std::string jobMainText_ = "JOB MAIN: 000.000MS";
        std::chrono::steady_clock::time_point terrainDebugSampleTime_{};
        std::string chunkUpdateProfileText_ = "UPDATE TOTAL: ---.---MS";
        std::string worldBuildProfileText_ = "WORLD TOTAL: ---.---MS";
        std::string gridScanProfileText_ = "GRID SCAN: ---.---MS";
        std::string newChunksProfileText_ = "NEW CHUNKS: ---.---MS";
        std::string metadataBuildProfileText_ = "META BUILD: ---.---MS";
        std::string cachedFpsText_;
        VkExtent2D lastResolutionExtent_{};
        TextBatch debugTextBatch_;
        bool debugTextBatchDirty_ = true;
        bool debugTextBufferDirty_ = true;
        VkDevice device_ = VK_NULL_HANDLE;
        VkQueue graphicsQueue_ = VK_NULL_HANDLE;
        VkQueue presentQueue_ = VK_NULL_HANDLE;

        VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
        std::vector<VkImage> swapchainImages_;
        std::vector<VkImageView> swapchainImageViews_;
        std::vector<VkFramebuffer> framebuffers_;
        VkFormat swapchainImageFormat_ = VK_FORMAT_UNDEFINED;
        VkExtent2D swapchainExtent_{};
        VkImage depthImage_ = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
        VkImageView depthImageView_ = VK_NULL_HANDLE;
        VkRenderPass renderPass_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        VkPipelineLayout terrainPipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline terrainPipeline_ = VK_NULL_HANDLE;
        VkPipeline terrainWireframePipeline_ = VK_NULL_HANDLE;
        VkPipeline playerPipeline_ = VK_NULL_HANDLE;
        VkCommandPool commandPool_ = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers_;
        VkQueryPool timestampQueryPool_ = VK_NULL_HANDLE;
        std::array<bool, 2> timestampQueryReady_{};
        bool timestampSupported_ = false;
        float timestampPeriod_ = 0.0f;
        double lastGpuFrameMs_ = 0.0;

        VkSampler sampler_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
        VkBuffer textVertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory textVertexMemory_ = VK_NULL_HANDLE;
        TerrainMesh playerMesh_;
        std::vector<TerrainVertex> playerLocalVertices_;
        std::vector<uint32_t> playerIndices_;
        int loadGridScale_ = 0;
        int loadedChunkDiameter_ = 0;
        int loadedCenterGroupChunkX_ = 0;
        int loadedCenterGroupChunkZ_ = 0;
        std::atomic<uint64_t> terrainGeneration_{0};
        int loadOrderDiameter_ = 0;
        std::vector<ChunkOffset> loadOrder_;
        std::unordered_set<uint64_t> desiredTerrainChunks_;
        std::unordered_set<uint64_t> requestedChunkJobs_;
        std::unordered_set<uint64_t> requestedMeshJobs_;
        std::unordered_set<uint64_t> pendingUnloadSet_;
        std::unordered_map<uint64_t, std::shared_ptr<ChunkData>> chunkData_;
        std::unordered_map<uint64_t, ChunkRenderData> terrainChunks_;
        std::deque<uint64_t> pendingUnloadChunks_;
        std::deque<RetiredChunkRenderData> retiredTerrainChunks_;
        std::vector<std::thread> terrainWorkers_;
        std::mutex terrainJobMutex_;
        std::condition_variable terrainJobCondition_;
        std::deque<TerrainJob> terrainDataJobs_;
        std::deque<TerrainJob> terrainMeshJobs_;
        std::deque<std::shared_ptr<ChunkData>> completedChunkData_;
        std::deque<CompletedChunkMesh> completedChunkMeshes_;
        bool stopTerrainWorkers_ = false;
        std::array<uint16_t, 1024> heightLut_{};
        VkDeviceSize localMemoryHeapSize_ = 0;
        uint32_t localMemoryHeapIndex_ = UINT32_MAX;
        bool memoryBudgetSupported_ = false;
        uint32_t terrainDrawCount_ = 0;
        uint32_t terrainFaceCount_ = 0;
        uint32_t terrainVertexCount_ = 0;
        bool terrainDebugInitialized_ = false;
        Texture sun_;
        Texture moon_;
        Texture crosshair_;
        Texture font_;
        Texture playerTexture_;
        Texture rock_;
        Texture grassSide_;
        Texture grassTop_;
        std::array<stbtt_bakedchar, 95> bakedChars_{};

        std::vector<VkSemaphore> imageAvailableSemaphores_;
        std::vector<VkSemaphore> renderFinishedSemaphores_;
        std::vector<VkFence> inFlightFences_;
        uint32_t currentFrame_ = 0;
        bool framebufferResized_ = false;
        std::chrono::steady_clock::time_point performanceSampleStart_{};
        double accumulatedCpuFrameMs_ = 0.0;
        double accumulatedGpuFrameMs_ = 0.0;
        uint32_t performanceSampleCount_ = 0;
    };
}
