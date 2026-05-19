#include "renderer/PlayerMeshRenderPath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace dolbuto
{
    namespace
    {
        constexpr float PlayerModelScale = 1.0f / 16.0f;
        constexpr float WalkArmSwing = 0.45f;
        constexpr float WalkElbowBendLimit = 0.35f;
        constexpr float WalkLegSwing = 0.65f;
        constexpr float WalkKneeBendLimit = 0.55f;

        struct PlayerAnimationPose
        {
            float walkPhase = 0.0f;
            float walkAmount = 0.0f;
        };

        std::array<float, 16> multiplyMatrix(const std::array<float, 16>& left, const std::array<float, 16>& right)
        {
            std::array<float, 16> result{};
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    for (int k = 0; k < 4; ++k)
                    {
                        result[static_cast<size_t>(column * 4 + row)] +=
                            left[static_cast<size_t>(k * 4 + row)] *
                            right[static_cast<size_t>(column * 4 + k)];
                    }
                }
            }
            return result;
        }

        Vec3 transformPoint(const std::array<float, 16>& matrix, Vec3 point)
        {
            return {
                matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12],
                matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13],
                matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14]};
        }

        std::array<float, 16> identityMatrix()
        {
            return {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f};
        }

        std::array<float, 16> rotationX(float angle)
        {
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            return {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, c, s, 0.0f,
                0.0f, -s, c, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f};
        }

        std::array<float, 16> rotationY(float angle)
        {
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            return {
                c, 0.0f, -s, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                s, 0.0f, c, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f};
        }

        std::array<float, 16> rotationZ(float angle)
        {
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            return {
                c, s, 0.0f, 0.0f,
                -s, c, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f};
        }

        bool isHeadNodeName(const std::string& name)
        {
            return name == "Head" || name == "head";
        }

        float walkingNodePitch(const std::string& name, const PlayerAnimationPose& pose)
        {
            if (pose.walkAmount <= 0.001f)
            {
                return 0.0f;
            }

            const float s = std::sin(pose.walkPhase);
            const float amount = std::clamp(pose.walkAmount, 0.0f, 1.0f);
            if (name == "Arm_L")
            {
                return s * WalkArmSwing * amount;
            }
            if (name == "Arm_R")
            {
                return -s * WalkArmSwing * amount;
            }
            if (name == "Arm_LL")
            {
                return std::clamp(std::max(0.0f, -s) * WalkElbowBendLimit * amount, 0.0f, WalkElbowBendLimit);
            }
            if (name == "Arm_RL")
            {
                return std::clamp(std::max(0.0f, s) * WalkElbowBendLimit * amount, 0.0f, WalkElbowBendLimit);
            }
            if (name == "Leg_L")
            {
                return -s * WalkLegSwing * amount;
            }
            if (name == "Leg_R")
            {
                return s * WalkLegSwing * amount;
            }
            if (name == "Leg_LL")
            {
                return std::clamp(std::max(0.0f, s) * WalkKneeBendLimit * amount, 0.0f, WalkKneeBendLimit);
            }
            if (name == "Leg_RL")
            {
                return std::clamp(std::max(0.0f, -s) * WalkKneeBendLimit * amount, 0.0f, WalkKneeBendLimit);
            }
            return 0.0f;
        }

        std::array<float, 16> animatedLocalTransform(
            const PlayerModelNode& node,
            const std::array<float, 16>* headRotation,
            const PlayerAnimationPose* pose)
        {
            std::array<float, 16> transform = node.localTransform;
            if (headRotation != nullptr && isHeadNodeName(node.name))
            {
                transform = multiplyMatrix(transform, *headRotation);
            }
            if (pose != nullptr)
            {
                const float pitch = walkingNodePitch(node.name, *pose);
                if (pitch != 0.0f)
                {
                    transform = multiplyMatrix(transform, rotationX(pitch));
                }
            }
            return transform;
        }

        bool isFirstPersonHandNode(const std::vector<PlayerModelNode>& nodes, int nodeIndex)
        {
            while (nodeIndex >= 0 && static_cast<size_t>(nodeIndex) < nodes.size())
            {
                const std::string& name = nodes[static_cast<size_t>(nodeIndex)].name;
                if (name == "Arm_RL" || name.rfind("Arm_RL", 0) == 0)
                {
                    return true;
                }
                nodeIndex = nodes[static_cast<size_t>(nodeIndex)].parent;
            }
            return false;
        }

        std::vector<std::array<float, 16>> nodeWorldTransforms(
            const std::vector<PlayerModelNode>& nodes,
            const std::array<float, 16>* headRotation,
            const PlayerAnimationPose* pose)
        {
            std::vector<std::array<float, 16>> worldTransforms(nodes.size(), identityMatrix());
            std::vector<bool> worldTransformReady(nodes.size(), false);
            auto resolveWorldTransform = [&](auto&& self, size_t index) -> const std::array<float, 16>&
            {
                if (worldTransformReady[index])
                {
                    return worldTransforms[index];
                }

                const int parent = nodes[index].parent;
                const std::array<float, 16> localTransform = animatedLocalTransform(nodes[index], headRotation, pose);
                worldTransforms[index] = parent >= 0 && static_cast<size_t>(parent) < nodes.size()
                    ? multiplyMatrix(self(self, static_cast<size_t>(parent)), localTransform)
                    : localTransform;
                worldTransformReady[index] = true;
                return worldTransforms[index];
            };
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                resolveWorldTransform(resolveWorldTransform, i);
            }
            return worldTransforms;
        }
    }

    PlayerMeshRenderPath::PlayerMeshRenderPath(const VkDevice* device, VulkanResourceManager* gpuResources)
    {
        setHandles(device, gpuResources);
    }

    void PlayerMeshRenderPath::setHandles(const VkDevice* device, VulkanResourceManager* gpuResources)
    {
        device_ = device;
        gpuResources_ = gpuResources;
    }

    VkDevice PlayerMeshRenderPath::device() const
    {
        return device_ != nullptr ? *device_ : VK_NULL_HANDLE;
    }

    VulkanResourceManager& PlayerMeshRenderPath::gpuResources() const
    {
        if (gpuResources_ == nullptr)
        {
            throw std::runtime_error("PlayerMeshRenderPath GPU resources are not configured.");
        }
        return *gpuResources_;
    }

    void PlayerMeshRenderPath::loadFromGlb(const std::filesystem::path& path)
    {
        PlayerModelData model = loadPlayerModelFromGlb(path);

        destroy();
        nodes_ = std::move(model.nodes);
        sourceVertices_ = std::move(model.vertices);
        indices_ = std::move(model.indices);
        mesh_.vertexCount = static_cast<uint32_t>(sourceVertices_.size());
        mesh_.indexCount = static_cast<uint32_t>(indices_.size());

        std::vector<TerrainVertex> initialVertices;
        initialVertices.reserve(sourceVertices_.size());
        for (const PlayerModelVertex& source : sourceVertices_)
        {
            initialVertices.push_back(source.vertex);
        }

        createMeshBuffers(mesh_, initialVertices, indices_);
        buildFirstPersonHandMesh();
    }

    void PlayerMeshRenderPath::createMeshBuffers(TerrainMesh& mesh, const std::vector<TerrainVertex>& vertices, const std::vector<uint32_t>& indices)
    {
        if (vertices.empty() || indices.empty())
        {
            return;
        }

        mesh.vertexCount = static_cast<uint32_t>(vertices.size());
        mesh.indexCount = static_cast<uint32_t>(indices.size());
        const VkDeviceSize vertexBufferSize = sizeof(TerrainVertex) * vertices.size();
        const VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        gpuResources().createBuffer(
            vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            mesh.vertexBuffer,
            mesh.vertexMemory);

        void* data = nullptr;
        vkMapMemory(device(), mesh.vertexMemory, 0, vertexBufferSize, 0, &data);
        std::memcpy(data, vertices.data(), static_cast<std::size_t>(vertexBufferSize));
        vkUnmapMemory(device(), mesh.vertexMemory);

        gpuResources().createBuffer(
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            mesh.indexBuffer,
            mesh.indexMemory);

        vkMapMemory(device(), mesh.indexMemory, 0, indexBufferSize, 0, &data);
        std::memcpy(data, indices.data(), static_cast<std::size_t>(indexBufferSize));
        vkUnmapMemory(device(), mesh.indexMemory);
    }

    void PlayerMeshRenderPath::update(Vec3 playerPosition, float playerYaw, float playerHeadYaw, float playerHeadPitch, float playerWalkPhase, float playerWalkAmount)
    {
        if (!ready())
        {
            return;
        }

        const std::array<float, 16> headRotation = multiplyMatrix(rotationY(playerHeadYaw), rotationX(-playerHeadPitch));
        const PlayerAnimationPose animationPose{
            playerWalkPhase,
            playerWalkAmount
        };
        const std::vector<std::array<float, 16>> worldTransforms = nodeWorldTransforms(nodes_, &headRotation, &animationPose);

        const Vec3 forward{std::cos(playerYaw), 0.0f, std::sin(playerYaw)};
        const Vec3 right{std::sin(playerYaw), 0.0f, -std::cos(playerYaw)};
        const VkDeviceSize size = sizeof(TerrainVertex) * sourceVertices_.size();
        void* data = nullptr;
        vkMapMemory(device(), mesh_.vertexMemory, 0, size, 0, &data);
        auto* vertices = static_cast<TerrainVertex*>(data);
        for (std::size_t i = 0; i < sourceVertices_.size(); ++i)
        {
            const PlayerModelVertex& source = sourceVertices_[i];
            const TerrainVertex& local = source.vertex;
            const std::array<float, 16>& nodeTransform = source.nodeIndex >= 0 && static_cast<size_t>(source.nodeIndex) < worldTransforms.size()
                ? worldTransforms[static_cast<size_t>(source.nodeIndex)]
                : worldTransforms.front();
            const Vec3 modelPoint = transformPoint(nodeTransform, {local.x, local.y, local.z});
            const Vec3 scaledLocal{
                modelPoint.x * PlayerModelScale,
                modelPoint.y * PlayerModelScale,
                modelPoint.z * PlayerModelScale};
            TerrainVertex vertex = local;
            vertex.x = playerPosition.x + scaledLocal.x * right.x - scaledLocal.z * forward.x;
            vertex.y = playerPosition.y + scaledLocal.y;
            vertex.z = playerPosition.z + scaledLocal.x * right.z - scaledLocal.z * forward.z;
            vertices[i] = vertex;
        }
        vkUnmapMemory(device(), mesh_.vertexMemory);
    }

    void PlayerMeshRenderPath::buildFirstPersonHandMesh()
    {
        firstPersonHandSourceVertices_.clear();
        firstPersonHandIndices_.clear();

        std::vector<int> sourceToHand(sourceVertices_.size(), -1);
        for (std::size_t i = 0; i < sourceVertices_.size(); ++i)
        {
            if (isFirstPersonHandNode(nodes_, sourceVertices_[i].nodeIndex))
            {
                sourceToHand[i] = static_cast<int>(firstPersonHandSourceVertices_.size());
                firstPersonHandSourceVertices_.push_back(sourceVertices_[i]);
            }
        }

        for (std::size_t i = 0; i + 2u < indices_.size(); i += 3u)
        {
            const uint32_t a = indices_[i];
            const uint32_t b = indices_[i + 1u];
            const uint32_t c = indices_[i + 2u];
            if (a >= sourceToHand.size() || b >= sourceToHand.size() || c >= sourceToHand.size())
            {
                continue;
            }
            const int mappedA = sourceToHand[a];
            const int mappedB = sourceToHand[b];
            const int mappedC = sourceToHand[c];
            if (mappedA < 0 || mappedB < 0 || mappedC < 0)
            {
                continue;
            }
            firstPersonHandIndices_.push_back(static_cast<uint32_t>(mappedA));
            firstPersonHandIndices_.push_back(static_cast<uint32_t>(mappedB));
            firstPersonHandIndices_.push_back(static_cast<uint32_t>(mappedC));
        }

        std::vector<TerrainVertex> initialVertices;
        initialVertices.reserve(firstPersonHandSourceVertices_.size());
        for (const PlayerModelVertex& source : firstPersonHandSourceVertices_)
        {
            initialVertices.push_back(source.vertex);
        }
        createMeshBuffers(firstPersonHandMesh_, initialVertices, firstPersonHandIndices_);
    }

    void PlayerMeshRenderPath::updateFirstPersonHand(const Camera& camera, Vec3 cameraPosition, const config::ViewmodelHandConfig& config)
    {
        if (!firstPersonHandReady())
        {
            return;
        }

        const std::vector<std::array<float, 16>> worldTransforms = nodeWorldTransforms(nodes_, nullptr, nullptr);
        std::vector<Vec3> modelPoints(firstPersonHandSourceVertices_.size());
        Vec3 minPoint{999999.0f, 999999.0f, 999999.0f};
        Vec3 maxPoint{-999999.0f, -999999.0f, -999999.0f};
        for (std::size_t i = 0; i < firstPersonHandSourceVertices_.size(); ++i)
        {
            const PlayerModelVertex& source = firstPersonHandSourceVertices_[i];
            const TerrainVertex& local = source.vertex;
            const std::array<float, 16>& nodeTransform = source.nodeIndex >= 0 && static_cast<size_t>(source.nodeIndex) < worldTransforms.size()
                ? worldTransforms[static_cast<size_t>(source.nodeIndex)]
                : worldTransforms.front();
            const Vec3 modelPoint = transformPoint(nodeTransform, {local.x, local.y, local.z});
            const Vec3 scaledPoint{
                modelPoint.x * PlayerModelScale,
                modelPoint.y * PlayerModelScale,
                modelPoint.z * PlayerModelScale};
            modelPoints[i] = scaledPoint;
            minPoint.x = std::min(minPoint.x, scaledPoint.x);
            minPoint.y = std::min(minPoint.y, scaledPoint.y);
            minPoint.z = std::min(minPoint.z, scaledPoint.z);
            maxPoint.x = std::max(maxPoint.x, scaledPoint.x);
            maxPoint.y = std::max(maxPoint.y, scaledPoint.y);
            maxPoint.z = std::max(maxPoint.z, scaledPoint.z);
        }

        const Vec3 cameraRight = camera.right();
        const Vec3 handRight{-cameraRight.x, -cameraRight.y, -cameraRight.z};
        const Vec3 cameraForward = camera.forward();
        const Vec3 handForward{cameraForward.x, -cameraForward.y, cameraForward.z};
        const Vec3 handUp = normalize(cross(handForward, handRight));
        const Vec3 anchor{
            cameraPosition.x + handRight.x * config.x + handUp.x * config.y + handForward.x * config.z,
            cameraPosition.y + handRight.y * config.x + handUp.y * config.y + handForward.y * config.z,
            cameraPosition.z + handRight.z * config.x + handUp.z * config.y + handForward.z * config.z};
        const Vec3 origin{
            (minPoint.x + maxPoint.x) * 0.5f,
            maxPoint.y,
            (minPoint.z + maxPoint.z) * 0.5f};
        const std::array<float, 16> rotation = multiplyMatrix(
            rotationY(config.rotationY),
            multiplyMatrix(rotationZ(config.rotationZ), rotationX(config.rotationX)));

        const VkDeviceSize size = sizeof(TerrainVertex) * firstPersonHandSourceVertices_.size();
        void* data = nullptr;
        vkMapMemory(device(), firstPersonHandMesh_.vertexMemory, 0, size, 0, &data);
        auto* vertices = static_cast<TerrainVertex*>(data);
        for (std::size_t i = 0; i < firstPersonHandSourceVertices_.size(); ++i)
        {
            const Vec3 local{
                -(modelPoints[i].x - origin.x) * config.scale,
                (modelPoints[i].y - origin.y) * config.scale,
                (modelPoints[i].z - origin.z) * config.scale};
            const Vec3 posedLocal = transformPoint(rotation, local);

            TerrainVertex vertex = firstPersonHandSourceVertices_[i].vertex;
            vertex.x = anchor.x + handRight.x * posedLocal.x + handUp.x * posedLocal.y + handForward.x * posedLocal.z;
            vertex.y = anchor.y + handRight.y * posedLocal.x + handUp.y * posedLocal.y + handForward.y * posedLocal.z;
            vertex.z = anchor.z + handRight.z * posedLocal.x + handUp.z * posedLocal.y + handForward.z * posedLocal.z;
            vertices[i] = vertex;
        }
        vkUnmapMemory(device(), firstPersonHandMesh_.vertexMemory);
    }

    void PlayerMeshRenderPath::draw(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const Texture& texture) const
    {
        if (!ready())
        {
            return;
        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout, 0, 1, &texture.descriptorSet, 0, nullptr);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh_.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, mesh_.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, mesh_.indexCount, 1, 0, 0, 0);
    }

    void PlayerMeshRenderPath::drawFirstPersonHand(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const Texture& texture) const
    {
        if (!firstPersonHandReady())
        {
            return;
        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout, 0, 1, &texture.descriptorSet, 0, nullptr);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &firstPersonHandMesh_.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, firstPersonHandMesh_.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, firstPersonHandMesh_.indexCount, 1, 0, 0, 0);
    }

    void PlayerMeshRenderPath::destroy()
    {
        destroyMesh(mesh_);
        destroyMesh(firstPersonHandMesh_);
        nodes_.clear();
        sourceVertices_.clear();
        indices_.clear();
        firstPersonHandSourceVertices_.clear();
        firstPersonHandIndices_.clear();
    }

    void PlayerMeshRenderPath::destroyMesh(TerrainMesh& mesh)
    {
        const VkDevice logicalDevice = device();
        if (mesh.vertexDescriptorSet != VK_NULL_HANDLE)
        {
            mesh.vertexDescriptorSet = VK_NULL_HANDLE;
        }
        if (mesh.vertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(logicalDevice, mesh.vertexBuffer, nullptr);
        }
        if (mesh.vertexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(logicalDevice, mesh.vertexMemory, nullptr);
        }
        if (mesh.indexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(logicalDevice, mesh.indexBuffer, nullptr);
        }
        if (mesh.indexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(logicalDevice, mesh.indexMemory, nullptr);
        }
        mesh = {};
    }

    bool PlayerMeshRenderPath::ready() const
    {
        return mesh_.indexCount > 0 && mesh_.vertexBuffer != VK_NULL_HANDLE && mesh_.indexBuffer != VK_NULL_HANDLE;
    }

    bool PlayerMeshRenderPath::firstPersonHandReady() const
    {
        return firstPersonHandMesh_.indexCount > 0 &&
            firstPersonHandMesh_.vertexBuffer != VK_NULL_HANDLE &&
            firstPersonHandMesh_.indexBuffer != VK_NULL_HANDLE;
    }
}
