#pragma once

#include "camera/Camera.h"
#include "config/ViewmodelConfig.h"
#include "game/ViewmodelMotion.h"
#include "renderer/PlayerModelLoader.h"
#include "renderer/RendererGpuResources.h"
#include "renderer/TerrainTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace dolbuto
{
    struct PlayerVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float ao = 1.0f;
        float textureLayer = 0.0f;
        float mipDistanceScale = 1.0f;
        uint32_t nodeIndex = 0;
        uint8_t packedLight = 0xF0u;
    };

    struct PlayerAnimationNodePose
    {
        std::array<float, 3> translation{0.0f, 0.0f, 0.0f};
        std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
        std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    };

    class PlayerMeshRenderPath
    {
    public:
        struct ItemAttachment
        {
            bool valid = false;
            Vec3 center{};
            Vec3 xAxis{1.0f, 0.0f, 0.0f};
            Vec3 yAxis{0.0f, 1.0f, 0.0f};
            Vec3 zAxis{0.0f, 0.0f, 1.0f};
        };

        PlayerMeshRenderPath() = default;
        PlayerMeshRenderPath(
            const VkDevice* device,
            const VkDescriptorPool* descriptorPool,
            const VkDescriptorSetLayout* transformDescriptorSetLayout,
            VulkanResourceManager* gpuResources);

        void setHandles(
            const VkDevice* device,
            const VkDescriptorPool* descriptorPool,
            const VkDescriptorSetLayout* transformDescriptorSetLayout,
            VulkanResourceManager* gpuResources);

        void loadFromGlb(const std::filesystem::path& path);
        void update(Vec3 playerPosition, float playerYaw, float playerHeadYaw, float playerHeadPitch, float playerWalkPhase, float playerWalkAmount, bool playerWalkReverse, bool playerCrouching, bool playerSprinting, bool playerProne, float animationSeconds, uint32_t frameIndex, uint8_t packedLight);
        void updateFirstPersonHand(const Camera& camera, Vec3 cameraPosition, const config::ViewmodelHandConfig& config, const ViewmodelMotion& viewmodelMotion, uint32_t frameIndex, uint8_t packedLight);
        void draw(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const Texture& texture, uint32_t frameIndex) const;
        void drawFirstPersonHand(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const Texture& texture, uint32_t frameIndex) const;
        void destroy();
        bool ready() const;
        bool firstPersonHandReady() const;
        const ItemAttachment& leftItemAttachment() const;
        const ItemAttachment& rightItemAttachment() const;

    private:
        struct TransformFrame
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        };

        VkDevice device() const;
        VkDescriptorPool descriptorPool() const;
        VkDescriptorSetLayout transformDescriptorSetLayout() const;
        VulkanResourceManager& gpuResources() const;
        void buildFirstPersonHandMesh();
        void createMeshBuffers(TerrainMesh& mesh, const std::vector<PlayerVertex>& vertices, const std::vector<uint32_t>& indices);
        void createTransformFrames(std::vector<TransformFrame>& frames, std::size_t nodeCount);
        void updateTransformFrame(std::vector<TransformFrame>& frames, const std::vector<std::array<float, 16>>& transforms, uint32_t frameIndex);
        void updateMeshLight(TerrainMesh& mesh, uint8_t packedLight, uint8_t& cachedLight);
        void destroyMesh(TerrainMesh& mesh);
        void destroyTransformFrames(std::vector<TransformFrame>& frames);
        VkDescriptorSet transformDescriptor(const std::vector<TransformFrame>& frames, uint32_t frameIndex) const;

        const VkDevice* device_ = nullptr;
        const VkDescriptorPool* descriptorPool_ = nullptr;
        const VkDescriptorSetLayout* transformDescriptorSetLayout_ = nullptr;
        VulkanResourceManager* gpuResources_ = nullptr;
        TerrainMesh mesh_{};
        TerrainMesh firstPersonHandMesh_{};
        std::vector<TransformFrame> transformFrames_;
        std::vector<TransformFrame> firstPersonHandTransformFrames_;
        std::vector<PlayerModelNode> nodes_;
        std::vector<PlayerAnimationClip> animations_;
        std::vector<PlayerAnimationNodePose> lastAnimationPose_;
        std::vector<PlayerAnimationNodePose> transitionFromAnimationPose_;
        std::vector<PlayerModelVertex> sourceVertices_;
        std::vector<uint32_t> indices_;
        std::vector<PlayerModelVertex> firstPersonHandSourceVertices_;
        std::vector<uint32_t> firstPersonHandIndices_;
        std::string animationStateKey_;
        int leftItemAttachmentNodeIndex_ = -1;
        int rightItemAttachmentNodeIndex_ = -1;
        ItemAttachment leftItemAttachment_{};
        ItemAttachment rightItemAttachment_{};
        float animationTransitionStartSeconds_ = 0.0f;
        bool animationTransitionActive_ = false;
        uint8_t meshPackedLight_ = 0xFFu;
        uint8_t firstPersonHandPackedLight_ = 0xFFu;
    };
}
