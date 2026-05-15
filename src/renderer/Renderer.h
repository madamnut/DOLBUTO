#pragma once

#include "audio/AudioSystem.h"
#include "camera/Camera.h"
#include "game/ClientContent.h"
#include "game/ClientSceneLifecycle.h"
#include "game/ClientTerrainSceneRuntime.h"
#include "game/ClientWorldRuntime.h"
#include "game/ClientUiTypes.h"
#include "gameplay/BlockInteractionSystem.h"
#include "gameplay/ClientGameplayRuntime.h"
#include "gameplay/PlayerInventory.h"
#include "items/ItemData.h"
#include "renderer/DroppedItemRenderPath.h"
#include "renderer/ClimateOverlayTextureBuilder.h"
#include "renderer/DebugOverlayText.h"
#include "renderer/ParticleRenderPath.h"
#include "renderer/RendererAssetStore.h"
#include "renderer/RendererFrame.h"
#include "renderer/RendererGpuResources.h"
#include "renderer/PlayerMeshRenderPath.h"
#include "renderer/ScreenPresentation.h"
#include "renderer/SpriteRenderPath.h"
#include "renderer/TerrainRenderPath.h"
#include "renderer/TerrainTypes.h"
#include "renderer/TextRenderPath.h"
#include "ui/ClientUiBridge.h"
#include "ui/UiSystem.h"
#include "world/BlockData.h"
#include "world/ClimateSystem.h"
#include "world/TerrainBuilder.h"
#include "world/TerrainJobSystem.h"
#include "world/WorldRuntime.h"
#include "world/WorldTypes.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/RenderInterface.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dolbuto
{
    namespace game
    {
        class ClientRuntimeFacade;
    }

    class Renderer : public Rml::RenderInterface
    {
    public:
        explicit Renderer(GLFWwindow* window);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        void drawFrame(const RendererFrame& frame);
        void setFramebufferResized();

    private:
        friend class game::ClientRuntimeFacade;

        bool playerColliderIntersectsTerrain(DVec3 playerPosition) const;
        void updateBlockSelection(DVec3 origin, Vec3 direction);
        void updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, DVec3 playerPosition, float deltaSeconds);
        bool editBlockInView(DVec3 origin, Vec3 direction, bool placeRock, DVec3 playerPosition);
        bool pickupDroppedItemInView(DVec3 origin, Vec3 direction);
        bool dropSelectedHotbarItem(bool wholeStack, DVec3 playerPosition, Vec3 direction);
        std::string selectedBlockText() const;
        std::string climateText(DVec3 position) const;
        void loadGameScene(const std::filesystem::path& worldDirectory, uint64_t worldSeed);
        void unloadGameScene();
        void setWorldList(const std::vector<game::WorldListItem>& worlds);
        void setHotbarSelectedSlot(int slot);
        std::array<ItemStack, gameplay::PlayerInventory::SlotCount> inventorySnapshot() const;
        void setInventorySnapshot(const std::array<ItemStack, gameplay::PlayerInventory::SlotCount>& slots);
        std::string uiInputValue(std::string_view id) const;
        void uiMouseMove(double x, double y);
        void uiMouseButton(int button, bool pressed, int modifiers);
        void uiMouseWheel(double yOffset);
        void uiTextInput(unsigned int codepoint);
        void uiKey(int key, bool pressed, int modifiers);
        void closeInventoryInteraction();
        bool rmlUiAvailable() const;
        std::optional<std::string> consumeUiAction();

        struct QueueFamilyIndices
        {
            uint32_t graphics = UINT32_MAX;
            uint32_t present = UINT32_MAX;

            bool complete() const;
        };

        struct UiVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;
            float a = 1.0f;
            float u = 0.0f;
            float v = 0.0f;
        };

        struct UiPush
        {
            float viewportWidth = 1.0f;
            float viewportHeight = 1.0f;
            float translateX = 0.0f;
            float translateY = 0.0f;
        };

        struct UiGeometry
        {
            std::vector<UiVertex> vertices;
            std::vector<uint32_t> indices;
        };

        struct LineVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct TerrainPush
        {
            float mvp[16]{};
            float cameraPosition[4]{};
            float fluidWaterParams[4]{};
        };

        using ChunkOffset = game::ClientWorldRuntime::ChunkOffset;

        using BlockRaycastHit = gameplay::BlockRaycastHit;

        void createInstance();
        void createSurface();
        void pickPhysicalDevice();
        void collectHardwareInfo();
        void createDevice();
        void createSwapchain();
        void createImageViews();
        void createRenderPass();
        void createSceneRenderPass();
        void createDepthResources();
        void createSceneTargets();
        void createDescriptorSetLayout();
        void createTerrainVertexDescriptorSetLayout();
        void createPipeline();
        void createUiPipeline();
        void createTerrainPipeline();
        void createParticlePipeline();
        void createSelectionPipeline();
        void createFramebuffers();
        void createCommandPool();
        void createSampler();
        void createDescriptorPool();
        void createPerformanceQueries();
        void createTextures();
        void createTextRenderPath();
        void createUiBuffers();
        void createRenderPathBuffers();
        void createSelectionLineBuffer();
        void initializeRmlUi();
        void shutdownRmlUi();
        bool renderRmlUi(VkCommandBuffer commandBuffer, int menuOverlayMode, bool hudVisible);
        void createPlayerMesh();
        void loadWorldConfig();
        void loadRenderConfig();
        void loadHeightLut();
        void updateLoadedChunks(DVec3 playerPosition);
        void requestTerrainLoad(int centerGroupChunkX, int centerGroupChunkZ);
        world::TerrainJobResult processRenderTerrainMeshJob(TerrainJob job);
        void processCompletedTerrainJobs();
        uint32_t processPendingTerrainUnloads();
        void processRetiredTerrainChunks();
        void markRuntimeChunkDataDirty(RuntimeChunk& chunk);
        world::TerrainBuilderConfig terrainBuilderConfig() const;
        bool setBlockAtWorld(int x, int y, int z, uint16_t block);
        void updateChunkEmptySubchunk(const std::shared_ptr<ChunkData>& chunk, int subchunkY);
        void rebuildSubchunkMeshNow(int chunkX, int chunkZ, int subchunkY);
        void rebuildEditedChunkMeshes(int blockX, int blockY, int blockZ);
        bool chunkMeshReady(uint64_t key) const;
        void destroyAllTerrainChunks();
        void updateTerrainStats();
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
        void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera, Vec3 cameraPosition, Vec3 playerPosition, std::string_view fpsText, bool debugTextVisible, VkBuffer screenshotBuffer, bool showPlayer, bool terrainWireframe, int climateOverlayMode, int menuOverlayMode, bool hudVisible, bool gameSceneRenderEnabled, uint64_t worldTicks);
        void copySwapchainImageToBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, VkBuffer buffer) const;
        void saveScreenshot(VkDeviceMemory memory, VkDeviceSize size) const;
        void updatePlayerMesh(Vec3 playerPosition, float playerYaw);
        void drawTerrain(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, bool wireframe, bool drawBlocks, bool drawFluids, uint32_t sceneImageIndex = 0);
        const BlockDefinition& blockDefinition(uint16_t block) const;
        bool raycastBlock(DVec3 origin, Vec3 direction, BlockRaycastHit& hit) const;
        uint16_t blockAtWorld(int x, int y, int z) const;
        bool applyBlockEditResult(const gameplay::BlockEditResult& result);
        bool breakBlockAtHit(const BlockRaycastHit& hit);
        void resetBlockBreaking();
        bool terrainCellBlocksPlayer(int x, int y, int z) const;
        uint32_t blockFaceTextureLayer(uint16_t block, int face) const;
        uint32_t blockFaceTextureLayerForHit(uint16_t block, const BlockRaycastHit& hit) const;
        void drawBlockSelection(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition);
        void drawPlayer(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition) const;
        void spawnBlockBreakParticles(int x, int y, int z, uint16_t block);
        void spawnBlockMiningParticle(const BlockRaycastHit& hit, uint16_t block);
        void drawBlockBreakParticles(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition);
        void initializeAudio();
        void shutdownAudio();
        void updateAudioListener(const Camera& camera, Vec3 cameraPosition);
        void updateMusicPlayback(int menuOverlayMode, bool gameSceneRenderEnabled);
        void playBlockBreakSound(int x, int y, int z);
        void playBlockPlaceSound(int x, int y, int z);
        void playItemPickupSound();
        void drawDroppedItems(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, Vec3 playerPosition);
        void updateInventoryUi();
        void ensureClimateOverlayTexture(int mode);
        world::ClimateSystem climateSystem() const;
        void populateChunkClimate(ChunkData& chunk) const;
        void updateDebugTextBatch(std::string_view fpsText);
        void updatePerformanceText(double cpuFrameMs);
        std::string readCpuName() const;
        std::string formatVersion(uint32_t version) const;
        void updateTerrainDebugText();
        void updateVramText();

        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
        void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
        void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
        Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override;
        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) override;
        void ReleaseTexture(Rml::TextureHandle texture) override;
        void EnableScissorRegion(bool enable) override;
        void SetScissorRegion(Rml::Rectanglei region) override;

        GLFWwindow* window_ = nullptr;

        VkInstance instance_ = VK_NULL_HANDLE;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        DebugOverlayText debugOverlayText_;
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
        VkRenderPass sceneRenderPass_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout terrainVertexDescriptorSetLayout_ = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        VkPipelineLayout uiPipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline uiPipeline_ = VK_NULL_HANDLE;
        VkPipelineLayout terrainPipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline terrainPipeline_ = VK_NULL_HANDLE;
        VkPipeline terrainWireframePipeline_ = VK_NULL_HANDLE;
        VkPipeline fluidPipeline_ = VK_NULL_HANDLE;
        VkPipeline playerPipeline_ = VK_NULL_HANDLE;
        VkPipelineLayout particlePipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline particlePipeline_ = VK_NULL_HANDLE;
        VkPipeline itemPipeline_ = VK_NULL_HANDLE;
        VkPipelineLayout selectionPipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline selectionPipeline_ = VK_NULL_HANDLE;
        VkCommandPool commandPool_ = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers_;
        VkQueryPool timestampQueryPool_ = VK_NULL_HANDLE;
        std::array<bool, 2> timestampQueryReady_{};
        bool timestampSupported_ = false;
        float timestampPeriod_ = 0.0f;
        double lastGpuFrameMs_ = 0.0;

        VkSampler sampler_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
        VkBuffer uiVertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory uiVertexMemory_ = VK_NULL_HANDLE;
        VkBuffer uiIndexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory uiIndexMemory_ = VK_NULL_HANDLE;
        VkBuffer selectionLineVertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory selectionLineVertexMemory_ = VK_NULL_HANDLE;
        bool hasSelectedBlock_ = false;
        int selectedBlockX_ = 0;
        int selectedBlockY_ = 0;
        int selectedBlockZ_ = 0;
        uint16_t selectedBlockId_ = 0;
        gameplay::ClientGameplayRuntime gameplayRuntime_;
        int loadGridScale_ = 0;
        int terrainWorkerCount_ = 4;
        int maxTerrainUploadChunksPerFrame_ = 8;
        int maxTerrainUnloadChunksPerFrame_ = 16;
        int maxTerrainRetiredDestroyPerFrame_ = 4;
        float terrainNoiseFeatureScale_ = 0.0f;
        int terrainNoiseOctaveCount_ = 0;
        float terrainNoiseLacunarity_ = 0.0f;
        float terrainNoiseGain_ = 0.0f;
        float terrainNoiseSimplexScale_ = 0.0f;
        bool terrainDomainWarpEnabled_ = false;
        float terrainDomainWarpAmplitude_ = 0.0f;
        float terrainDomainWarpFrequency_ = 0.0f;
        int terrainDomainWarpOctaveCount_ = 0;
        float terrainDomainWarpGain_ = 0.0f;
        float temperatureNoiseStrength_ = 0.12f;
        float temperatureNoiseFeatureScale_ = 8192.0f;
        int temperatureNoiseOctaveCount_ = 2;
        float temperatureNoiseLacunarity_ = 2.0f;
        float temperatureNoiseGain_ = 0.5f;
        float temperatureNoiseSimplexScale_ = 1.0f;
        float precipitationNoiseFeatureScale_ = 4096.0f;
        int precipitationNoiseOctaveCount_ = 3;
        float precipitationNoiseLacunarity_ = 2.0f;
        float precipitationNoiseGain_ = 0.5f;
        float precipitationNoiseSimplexScale_ = 1.0f;
        int seaLevel_ = 0;
        float fluidWaterAlpha_ = 0.8f;
        game::ClientWorldRuntime clientWorldRuntime_;
        game::ClientTerrainSceneRuntime terrainSceneRuntime_;
        game::ClientSceneLifecycle sceneLifecycle_;
        world::WorldRuntime& worldRuntime_;
        VulkanResourceManager gpuResources_;
        TerrainRenderPath terrainRenderPath_;
        TextRenderPath textRenderPath_;
        SpriteRenderPath spriteRenderPath_;
        ScreenPresentation screenPresentation_;
        PlayerMeshRenderPath playerMeshRenderPath_;
        ParticleRenderPath particleRenderPath_;
        DroppedItemRenderPath droppedItemRenderPath_;
        std::array<uint16_t, 1024> heightLut_{};
        VkDeviceSize localMemoryHeapSize_ = 0;
        uint32_t localMemoryHeapIndex_ = UINT32_MAX;
        bool memoryBudgetSupported_ = false;
        uint32_t terrainDrawCount_ = 0;
        uint32_t terrainFaceCount_ = 0;
        uint32_t terrainVertexCount_ = 0;
        bool terrainDebugInitialized_ = false;
        game::ClientContent content_;
        RendererAssetStore rendererAssets_;
        bool climateTemperatureOverlayReady_ = false;
        bool climatePrecipitationOverlayReady_ = false;
        ui::UiSystem ui_;
        ui::ClientUiBridge uiBridge_;
        VkCommandBuffer rmlCommandBuffer_ = VK_NULL_HANDLE;
        bool rmlScissorEnabled_ = false;
        VkRect2D rmlScissor_{};
        size_t rmlUiVertexOffset_ = 0;
        size_t rmlUiIndexOffset_ = 0;
        std::vector<Texture> sceneColorTargets_;
        std::vector<Texture> sceneDepthTargets_;
        std::vector<VkFramebuffer> sceneFramebuffers_;
        audio::AudioSystem audio_;

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
