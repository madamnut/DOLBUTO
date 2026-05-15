#include "renderer/ParticleRenderPath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <random>
#include <stdexcept>

namespace dolbuto
{
    namespace
    {
        constexpr std::size_t MaxBlockBreakParticles = 2048;
        constexpr uint32_t BlockBreakParticleCount = 24;
        constexpr float BlockBreakParticleGravity = 22.0f;
        constexpr float BlockBreakParticleDrag = 0.92f;
        constexpr float TileSize = 0.25f;
        constexpr uint32_t ParticlePlacementSalt = 0x9A7D3E21u;

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

        int blockCoordinateXz(double worldCoordinate)
        {
            return static_cast<int>(std::floor(worldCoordinate + 0.5));
        }

        int blockCoordinateY(double worldCoordinate)
        {
            return static_cast<int>(std::floor(worldCoordinate));
        }
    }

    ParticleRenderPath::ParticleRenderPath(const VkDevice* device, VulkanResourceManager* gpuResources)
        : device_(device),
        gpuResources_(gpuResources)
    {
    }

    void ParticleRenderPath::setHandles(const VkDevice* device, VulkanResourceManager* gpuResources)
    {
        device_ = device;
        gpuResources_ = gpuResources;
    }

    VkDevice ParticleRenderPath::device() const
    {
        if (device_ == nullptr || *device_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ParticleRenderPath device handle is not initialized.");
        }
        return *device_;
    }

    VulkanResourceManager& ParticleRenderPath::gpuResources() const
    {
        if (gpuResources_ == nullptr)
        {
            throw std::runtime_error("ParticleRenderPath GPU resource manager is not initialized.");
        }
        return *gpuResources_;
    }

    void ParticleRenderPath::createBuffers()
    {
        if (vertexBuffer_ != VK_NULL_HANDLE || indexBuffer_ != VK_NULL_HANDLE)
        {
            return;
        }

        gpuResources().createBuffer(
            sizeof(TerrainVertex) * MaxBlockBreakParticles * 4u,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vertexBuffer_,
            vertexMemory_);
        gpuResources().createBuffer(
            sizeof(uint32_t) * MaxBlockBreakParticles * 6u,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            indexBuffer_,
            indexMemory_);
    }

    void ParticleRenderPath::destroy()
    {
        if (device_ == nullptr || *device_ == VK_NULL_HANDLE)
        {
            vertexBuffer_ = VK_NULL_HANDLE;
            vertexMemory_ = VK_NULL_HANDLE;
            indexBuffer_ = VK_NULL_HANDLE;
            indexMemory_ = VK_NULL_HANDLE;
            particles_.clear();
            return;
        }

        VkDevice currentDevice = *device_;
        if (vertexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(currentDevice, vertexBuffer_, nullptr);
            vertexBuffer_ = VK_NULL_HANDLE;
        }
        if (vertexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(currentDevice, vertexMemory_, nullptr);
            vertexMemory_ = VK_NULL_HANDLE;
        }
        if (indexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(currentDevice, indexBuffer_, nullptr);
            indexBuffer_ = VK_NULL_HANDLE;
        }
        if (indexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(currentDevice, indexMemory_, nullptr);
            indexMemory_ = VK_NULL_HANDLE;
        }
        particles_.clear();
        lastUpdateTime_ = 0.0;
    }

    void ParticleRenderPath::clear(double lastUpdateTime)
    {
        particles_.clear();
        lastUpdateTime_ = lastUpdateTime;
    }

    bool ParticleRenderPath::empty() const
    {
        return particles_.empty();
    }

    void ParticleRenderPath::trimForAdditional(std::size_t count)
    {
        if (particles_.size() + count <= MaxBlockBreakParticles)
        {
            return;
        }

        const std::size_t removeCount = std::min(particles_.size(), particles_.size() + count - MaxBlockBreakParticles);
        particles_.erase(particles_.begin(), particles_.begin() + static_cast<std::ptrdiff_t>(removeCount));
    }

    void ParticleRenderPath::spawnBlockBreak(int x, int y, int z, uint16_t block, uint32_t textureLayer)
    {
        trimForAdditional(BlockBreakParticleCount);

        uint32_t state = worldRandomHash(x, y, z, ParticlePlacementSalt) ^ (static_cast<uint32_t>(block) * 0x45d9f3bu);
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

        for (uint32_t i = 0; i < BlockBreakParticleCount; ++i)
        {
            const int tileX = static_cast<int>(nextRandom() * 4.0f) & 3;
            const int tileY = static_cast<int>(nextRandom() * 4.0f) & 3;

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
            particles_.push_back(particle);
        }
    }

