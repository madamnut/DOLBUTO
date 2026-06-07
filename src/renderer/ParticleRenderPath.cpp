#include "renderer/ParticleRenderPath.h"

#include "world/SkyLightSystem.h"

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
        constexpr std::size_t MaxSmokeParticles = 2048;
        constexpr uint32_t BlockBreakParticleCount = 24;
        constexpr float BlockBreakParticleGravity = 22.0f;
        constexpr float BlockBreakParticleDrag = 0.92f;
        constexpr float SmokeSpawnInterval = 0.32f;
        constexpr uint32_t SmokeFrameCount = 8;
        constexpr float TileSize = 0.25f;
        constexpr int FluidAmountBits = 7;
        constexpr uint16_t FluidAmountMask = (1u << FluidAmountBits) - 1u;
        constexpr uint16_t FluidWater = 1;
        constexpr uint16_t FluidFullAmount = 100;
        constexpr uint16_t FluidHeightStepAmount = 10;
        constexpr uint16_t FluidHeightLevels = 10;
        constexpr float FluidSurfaceMaxHeight = 0.8f;
        constexpr uint32_t ParticlePlacementSalt = 0x9A7D3E21u;
        constexpr uint32_t SmokePlacementSalt = 0x51A7E3D9u;

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

        uint16_t fluidId(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid >> FluidAmountBits);
        }

        uint16_t fluidAmount(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid & FluidAmountMask);
        }

        bool isWater(uint16_t fluid)
        {
            return fluidId(fluid) == FluidWater && fluidAmount(fluid) != 0;
        }

        float fluidSurfaceHeight(uint16_t amount)
        {
            const uint16_t clampedAmount = amount > FluidFullAmount ? FluidFullAmount : amount;
            const uint16_t level = static_cast<uint16_t>((clampedAmount + FluidHeightStepAmount - 1u) / FluidHeightStepAmount);
            return (static_cast<float>(level) / static_cast<float>(FluidHeightLevels)) * FluidSurfaceMaxHeight;
        }

        int chunkCoordinateForBlock(int blockCoordinate)
        {
            return static_cast<int>(std::floor(static_cast<double>(blockCoordinate) / 16.0));
        }

        float nextEmitterRandom(uint32_t& state)
        {
            state = state * 1664525u + 1013904223u;
            state ^= state >> 16u;
            return static_cast<float>(state & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
        }

        void uploadStaticQuadIndices(VkDevice device, VkDeviceMemory memory, std::size_t maxQuads)
        {
            const VkDeviceSize indexBytes = sizeof(uint32_t) * maxQuads * 6u;
            void* indexData = nullptr;
            vkMapMemory(device, memory, 0, indexBytes, 0, &indexData);
            uint32_t* indices = static_cast<uint32_t*>(indexData);
            for (std::size_t quad = 0; quad < maxQuads; ++quad)
            {
                const uint32_t baseIndex = static_cast<uint32_t>(quad * 4u);
                const std::size_t indexOffset = quad * 6u;
                indices[indexOffset] = baseIndex;
                indices[indexOffset + 1u] = baseIndex + 1u;
                indices[indexOffset + 2u] = baseIndex + 2u;
                indices[indexOffset + 3u] = baseIndex;
                indices[indexOffset + 4u] = baseIndex + 2u;
                indices[indexOffset + 5u] = baseIndex + 3u;
            }
            vkUnmapMemory(device, memory);
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

        const VkDeviceSize blockVertexBytes = sizeof(TerrainVertex) * MaxBlockBreakParticles * 4u;
        const VkDeviceSize smokeVertexBytes = sizeof(TerrainVertex) * MaxSmokeParticles * 4u;
        gpuResources().createBuffer(
            blockVertexBytes,
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
        gpuResources().createBuffer(
            smokeVertexBytes,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            smokeVertexBuffer_,
            smokeVertexMemory_);
        gpuResources().createBuffer(
            sizeof(uint32_t) * MaxSmokeParticles * 6u,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            smokeIndexBuffer_,
            smokeIndexMemory_);
        uploadStaticQuadIndices(device(), indexMemory_, MaxBlockBreakParticles);
        uploadStaticQuadIndices(device(), smokeIndexMemory_, MaxSmokeParticles);
        vkMapMemory(device(), vertexMemory_, 0, blockVertexBytes, 0, &vertexMapped_);
        vkMapMemory(device(), smokeVertexMemory_, 0, smokeVertexBytes, 0, &smokeVertexMapped_);
        vertexScratch_.reserve(MaxBlockBreakParticles * 4u);
        smokeVertexScratch_.reserve(MaxSmokeParticles * 4u);
    }

    void ParticleRenderPath::destroy()
    {
        if (device_ == nullptr || *device_ == VK_NULL_HANDLE)
        {
            vertexBuffer_ = VK_NULL_HANDLE;
            vertexMemory_ = VK_NULL_HANDLE;
            vertexMapped_ = nullptr;
            indexBuffer_ = VK_NULL_HANDLE;
            indexMemory_ = VK_NULL_HANDLE;
            smokeVertexBuffer_ = VK_NULL_HANDLE;
            smokeVertexMemory_ = VK_NULL_HANDLE;
            smokeVertexMapped_ = nullptr;
            smokeIndexBuffer_ = VK_NULL_HANDLE;
            smokeIndexMemory_ = VK_NULL_HANDLE;
            particles_.clear();
            smokeParticles_.clear();
            fireEmitters_.clear();
            vertexScratch_.clear();
            smokeVertexScratch_.clear();
            return;
        }

        VkDevice currentDevice = *device_;
        if (vertexMapped_ != nullptr && vertexMemory_ != VK_NULL_HANDLE)
        {
            vkUnmapMemory(currentDevice, vertexMemory_);
            vertexMapped_ = nullptr;
        }
        if (smokeVertexMapped_ != nullptr && smokeVertexMemory_ != VK_NULL_HANDLE)
        {
            vkUnmapMemory(currentDevice, smokeVertexMemory_);
            smokeVertexMapped_ = nullptr;
        }
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
        if (smokeVertexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(currentDevice, smokeVertexBuffer_, nullptr);
            smokeVertexBuffer_ = VK_NULL_HANDLE;
        }
        if (smokeVertexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(currentDevice, smokeVertexMemory_, nullptr);
            smokeVertexMemory_ = VK_NULL_HANDLE;
        }
        if (smokeIndexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(currentDevice, smokeIndexBuffer_, nullptr);
            smokeIndexBuffer_ = VK_NULL_HANDLE;
        }
        if (smokeIndexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(currentDevice, smokeIndexMemory_, nullptr);
            smokeIndexMemory_ = VK_NULL_HANDLE;
        }
        particles_.clear();
        smokeParticles_.clear();
        fireEmitters_.clear();
        vertexScratch_.clear();
        smokeVertexScratch_.clear();
        lastUpdateTime_ = 0.0;
    }

    void ParticleRenderPath::clear(double lastUpdateTime)
    {
        particles_.clear();
        smokeParticles_.clear();
        fireEmitters_.clear();
        lastUpdateTime_ = lastUpdateTime;
    }

    bool ParticleRenderPath::empty() const
    {
        return particles_.empty() && smokeParticles_.empty();
    }

    void ParticleRenderPath::registerFireEmitter(int x, int y, int z)
    {
        const auto it = std::find_if(fireEmitters_.begin(), fireEmitters_.end(), [&](const FireEmitter& emitter)
        {
            return emitter.x == x && emitter.y == y && emitter.z == z;
        });
        if (it != fireEmitters_.end())
        {
            return;
        }

        FireEmitter emitter{};
        emitter.x = x;
        emitter.y = y;
        emitter.z = z;
        emitter.randomState = worldRandomHash(x, y, z, SmokePlacementSalt);
        emitter.spawnTimer = nextEmitterRandom(emitter.randomState) * SmokeSpawnInterval;
        fireEmitters_.push_back(emitter);
    }

    void ParticleRenderPath::unregisterFireEmitter(int x, int y, int z)
    {
        fireEmitters_.erase(std::remove_if(fireEmitters_.begin(), fireEmitters_.end(), [&](const FireEmitter& emitter)
        {
            return emitter.x == x && emitter.y == y && emitter.z == z;
        }), fireEmitters_.end());
    }

    void ParticleRenderPath::removeFireEmittersForChunk(int chunkX, int chunkZ)
    {
        fireEmitters_.erase(std::remove_if(fireEmitters_.begin(), fireEmitters_.end(), [&](const FireEmitter& emitter)
        {
            return chunkCoordinateForBlock(emitter.x) == chunkX && chunkCoordinateForBlock(emitter.z) == chunkZ;
        }), fireEmitters_.end());
    }

    void ParticleRenderPath::setFireEmitterSmokeStyle(int x, int y, int z, float multiplier, uint32_t textureSet)
    {
        const float clampedMultiplier = std::max(0.1f, multiplier);
        constexpr uint32_t SmokeTextureSetCount = 3;
        const uint32_t clampedTextureSet = std::min(textureSet, SmokeTextureSetCount - 1u);
        const auto it = std::find_if(fireEmitters_.begin(), fireEmitters_.end(), [&](const FireEmitter& emitter)
        {
            return emitter.x == x && emitter.y == y && emitter.z == z;
        });
        if (it != fireEmitters_.end())
        {
            it->smokeMultiplier = clampedMultiplier;
            it->smokeTextureSet = clampedTextureSet;
            return;
        }

        registerFireEmitter(x, y, z);
        if (!fireEmitters_.empty())
        {
            fireEmitters_.back().smokeMultiplier = clampedMultiplier;
            fireEmitters_.back().smokeTextureSet = clampedTextureSet;
        }
    }

    void ParticleRenderPath::handleBlockChanged(int x, int y, int z, uint16_t previousBlock, uint16_t nextBlock, uint16_t fireBlock)
    {
        if (fireBlock == 0 || previousBlock == nextBlock)
        {
            return;
        }
        if (previousBlock == fireBlock)
        {
            unregisterFireEmitter(x, y, z);
        }
        if (nextBlock == fireBlock)
        {
            registerFireEmitter(x, y, z);
        }
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

    void ParticleRenderPath::trimSmokeForAdditional(std::size_t count)
    {
        if (smokeParticles_.size() + count <= MaxSmokeParticles)
        {
            return;
        }

        const std::size_t removeCount = std::min(smokeParticles_.size(), smokeParticles_.size() + count - MaxSmokeParticles);
        smokeParticles_.erase(smokeParticles_.begin(), smokeParticles_.begin() + static_cast<std::ptrdiff_t>(removeCount));
    }

    void ParticleRenderPath::spawnSmoke(FireEmitter& emitter)
    {
        trimSmokeForAdditional(1u);

        auto randomRange = [&](float minValue, float maxValue)
        {
            return minValue + (maxValue - minValue) * nextEmitterRandom(emitter.randomState);
        };

        SmokeParticle particle{};
        particle.position = {
            static_cast<float>(emitter.x) + randomRange(-0.40f, 0.40f),
            static_cast<float>(emitter.y) + randomRange(0.70f, 0.95f),
            static_cast<float>(emitter.z) + randomRange(-0.40f, 0.40f)
        };
        particle.velocity = {
            randomRange(-0.875f, 0.875f),
            randomRange(0.35f, 0.55f),
            randomRange(-0.875f, 0.875f)
        };
        particle.lifetime = randomRange(2.0f, 3.0f);
        particle.size = randomRange(0.8f, 1.0f);
        particle.textureSet = emitter.smokeTextureSet;
        smokeParticles_.push_back(particle);
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
        if (dt <= 0.0f)
        {
            return;
        }

        for (FireEmitter& emitter : fireEmitters_)
        {
            emitter.spawnTimer += dt;
            const float spawnInterval = SmokeSpawnInterval / std::max(0.1f, emitter.smokeMultiplier);
            while (emitter.spawnTimer >= spawnInterval)
            {
                emitter.spawnTimer -= spawnInterval;
                spawnSmoke(emitter);
            }
        }

        const float drag = std::pow(BlockBreakParticleDrag, dt * 60.0f);
        auto particleBlocked = [&](Vec3 position, float radius)
        {
            return terrainBlocks &&
                terrainBlocks(
                    DVec3{
                        static_cast<double>(position.x - radius),
                        static_cast<double>(position.y - radius),
                        static_cast<double>(position.z - radius)
                    },
                    DVec3{
                        static_cast<double>(position.x + radius),
                        static_cast<double>(position.y + radius),
                        static_cast<double>(position.z + radius)
                    });
        };
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
            if (terrainBlocks &&
                particle.velocity.y < 0.0f &&
                particle.position.y <= previousY &&
                particleBlocked(particle.position, radius))
            {
                float low = particle.position.y;
                float high = previousY;
                for (int iteration = 0; iteration < 8; ++iteration)
                {
                    const float mid = (low + high) * 0.5f;
                    if (particleBlocked(Vec3{particle.position.x, mid, particle.position.z}, radius))
                    {
                        low = mid;
                    }
                    else
                    {
                        high = mid;
                    }
                }
                particle.position.y = high;
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

        constexpr float SmokeRiseAcceleration = 0.18f;
        constexpr float SmokeMaxRiseSpeed = 0.55f;
        constexpr float SmokeHorizontalFadeDuration = 2.0f;
        auto smokeBlocked = [&](Vec3 position)
        {
            constexpr float SmokeCollisionHalfExtent = 0.001f;
            return terrainBlocks &&
                terrainBlocks(
                    DVec3{
                        static_cast<double>(position.x - SmokeCollisionHalfExtent),
                        static_cast<double>(position.y - SmokeCollisionHalfExtent),
                        static_cast<double>(position.z - SmokeCollisionHalfExtent)
                    },
                    DVec3{
                        static_cast<double>(position.x + SmokeCollisionHalfExtent),
                        static_cast<double>(position.y + SmokeCollisionHalfExtent),
                        static_cast<double>(position.z + SmokeCollisionHalfExtent)
                    });
        };
        for (SmokeParticle& particle : smokeParticles_)
        {
            particle.age += dt;
            particle.velocity.y = std::min(particle.velocity.y + SmokeRiseAcceleration * dt, SmokeMaxRiseSpeed);
            const float horizontalFade = std::clamp(1.0f - particle.age / SmokeHorizontalFadeDuration, 0.0f, 1.0f);

            auto tryMoveAxis = [&](float& coordinate, float delta, int axis)
            {
                if (delta == 0.0f)
                {
                    return;
                }

                const float target = coordinate + delta;
                Vec3 targetPosition = particle.position;
                if (axis == 0)
                {
                    targetPosition.x = target;
                }
                else if (axis == 1)
                {
                    targetPosition.y = target;
                }
                else
                {
                    targetPosition.z = target;
                }
                if (smokeBlocked(targetPosition))
                {
                    if (axis == 0)
                    {
                        particle.velocity.x = 0.0f;
                    }
                    else if (axis == 1)
                    {
                        particle.velocity.y = 0.0f;
                    }
                    else
                    {
                        particle.velocity.z = 0.0f;
                    }
                    return;
                }

                coordinate = target;
            };

            tryMoveAxis(particle.position.x, particle.velocity.x * horizontalFade * dt, 0);
            tryMoveAxis(particle.position.z, particle.velocity.z * horizontalFade * dt, 2);
            tryMoveAxis(particle.position.y, particle.velocity.y * dt, 1);
        }

        smokeParticles_.erase(std::remove_if(smokeParticles_.begin(), smokeParticles_.end(), [](const SmokeParticle& particle)
        {
            return particle.age >= particle.lifetime;
        }), smokeParticles_.end());
    }

    void ParticleRenderPath::draw(
        VkCommandBuffer commandBuffer,
        const Camera& camera,
        VkExtent2D extent,
        VkPipeline pipeline,
        VkPipelineLayout pipelineLayout,
        const Texture& terrainTexture,
        const Texture& smokeTexture,
        const PushConstants& push,
        const BreakingOverlay& overlay,
        double now,
        const TerrainCollisionFn& terrainBlocks,
        const LightSamplerFn& lightAtWorld,
        const FluidSamplerFn& fluidAtWorld)
    {
        update(now, terrainBlocks);

        const bool drawBreakingOverlay = overlay.active &&
            overlay.progress > 0.0f &&
            overlay.progress < 1.0f &&
            overlay.textureLayers != nullptr &&
            overlay.textureLayerCount > 0;
        const bool drawBlockParticles = drawBreakingOverlay || !particles_.empty();
        const bool drawSmokeParticles = !smokeParticles_.empty() && smokeTexture.descriptorSet != VK_NULL_HANDLE;
        if ((!drawBlockParticles && !drawSmokeParticles) ||
            pipeline == VK_NULL_HANDLE ||
            pipelineLayout == VK_NULL_HANDLE ||
            vertexBuffer_ == VK_NULL_HANDLE ||
            indexBuffer_ == VK_NULL_HANDLE ||
            smokeVertexBuffer_ == VK_NULL_HANDLE ||
            smokeIndexBuffer_ == VK_NULL_HANDLE ||
            (drawBlockParticles && vertexMapped_ == nullptr) ||
            (drawSmokeParticles && smokeVertexMapped_ == nullptr) ||
            (drawBlockParticles && terrainTexture.descriptorSet == VK_NULL_HANDLE))
        {
            return;
        }

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

        vertexScratch_.clear();
        if (vertexScratch_.capacity() < MaxBlockBreakParticles * 4u)
        {
            vertexScratch_.reserve(MaxBlockBreakParticles * 4u);
        }

        auto waterTintAt = [&](Vec3 position, float particleHeight)
        {
            if (!fluidAtWorld)
            {
                return 0.0f;
            }

            const int fluidX = blockCoordinateXz(position.x);
            const int fluidY = blockCoordinateY(position.y);
            const int fluidZ = blockCoordinateXz(position.z);
            const uint16_t fluid = fluidAtWorld(fluidX, fluidY, fluidZ);
            if (!isWater(fluid))
            {
                return 0.0f;
            }

            const bool hasWaterAbove = isWater(fluidAtWorld(fluidX, fluidY + 1, fluidZ));
            const float waterTop = static_cast<float>(fluidY) + (hasWaterAbove ? 1.0f : fluidSurfaceHeight(fluidAmount(fluid)));
            if (position.y >= waterTop)
            {
                return 0.0f;
            }
            return hasWaterAbove ? 1.0f : std::clamp((waterTop - position.y) / std::max(particleHeight, 0.01f), 0.0f, 1.0f);
        };

        auto appendQuad = [&](std::vector<TerrainVertex>& targetVertices, std::size_t maxParticles, const std::array<Vec3, 4>& positions, float u0, float v0, float u1, float v1, float ao, uint32_t textureLayer, float mipDistanceScale, float alphaBlend, uint8_t packedLight, float waterTint)
        {
            if (targetVertices.size() + 4u > maxParticles * 4u)
            {
                return;
            }

            const uint32_t baseIndex = static_cast<uint32_t>(targetVertices.size());
            const float layer = static_cast<float>(textureLayer);
            targetVertices.push_back({positions[0].x, positions[0].y, positions[0].z, u0, v1, ao, layer, mipDistanceScale});
            targetVertices.push_back({positions[1].x, positions[1].y, positions[1].z, u0, v0, ao, layer, mipDistanceScale});
            targetVertices.push_back({positions[2].x, positions[2].y, positions[2].z, u1, v0, ao, layer, mipDistanceScale});
            targetVertices.push_back({positions[3].x, positions[3].y, positions[3].z, u1, v1, ao, layer, mipDistanceScale});
            targetVertices[baseIndex].alphaBlend = alphaBlend;
            targetVertices[baseIndex + 1u].alphaBlend = alphaBlend;
            targetVertices[baseIndex + 2u].alphaBlend = alphaBlend;
            targetVertices[baseIndex + 3u].alphaBlend = alphaBlend;
            targetVertices[baseIndex].waterTint = waterTint;
            targetVertices[baseIndex + 1u].waterTint = waterTint;
            targetVertices[baseIndex + 2u].waterTint = waterTint;
            targetVertices[baseIndex + 3u].waterTint = waterTint;
            targetVertices[baseIndex].packedLight = packedLight;
            targetVertices[baseIndex + 1u].packedLight = packedLight;
            targetVertices[baseIndex + 2u].packedLight = packedLight;
            targetVertices[baseIndex + 3u].packedLight = packedLight;
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
            const uint8_t overlayLight = lightAtWorld ? lightAtWorld(overlay.x, overlay.y + 1, overlay.z) : world::packLight(world::MaxSkyLight, 0);
            const float overlayWaterTint = waterTintAt(Vec3{static_cast<float>(overlay.x), static_cast<float>(overlay.y) + 0.5f, static_cast<float>(overlay.z)}, 1.0f);
            appendQuad(vertexScratch_, MaxBlockBreakParticles, {Vec3{minX, maxY, minZ}, Vec3{minX, maxY, maxZ}, Vec3{maxX, maxY, maxZ}, Vec3{maxX, maxY, minZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f, 1.0f, overlayLight, overlayWaterTint);
            appendQuad(vertexScratch_, MaxBlockBreakParticles, {Vec3{minX, minY, maxZ}, Vec3{minX, minY, minZ}, Vec3{maxX, minY, minZ}, Vec3{maxX, minY, maxZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f, 1.0f, overlayLight, overlayWaterTint);
            appendQuad(vertexScratch_, MaxBlockBreakParticles, {Vec3{minX, minY, maxZ}, Vec3{minX, maxY, maxZ}, Vec3{minX, maxY, minZ}, Vec3{minX, minY, minZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f, 1.0f, overlayLight, overlayWaterTint);
            appendQuad(vertexScratch_, MaxBlockBreakParticles, {Vec3{maxX, minY, minZ}, Vec3{maxX, maxY, minZ}, Vec3{maxX, maxY, maxZ}, Vec3{maxX, minY, maxZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f, 1.0f, overlayLight, overlayWaterTint);
            appendQuad(vertexScratch_, MaxBlockBreakParticles, {Vec3{minX, minY, minZ}, Vec3{minX, maxY, minZ}, Vec3{maxX, maxY, minZ}, Vec3{maxX, minY, minZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f, 1.0f, overlayLight, overlayWaterTint);
            appendQuad(vertexScratch_, MaxBlockBreakParticles, {Vec3{maxX, minY, maxZ}, Vec3{maxX, maxY, maxZ}, Vec3{minX, maxY, maxZ}, Vec3{minX, minY, maxZ}}, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, layer, 0.0f, 1.0f, overlayLight, overlayWaterTint);
        }

        const Vec3 cameraRight = camera.right();
        const Vec3 right{-cameraRight.x, -cameraRight.y, -cameraRight.z};
        const Vec3 forward = camera.forward();
        const Vec3 terrainForward{forward.x, -forward.y, forward.z};
        const Vec3 up = normalize(cross(terrainForward, right));
        const std::size_t remainingQuads = MaxBlockBreakParticles - std::min(MaxBlockBreakParticles, vertexScratch_.size() / 4u);
        const std::size_t particleCount = std::min(particles_.size(), remainingQuads);
        for (std::size_t i = 0; i < particleCount; ++i)
        {
            const BlockBreakParticle& particle = particles_[i];
            const float half = particle.size * 0.5f;
            const Vec3 rightOffset{right.x * half, right.y * half, right.z * half};
            const Vec3 upOffset{up.x * half, up.y * half, up.z * half};
            const float ao = std::clamp(1.0f - particle.age / particle.lifetime * 0.25f, 0.75f, 1.0f);
            const uint8_t particleLight = lightAtWorld
                ? lightAtWorld(blockCoordinateXz(particle.position.x), blockCoordinateY(particle.position.y), blockCoordinateXz(particle.position.z))
                : world::packLight(world::MaxSkyLight, 0);
            const float particleWaterTint = waterTintAt(particle.position, particle.size);
            appendQuad(vertexScratch_, MaxBlockBreakParticles, {
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
                1.0f,
                1.0f,
                particleLight,
                particleWaterTint);
        }

        if (!vertexScratch_.empty())
        {
            const VkDeviceSize vertexBytes = sizeof(TerrainVertex) * vertexScratch_.size();
            std::memcpy(vertexMapped_, vertexScratch_.data(), static_cast<std::size_t>(vertexBytes));

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &terrainTexture.descriptorSet, 0, nullptr);
            const VkDeviceSize vertexOffset = 0;
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer_, &vertexOffset);
            vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>((vertexScratch_.size() / 4u) * 6u), 1, 0, 0, 0);
        }

        if (!drawSmokeParticles)
        {
            return;
        }

        smokeVertexScratch_.clear();
        if (smokeVertexScratch_.capacity() < MaxSmokeParticles * 4u)
        {
            smokeVertexScratch_.reserve(MaxSmokeParticles * 4u);
        }
        const std::size_t smokeCount = std::min(smokeParticles_.size(), MaxSmokeParticles);
        for (std::size_t i = 0; i < smokeCount; ++i)
        {
            const SmokeParticle& particle = smokeParticles_[i];
            const float lifeRatio = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
            const float fadeIn = std::clamp(lifeRatio / 0.18f, 0.0f, 1.0f);
            const float fadeOut = std::clamp((1.0f - lifeRatio) / 0.45f, 0.0f, 1.0f);
            const float alpha = fadeIn * fadeOut * 0.55f;
            if (alpha <= 0.01f)
            {
                continue;
            }

            const uint32_t frame = std::min<uint32_t>(SmokeFrameCount - 1u, static_cast<uint32_t>(std::floor(lifeRatio * static_cast<float>(SmokeFrameCount))));
            const uint32_t textureLayer = particle.textureSet * SmokeFrameCount + frame;
            const float half = particle.size * 0.5f;
            const Vec3 rightOffset{right.x * half, right.y * half, right.z * half};
            const Vec3 upOffset{up.x * half, up.y * half, up.z * half};
            const uint8_t particleLight = lightAtWorld
                ? lightAtWorld(blockCoordinateXz(particle.position.x), blockCoordinateY(particle.position.y), blockCoordinateXz(particle.position.z))
                : world::packLight(world::MaxSkyLight, 0);
            const float particleWaterTint = waterTintAt(particle.position, particle.size);
            appendQuad(smokeVertexScratch_, MaxSmokeParticles, {
                Vec3{particle.position.x - rightOffset.x - upOffset.x, particle.position.y - rightOffset.y - upOffset.y, particle.position.z - rightOffset.z - upOffset.z},
                Vec3{particle.position.x - rightOffset.x + upOffset.x, particle.position.y - rightOffset.y + upOffset.y, particle.position.z - rightOffset.z + upOffset.z},
                Vec3{particle.position.x + rightOffset.x + upOffset.x, particle.position.y + rightOffset.y + upOffset.y, particle.position.z + rightOffset.z + upOffset.z},
                Vec3{particle.position.x + rightOffset.x - upOffset.x, particle.position.y - upOffset.y + rightOffset.y, particle.position.z + rightOffset.z - upOffset.z}},
                0.0f,
                0.0f,
                1.0f,
                1.0f,
                1.0f,
                textureLayer,
                1.0f,
                alpha,
                particleLight,
                particleWaterTint);
        }

        if (smokeVertexScratch_.empty())
        {
            return;
        }

        const VkDeviceSize smokeVertexBytes = sizeof(TerrainVertex) * smokeVertexScratch_.size();
        std::memcpy(smokeVertexMapped_, smokeVertexScratch_.data(), static_cast<std::size_t>(smokeVertexBytes));

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &smokeTexture.descriptorSet, 0, nullptr);
        const VkDeviceSize smokeVertexOffset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &smokeVertexBuffer_, &smokeVertexOffset);
        vkCmdBindIndexBuffer(commandBuffer, smokeIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>((smokeVertexScratch_.size() / 4u) * 6u), 1, 0, 0, 0);
    }
}
