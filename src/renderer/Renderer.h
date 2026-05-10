#pragma once

#include "camera/Camera.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dolbuto
{
    class Renderer : public Rml::RenderInterface, public Rml::EventListener
    {
    public:
        explicit Renderer(GLFWwindow* window);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        struct WorldListItem
        {
            std::string name;
            std::string createdText;
            std::string lastPlayedText;
        };

        struct InventorySlot
        {
            uint16_t itemId = 0;
            uint16_t count = 0;
        };

        void drawFrame(
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
            uint64_t worldTicks);
        void setFramebufferResized();
        bool playerColliderIntersectsTerrain(DVec3 playerPosition) const;
        void updateBlockSelection(DVec3 origin, Vec3 direction);
        void updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, DVec3 playerPosition, float deltaSeconds);
        bool editBlockInView(DVec3 origin, Vec3 direction, bool placeRock, DVec3 playerPosition);
        bool pickupDroppedItemInView(DVec3 origin, Vec3 direction);
        std::string selectedBlockText() const;
        std::string climateText(DVec3 position) const;
        void loadGameScene(const std::filesystem::path& worldDirectory, uint64_t worldSeed);
        void unloadGameScene();
        void resetPeakProfiler();
        void setWorldList(const std::vector<WorldListItem>& worlds);
        void setHotbarSelectedSlot(int slot);
        std::array<InventorySlot, 50> inventorySnapshot() const;
        void setInventorySnapshot(const std::array<InventorySlot, 50>& slots);
        std::string uiInputValue(std::string_view id) const;
        void uiMouseMove(double x, double y);
        void uiMouseButton(int button, bool pressed, int modifiers);
        void uiMouseWheel(double yOffset);
        void uiTextInput(unsigned int codepoint);
        void uiKey(int key, bool pressed, int modifiers);
        void closeInventoryInteraction();
        bool rmlUiAvailable() const;
        std::optional<std::string> consumeUiAction();

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
            uint32_t layers = 1;
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

        struct FontCharacter
        {
            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            float xOffset = 0.0f;
            float yOffset = 0.0f;
            float advance = 0.0f;
        };

        struct TextVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float u = 0.0f;
            float v = 0.0f;
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
            float ao = 1.0f;
            float textureLayer = 0.0f;
            float mipDistanceScale = 1.0f;
        };

        struct BlockBreakParticle
        {
            Vec3 position{};
            Vec3 velocity{};
            float age = 0.0f;
            float lifetime = 0.0f;
            float size = 0.0f;
            uint32_t textureLayer = 0;
            float u0 = 0.0f;
            float v0 = 0.0f;
            float u1 = 1.0f;
            float v1 = 1.0f;
        };

        struct ItemStack
        {
            uint16_t itemId = 0;
            uint16_t count = 0;
        };

        enum class WorldEntityType : uint16_t
        {
            None = 0,
            DroppedItem = 1
        };

        struct DroppedItemEntityData
        {
            ItemStack stack{};
        };

        struct WorldEntity
        {
            uint64_t entityId = 0;
            WorldEntityType type = WorldEntityType::None;
            Vec3 previousPosition{};
            Vec3 position{};
            Vec3 velocity{};
            uint8_t flags = 0;
            DroppedItemEntityData droppedItem{};
            float age = 0.0f;
            float renderRotationX = 0.0f;
            float renderRotation = 0.0f;
            float renderRotationZ = 0.0f;
            float renderSpinX = 0.0f;
            float renderSpin = 0.0f;
            float renderSpinZ = 0.0f;
            bool collecting = false;
            float collectAge = 0.0f;
        };

        struct ItemSpriteQuad
        {
            std::array<Vec3, 4> positions{};
            std::array<std::array<float, 2>, 4> uvs{};
            float ao = 1.0f;
        };

        struct ItemSpriteMesh
        {
            std::vector<ItemSpriteQuad> quads;
        };

        struct PackedTerrainQuad
        {
            uint32_t p0x = 0;
            uint32_t p0y = 0;
            uint32_t p0z = 0;
            uint32_t edgeUxy = 0;
            uint32_t edgeUzVx = 0;
            uint32_t edgeVyz = 0;
            uint32_t uv0 = 0;
            uint32_t uvU = 0;
            uint32_t uvV = 0;
            uint32_t material = 0;
        };

        struct PropMesh
        {
            std::vector<float> quads;
        };

        struct TerrainPush
        {
            float mvp[16]{};
            float cameraPosition[4]{};
            float fluidWaterParams[4]{};
        };

        struct BufferUploadRegion
        {
            const void* source = nullptr;
            VkDeviceSize size = 0;
            VkDeviceSize destinationOffset = 0;
        };

        struct TextureMipOverride
        {
            uint32_t layer = 0;
            uint32_t mipLevel = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            VkDeviceSize bufferOffset = 0;
        };

        struct TerrainMesh
        {
            VkBuffer vertexBuffer = VK_NULL_HANDLE;
            VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
            VkBuffer indexBuffer = VK_NULL_HANDLE;
            VkDeviceMemory indexMemory = VK_NULL_HANDLE;
            VkDescriptorSet vertexDescriptorSet = VK_NULL_HANDLE;
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;
        };

        struct TerrainBuildData
        {
            std::vector<TerrainVertex> vertices;
            std::vector<uint32_t> indices;
        };

        struct BlockTextureLayers
        {
            std::array<uint32_t, 6> faces{};
        };

        enum class ItemRenderType : uint8_t
        {
            ExtrudedSprite
        };

        struct BlockDrop
        {
            uint16_t itemId = 0;
            uint16_t min = 1;
            uint16_t max = 1;
            float chance = 1.0f;
        };

        struct ItemDefinition
        {
            std::string key = "none";
            std::string name = "None";
            std::string slotTexture = "none";
            std::string droppedTexture = "none";
            std::string heldTexture = "none";
            uint16_t stackSize = 0;
            uint32_t droppedTextureLayer = 0;
            uint32_t heldTextureLayer = 0;
            ItemRenderType droppedRender = ItemRenderType::ExtrudedSprite;
            ItemRenderType heldRender = ItemRenderType::ExtrudedSprite;
        };

        enum class BlockRenderType : uint8_t
        {
            None,
            Cube,
            Cross,
            Prop
        };

        enum class BlockFaceOcclusion : uint8_t
        {
            None,
            Opaque,
            Cutout
        };

        enum class BlockAlphaMode : uint8_t
        {
            Opaque,
            Cutout,
            Blend
        };

        struct BlockDefinition
        {
            std::string name = "unknown";
            BlockRenderType renderType = BlockRenderType::None;
            bool directional = false;
            bool collision = false;
            bool ao = false;
            BlockFaceOcclusion faceOcclusion = BlockFaceOcclusion::None;
            bool sameBlockFaceCulling = false;
            BlockAlphaMode alphaMode = BlockAlphaMode::Opaque;
            float alphaCutoff = 0.5f;
            float mipDistanceScale = 1.0f;
            float hardness = -1.0f;
            bool randomOffset = false;
            std::vector<BlockDrop> drops;
        };

        struct ChunkOffset
        {
            int x = 0;
            int z = 0;
        };

        struct ChunkData
        {
            uint64_t generation = 0;
            uint64_t revision = 0;
            int chunkX = 0;
            int chunkZ = 0;
            std::vector<uint16_t> blocks;
            std::vector<uint16_t> fluids;
            std::array<uint8_t, ChunkColumnCount> temperature{};
            std::array<uint8_t, ChunkColumnCount> precipitation{};
            std::vector<WorldEntity> entities;
            std::array<bool, SubchunkCount> emptySubchunks{};
        };

        enum class ChunkGenState : uint8_t
        {
            Empty,
            Featuring,
            Full,
            Meshed
        };

        struct FeatureWrite
        {
            int localX = 0;
            int y = 0;
            int localZ = 0;
            uint16_t block = 0;
        };

        using FeatureWriteList = std::vector<FeatureWrite>;
        using FeatureWriteListPtr = std::shared_ptr<FeatureWriteList>;
        static constexpr size_t FeatureNeighborCount = 8;

        struct CompletedChunkData
        {
            std::shared_ptr<ChunkData> chunk;
            std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingFeatureSlots{};
        };

        struct TerrainJob
        {
            enum class Type
            {
                BuildFeaturing,
                FinalizeFeatures,
                BuildChunkMesh
            };

            Type type = Type::BuildFeaturing;
            uint64_t generation = 0;
            uint64_t revision = 0;
            uint32_t priority = UINT32_MAX;
            uint64_t sequence = 0;
            int chunkX = 0;
            int chunkZ = 0;
            std::shared_ptr<ChunkData> chunk;
            std::array<FeatureWriteListPtr, FeatureNeighborCount> incomingFeatureSlots{};
            std::array<std::shared_ptr<ChunkData>, 9> meshChunks{};
        };

        struct CompletedChunkMesh
        {
            uint64_t generation = 0;
            uint64_t revision = 0;
            int chunkX = 0;
            int chunkZ = 0;
            std::array<TerrainBuildData, SubchunkCount> solidSubchunks;
            std::array<TerrainBuildData, SubchunkCount> fluidSubchunks;
        };

        struct RuntimeChunk
        {
            ChunkGenState genState = ChunkGenState::Empty;
            int chunkX = 0;
            int chunkZ = 0;
            std::shared_ptr<ChunkData> data;
            std::array<FeatureWriteListPtr, FeatureNeighborCount> incomingFeatureSlots{};
            std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingFeatureSlots{};
            uint8_t incomingFeatureMask = 0;
            uint64_t outgoingPublishedTicket = 0;
            uint64_t renderTicket = 0;
            uint64_t meshTicket = 0;
            uint64_t fullTicket = 0;
            uint64_t featuringTicket = 0;
            uint32_t bestPriority = UINT32_MAX;
            uint64_t buildQueuedTicket = 0;
            uint64_t finalizeQueuedTicket = 0;
            uint64_t meshQueuedTicket = 0;
            bool hasSavedBacking = false;
            bool dataDirtyForSave = false;
            uint64_t dataDirtySerial = 0;
        };

        struct SaveChunkSnapshot
        {
            int chunkX = 0;
            int chunkZ = 0;
            ChunkGenState genState = ChunkGenState::Empty;
            uint64_t revision = 0;
            bool hasData = false;
            bool forceSave = false;
            bool hasSavedBacking = false;
            uint64_t dataDirtySerial = 0;
            std::shared_ptr<const ChunkData> chunkData;
            std::vector<uint16_t> blocks;
            std::vector<uint16_t> fluids;
            std::array<uint8_t, ChunkColumnCount> temperature{};
            std::array<uint8_t, ChunkColumnCount> precipitation{};
            std::vector<WorldEntity> entities;
            std::array<FeatureWriteListPtr, FeatureNeighborCount> incomingFeatureSlots{};
            uint8_t incomingFeatureMask = 0;
        };

        struct WorldEntityHandle
        {
            uint64_t chunkKey = 0;
            size_t index = 0;
        };

        struct RegionChunkEntry
        {
            uint32_t offsetSector = 0;
            uint32_t sectorCount = 0;
            uint32_t storedSize = 0;
            uint32_t rawSize = 0;
        };

        struct RegionHeaderCache
        {
            bool exists = false;
            std::array<RegionChunkEntry, 16u * 16u> entries{};
        };

        struct ChunkRenderData
        {
            uint64_t revision = 0;
            int chunkX = 0;
            int chunkZ = 0;
            std::array<TerrainMesh, SubchunkCount> solidSubchunks;
            std::array<TerrainMesh, SubchunkCount> fluidSubchunks;
        };

        struct RetiredChunkRenderData
        {
            uint32_t framesLeft = 0;
            ChunkRenderData chunk;
        };

        struct BlockRaycastHit
        {
            int blockX = 0;
            int blockY = 0;
            int blockZ = 0;
            int previousBlockX = 0;
            int previousBlockY = 0;
            int previousBlockZ = 0;
        };

        struct BlockBreakingState
        {
            bool active = false;
            int x = 0;
            int y = 0;
            int z = 0;
            uint16_t block = 0;
            float progress = 0.0f;
            float particleTimer = 0.0f;
        };

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
        void createFont();
        void createTextVertexBuffer();
        void createUiBuffers();
        void createParticleBuffers();
        void createSelectionLineBuffer();
        void initializeRmlUi();
        void shutdownRmlUi();
        bool renderRmlUi(VkCommandBuffer commandBuffer, int menuOverlayMode);
        void setRmlUiDocument(int menuOverlayMode);
        void updateHotbarScopeClass();
        void attachRmlUiEvents(Rml::ElementDocument* document);
        Rml::Input::KeyIdentifier rmlKeyFromGlfw(int key) const;
        int rmlKeyModifiersFromGlfw(int modifiers) const;
        int currentRmlKeyModifiers() const;
        void createPlayerMesh();
        void loadWorldConfig();
        void loadRenderConfig();
        void loadHeightLut();
        void updateLoadedChunks(DVec3 playerPosition);
        void requestTerrainLoad(int centerGroupChunkX, int centerGroupChunkZ);
        void rebuildLoadOrderIfNeeded();
        void startTerrainWorkers();
        void stopTerrainWorkers();
        void startSaveWorker();
        void stopSaveWorker();
        void saveWorkerLoop();
        void terrainWorkerLoop();
        void enqueueTerrainJob(TerrainJob job);
        void processCompletedTerrainJobs();
        uint32_t processPendingTerrainUnloads();
        void processRetiredTerrainChunks();
        RuntimeChunk& ensureRuntimeChunk(int chunkX, int chunkZ, uint64_t generation);
        void markRuntimeChunkDataDirty(RuntimeChunk& chunk);
        void wantRender(int chunkX, int chunkZ, uint32_t priority);
        void wantMesh(int chunkX, int chunkZ, uint32_t priority);
        void wantFull(int chunkX, int chunkZ, uint32_t priority);
        void wantFeaturing(int chunkX, int chunkZ, uint32_t priority);
        SaveChunkSnapshot makeSaveSnapshot(const RuntimeChunk& chunk) const;
        void enqueueSaveSnapshot(SaveChunkSnapshot snapshot);
        void enqueueSaveAllRuntimeChunks();
        void saveChunkSnapshot(const SaveChunkSnapshot& snapshot);
        std::optional<SaveChunkSnapshot> loadChunkSnapshot(int chunkX, int chunkZ);
        RuntimeChunk runtimeChunkFromSnapshot(const SaveChunkSnapshot& snapshot, uint64_t generation);
        std::shared_ptr<ChunkData> buildChunkData(int chunkX, int chunkZ) const;
        std::array<FeatureWriteListPtr, FeatureNeighborCount> buildTreeFeatures(const std::shared_ptr<ChunkData>& chunk, const std::array<int, ChunkColumnCount>& heights) const;
        bool applyFeatureWrites(const std::shared_ptr<ChunkData>& chunk, const std::array<FeatureWriteListPtr, FeatureNeighborCount>& incomingFeatureSlots) const;
        void acceptFeatureSlot(int targetChunkX, int targetChunkZ, size_t sourceSlot, FeatureWriteListPtr writes);
        void publishFeatureSlots(RuntimeChunk& sourceChunk);
        void tryQueueFeatureFinalize(uint64_t key);
        void tryQueueMeshIfReady(int chunkX, int chunkZ);
        void tryQueueMeshesAround(int chunkX, int chunkZ);
        bool setBlockAtWorld(int x, int y, int z, uint16_t block);
        bool blockIntersectsPlayerCollider(int x, int y, int z, uint16_t block, DVec3 playerPosition) const;
        void updateChunkEmptySubchunk(const std::shared_ptr<ChunkData>& chunk, int subchunkY);
        void rebuildSubchunkMeshNow(int chunkX, int chunkZ, int subchunkY);
        void rebuildEditedChunkMeshes(int blockX, int blockY, int blockZ);
        std::vector<uint16_t> buildMeshingBlocks(const std::shared_ptr<ChunkData>& chunk) const;
        TerrainBuildData buildSubchunkMesh(const std::shared_ptr<ChunkData>& chunk, const std::vector<uint16_t>& meshingBlocks, int subchunkY) const;
        TerrainBuildData buildEditedSubchunkMesh(const std::shared_ptr<ChunkData>& chunk, int subchunkY) const;
        TerrainBuildData buildSubchunkMesh(const std::shared_ptr<ChunkData>& chunk, int subchunkY, const std::function<uint16_t(int, int, int)>& blockAt) const;
        TerrainBuildData buildFluidSubchunkMesh(const std::array<std::shared_ptr<ChunkData>, 9>& chunks, int subchunkY) const;
        CompletedChunkMesh buildChunkMesh(const std::array<std::shared_ptr<ChunkData>, 9>& chunks, uint64_t generation) const;
        bool chunkMeshReady(uint64_t key) const;
        void destroyChunkRenderData(ChunkRenderData& chunk);
        void destroyAllTerrainChunks();
        void updateTerrainStats();
        std::array<int, ChunkColumnCount> buildChunkHeightmap(int chunkX, int chunkZ) const;
        void createTerrainBuffer(const TerrainBuildData& buildData, TerrainMesh& mesh, bool deviceLocal = true);
        void createChunkTerrainBuffers(const std::array<TerrainBuildData, SubchunkCount>& buildData, std::array<TerrainMesh, SubchunkCount>& meshes);
        PackedTerrainQuad packTerrainQuad(const TerrainVertex& a, const TerrainVertex& b, const TerrainVertex& c, const TerrainVertex& d) const;
        std::vector<PackedTerrainQuad> buildPackedTerrainQuads(const TerrainBuildData& buildData) const;
        void createTerrainVertexDescriptorSet(TerrainMesh& mesh, VkDeviceSize vertexBufferSize);
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
        Texture createTexture(const std::string& path, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
        Texture createTextureFromRgba(const unsigned char* pixels, int width, int height, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
        Texture createTextureArray(const std::vector<std::string>& paths, float alphaMultiplier = 1.0f);
        Texture createRenderTargetTexture(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspectMask, VkImageLayout descriptorLayout);
        uint32_t calculateMipLevels(int width, int height) const;
        void generateMipmaps(VkImage image, int32_t width, int32_t height, uint32_t mipLevels, uint32_t layerCount = 1) const;
        void generateTextureArrayMipmaps(VkImage image, int32_t width, int32_t height, uint32_t mipLevels, uint32_t layerCount, const std::vector<TextureMipOverride>& mipOverrides, VkBuffer mipOverrideBuffer) const;
        void destroyTexture(Texture& texture);
        void destroyTerrainMesh(TerrainMesh& mesh);

        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
        void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory, uint32_t* memoryTypeIndex = nullptr) const;
        void createDeviceLocalBuffer(const void* source, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory, uint32_t* memoryTypeIndex = nullptr) const;
        void copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) const;
        void uploadBufferRegions(VkBuffer destination, const std::vector<BufferUploadRegion>& regions) const;
        void uploadBufferData(VkBuffer destination, const void* source, VkDeviceSize size, VkDeviceSize destinationOffset = 0) const;
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount = 1) const;
        void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels = 1, uint32_t layerCount = 1) const;
        VkCommandBuffer beginSingleTimeCommands() const;
        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

        void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera, Vec3 cameraPosition, Vec3 playerPosition, std::string_view fpsText, bool debugTextVisible, VkBuffer screenshotBuffer, bool showPlayer, bool terrainWireframe, int climateOverlayMode, int menuOverlayMode, bool gameSceneRenderEnabled, uint64_t worldTicks);
        void copySwapchainImageToBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, VkBuffer buffer) const;
        void saveScreenshot(VkDeviceMemory memory, VkDeviceSize size) const;
        void updatePlayerMesh(Vec3 playerPosition, float playerYaw);
        void drawTerrain(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, bool wireframe, bool drawBlocks, bool drawFluids, uint32_t sceneImageIndex = 0);
        void drawTerrainMeshBound(VkCommandBuffer commandBuffer, const TerrainMesh& mesh) const;
        void drawTerrainMesh(VkCommandBuffer commandBuffer, const TerrainMesh& mesh, const Texture& texture) const;
        const BlockDefinition& blockDefinition(uint16_t block) const;
        bool raycastBlock(DVec3 origin, Vec3 direction, BlockRaycastHit& hit) const;
        uint16_t blockAtWorld(int x, int y, int z) const;
        bool breakBlockAtHit(const BlockRaycastHit& hit);
        void resetBlockBreaking();
        bool terrainCellBlocksPlayer(int x, int y, int z) const;
        uint32_t blockFaceTextureLayer(uint16_t block, int face) const;
        uint32_t blockFaceTextureLayerForHit(uint16_t block, const BlockRaycastHit& hit) const;
        bool blockUsesCubeMesh(uint16_t block) const;
        bool blockContributesAo(uint16_t block) const;
        bool neighborCullsFace(uint16_t block, uint16_t neighbor) const;
        void drawBlockSelection(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition);
        void drawPlayer(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition) const;
        void spawnBlockBreakParticles(int x, int y, int z, uint16_t block);
        void spawnBlockMiningParticle(const BlockRaycastHit& hit, uint16_t block);
        void updateBlockBreakParticles();
        void drawBlockBreakParticles(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition);
        void spawnBlockDrops(int x, int y, int z, uint16_t block);
        uint64_t allocateWorldEntityId();
        uint64_t entityChunkKey(const WorldEntity& entity) const;
        RuntimeChunk* runtimeChunkForEntity(const WorldEntity& entity);
        const RuntimeChunk* runtimeChunkForEntity(const WorldEntity& entity) const;
        bool addWorldEntity(WorldEntity entity);
        size_t loadedDroppedItemCount() const;
        bool worldEntityGrounded(const WorldEntity& entity) const;
        void setWorldEntityGrounded(WorldEntity& entity, bool grounded) const;
        bool raycastDroppedItem(DVec3 origin, Vec3 direction, WorldEntityHandle& itemHandle) const;
        bool droppedItemTouchesPlayerCollider(const WorldEntity& item, Vec3 playerPosition) const;
        void updateDroppedItems(Vec3 playerPosition);
        void updateDroppedItemsTick(Vec3 playerPosition, float dt);
        void drawDroppedItems(VkCommandBuffer commandBuffer, const Camera& camera, Vec3 cameraPosition, Vec3 playerPosition);
        void updateInventoryDebugSlots();
        std::string inventoryDebugSlotRml(size_t slotIndex, bool inventorySlot) const;
        uint16_t addItemToPlayerInventory(ItemStack stack);
        uint16_t addItemToInventoryRange(ItemStack& stack, size_t begin, size_t end);
        bool inventoryStackCanMerge(const ItemStack& slot, const ItemStack& stack) const;
        std::optional<size_t> inventorySlotAt(double x, double y) const;
        void handleInventorySlotClick(size_t slotIndex, int button, int modifiers);
        void handleInventoryHotbarSwapKey(int key);
        void updateInventoryCursorUi();
        void updateItemTooltipUi();
        std::string itemTooltipRml(const ItemStack& stack) const;
        void updateInventoryUi();
        std::string itemSlotImageRml(size_t slotIndex, bool inventorySlot) const;
        std::string itemStackContentRml(const ItemStack& stack, int itemLeft, int itemTop) const;
        ItemSpriteMesh buildItemSpriteMesh(const std::filesystem::path& path) const;
        void drawSprite(VkCommandBuffer commandBuffer, const Texture& texture, SpriteRect rect, UvRect uv = {}, Color color = {}) const;
        void drawSpriteDescriptor(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, SpriteRect rect, UvRect uv = {}, Color color = {}) const;
        void drawMenuOverlay(VkCommandBuffer commandBuffer, int menuOverlayMode);
        void ensureClimateOverlayTexture(int mode);
        std::vector<unsigned char> buildClimateOverlayPixels(int mode) const;
        std::vector<float> buildTileableClimateNoise(float featureScale, float simplexScale, int octaveCount, float lacunarity, float gain, int seed) const;
        std::array<float, ChunkColumnCount> buildChunkTileableClimateNoise(int chunkX, int chunkZ, float featureScale, float simplexScale, int octaveCount, float lacunarity, float gain, int seed) const;
        float sampleTileableClimateNoise(int wrappedX, int wrappedZ, float featureScale, float simplexScale, int octaveCount, float lacunarity, float gain, int seed) const;
        void populateChunkClimate(ChunkData& chunk) const;
        float baseTemperatureAtWrappedZ(int wrappedZ) const;
        float temperatureAtWrapped(int wrappedZ, float noise) const;
        float precipitationAtNoise(float noise) const;
        void drawClimateOverlay(VkCommandBuffer commandBuffer, int mode) const;
        std::string_view resolutionText();
        void updateDebugTextBatch(std::string_view fpsText);
        void buildMenuTextBatch(int menuOverlayMode);
        void updatePerformanceText(double cpuFrameMs);
        void updatePeakProfiler(double frameMs);
        void updatePeakProfilerText();
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
        int terrainSeed(int offset = 0) const;
        int temperatureSeed() const;
        int precipitationSeed() const;
        static std::string escapeRml(std::string_view text);
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
        void ProcessEvent(Rml::Event& event) override;

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
        std::string dataQueueText_ = "WANT RENDER/FEATURE/DATA: 0 / 0 / 0";
        std::string finalizeQueueText_ = "STATE EMPTY/FEATURING/FULL/MESHED: 0 / 0 / 0 / 0";
        std::string meshQueueText_ = "RENDER MISS: 0";
        std::string dataDoneText_ = "QUEUE BUILD/FINALIZE/MESH: 0 / 0 / 0";
        std::string meshDoneText_ = "DONE BUILD/FINALIZE/MESH: 0 / 0 / 0";
        std::string saveQueueText_ = "SAVE QUEUE/CACHE: 0 / 0";
        std::string saveDoneText_ = "SAVE CHUNK/FEATURE/FAILED: 0 / 0 / 0";
        std::string loadText_ = "LOAD PENDING/REGION/MISS: 0 / 0 / 0";
        std::string uploadText_ = "UPLOAD: 0000 / 8";
        std::string unloadText_ = "UNLOAD: 0000 / 16";
        std::string retiredText_ = "RETIRED: 0000";
        std::string jobMainText_ = "JOB MAIN: 000.000MS";
        std::string peakProfilerStatusText_ = "PEAK SAMPLE: WAIT 5S";
        std::string peakFrameText_ = "PEAK FRAME: 000.000MS";
        std::string peakUpdateText_ = "PEAK UPDATE: 000.000MS";
        std::string peakEnsureRuntimeText_ = "PEAK ENSURE RUNTIME: 000.000MS";
        std::string peakWantRenderText_ = "PEAK WANT RENDER: 000.000MS";
        std::string peakEnsureKeyText_ = "PEAK ENSURE KEY: 000.000MS";
        std::string peakEnsureMarkText_ = "PEAK ENSURE MARK: 000.000MS";
        std::string peakEnsureFindText_ = "PEAK ENSURE FIND: 000.000MS";
        std::string peakEnsureLoadText_ = "PEAK ENSURE LOAD: 000.000MS";
        std::string peakEnsureCreateText_ = "PEAK ENSURE CREATE: 000.000MS";
        std::string peakEnsureDataTouchText_ = "PEAK ENSURE DATA TOUCH: 000.000MS";
        std::chrono::steady_clock::time_point terrainDebugSampleTime_{};
        std::string chunkUpdateProfileText_ = "UPDATE TOTAL: ---.---MS";
        std::string worldBuildProfileText_ = "WORLD TOTAL: ---.---MS";
        std::string gridScanProfileText_ = "GRID SCAN: ---.---MS";
        std::string newChunksProfileText_ = "NEW CHUNKS: ---.---MS";
        std::string metadataBuildProfileText_ = "META BUILD: ---.---MS";
        std::string cachedFpsText_;
        VkExtent2D lastResolutionExtent_{};
        TextBatch debugTextBatch_;
        TextBatch menuTextBatch_;
        int cachedMenuOverlayMode_ = -1;
        VkExtent2D cachedMenuExtent_{};
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
        VkBuffer textVertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory textVertexMemory_ = VK_NULL_HANDLE;
        VkBuffer uiVertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory uiVertexMemory_ = VK_NULL_HANDLE;
        VkBuffer uiIndexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory uiIndexMemory_ = VK_NULL_HANDLE;
        VkBuffer particleVertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory particleVertexMemory_ = VK_NULL_HANDLE;
        VkBuffer particleIndexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory particleIndexMemory_ = VK_NULL_HANDLE;
        VkBuffer droppedItemVertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory droppedItemVertexMemory_ = VK_NULL_HANDLE;
        VkBuffer droppedItemIndexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory droppedItemIndexMemory_ = VK_NULL_HANDLE;
        VkBuffer selectionLineVertexBuffer_ = VK_NULL_HANDLE;
        VkDeviceMemory selectionLineVertexMemory_ = VK_NULL_HANDLE;
        bool hasSelectedBlock_ = false;
        int selectedBlockX_ = 0;
        int selectedBlockY_ = 0;
        int selectedBlockZ_ = 0;
        uint16_t selectedBlockId_ = 0;
        TerrainMesh playerMesh_;
        std::vector<TerrainVertex> playerLocalVertices_;
        std::vector<uint32_t> playerIndices_;
        std::vector<BlockBreakParticle> blockBreakParticles_;
        BlockBreakingState blockBreaking_;
        uint64_t nextWorldEntityId_ = 1;
        double lastParticleUpdateTime_ = 0.0;
        double lastDroppedItemUpdateTime_ = 0.0;
        float droppedItemTickAccumulator_ = 0.0f;
        float droppedItemRenderAlpha_ = 0.0f;
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
        std::filesystem::path activeWorldDirectory_;
        uint64_t activeWorldSeed_ = 0;
        int activeWorldSeedSalt_ = 0;
        int loadedChunkDiameter_ = 0;
        int loadedCenterGroupChunkX_ = 0;
        int loadedCenterGroupChunkZ_ = 0;
        bool terrainLoadRequested_ = false;
        bool gameSceneLoaded_ = false;
        std::atomic<uint64_t> terrainGeneration_{0};
        int loadOrderDiameter_ = 0;
        std::vector<ChunkOffset> loadOrder_;
        std::unordered_set<uint64_t> desiredTerrainChunks_;
        std::unordered_set<uint64_t> desiredFeatureChunks_;
        std::unordered_set<uint64_t> desiredRenderChunks_;
        std::unordered_set<uint64_t> requestedChunkJobs_;
        std::unordered_set<uint64_t> requestedMeshJobs_;
        std::unordered_set<uint64_t> pendingUnloadSet_;
        std::unordered_map<uint64_t, RuntimeChunk> runtimeChunks_;
        std::unordered_map<uint64_t, ChunkRenderData> terrainChunks_;
        std::deque<uint64_t> pendingUnloadChunks_;
        std::deque<RetiredChunkRenderData> retiredTerrainChunks_;
        std::vector<std::thread> terrainWorkers_;
        std::thread saveWorker_;
        std::mutex terrainJobMutex_;
        std::condition_variable terrainJobCondition_;
        std::deque<TerrainJob> terrainFeatureJobs_;
        std::deque<TerrainJob> terrainFinalizeJobs_;
        std::deque<TerrainJob> terrainMeshJobs_;
        uint64_t terrainJobSequence_ = 0;
        std::deque<CompletedChunkData> completedChunkData_;
        std::deque<std::shared_ptr<ChunkData>> completedMergedChunks_;
        std::deque<CompletedChunkMesh> completedChunkMeshes_;
        bool stopTerrainWorkers_ = false;
        std::mutex saveJobMutex_;
        std::condition_variable saveJobCondition_;
        std::deque<SaveChunkSnapshot> saveJobs_;
        bool stopSaveWorker_ = false;
        std::atomic<uint64_t> saveChunkDoneCount_{0};
        std::atomic<uint64_t> saveFeatureDoneCount_{0};
        std::atomic<uint64_t> saveFailedCount_{0};
        std::atomic<uint64_t> loadPendingHitCount_{0};
        std::atomic<uint64_t> loadRegionHitCount_{0};
        std::atomic<uint64_t> loadMissCount_{0};
        std::mutex savedChunkMutex_;
        std::unordered_map<uint64_t, uint64_t> savedCleanRevisions_;
        std::unordered_map<uint64_t, SaveChunkSnapshot> pendingSaveSnapshots_;
        std::mutex regionHeaderCacheMutex_;
        std::unordered_map<uint64_t, RegionHeaderCache> regionHeaderCache_;
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
        Texture white_;
        Texture lobbyBackground_;
        Texture lobbyTitle_;
        Texture climateTemperatureOverlay_;
        Texture climatePrecipitationOverlay_;
        bool climateTemperatureOverlayReady_ = false;
        bool climatePrecipitationOverlayReady_ = false;
        Texture font_;
        Texture playerTexture_;
        Texture terrainTextureArray_;
        Texture fluidTextureArray_;
        Texture itemTextureArray_;
        std::unique_ptr<Rml::SystemInterface> rmlSystemInterface_;
        bool rmlInitialized_ = false;
        Rml::Context* rmlContext_ = nullptr;
        Rml::ElementDocument* rmlLobbyDocument_ = nullptr;
        Rml::ElementDocument* rmlWorldSelectDocument_ = nullptr;
        Rml::ElementDocument* rmlWorldCreateDocument_ = nullptr;
        Rml::ElementDocument* rmlHudDocument_ = nullptr;
        Rml::ElementDocument* rmlInventoryDocument_ = nullptr;
        Rml::ElementDocument* rmlPauseDocument_ = nullptr;
        int hotbarSelectedSlot_ = 0;
        int activeRmlMenuOverlayMode_ = -1;
        VkCommandBuffer rmlCommandBuffer_ = VK_NULL_HANDLE;
        bool rmlScissorEnabled_ = false;
        VkRect2D rmlScissor_{};
        size_t rmlUiVertexOffset_ = 0;
        size_t rmlUiIndexOffset_ = 0;
        std::optional<std::string> pendingUiAction_;
        std::vector<Texture> sceneColorTargets_;
        std::vector<Texture> sceneDepthTargets_;
        std::vector<VkFramebuffer> sceneFramebuffers_;
        std::vector<BlockDefinition> blockDefinitions_;
        std::vector<BlockTextureLayers> blockTextureLayers_;
        std::array<uint32_t, 10> blockBreakingTextureLayers_{};
        std::vector<ItemDefinition> itemDefinitions_;
        std::vector<ItemSpriteMesh> itemSpriteMeshes_;
        std::unordered_map<std::string, uint16_t> itemIdByKey_;
        std::array<ItemStack, 50> playerInventorySlots_{};
        ItemStack inventoryCursorStack_{};
        double rmlMouseX_ = 0.0;
        double rmlMouseY_ = 0.0;
        bool inventoryDebugSlotsVisible_ = false;
        std::unordered_map<uint16_t, PropMesh> propMeshesByBlock_;
        std::array<FontCharacter, 95> fontCharacters_{};

        std::vector<VkSemaphore> imageAvailableSemaphores_;
        std::vector<VkSemaphore> renderFinishedSemaphores_;
        std::vector<VkFence> inFlightFences_;
        uint32_t currentFrame_ = 0;
        bool framebufferResized_ = false;
        std::chrono::steady_clock::time_point performanceSampleStart_{};
        double accumulatedCpuFrameMs_ = 0.0;
        double accumulatedGpuFrameMs_ = 0.0;
        uint32_t performanceSampleCount_ = 0;
        std::chrono::steady_clock::time_point peakProfilerStartTime_{};
        bool peakProfilerSamplingStarted_ = false;
        double frameChunkUpdateMs_ = 0.0;
        double frameJobMainMs_ = 0.0;
        double frameUploadMs_ = 0.0;
        double frameUnloadMs_ = 0.0;
        double frameRetireMs_ = 0.0;
        double frameSaveEnqueueMs_ = 0.0;
        double frameEnsureRuntimeMs_ = 0.0;
        double frameWantRenderMs_ = 0.0;
        double frameRenderDetachMs_ = 0.0;
        double frameUnloadScanMs_ = 0.0;
        double frameWantEnsureMs_ = 0.0;
        double frameWantInsertMs_ = 0.0;
        double frameWantReadyMs_ = 0.0;
        double frameWantDependMs_ = 0.0;
        double frameWantMeshReadyMs_ = 0.0;
        double frameWantMeshDependMs_ = 0.0;
        double frameEnsureKeyMs_ = 0.0;
        double frameEnsureMarkMs_ = 0.0;
        double frameEnsureFindMs_ = 0.0;
        double frameEnsureLoadMs_ = 0.0;
        double frameEnsureCreateMs_ = 0.0;
        double frameEnsureDataTouchMs_ = 0.0;
        double peakFrameMs_ = 0.0;
        double peakChunkUpdateMs_ = 0.0;
        double peakJobMainMs_ = 0.0;
        double peakUploadMs_ = 0.0;
        double peakUnloadMs_ = 0.0;
        double peakRetireMs_ = 0.0;
        double peakSaveEnqueueMs_ = 0.0;
        double peakEnsureRuntimeMs_ = 0.0;
        double peakWantRenderMs_ = 0.0;
        double peakRenderDetachMs_ = 0.0;
        double peakUnloadScanMs_ = 0.0;
        double peakWantEnsureMs_ = 0.0;
        double peakWantInsertMs_ = 0.0;
        double peakWantReadyMs_ = 0.0;
        double peakWantDependMs_ = 0.0;
        double peakWantMeshReadyMs_ = 0.0;
        double peakWantMeshDependMs_ = 0.0;
        double peakEnsureKeyMs_ = 0.0;
        double peakEnsureMarkMs_ = 0.0;
        double peakEnsureFindMs_ = 0.0;
        double peakEnsureLoadMs_ = 0.0;
        double peakEnsureCreateMs_ = 0.0;
        double peakEnsureDataTouchMs_ = 0.0;
    };
}