    void ParticleRenderPath::spawnMiningParticle(const MiningHit& hit, uint32_t textureLayer)
    {
        trimForAdditional(1u);

        static thread_local std::mt19937 random{std::random_device{}()};
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        std::uniform_real_distribution<float> signedUnit(-1.0f, 1.0f);

        Vec3 normal{
            static_cast<float>(hit.previousBlockX - hit.blockX),
            static_cast<float>(hit.previousBlockY - hit.blockY),
            static_cast<float>(hit.previousBlockZ - hit.blockZ)
        };
        if (normal.x == 0.0f && normal.y == 0.0f && normal.z == 0.0f)
        {
            normal = {0.0f, 1.0f, 0.0f};
        }
        normal = normalize(normal);

        Vec3 tangentA = std::abs(normal.y) > 0.5f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
        Vec3 tangentB = normalize(cross(normal, tangentA));
        tangentA = normalize(cross(tangentB, normal));

        const float localA = signedUnit(random) * 0.42f;
        const float localB = signedUnit(random) * 0.42f;
        const int tileX = static_cast<int>(unit(random) * 4.0f) & 3;
        const int tileY = static_cast<int>(unit(random) * 4.0f) & 3;

        BlockBreakParticle particle{};
        particle.position = {
            static_cast<float>(hit.blockX) + normal.x * 0.51f + tangentA.x * localA + tangentB.x * localB,
            static_cast<float>(hit.blockY) + 0.5f + normal.y * 0.51f + tangentA.y * localA + tangentB.y * localB,
            static_cast<float>(hit.blockZ) + normal.z * 0.51f + tangentA.z * localA + tangentB.z * localB
        };
        particle.velocity = {
            normal.x * 1.6f + signedUnit(random) * 0.35f,
            normal.y * 1.6f + 1.0f + unit(random) * 0.8f,
            normal.z * 1.6f + signedUnit(random) * 0.35f
        };
        particle.lifetime = 0.28f + unit(random) * 0.18f;
        particle.size = 0.08f + unit(random) * 0.04f;
        particle.textureLayer = textureLayer;
        particle.u0 = static_cast<float>(tileX) * TileSize;
        particle.v0 = static_cast<float>(tileY) * TileSize;
        particle.u1 = particle.u0 + TileSize;
        particle.v1 = particle.v0 + TileSize;
        particles_.push_back(particle);
    }

