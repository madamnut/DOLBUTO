#pragma once

#include "camera/Camera.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stb_truetype.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
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
            Vec3 cameraPosition,
            std::string_view fpsText,
            bool debugTextVisible,
            bool screenshotRequested,
            bool showPlayer,
            Vec3 playerPosition,
            float playerYaw);
        void setFramebufferResized();

    private:
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

        struct RaymarchPush
        {
            float data[24]{};
        };

        struct RaymarchSubchunk
        {
            uint32_t flags = 0;
            uint32_t cellOffset = 0;
            uint32_t uniformCell = 0;
            uint32_t reserved = 0;
        };

        struct RaymarchChunkData
        {
            int chunkX = 0;
            int chunkZ = 0;
            std::vector<RaymarchSubchunk> subchunks;
            std::vector<uint32_t> cells;
        };

        struct RaymarchPendingUpload
        {
            uint64_t chunkKey = 0;
            uint32_t physicalSlot = 0;
        };

        struct RaymarchBuildRequest
        {
            int chunkX = 0;
            int chunkZ = 0;
            uint64_t chunkKey = 0;
        };

        struct RaymarchCompletedChunk
        {
            uint64_t chunkKey = 0;
            RaymarchChunkData chunk;
            double buildMs = 0.0;
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

        void createInstance();
        void createSurface();
        void pickPhysicalDevice();
        void collectHardwareInfo();
        void createDevice();
        void createSwapchain();
        void createImageViews();
        void createRenderPass();
        void createDepthResources();
        void createTerrainRenderResources();
        void createDescriptorSetLayout();
        void createRaymarchDescriptorSetLayout();
        void createPipeline();
        void createTerrainPipeline();
        void createRaymarchPipeline();
        void createFramebuffers();
        void createCommandPool();
        void createSampler();
        void createTerrainUpscaleSampler();
        void createDescriptorPool();
        void createTerrainUpscaleDescriptors();
        void createPerformanceQueries();
        void createTextures();
        void createBlockDefinitionBuffer();
        void createFont();
        void createTextVertexBuffer();
        void createPlayerMesh();
        void createTerrainMesh();
        void loadWorldConfig();
        void updateLoadedChunks(Vec3 playerPosition);
        RaymarchChunkData buildRaymarchChunk(int chunkX, int chunkZ) const;
        const RaymarchChunkData& cachedRaymarchChunk(int chunkX, int chunkZ);
        void pruneRaymarchChunkCache();
        void startChunkWorkers();
        void stopChunkWorkers();
        void chunkWorkerLoop();
        void enqueueRaymarchChunkBuild(int chunkX, int chunkZ, uint64_t chunkKey);
        bool applyCompletedRaymarchChunks(double& newChunksMs, double& metadataBuildMs);
        void clearRaymarchSlotMetadata(uint32_t physicalSlot);
        void writeRaymarchChunkMetadata(const RaymarchChunkData& chunk, uint32_t physicalSlot);
        void createTerrainBuffer(const TerrainBuildData& buildData, TerrainMesh& mesh);
        void createRaymarchBlockBuffer();
        void createCommandBuffers();
        void createSyncObjects();

        void cleanupSwapchain();
        void recreateSwapchain();
        void cleanupTerrainRenderResources();

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
        Texture createTextureArray(const std::vector<std::string>& paths);
        void copyBufferToImageArray(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layers) const;
        void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layers) const;
        void generateTextureArrayMipmaps(VkImage image, VkFormat format, uint32_t width, uint32_t height, uint32_t layers, uint32_t mipLevels) const;

        void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera, Vec3 cameraPosition, std::string_view fpsText, bool debugTextVisible, VkBuffer screenshotBuffer, bool showPlayer);
        void copySwapchainImageToBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, VkBuffer buffer) const;
        void saveScreenshot(VkDeviceMemory memory, VkDeviceSize size) const;
        void updatePlayerMesh(Vec3 playerPosition, float playerYaw);
        void drawTerrain(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, bool wireframe) const;
        void drawRaymarchTerrain(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, VkExtent2D renderExtent) const;
        void drawTerrainMesh(VkCommandBuffer commandBuffer, const TerrainMesh& mesh, const Texture& texture) const;
        void drawPlayer(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition) const;
        void drawSprite(VkCommandBuffer commandBuffer, const Texture& texture, SpriteRect rect, UvRect uv = {}, Color color = {}) const;
        void drawSpriteDescriptor(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, SpriteRect rect, UvRect uv = {}, Color color = {}) const;
        void drawTerrainUpscale(VkCommandBuffer commandBuffer) const;
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
        std::string blockHeapText_ = "BLOCK HEAP: N/A";
        std::string bufferText_ = "BUFFER: 0.0MB";
        std::string vramText_ = "VRAM: 0MB";
        std::string chunkUpdateProfileText_ = "UPDATE TOTAL: ---.---MS";
        std::string worldBuildProfileText_ = "WORLD TOTAL: ---.---MS";
        std::string slotPruneProfileText_ = "SLOT PRUNE: ---.---MS";
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
        VkExtent2D terrainRenderExtent_{};
        std::vector<VkImage> terrainColorImages_;
        std::vector<VkDeviceMemory> terrainColorMemories_;
        std::vector<VkImageView> terrainColorImageViews_;
        std::vector<VkImage> terrainDepthImages_;
        std::vector<VkDeviceMemory> terrainDepthMemories_;
        std::vector<VkImageView> terrainDepthImageViews_;
        std::vector<VkFramebuffer> terrainFramebuffers_;
        std::vector<VkDescriptorSet> terrainUpscaleDescriptorSets_;

        VkRenderPass renderPass_ = VK_NULL_HANDLE;
        VkRenderPass terrainRenderPass_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout raymarchDescriptorSetLayout_ = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        VkPipelineLayout terrainPipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline terrainPipeline_ = VK_NULL_HANDLE;
        VkPipeline terrainWireframePipeline_ = VK_NULL_HANDLE;
        VkPipeline playerPipeline_ = VK_NULL_HANDLE;
        VkPipelineLayout raymarchPipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline raymarchPipeline_ = VK_NULL_HANDLE;
        VkCommandPool commandPool_ = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers_;
        VkQueryPool timestampQueryPool_ = VK_NULL_HANDLE;
        std::array<bool, 2> timestampQueryReady_{};
        bool timestampSupported_ = false;
        float timestampPeriod_ = 0.0f;
        double lastGpuFrameMs_ = 0.0;

        VkSampler sampler_ = VK_NULL_HANDLE;
        VkSampler terrainUpscaleSampler_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
        VkBuffer textVertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory textVertexMemory_ = VK_NULL_HANDLE;
        TerrainMesh rockTerrain_;
        TerrainMesh grassSideTerrain_;
        TerrainMesh grassTopTerrain_;
        TerrainMesh playerMesh_;
        std::vector<TerrainVertex> playerLocalVertices_;
        std::vector<uint32_t> playerIndices_;
        std::vector<RaymarchSubchunk> raymarchSubchunks_;
        std::unordered_map<uint64_t, RaymarchChunkData> raymarchChunkCache_;
        std::unordered_map<uint64_t, uint32_t> raymarchChunkSlots_;
        std::vector<uint32_t> freeRaymarchSlots_;
        std::vector<uint32_t> raymarchSlotMap_;
        std::vector<RaymarchPendingUpload> raymarchPendingUploads_;
        std::vector<uint32_t> raymarchDirtySubchunkSlots_;
        int loadGridScale_ = 0;
        int chunkWorkerCount_ = 2;
        int maxCompletedChunksAppliedPerFrame_ = 4;
        int loadedChunkDiameter_ = 0;
        uint32_t raymarchPhysicalSlotCount_ = 0;
        uint32_t raymarchBufferPhysicalSlotCount_ = 0;
        uint32_t raymarchSlotsPerCellPage_ = 0;
        int loadedCenterGroupChunkX_ = 0;
        int loadedCenterGroupChunkZ_ = 0;
        int raymarchWorldMinX_ = 0;
        int raymarchWorldMinZ_ = 0;
        int raymarchWorldWidth_ = 0;
        int raymarchWorldDepth_ = 0;
        VkDeviceSize raymarchBlockBufferSize_ = 0;
        VkDeviceSize localMemoryHeapSize_ = 0;
        uint32_t localMemoryHeapIndex_ = UINT32_MAX;
        uint32_t raymarchBlockMemoryTypeIndex_ = UINT32_MAX;
        bool memoryBudgetSupported_ = false;
        VkBuffer raymarchSubchunkBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory raymarchSubchunkMemory_ = VK_NULL_HANDLE;
        VkBuffer raymarchSlotMapBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory raymarchSlotMapMemory_ = VK_NULL_HANDLE;
        VkBuffer blockDefinitionBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory blockDefinitionMemory_ = VK_NULL_HANDLE;
        std::array<VkBuffer, 4> raymarchCellPageBuffers_{};
        std::array<VkDeviceMemory, 4> raymarchCellPageMemories_{};
        VkDescriptorSet raymarchDescriptorSet_ = VK_NULL_HANDLE;
        uint32_t terrainDrawCount_ = 0;
        uint32_t terrainFaceCount_ = 0;
        uint32_t terrainVertexCount_ = 0;
        bool terrainDebugInitialized_ = false;
        std::vector<std::thread> chunkWorkers_;
        std::deque<RaymarchBuildRequest> raymarchBuildRequests_;
        std::deque<RaymarchCompletedChunk> raymarchCompletedChunks_;
        std::unordered_set<uint64_t> queuedRaymarchChunks_;
        std::mutex chunkWorkerMutex_;
        std::condition_variable chunkWorkerCondition_;
        bool stopChunkWorkers_ = false;
        Texture sun_;
        Texture moon_;
        Texture crosshair_;
        Texture font_;
        Texture playerTexture_;
        Texture rock_;
        Texture grassSide_;
        Texture grassTop_;
        Texture blockTextureArray_;
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
