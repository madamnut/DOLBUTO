#include "renderer/PlayerMeshRenderPath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace dolbuto
{
    namespace
    {
        constexpr float PlayerModelScale = 1.0f / 16.0f;
        constexpr float PlayerStandingHeight = 1.75f;
        constexpr float PlayerProneColliderHeight = 0.6f;
        constexpr uint32_t PlayerTransformFrameCount = 2;
        constexpr float TwoPi = 6.28318530718f;
        constexpr float AnimationTransitionSeconds = 0.4f;

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

        Vec3 matrixAxis(const std::array<float, 16>& matrix, int column)
        {
            const std::size_t offset = static_cast<std::size_t>(std::clamp(column, 0, 2)) * 4u;
            const Vec3 axis{matrix[offset + 0u], matrix[offset + 1u], matrix[offset + 2u]};
            const float lengthSquared = dot(axis, axis);
            return lengthSquared > 0.000001f ? normalize(axis) : Vec3{};
        }

        int findNodeIndexByName(const std::vector<PlayerModelNode>& nodes, const std::string& name)
        {
            const auto it = std::find_if(nodes.begin(), nodes.end(), [&name](const PlayerModelNode& node)
            {
                return node.name == name;
            });
            return it != nodes.end() ? static_cast<int>(std::distance(nodes.begin(), it)) : -1;
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

        std::array<float, 16> translationMatrix(Vec3 translation)
        {
            std::array<float, 16> matrix = identityMatrix();
            matrix[12] = translation.x;
            matrix[13] = translation.y;
            matrix[14] = translation.z;
            return matrix;
        }

        std::array<float, 16> scaleMatrix(float x, float y, float z)
        {
            return {
                x, 0.0f, 0.0f, 0.0f,
                0.0f, y, 0.0f, 0.0f,
                0.0f, 0.0f, z, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f};
        }

        std::array<float, 16> basisMatrix(Vec3 xAxis, Vec3 yAxis, Vec3 zAxis, Vec3 origin)
        {
            return {
                xAxis.x, xAxis.y, xAxis.z, 0.0f,
                yAxis.x, yAxis.y, yAxis.z, 0.0f,
                zAxis.x, zAxis.y, zAxis.z, 0.0f,
                origin.x, origin.y, origin.z, 1.0f};
        }

        PlayerVertex playerVertexFromModelVertex(const PlayerModelVertex& source)
        {
            PlayerVertex vertex{};
            vertex.x = source.vertex.x;
            vertex.y = source.vertex.y;
            vertex.z = source.vertex.z;
            vertex.u = source.vertex.u;
            vertex.v = source.vertex.v;
            vertex.ao = source.vertex.ao;
            vertex.textureLayer = source.vertex.textureLayer;
            vertex.mipDistanceScale = source.vertex.mipDistanceScale;
            vertex.nodeIndex = source.nodeIndex >= 0 ? static_cast<uint32_t>(source.nodeIndex) : 0u;
            vertex.packedLight = source.vertex.packedLight;
            return vertex;
        }

        bool isHeadNodeName(const std::string& name)
        {
            return name == "Head" || name == "head";
        }

        bool hasHeadControlNode(const std::vector<PlayerModelNode>& nodes)
        {
            return std::any_of(nodes.begin(), nodes.end(), [](const PlayerModelNode& node)
            {
                return isHeadNodeName(node.name) && !node.hasMesh && !node.children.empty();
            });
        }

        bool isHeadControlNode(const std::vector<PlayerModelNode>& nodes, size_t index)
        {
            if (index >= nodes.size() || !isHeadNodeName(nodes[index].name))
            {
                return false;
            }
            if (hasHeadControlNode(nodes))
            {
                return !nodes[index].hasMesh && !nodes[index].children.empty();
            }
            return true;
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

        std::array<float, 4> normalizeQuat(std::array<float, 4> value)
        {
            const float length = std::sqrt(
                value[0] * value[0] +
                value[1] * value[1] +
                value[2] * value[2] +
                value[3] * value[3]);
            if (length <= 0.000001f)
            {
                return {0.0f, 0.0f, 0.0f, 1.0f};
            }
            const float invLength = 1.0f / length;
            return {value[0] * invLength, value[1] * invLength, value[2] * invLength, value[3] * invLength};
        }

        std::array<float, 3> lerp3(const std::array<float, 3>& left, const std::array<float, 3>& right, float amount)
        {
            return {
                left[0] + (right[0] - left[0]) * amount,
                left[1] + (right[1] - left[1]) * amount,
                left[2] + (right[2] - left[2]) * amount};
        }

        std::array<float, 4> nlerpQuat(std::array<float, 4> left, std::array<float, 4> right, float amount)
        {
            const float d =
                left[0] * right[0] +
                left[1] * right[1] +
                left[2] * right[2] +
                left[3] * right[3];
            if (d < 0.0f)
            {
                right = {-right[0], -right[1], -right[2], -right[3]};
            }
            return normalizeQuat({
                left[0] + (right[0] - left[0]) * amount,
                left[1] + (right[1] - left[1]) * amount,
                left[2] + (right[2] - left[2]) * amount,
                left[3] + (right[3] - left[3]) * amount});
        }

        std::array<float, 16> matrixFromTrs(const PlayerAnimationNodePose& pose)
        {
            const float x = pose.rotation[0];
            const float y = pose.rotation[1];
            const float z = pose.rotation[2];
            const float w = pose.rotation[3];
            const float x2 = x + x;
            const float y2 = y + y;
            const float z2 = z + z;
            const float xx = x * x2;
            const float xy = x * y2;
            const float xz = x * z2;
            const float yy = y * y2;
            const float yz = y * z2;
            const float zz = z * z2;
            const float wx = w * x2;
            const float wy = w * y2;
            const float wz = w * z2;

            return {
                (1.0f - (yy + zz)) * pose.scale[0], (xy + wz) * pose.scale[0], (xz - wy) * pose.scale[0], 0.0f,
                (xy - wz) * pose.scale[1], (1.0f - (xx + zz)) * pose.scale[1], (yz + wx) * pose.scale[1], 0.0f,
                (xz + wy) * pose.scale[2], (yz - wx) * pose.scale[2], (1.0f - (xx + yy)) * pose.scale[2], 0.0f,
                pose.translation[0], pose.translation[1], pose.translation[2], 1.0f};
        }

        PlayerAnimationNodePose basePoseForNode(const PlayerModelNode& node)
        {
            return PlayerAnimationNodePose{node.translation, node.rotation, node.scale};
        }

        std::array<float, 4> sampleKeyframes(const PlayerAnimationChannel& channel, float time)
        {
            if (channel.keyframes.empty())
            {
                return {};
            }
            if (channel.keyframes.size() == 1u || time <= channel.keyframes.front().time)
            {
                return channel.keyframes.front().value;
            }
            if (time >= channel.keyframes.back().time)
            {
                return channel.keyframes.back().value;
            }

            for (size_t i = 0; i + 1u < channel.keyframes.size(); ++i)
            {
                const PlayerAnimationKeyframe& left = channel.keyframes[i];
                const PlayerAnimationKeyframe& right = channel.keyframes[i + 1u];
                if (time < left.time || time > right.time)
                {
                    continue;
                }

                const float span = std::max(right.time - left.time, 0.000001f);
                const float amount = std::clamp((time - left.time) / span, 0.0f, 1.0f);
                if (channel.path == PlayerAnimationPath::Rotation)
                {
                    return nlerpQuat(left.value, right.value, amount);
                }
                return {
                    left.value[0] + (right.value[0] - left.value[0]) * amount,
                    left.value[1] + (right.value[1] - left.value[1]) * amount,
                    left.value[2] + (right.value[2] - left.value[2]) * amount,
                    left.value[3] + (right.value[3] - left.value[3]) * amount};
            }

            return channel.keyframes.back().value;
        }

        const PlayerAnimationClip* findClip(const std::vector<PlayerAnimationClip>& animations, const std::string& name)
        {
            const auto it = std::find_if(animations.begin(), animations.end(), [&name](const PlayerAnimationClip& clip)
            {
                return clip.name == name;
            });
            return it != animations.end() ? &*it : nullptr;
        }

        std::vector<PlayerAnimationNodePose> sampleClip(
            const std::vector<PlayerModelNode>& nodes,
            const PlayerAnimationClip* clip,
            float time)
        {
            std::vector<PlayerAnimationNodePose> poses;
            poses.reserve(nodes.size());
            for (const PlayerModelNode& node : nodes)
            {
                poses.push_back(basePoseForNode(node));
            }
            if (clip == nullptr || clip->duration <= 0.0f)
            {
                return poses;
            }

            float clipTime = std::fmod(std::max(time, 0.0f), clip->duration);
            if (clipTime < 0.0f)
            {
                clipTime += clip->duration;
            }
            for (const PlayerAnimationChannel& channel : clip->channels)
            {
                if (channel.nodeIndex < 0 || static_cast<size_t>(channel.nodeIndex) >= poses.size())
                {
                    continue;
                }

                PlayerAnimationNodePose& pose = poses[static_cast<size_t>(channel.nodeIndex)];
                const std::array<float, 4> value = sampleKeyframes(channel, clipTime);
                if (channel.path == PlayerAnimationPath::Translation)
                {
                    pose.translation = {value[0], value[1], value[2]};
                }
                else if (channel.path == PlayerAnimationPath::Rotation)
                {
                    pose.rotation = normalizeQuat(value);
                }
                else if (channel.path == PlayerAnimationPath::Scale)
                {
                    pose.scale = {value[0], value[1], value[2]};
                }
            }
            return poses;
        }

        PlayerAnimationNodePose blendNodePose(
            const PlayerAnimationNodePose& left,
            const PlayerAnimationNodePose& right,
            float amount)
        {
            return PlayerAnimationNodePose{
                lerp3(left.translation, right.translation, amount),
                nlerpQuat(left.rotation, right.rotation, amount),
                lerp3(left.scale, right.scale, amount)};
        }

        std::vector<PlayerAnimationNodePose> blendPoseVectors(
            const std::vector<PlayerModelNode>& nodes,
            const std::vector<PlayerAnimationNodePose>& leftPose,
            const std::vector<PlayerAnimationNodePose>& rightPose,
            float amount)
        {
            const float clampedAmount = std::clamp(amount, 0.0f, 1.0f);
            std::vector<PlayerAnimationNodePose> poses;
            poses.reserve(nodes.size());
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                const PlayerAnimationNodePose fallback = basePoseForNode(nodes[i]);
                const PlayerAnimationNodePose& left = i < leftPose.size() ? leftPose[i] : fallback;
                const PlayerAnimationNodePose& right = i < rightPose.size() ? rightPose[i] : fallback;
                poses.push_back(blendNodePose(left, right, clampedAmount));
            }
            return poses;
        }

        std::vector<PlayerAnimationNodePose> animationPoseFromIdleMovement(
            const std::vector<PlayerModelNode>& nodes,
            const std::vector<PlayerAnimationNodePose>& idlePose,
            const std::vector<PlayerAnimationNodePose>& walkPose,
            float walkAmount)
        {
            return blendPoseVectors(nodes, idlePose, walkPose, walkAmount);
        }

        std::vector<std::array<float, 16>> localTransformsFromPose(
            const std::vector<PlayerModelNode>& nodes,
            const std::vector<PlayerAnimationNodePose>& pose,
            const std::array<float, 16>* headRotation)
        {
            std::vector<std::array<float, 16>> localTransforms(nodes.size(), identityMatrix());
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                const PlayerAnimationNodePose fallback = basePoseForNode(nodes[i]);
                const PlayerAnimationNodePose& nodePose = i < pose.size() ? pose[i] : fallback;
                localTransforms[i] = matrixFromTrs(nodePose);
                if (headRotation != nullptr && isHeadControlNode(nodes, i))
                {
                    localTransforms[i] = multiplyMatrix(localTransforms[i], *headRotation);
                }
            }
            return localTransforms;
        }

        std::vector<std::array<float, 16>> nodeWorldTransforms(
            const std::vector<PlayerModelNode>& nodes,
            const std::vector<std::array<float, 16>>& localTransforms)
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
                const std::array<float, 16>& localTransform = index < localTransforms.size()
                    ? localTransforms[index]
                    : nodes[index].localTransform;
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

        PlayerMeshRenderPath::ItemAttachment itemAttachmentFromNode(
            const std::vector<std::array<float, 16>>& transforms,
            int nodeIndex)
        {
            if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= transforms.size())
            {
                return {};
            }

            const std::array<float, 16>& transform = transforms[static_cast<std::size_t>(nodeIndex)];
            return PlayerMeshRenderPath::ItemAttachment{
                true,
                transformPoint(transform, {}),
                matrixAxis(transform, 0),
                matrixAxis(transform, 1),
                matrixAxis(transform, 2)
            };
        }
    }

    PlayerMeshRenderPath::PlayerMeshRenderPath(
        const VkDevice* device,
        const VkDescriptorPool* descriptorPool,
        const VkDescriptorSetLayout* transformDescriptorSetLayout,
        VulkanResourceManager* gpuResources)
    {
        setHandles(device, descriptorPool, transformDescriptorSetLayout, gpuResources);
    }

    void PlayerMeshRenderPath::setHandles(
        const VkDevice* device,
        const VkDescriptorPool* descriptorPool,
        const VkDescriptorSetLayout* transformDescriptorSetLayout,
        VulkanResourceManager* gpuResources)
    {
        device_ = device;
        descriptorPool_ = descriptorPool;
        transformDescriptorSetLayout_ = transformDescriptorSetLayout;
        gpuResources_ = gpuResources;
    }

    VkDevice PlayerMeshRenderPath::device() const
    {
        return device_ != nullptr ? *device_ : VK_NULL_HANDLE;
    }

    VkDescriptorPool PlayerMeshRenderPath::descriptorPool() const
    {
        return descriptorPool_ != nullptr ? *descriptorPool_ : VK_NULL_HANDLE;
    }

    VkDescriptorSetLayout PlayerMeshRenderPath::transformDescriptorSetLayout() const
    {
        return transformDescriptorSetLayout_ != nullptr ? *transformDescriptorSetLayout_ : VK_NULL_HANDLE;
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
        animations_ = std::move(model.animations);
        sourceVertices_ = std::move(model.vertices);
        indices_ = std::move(model.indices);
        leftItemAttachmentNodeIndex_ = findNodeIndexByName(nodes_, "Attach_L");
        rightItemAttachmentNodeIndex_ = findNodeIndexByName(nodes_, "Attach_R");
        mesh_.vertexCount = static_cast<uint32_t>(sourceVertices_.size());
        mesh_.indexCount = static_cast<uint32_t>(indices_.size());

        std::vector<PlayerVertex> initialVertices;
        initialVertices.reserve(sourceVertices_.size());
        for (const PlayerModelVertex& source : sourceVertices_)
        {
            initialVertices.push_back(playerVertexFromModelVertex(source));
        }

        createMeshBuffers(mesh_, initialVertices, indices_);
        createTransformFrames(transformFrames_, nodes_.size());
        buildFirstPersonHandMesh();
    }

    void PlayerMeshRenderPath::createMeshBuffers(TerrainMesh& mesh, const std::vector<PlayerVertex>& vertices, const std::vector<uint32_t>& indices)
    {
        if (vertices.empty() || indices.empty())
        {
            return;
        }

        mesh.vertexCount = static_cast<uint32_t>(vertices.size());
        mesh.indexCount = static_cast<uint32_t>(indices.size());
        const VkDeviceSize vertexBufferSize = sizeof(PlayerVertex) * vertices.size();
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

    void PlayerMeshRenderPath::createTransformFrames(std::vector<TransformFrame>& frames, std::size_t nodeCount)
    {
        destroyTransformFrames(frames);
        if (nodeCount == 0 || descriptorPool() == VK_NULL_HANDLE || transformDescriptorSetLayout() == VK_NULL_HANDLE)
        {
            return;
        }

        frames.resize(PlayerTransformFrameCount);
        const VkDeviceSize bufferSize = sizeof(std::array<float, 16>) * nodeCount;
        for (TransformFrame& frame : frames)
        {
            gpuResources().createBuffer(
                bufferSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                frame.buffer,
                frame.memory);

            VkDescriptorSetAllocateInfo setInfo{};
            setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            setInfo.descriptorPool = descriptorPool();
            setInfo.descriptorSetCount = 1;
            const VkDescriptorSetLayout layout = transformDescriptorSetLayout();
            setInfo.pSetLayouts = &layout;
            if (vkAllocateDescriptorSets(device(), &setInfo, &frame.descriptorSet) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to allocate player transform descriptor set.");
            }

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = frame.buffer;
            bufferInfo.offset = 0;
            bufferInfo.range = bufferSize;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = frame.descriptorSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(device(), 1, &write, 0, nullptr);
        }
    }

    void PlayerMeshRenderPath::updateTransformFrame(
        std::vector<TransformFrame>& frames,
        const std::vector<std::array<float, 16>>& transforms,
        uint32_t frameIndex)
    {
        if (frames.empty() || transforms.empty())
        {
            return;
        }

        TransformFrame& frame = frames[static_cast<std::size_t>(frameIndex) % frames.size()];
        if (frame.memory == VK_NULL_HANDLE)
        {
            return;
        }

        const VkDeviceSize size = sizeof(std::array<float, 16>) * transforms.size();
        void* data = nullptr;
        vkMapMemory(device(), frame.memory, 0, size, 0, &data);
        std::memcpy(data, transforms.data(), static_cast<std::size_t>(size));
        vkUnmapMemory(device(), frame.memory);
    }

    void PlayerMeshRenderPath::updateMeshLight(TerrainMesh& mesh, uint8_t packedLight, uint8_t& cachedLight)
    {
        if (mesh.vertexMemory == VK_NULL_HANDLE || mesh.vertexCount == 0 || cachedLight == packedLight)
        {
            return;
        }

        const VkDeviceSize size = sizeof(PlayerVertex) * mesh.vertexCount;
        void* data = nullptr;
        vkMapMemory(device(), mesh.vertexMemory, 0, size, 0, &data);
        PlayerVertex* vertices = static_cast<PlayerVertex*>(data);
        for (uint32_t i = 0; i < mesh.vertexCount; ++i)
        {
            vertices[i].packedLight = packedLight;
        }
        vkUnmapMemory(device(), mesh.vertexMemory);
        cachedLight = packedLight;
    }

    void PlayerMeshRenderPath::update(
        Vec3 playerPosition,
        float playerYaw,
        float playerHeadYaw,
        float playerHeadPitch,
        float playerWalkPhase,
        float playerWalkAmount,
        bool playerWalkReverse,
        bool playerCrouching,
        bool playerSprinting,
        bool playerProne,
        float animationSeconds,
        uint32_t frameIndex,
        uint8_t packedLight)
    {
        if (!ready())
        {
            return;
        }
        updateMeshLight(mesh_, packedLight, meshPackedLight_);

        const std::array<float, 16> headRotation = multiplyMatrix(rotationY(playerHeadYaw), rotationX(-playerHeadPitch));
        const bool moving = playerWalkAmount > 0.001f;
        const char* idleClipName = playerProne
            ? "proneidle"
            : (playerCrouching ? "crouchidle" : "idle");
        const char* movementClipName = playerProne
            ? "pronewalk"
            : (playerCrouching ? "crouchwalk" : (playerSprinting ? "run" : "walk"));
        const PlayerAnimationClip* idleClip = findClip(animations_, idleClipName);
        if (idleClip == nullptr)
        {
            idleClip = findClip(animations_, "idle");
        }
        const PlayerAnimationClip* movementClip = findClip(animations_, movementClipName);
        if (movementClip == nullptr)
        {
            movementClip = findClip(animations_, "walk");
        }
        const bool animationProvidesPronePosture = playerProne &&
            (findClip(animations_, "proneidle") != nullptr || findClip(animations_, "pronewalk") != nullptr);
        const std::string targetAnimationStateKey = std::string(idleClipName) + "|" + movementClipName;
        const std::vector<PlayerAnimationNodePose> idlePose = sampleClip(nodes_, idleClip, animationSeconds);
        float movementTime = 0.0f;
        if (movementClip != nullptr && movementClip->duration > 0.0f)
        {
            float normalizedPhase = std::fmod(playerWalkPhase / TwoPi, 1.0f);
            if (normalizedPhase < 0.0f)
            {
                normalizedPhase += 1.0f;
            }
            if (playerWalkReverse)
            {
                normalizedPhase = normalizedPhase <= 0.0f ? 0.0f : 1.0f - normalizedPhase;
            }
            movementTime = normalizedPhase * movementClip->duration;
        }
        const std::vector<PlayerAnimationNodePose> movementPose = sampleClip(nodes_, movementClip, movementTime);
        const float movementBlend = moving
            ? std::clamp(playerWalkAmount * ((playerCrouching || playerProne) ? 3.0f : 1.0f), 0.0f, 1.0f)
            : 0.0f;
        std::vector<PlayerAnimationNodePose> animationPose =
            animationPoseFromIdleMovement(nodes_, idlePose, movementPose, movementBlend);
        if (animationStateKey_ != targetAnimationStateKey)
        {
            if (lastAnimationPose_.size() == nodes_.size())
            {
                transitionFromAnimationPose_ = lastAnimationPose_;
                animationTransitionStartSeconds_ = animationSeconds;
                animationTransitionActive_ = true;
            }
            else
            {
                animationTransitionActive_ = false;
                transitionFromAnimationPose_.clear();
            }
            animationStateKey_ = targetAnimationStateKey;
        }
        if (animationTransitionActive_)
        {
            const float transitionAmount = (animationSeconds - animationTransitionStartSeconds_) / AnimationTransitionSeconds;
            if (transitionAmount >= 1.0f)
            {
                animationTransitionActive_ = false;
                transitionFromAnimationPose_.clear();
            }
            else
            {
                animationPose = blendPoseVectors(nodes_, transitionFromAnimationPose_, animationPose, transitionAmount);
            }
        }
        lastAnimationPose_ = animationPose;
        const std::vector<std::array<float, 16>> localTransforms =
            localTransformsFromPose(nodes_, animationPose, &headRotation);
        const std::vector<std::array<float, 16>> worldTransforms = nodeWorldTransforms(nodes_, localTransforms);

        const Vec3 forward{std::cos(playerYaw), 0.0f, std::sin(playerYaw)};
        const Vec3 right{std::sin(playerYaw), 0.0f, -std::cos(playerYaw)};
        Vec3 modelOrigin = playerPosition;
        std::array<float, 16> postureTransform = identityMatrix();
        if (playerProne && !animationProvidesPronePosture)
        {
            modelOrigin.x -= forward.x * (PlayerStandingHeight * 0.5f);
            modelOrigin.y += PlayerProneColliderHeight * 0.5f;
            modelOrigin.z -= forward.z * (PlayerStandingHeight * 0.5f);
            postureTransform = rotationX(-1.5707963268f);
        }
        const std::array<float, 16> worldBasis = basisMatrix(
            {right.x * PlayerModelScale, right.y * PlayerModelScale, right.z * PlayerModelScale},
            {0.0f, PlayerModelScale, 0.0f},
            {-forward.x * PlayerModelScale, -forward.y * PlayerModelScale, -forward.z * PlayerModelScale},
            modelOrigin);

        std::vector<std::array<float, 16>> transforms;
        transforms.reserve(worldTransforms.size());
        for (const std::array<float, 16>& nodeTransform : worldTransforms)
        {
            transforms.push_back(multiplyMatrix(worldBasis, multiplyMatrix(postureTransform, nodeTransform)));
        }
        leftItemAttachment_ = itemAttachmentFromNode(transforms, leftItemAttachmentNodeIndex_);
        rightItemAttachment_ = itemAttachmentFromNode(transforms, rightItemAttachmentNodeIndex_);
        updateTransformFrame(transformFrames_, transforms, frameIndex);
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

        std::vector<PlayerVertex> initialVertices;
        initialVertices.reserve(firstPersonHandSourceVertices_.size());
        for (const PlayerModelVertex& source : firstPersonHandSourceVertices_)
        {
            initialVertices.push_back(playerVertexFromModelVertex(source));
        }
        createMeshBuffers(firstPersonHandMesh_, initialVertices, firstPersonHandIndices_);
        createTransformFrames(firstPersonHandTransformFrames_, nodes_.size());
    }

    void PlayerMeshRenderPath::updateFirstPersonHand(
        const Camera& camera,
        Vec3 cameraPosition,
        const config::ViewmodelHandConfig& config,
        uint32_t frameIndex,
        uint8_t packedLight)
    {
        if (!firstPersonHandReady())
        {
            return;
        }
        updateMeshLight(firstPersonHandMesh_, packedLight, firstPersonHandPackedLight_);

        const std::vector<PlayerAnimationNodePose> neutralPose = sampleClip(nodes_, nullptr, 0.0f);
        const std::vector<std::array<float, 16>> localTransforms =
            localTransformsFromPose(nodes_, neutralPose, nullptr);
        const std::vector<std::array<float, 16>> worldTransforms = nodeWorldTransforms(nodes_, localTransforms);
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
        const std::array<float, 16> viewmodelBasis = basisMatrix(handRight, handUp, handForward, anchor);
        const std::array<float, 16> mirrorScale = scaleMatrix(-config.scale, config.scale, config.scale);
        const std::array<float, 16> originOffset = translationMatrix({-origin.x, -origin.y, -origin.z});
        const std::array<float, 16> modelScale = scaleMatrix(PlayerModelScale, PlayerModelScale, PlayerModelScale);
        const std::array<float, 16> viewmodelTransform =
            multiplyMatrix(viewmodelBasis, multiplyMatrix(rotation, multiplyMatrix(mirrorScale, multiplyMatrix(originOffset, modelScale))));

        std::vector<std::array<float, 16>> transforms;
        transforms.reserve(worldTransforms.size());
        for (const std::array<float, 16>& nodeTransform : worldTransforms)
        {
            transforms.push_back(multiplyMatrix(viewmodelTransform, nodeTransform));
        }
        updateTransformFrame(firstPersonHandTransformFrames_, transforms, frameIndex);
    }

    void PlayerMeshRenderPath::draw(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const Texture& texture, uint32_t frameIndex) const
    {
        if (!ready())
        {
            return;
        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout, 0, 1, &texture.descriptorSet, 0, nullptr);
        const VkDescriptorSet transformSet = transformDescriptor(transformFrames_, frameIndex);
        if (transformSet != VK_NULL_HANDLE)
        {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout, 1, 1, &transformSet, 0, nullptr);
        }
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh_.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, mesh_.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, mesh_.indexCount, 1, 0, 0, 0);
    }

    void PlayerMeshRenderPath::drawFirstPersonHand(VkCommandBuffer commandBuffer, VkPipelineLayout terrainPipelineLayout, const Texture& texture, uint32_t frameIndex) const
    {
        if (!firstPersonHandReady())
        {
            return;
        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout, 0, 1, &texture.descriptorSet, 0, nullptr);
        const VkDescriptorSet transformSet = transformDescriptor(firstPersonHandTransformFrames_, frameIndex);
        if (transformSet != VK_NULL_HANDLE)
        {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout, 1, 1, &transformSet, 0, nullptr);
        }
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &firstPersonHandMesh_.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, firstPersonHandMesh_.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, firstPersonHandMesh_.indexCount, 1, 0, 0, 0);
    }

    void PlayerMeshRenderPath::destroy()
    {
        destroyMesh(mesh_);
        destroyMesh(firstPersonHandMesh_);
        destroyTransformFrames(transformFrames_);
        destroyTransformFrames(firstPersonHandTransformFrames_);
        nodes_.clear();
        animations_.clear();
        lastAnimationPose_.clear();
        transitionFromAnimationPose_.clear();
        sourceVertices_.clear();
        indices_.clear();
        firstPersonHandSourceVertices_.clear();
        firstPersonHandIndices_.clear();
        animationStateKey_.clear();
        leftItemAttachmentNodeIndex_ = -1;
        rightItemAttachmentNodeIndex_ = -1;
        leftItemAttachment_ = {};
        rightItemAttachment_ = {};
        animationTransitionStartSeconds_ = 0.0f;
        animationTransitionActive_ = false;
        meshPackedLight_ = 0xFFu;
        firstPersonHandPackedLight_ = 0xFFu;
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

    void PlayerMeshRenderPath::destroyTransformFrames(std::vector<TransformFrame>& frames)
    {
        const VkDevice logicalDevice = device();
        for (TransformFrame& frame : frames)
        {
            if (frame.descriptorSet != VK_NULL_HANDLE && descriptorPool() != VK_NULL_HANDLE)
            {
                vkFreeDescriptorSets(logicalDevice, descriptorPool(), 1, &frame.descriptorSet);
            }
            if (frame.buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(logicalDevice, frame.buffer, nullptr);
            }
            if (frame.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(logicalDevice, frame.memory, nullptr);
            }
            frame = {};
        }
        frames.clear();
    }

    VkDescriptorSet PlayerMeshRenderPath::transformDescriptor(const std::vector<TransformFrame>& frames, uint32_t frameIndex) const
    {
        if (frames.empty())
        {
            return VK_NULL_HANDLE;
        }
        return frames[static_cast<std::size_t>(frameIndex) % frames.size()].descriptorSet;
    }

    bool PlayerMeshRenderPath::ready() const
    {
        return mesh_.indexCount > 0 &&
            mesh_.vertexBuffer != VK_NULL_HANDLE &&
            mesh_.indexBuffer != VK_NULL_HANDLE &&
            !transformFrames_.empty();
    }

    bool PlayerMeshRenderPath::firstPersonHandReady() const
    {
        return firstPersonHandMesh_.indexCount > 0 &&
            firstPersonHandMesh_.vertexBuffer != VK_NULL_HANDLE &&
            firstPersonHandMesh_.indexBuffer != VK_NULL_HANDLE &&
            !firstPersonHandTransformFrames_.empty();
    }

    const PlayerMeshRenderPath::ItemAttachment& PlayerMeshRenderPath::leftItemAttachment() const
    {
        return leftItemAttachment_;
    }

    const PlayerMeshRenderPath::ItemAttachment& PlayerMeshRenderPath::rightItemAttachment() const
    {
        return rightItemAttachment_;
    }
}