    void ParticleRenderPath::update(double now, const TerrainCollisionFn& terrainBlocks)
    {
        if (lastUpdateTime_ <= 0.0)
        {
            lastUpdateTime_ = now;
            return;
        }

        const float dt = static_cast<float>(std::clamp(now - lastUpdateTime_, 0.0, 0.05));
        lastUpdateTime_ = now;
        if (dt <= 0.0f || particles_.empty())
        {
            return;
        }

        const float drag = std::pow(BlockBreakParticleDrag, dt * 60.0f);
        for (BlockBreakParticle& particle : particles_)
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
            if (terrainBlocks &&
                particle.velocity.y < 0.0f &&
                particle.position.y <= previousY &&
                terrainBlocks(groundX, groundY, groundZ))
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

        particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const BlockBreakParticle& particle)
        {
            return particle.age >= particle.lifetime;
        }), particles_.end());
    }

    void ParticleRenderPath::draw(
        VkCommandBuffer commandBuffer,
        const Camera& camera,
        VkExtent2D extent,
        VkPipeline pipeline,
        VkPipelineLayout pipelineLayout,
        const Texture& terrainTexture,
        const PushConstants& push,
        const BreakingOverlay& overlay,
        double now,
        const TerrainCollisionFn& terrainBlocks)
    {
        update(now, terrainBlocks);

        const bool drawBreakingOverlay = overlay.active &&
            overlay.progress > 0.0f &&
            overlay.progress < 1.0f &&
            overlay.textureLayers != nullptr &&
            overlay.textureLayerCount > 0;
        if ((!drawBreakingOverlay && particles_.empty()) ||
            pipeline == VK_NULL_HANDLE ||
            pipelineLayout == VK_NULL_HANDLE ||
            vertexBuffer_ == VK_NULL_HANDLE ||
            indexBuffer_ == VK_NULL_HANDLE ||
            terrainTexture.descriptorSet == VK_NULL_HANDLE)
        {
            return;
        }

        std::vector<TerrainVertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(MaxBlockBreakParticles * 4u);
        indices.reserve(MaxBlockBreakParticles * 6u);

        auto appendQuad = [&](const std::array<Vec3, 4>& positions, float u0, float v0, float u1, float v1, float ao, uint32_t textureLayer, float mipDistanceScale)
        {
            if (vertices.size() + 4u > MaxBlockBreakParticles * 4u)
            {
                return;
            }

            const uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
            const float layer = static_cast<float>(textureLayer);
            vertices.push_back({positions[0].x, positions[0].y, positions[0].z, u0, v1, ao, layer, mipDistanceScale});
            vertices.push_back({positions[1].x, positions[1].y, positions[1].z, u0, v0, ao, layer, mipDistanceScale});
            vertices.push_back({positions[2].x, positions[2].y, positions[2].z, u1, v0, ao, layer, mipDistanceScale});
            vertices.push_back({positions[3].x, positions[3].y, positions[3].z, u1, v1, ao, layer, mipDistanceScale});
            indices.push_back(baseIndex);
            indices.push_back(baseIndex + 1u);
            indices.push_back(baseIndex + 2u);
            indices.push_back(baseIndex);
            indices.push_back(baseIndex + 2u);
            indices.push_back(baseIndex + 3u);
        };

        if (drawBreakingOverlay)
        {
            const std::size_t stage = std::min(
                overlay.textureLayerCount - 1u,
                static_cast<std::size_t>(std::floor(overlay.progress * static_cast<float>(overlay.textureLayerCount))));
            const uint32_t layer = overlay.textureLayers[stage];
            constexpr float Expand = 0.006f;
            const float minX = static_cast<float>(overlay.x) - 0.5f - Expand;
            const float maxX = static_cast<float>(overlay.x) + 0.5f + Expand;
            const float minY = static_cast<float>(overlay.y) - Expand;
            const float maxY = static_cast<float>(overlay.y + 1) + Expand;
            const float minZ = static_cast<float>(overlay.z) - 0.5f - Expand;
            const float maxZ = static_cast<float>(overlay.z) + 0.5f + Expand;
            appendQuad({Vec3{minX, maxY, minZ}, Vec3{minX, maxY, maxZ}, Vec3{maxX, maxY, maxZ}, Vec3{maxX, maxY, minZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f);
            appendQuad({Vec3{minX, minY, maxZ}, Vec3{minX, minY, minZ}, Vec3{maxX, minY, minZ}, Vec3{maxX, minY, maxZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f);
            appendQuad({Vec3{minX, minY, maxZ}, Vec3{minX, maxY, maxZ}, Vec3{minX, maxY, minZ}, Vec3{minX, minY, minZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f);
            appendQuad({Vec3{maxX, minY, minZ}, Vec3{maxX, maxY, minZ}, Vec3{maxX, maxY, maxZ}, Vec3{maxX, minY, maxZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f);
            appendQuad({Vec3{minX, minY, minZ}, Vec3{minX, maxY, minZ}, Vec3{maxX, maxY, minZ}, Vec3{maxX, minY, minZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f);
            appendQuad({Vec3{maxX, minY, maxZ}, Vec3{maxX, maxY, maxZ}, Vec3{minX, maxY, maxZ}, Vec3{minX, minY, maxZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f);
        }

        const Vec3 cameraRight = camera.right();
        const Vec3 right{-cameraRight.x, -cameraRight.y, -cameraRight.z};
        const Vec3 forward = camera.forward();
        const Vec3 terrainForward{forward.x, -forward.y, forward.z};
        const Vec3 up = normalize(cross(terrainForward, right));
        const std::size_t remainingQuads = MaxBlockBreakParticles - std::min(MaxBlockBreakParticles, vertices.size() / 4u);
        const std::size_t particleCount = std::min(particles_.size(), remainingQuads);
        for (std::size_t i = 0; i < particleCount; ++i)
        {
            const BlockBreakParticle& particle = particles_[i];
            const float half = particle.size * 0.5f;
            const Vec3 rightOffset{right.x * half, right.y * half, right.z * half};
            const Vec3 upOffset{up.x * half, up.y * half, up.z * half};
            const float ao = std::clamp(1.0f - particle.age / particle.lifetime * 0.25f, 0.75f, 1.0f);
            appendQuad({
                Vec3{particle.position.x - rightOffset.x - upOffset.x, particle.position.y - rightOffset.y - upOffset.y, particle.position.z - rightOffset.z - upOffset.z},
                Vec3{particle.position.x - rightOffset.x + upOffset.x, particle.position.y - rightOffset.y + upOffset.y, particle.position.z - rightOffset.z + upOffset.z},
                Vec3{particle.position.x + rightOffset.x + upOffset.x, particle.position.y + rightOffset.y + upOffset.y, particle.position.z + rightOffset.z + upOffset.z},
                Vec3{particle.position.x + rightOffset.x - upOffset.x, particle.position.y - upOffset.y + rightOffset.y, particle.position.z + rightOffset.z - upOffset.z}},
                particle.u0,
                particle.v0,
                particle.u1,
                particle.v1,
                ao,
                particle.textureLayer,
                1.0f);
        }

        if (indices.empty())
        {
            return;
        }

        const VkDeviceSize vertexBytes = sizeof(TerrainVertex) * vertices.size();
        const VkDeviceSize indexBytes = sizeof(uint32_t) * indices.size();
        void* vertexData = nullptr;
        vkMapMemory(device(), vertexMemory_, 0, vertexBytes, 0, &vertexData);
        std::memcpy(vertexData, vertices.data(), static_cast<std::size_t>(vertexBytes));
        vkUnmapMemory(device(), vertexMemory_);

        void* indexData = nullptr;
        vkMapMemory(device(), indexMemory_, 0, indexBytes, 0, &indexData);
        std::memcpy(indexData, indices.data(), static_cast<std::size_t>(indexBytes));
        vkUnmapMemory(device(), indexMemory_);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = extent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &terrainTexture.descriptorSet, 0, nullptr);
        const VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer_, &vertexOffset);
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    }
}
