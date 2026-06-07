#include "game/ClientRuntime.h"

#include "audio/AudioSystem.h"
#include "game/ClientRenderRuntime.h"
#include "game/ClientRuntimeState.h"
#include "gameplay/BlockInteractionSystem.h"
#include "world/Biome.h"
#include "world/BlockCollisionShape.h"
#include "world/ClimateSystem.h"
#include "world/TerrainBuilder.h"
#include "world/WorldRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace dolbuto::game
{
    namespace
    {
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeZ = 16;
        constexpr int TerrainTilePeriod = 65536;
        constexpr int WorldSizeBlocks = TerrainTilePeriod;
        constexpr int FluidAmountBits = 7;
        constexpr uint16_t FluidAmountMask = (1u << FluidAmountBits) - 1u;
        constexpr uint16_t FluidWater = 1;
        constexpr uint16_t FluidFullAmount = 100;
        constexpr uint16_t FluidHeightStepAmount = 10;
        constexpr uint16_t FluidHeightLevels = 10;
        constexpr double FluidSurfaceMaxHeight = 0.8;

        uint32_t stableStringHash(const std::string& value)
        {
            uint32_t hash = 2166136261u;
            for (const char c : value)
            {
                hash ^= static_cast<uint8_t>(c);
                hash *= 16777619u;
            }
            return hash;
        }

        int positiveModulo(int value, int divisor)
        {
            return world::WorldRuntime::positiveModulo(value, divisor);
        }

        int wrapBlockCoordinate(int value)
        {
            return positiveModulo(value, WorldSizeBlocks);
        }

        int floorDiv(int value, int divisor)
        {
            return world::WorldRuntime::floorDiv(value, divisor);
        }

        int blockCoordinateXz(double worldCoordinate)
        {
            return world::WorldRuntime::blockCoordinateXz(worldCoordinate);
        }

        int blockCoordinateY(double worldCoordinate)
        {
            return world::WorldRuntime::blockCoordinateY(worldCoordinate);
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

        double fluidSurfaceHeight(uint16_t amount)
        {
            const uint16_t clampedAmount = std::min<uint16_t>(amount, FluidFullAmount);
            if (clampedAmount == 0)
            {
                return 0.0;
            }

            const uint16_t level = static_cast<uint16_t>((clampedAmount + FluidHeightStepAmount - 1u) / FluidHeightStepAmount);
            return (static_cast<double>(level) / static_cast<double>(FluidHeightLevels)) * FluidSurfaceMaxHeight;
        }

        uint64_t chunkKey(int chunkX, int chunkZ)
        {
            return world::WorldRuntime::chunkKey(chunkX, chunkZ);
        }

        const BlockDefinition& blockDefinition(const ClientRuntimeState& state, uint16_t block)
        {
            static const BlockDefinition fallback{};
            if (static_cast<std::size_t>(block) >= state.content.blockDefinitions().size())
            {
                return fallback;
            }
            return state.content.blockDefinitions()[block];
        }

        uint16_t blockAtWorld(const ClientRuntimeState& state, int x, int y, int z)
        {
            return state.worldRuntime.blockAtWorld(x, y, z);
        }

        struct ClimateDebugSample
        {
            float temperature = 0.0f;
            float precipitation = 0.0f;
        };

        bool terrainCellBlocksPlayer(const ClientRuntimeState& state, int x, int y, int z)
        {
            return state.worldRuntime.terrainCellBlocksPlayer(
                x,
                y,
                z,
                [&state](uint16_t block) -> const BlockDefinition&
                {
                    return blockDefinition(state, block);
                });
        }

        bool terrainCellIntersectsPlayerAabb(const ClientRuntimeState& state, int x, int y, int z, DVec3 min, DVec3 max)
        {
            return state.worldRuntime.terrainCellIntersectsAabb(
                x,
                y,
                z,
                min,
                max,
                [&state](uint16_t block) -> const BlockDefinition&
                {
                    return blockDefinition(state, block);
                });
        }

        double playerTerrainClimbHeight(const ClientRuntimeState& state, DVec3 playerPosition, double heightScale, double maxHeight)
        {
            if (maxHeight <= 0.0)
            {
                return 0.0;
            }

            constexpr double HalfWidth = 0.3;
            constexpr double Height = 1.75;
            constexpr double Epsilon = 0.000001;
            const double scaledHeight = std::max(0.1, Height * heightScale);

            const DVec3 min{playerPosition.x - HalfWidth, playerPosition.y, playerPosition.z - HalfWidth};
            const DVec3 max{playerPosition.x + HalfWidth, playerPosition.y + scaledHeight, playerPosition.z + HalfWidth};

            const int blockMinX = blockCoordinateXz(min.x);
            const int blockMaxX = blockCoordinateXz(max.x - Epsilon);
            const int blockMinY = blockCoordinateY(min.y);
            const int blockMaxY = blockCoordinateY(max.y - Epsilon);
            const int blockMinZ = blockCoordinateXz(min.z);
            const int blockMaxZ = blockCoordinateXz(max.z - Epsilon);

            double bestHeight = std::numeric_limits<double>::infinity();
            auto considerAabb = [&](const world::block_visual::LocalAabb& aabb)
            {
                const DVec3 blockMin{
                    static_cast<double>(aabb.min.x),
                    static_cast<double>(aabb.min.y),
                    static_cast<double>(aabb.min.z)
                };
                const DVec3 blockMax{
                    static_cast<double>(aabb.max.x),
                    static_cast<double>(aabb.max.y),
                    static_cast<double>(aabb.max.z)
                };
                if (!world::block_collision::aabbIntersects(min, max, blockMin, blockMax))
                {
                    return;
                }

                const double requiredHeight = blockMax.y - playerPosition.y;
                if (requiredHeight > Epsilon && requiredHeight <= maxHeight + Epsilon)
                {
                    bestHeight = std::min(bestHeight, requiredHeight);
                }
            };

            for (int y = blockMinY; y <= blockMaxY; ++y)
            {
                for (int z = blockMinZ; z <= blockMaxZ; ++z)
                {
                    for (int x = blockMinX; x <= blockMaxX; ++x)
                    {
                        const uint16_t block = state.worldRuntime.blockAtWorld(x, y, z);
                        if (block == world::WorldRuntime::BlockAir)
                        {
                            continue;
                        }

                        const BlockDefinition& definition = blockDefinition(state, block);
                        if (!definition.collision)
                        {
                            continue;
                        }

                        const uint16_t blockState = state.worldRuntime.blockStateAtWorld(x, y, z);
                        if (definition.renderType == BlockRenderType::Crucible)
                        {
                            world::block_visual::forEachCrucibleWorldAabb(x, y, z, considerAabb);
                        }
                        else
                        {
                            considerAabb(world::block_collision::blockWorldAabb(x, y, z, definition, blockState));
                        }
                    }
                }
            }

            return std::isfinite(bestHeight) ? bestHeight : 0.0;
        }

        world::TerrainBuilderConfig terrainBuilderConfig(const ClientRuntimeState& state)
        {
            world::TerrainBuilderConfig config{};
            config.heightLut = state.diagnostics.heightLut;
            config.groundnessBaselineLut = state.diagnostics.groundnessBaselineLut;
            config.groundnessInfluenceLut = state.diagnostics.groundnessInfluenceLut;
            config.smoothnessInfluenceLut = state.diagnostics.smoothnessInfluenceLut;
            config.pvWeightLut = state.diagnostics.pvWeightLut;
            config.groundnessPvWeightLut = state.diagnostics.groundnessPvWeightLut;
            config.smoothnessPvWeightLut = state.diagnostics.smoothnessPvWeightLut;
            config.lightAttenuationTables = state.content.lightAttenuationTables();
            config.activeWorldSeedSalt = state.clientWorldRuntime.activeWorldSeedSalt;
            config.seaLevel = state.worldConfig.seaLevel;
            config.groundnessNoiseFeatureScale = state.worldConfig.groundnessNoiseFeatureScale;
            config.groundnessNoiseOctaveCount = state.worldConfig.groundnessNoiseOctaveCount;
            config.groundnessNoiseLacunarity = state.worldConfig.groundnessNoiseLacunarity;
            config.groundnessNoiseGain = state.worldConfig.groundnessNoiseGain;
            config.groundnessDomainWarpEnabled = state.worldConfig.groundnessDomainWarpEnabled;
            config.groundnessDomainWarpAmplitude = state.worldConfig.groundnessDomainWarpAmplitude;
            config.groundnessDomainWarpFrequency = state.worldConfig.groundnessDomainWarpFrequency;
            config.groundnessDomainWarpOctaveCount = state.worldConfig.groundnessDomainWarpOctaveCount;
            config.groundnessDomainWarpGain = state.worldConfig.groundnessDomainWarpGain;
            config.baseNoiseFeatureScale = state.worldConfig.baseNoiseFeatureScale;
            config.baseNoiseOctaveCount = state.worldConfig.baseNoiseOctaveCount;
            config.baseNoiseLacunarity = state.worldConfig.baseNoiseLacunarity;
            config.baseNoiseGain = state.worldConfig.baseNoiseGain;
            config.smoothnessNoiseFeatureScale = state.worldConfig.smoothnessNoiseFeatureScale;
            config.smoothnessNoiseOctaveCount = state.worldConfig.smoothnessNoiseOctaveCount;
            config.smoothnessNoiseLacunarity = state.worldConfig.smoothnessNoiseLacunarity;
            config.smoothnessNoiseGain = state.worldConfig.smoothnessNoiseGain;
            config.weirdnessNoiseFeatureScale = state.worldConfig.weirdnessNoiseFeatureScale;
            config.weirdnessNoiseOctaveCount = state.worldConfig.weirdnessNoiseOctaveCount;
            config.weirdnessNoiseLacunarity = state.worldConfig.weirdnessNoiseLacunarity;
            config.weirdnessNoiseGain = state.worldConfig.weirdnessNoiseGain;
            config.weirdnessDomainWarpEnabled = state.worldConfig.weirdnessDomainWarpEnabled;
            config.weirdnessDomainWarpAmplitude = state.worldConfig.weirdnessDomainWarpAmplitude;
            config.weirdnessDomainWarpFrequency = state.worldConfig.weirdnessDomainWarpFrequency;
            config.weirdnessDomainWarpOctaveCount = state.worldConfig.weirdnessDomainWarpOctaveCount;
            config.weirdnessDomainWarpGain = state.worldConfig.weirdnessDomainWarpGain;
            config.temperatureNoiseStrength = state.worldConfig.temperatureNoiseStrength;
            config.temperatureNoiseFeatureScale = state.worldConfig.temperatureNoiseFeatureScale;
            config.temperatureNoiseOctaveCount = state.worldConfig.temperatureNoiseOctaveCount;
            config.temperatureNoiseLacunarity = state.worldConfig.temperatureNoiseLacunarity;
            config.temperatureNoiseGain = state.worldConfig.temperatureNoiseGain;
            config.temperatureNoiseSimplexScale = state.worldConfig.temperatureNoiseSimplexScale;
            config.precipitationNoiseFeatureScale = state.worldConfig.precipitationNoiseFeatureScale;
            config.precipitationNoiseOctaveCount = state.worldConfig.precipitationNoiseOctaveCount;
            config.precipitationNoiseLacunarity = state.worldConfig.precipitationNoiseLacunarity;
            config.precipitationNoiseGain = state.worldConfig.precipitationNoiseGain;
            config.precipitationNoiseSimplexScale = state.worldConfig.precipitationNoiseSimplexScale;
            for (const config::WorldOreFeatureConfig& ore : state.worldConfig.oreFeatures)
            {
                if (!ore.enabled || ore.attemptsPerChunk <= 0 || ore.size <= 0)
                {
                    continue;
                }

                const auto blockIt = state.content.blockIdByName().find(ore.block);
                const auto replaceIt = state.content.blockIdByName().find(ore.replace);
                if (blockIt == state.content.blockIdByName().end() || replaceIt == state.content.blockIdByName().end())
                {
                    continue;
                }

                world::TerrainBuilderConfig::OreFeature feature{};
                feature.block = blockIt->second;
                feature.replace = replaceIt->second;
                feature.minY = ore.minY;
                feature.maxY = ore.maxY;
                feature.attemptsPerChunk = ore.attemptsPerChunk;
                feature.size = ore.size;
                feature.salt = stableStringHash(ore.name.empty() ? ore.block : ore.name);
                config.oreFeatures.push_back(feature);
            }
            const config::WorldClayDiskFeatureConfig& clay = state.worldConfig.clayDiskFeature;
            if (clay.enabled && !clay.block.empty() && !clay.replace.empty() && clay.chancePerChunk > 0.0f && clay.radiusMax > 0)
            {
                const auto blockIt = state.content.blockIdByName().find(clay.block);
                if (blockIt != state.content.blockIdByName().end())
                {
                    world::TerrainBuilderConfig::ClayDiskFeature feature{};
                    feature.enabled = true;
                    feature.block = blockIt->second;
                    feature.chancePerChunk = clay.chancePerChunk;
                    feature.radiusMin = clay.radiusMin;
                    feature.radiusMax = clay.radiusMax;
                    feature.halfHeight = clay.halfHeight;
                    feature.salt = stableStringHash("clay_disk:" + clay.block);
                    for (const std::string& replace : clay.replace)
                    {
                        const auto replaceIt = state.content.blockIdByName().find(replace);
                        if (replaceIt != state.content.blockIdByName().end())
                        {
                            feature.replace.push_back(replaceIt->second);
                        }
                    }
                    if (!feature.replace.empty())
                    {
                        config.clayDiskFeature = std::move(feature);
                    }
                }
            }
            return config;
        }

        ClimateDebugSample climateAtWorld(const ClientRuntimeState& state, int blockX, int blockZ)
        {
            const int chunkX = floorDiv(blockX, ChunkSizeX);
            const int chunkZ = floorDiv(blockZ, ChunkSizeZ);
            const int localX = positiveModulo(blockX, ChunkSizeX);
            const int localZ = positiveModulo(blockZ, ChunkSizeZ);
            const std::size_t column = static_cast<std::size_t>(localZ * ChunkSizeX + localX);

            ClimateDebugSample sample{};
            const RuntimeChunk* chunk = state.worldRuntime.find(chunkKey(chunkX, chunkZ));
            const world::TerrainBuilderConfig config = terrainBuilderConfig(state);
            const world::ClimateSystem climate(config);
            if (chunk != nullptr && chunk->data)
            {
                const ChunkData& data = *chunk->data;
                sample.temperature = world::ClimateSystem::decodeClimateValue(data.temperature[column]);
                sample.precipitation = world::ClimateSystem::decodeClimateValue(data.precipitation[column]);
                return sample;
            }

            const int wrappedX = wrapBlockCoordinate(blockX);
            const int wrappedZ = wrapBlockCoordinate(blockZ);
            const float temperatureNoise = climate.sampleTileableNoise(
                wrappedX,
                wrappedZ,
                config.temperatureNoiseFeatureScale,
                config.temperatureNoiseSimplexScale,
                config.temperatureNoiseOctaveCount,
                config.temperatureNoiseLacunarity,
                config.temperatureNoiseGain,
                climate.temperatureSeed());
            const float precipitationNoise = climate.sampleTileableNoise(
                wrappedX,
                wrappedZ,
                config.precipitationNoiseFeatureScale,
                config.precipitationNoiseSimplexScale,
                config.precipitationNoiseOctaveCount,
                config.precipitationNoiseLacunarity,
                config.precipitationNoiseGain,
                climate.precipitationSeed());
            sample.temperature = climate.temperatureAtWrapped(wrappedZ, temperatureNoise);
            sample.precipitation = climate.precipitationAtNoise(precipitationNoise);
            return sample;
        }
    }

    ClientRuntime::RenderAccess::RenderAccess(ClientRuntime& owner) :
        owner_(owner)
    {
    }

    void ClientRuntime::RenderAccess::frame(const ClientFrame& frame)
    {
        owner_.renderRuntime_->renderFrame(frame);
    }

    void ClientRuntime::RenderAccess::notifyFramebufferResized()
    {
        owner_.renderRuntime_->notifyFramebufferResized();
    }

    ClientRuntime::SceneAccess::SceneAccess(ClientRuntime& owner) :
        owner_(owner)
    {
    }

    void ClientRuntime::SceneAccess::loadGameScene(const std::filesystem::path& worldDirectory, uint64_t worldSeed)
    {
        owner_.renderRuntime_->loadGameScene(worldDirectory, worldSeed);
    }

    void ClientRuntime::SceneAccess::unloadGameScene()
    {
        owner_.renderRuntime_->unloadGameScene();
    }

    ClientRuntime::GameplayAccess::GameplayAccess(ClientRuntime& owner) :
        owner_(owner)
    {
    }

    bool ClientRuntime::GameplayAccess::playerColliderIntersectsTerrain(DVec3 playerPosition, double heightScale) const
    {
        return owner_.state_->gameplayRuntime.playerColliderIntersectsTerrain(
            playerPosition,
            heightScale,
            [this](int x, int y, int z, DVec3 min, DVec3 max)
            {
                return terrainCellIntersectsPlayerAabb(*owner_.state_, x, y, z, min, max);
            });
    }

    bool ClientRuntime::GameplayAccess::playerColliderHasSupportBelow(DVec3 playerPosition) const
    {
        return owner_.state_->gameplayRuntime.playerColliderHasSupportBelow(
            playerPosition,
            [this](int x, int y, int z, DVec3 min, DVec3 max)
            {
                return terrainCellIntersectsPlayerAabb(*owner_.state_, x, y, z, min, max);
            });
    }

    bool ClientRuntime::GameplayAccess::playerColliderIntersectsWater(DVec3 playerPosition, double heightScale) const
    {
        return owner_.state_->gameplayRuntime.playerColliderIntersectsWater(
            playerPosition,
            heightScale,
            [this](int x, int y, int z)
            {
                return owner_.state_->worldRuntime.fluidAtWorld(x, y, z);
            });
    }

    bool ClientRuntime::GameplayAccess::terrainAabbIntersects(DVec3 min, DVec3 max) const
    {
        constexpr double Epsilon = 0.000001;
        const int blockMinX = blockCoordinateXz(min.x);
        const int blockMaxX = blockCoordinateXz(max.x - Epsilon);
        const int blockMinY = blockCoordinateY(min.y);
        const int blockMaxY = blockCoordinateY(max.y - Epsilon);
        const int blockMinZ = blockCoordinateXz(min.z);
        const int blockMaxZ = blockCoordinateXz(max.z - Epsilon);

        for (int y = blockMinY; y <= blockMaxY; ++y)
        {
            for (int z = blockMinZ; z <= blockMaxZ; ++z)
            {
                for (int x = blockMinX; x <= blockMaxX; ++x)
                {
                    if (terrainCellIntersectsPlayerAabb(*owner_.state_, x, y, z, min, max))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    double ClientRuntime::GameplayAccess::playerColliderTerrainClimbHeight(DVec3 playerPosition, double heightScale, double maxHeight) const
    {
        return playerTerrainClimbHeight(*owner_.state_, playerPosition, heightScale, maxHeight);
    }

    bool ClientRuntime::GameplayAccess::pointIntersectsWater(DVec3 position) const
    {
        const int x = blockCoordinateXz(position.x);
        const int y = blockCoordinateY(position.y);
        const int z = blockCoordinateXz(position.z);
        const uint16_t fluid = owner_.state_->worldRuntime.fluidAtWorld(x, y, z);
        if (!isWater(fluid))
        {
            return false;
        }

        const bool hasWaterAbove = isWater(owner_.state_->worldRuntime.fluidAtWorld(x, y + 1, z));
        const double waterTop = static_cast<double>(y) + (hasWaterAbove ? 1.0 : fluidSurfaceHeight(fluidAmount(fluid)));
        return position.y < waterTop;
    }

    void ClientRuntime::GameplayAccess::updateBlockSelection(DVec3 origin, Vec3 direction)
    {
        gameplay::BlockRaycastHit hit{};
        owner_.state_->selection.hasSelectedBlock = gameplay::BlockInteractionSystem::raycastBlock(
            origin,
            direction,
            [this](int x, int y, int z)
            {
                return blockAtWorld(*owner_.state_, x, y, z);
            },
            [this](uint16_t block) -> const BlockDefinition&
            {
                return blockDefinition(*owner_.state_, block);
            },
            hit,
            [this](uint16_t block)
            {
                return owner_.state_->content.propMeshForBlock(block);
            },
            [this](int x, int y, int z)
            {
                return owner_.state_->worldRuntime.blockStateAtWorld(x, y, z);
            });
        if (!owner_.state_->selection.hasSelectedBlock)
        {
            return;
        }

        owner_.state_->selection.selectedBlockX = hit.blockX;
        owner_.state_->selection.selectedBlockY = hit.blockY;
        owner_.state_->selection.selectedBlockZ = hit.blockZ;
        owner_.state_->selection.selectedBlockId = blockAtWorld(*owner_.state_, hit.blockX, hit.blockY, hit.blockZ);
    }

    void ClientRuntime::GameplayAccess::updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, DVec3 playerPosition, float deltaSeconds, bool sandboxMode)
    {
        owner_.renderRuntime_->updateBlockBreaking(origin, direction, breaking, playerPosition, deltaSeconds, sandboxMode);
    }

    bool ClientRuntime::GameplayAccess::editBlockInView(DVec3 origin, Vec3 direction, bool placeBlock, uint16_t placeBlockId, DVec3 playerPosition, double playerHeightScale)
    {
        return owner_.renderRuntime_->editBlockInView(origin, direction, placeBlock, placeBlockId, playerPosition, playerHeightScale);
    }

    bool ClientRuntime::GameplayAccess::placeSelectedItemBlockInView(DVec3 origin, Vec3 direction, DVec3 playerPosition, double playerHeightScale)
    {
        return owner_.renderRuntime_->placeSelectedItemBlockInView(origin, direction, playerPosition, playerHeightScale);
    }

    bool ClientRuntime::GameplayAccess::pickupDroppedItemInView(DVec3 origin, Vec3 direction)
    {
        return owner_.renderRuntime_->pickupDroppedItemInView(origin, direction);
    }

    bool ClientRuntime::GameplayAccess::dropSelectedHotbarItem(bool wholeStack, DVec3 sourcePosition, Vec3 direction)
    {
        return owner_.renderRuntime_->dropSelectedHotbarItem(wholeStack, sourcePosition, direction);
    }

    gameplay::ItemInteractionMenu ClientRuntime::GameplayAccess::beginItemInteractionInView(
        DVec3 origin,
        Vec3 direction,
        bool preferHeldItemBlockActions)
    {
        return owner_.renderRuntime_->beginItemInteractionInView(origin, direction, preferHeldItemBlockActions);
    }

    bool ClientRuntime::GameplayAccess::executePendingItemInteraction(std::size_t actionIndex, std::size_t candidateIndex, bool repeat)
    {
        return owner_.renderRuntime_->executePendingItemInteraction(actionIndex, candidateIndex, repeat);
    }

    void ClientRuntime::GameplayAccess::cancelPendingItemInteraction()
    {
        owner_.renderRuntime_->cancelPendingItemInteraction();
    }

    void ClientRuntime::GameplayAccess::tickBlockUpdates()
    {
        owner_.renderRuntime_->tickBlockUpdates();
    }

    void ClientRuntime::GameplayAccess::tickFluidSimulation()
    {
        owner_.renderRuntime_->tickFluidSimulation();
    }

    bool ClientRuntime::GameplayAccess::tickHeldBurningItems(bool extinguishHeldBurnableLights)
    {
        return owner_.renderRuntime_->tickHeldBurningItems(extinguishHeldBurnableLights);
    }

    std::array<ItemStack, gameplay::PlayerInventory::SlotCount> ClientRuntime::GameplayAccess::inventorySnapshot() const
    {
        return owner_.state_->gameplayRuntime.inventorySnapshot();
    }

    ItemStack ClientRuntime::GameplayAccess::offhandSlot() const
    {
        return owner_.state_->gameplayRuntime.offhandSlot();
    }

    uint16_t ClientRuntime::GameplayAccess::heldPortableLightEmission() const
    {
        return owner_.state_->gameplayRuntime.heldPortableLightEmission();
    }

    void ClientRuntime::GameplayAccess::setInventorySnapshot(const std::array<ItemStack, gameplay::PlayerInventory::SlotCount>& slots)
    {
        owner_.renderRuntime_->setInventorySnapshot(slots);
    }

    void ClientRuntime::GameplayAccess::setOffhandSlot(ItemStack stack)
    {
        owner_.renderRuntime_->setOffhandSlot(stack);
    }

    bool ClientRuntime::GameplayAccess::swapSelectedHotbarWithOffhand()
    {
        return owner_.renderRuntime_->swapSelectedHotbarWithOffhand();
    }

    ClientRuntime::UiAccess::UiAccess(ClientRuntime& owner) :
        owner_(owner)
    {
    }

    void ClientRuntime::UiAccess::setWorldList(const std::vector<WorldListItem>& worlds)
    {
        owner_.state_->uiBridge.setWorldList(worlds);
    }

    void ClientRuntime::UiAccess::setHotbarSelectedSlot(int slot)
    {
        owner_.state_->uiBridge.setHotbarSelectedSlot(slot);
    }

    std::string ClientRuntime::UiAccess::inputValue(std::string_view id) const
    {
        return owner_.state_->ui.inputValue(id);
    }

    std::string ClientRuntime::UiAccess::chatInputValue() const
    {
        return owner_.state_->ui.chatInputValue();
    }

    void ClientRuntime::UiAccess::setChatVisible(bool inputVisible, bool hasMessages)
    {
        owner_.state_->ui.setChatVisible(inputVisible, hasMessages);
    }

    void ClientRuntime::UiAccess::setChatMessages(std::string_view rml)
    {
        owner_.state_->ui.setChatMessages(rml);
    }

    void ClientRuntime::UiAccess::clearChatInput()
    {
        owner_.state_->ui.clearChatInput();
    }

    void ClientRuntime::UiAccess::focusChatInput()
    {
        owner_.state_->ui.focusChatInput();
    }

    void ClientRuntime::UiAccess::setOptionsVolumes(int bgmPercent, int sfxPercent)
    {
        owner_.state_->ui.setOptionsVolumes(bgmPercent, sfxPercent);
    }

    void ClientRuntime::UiAccess::setOptionsFov(int fovDegrees)
    {
        owner_.state_->ui.setOptionsFov(fovDegrees);
    }

    void ClientRuntime::UiAccess::setOptionsViewBobbing(bool enabled)
    {
        owner_.state_->ui.setOptionsViewBobbing(enabled);
    }

    void ClientRuntime::UiAccess::setOptionsControls(bool toggleSprint, bool toggleSneak, bool toggleProne)
    {
        owner_.state_->ui.setOptionsControls(toggleSprint, toggleSneak, toggleProne);
    }

    void ClientRuntime::UiAccess::setOptionsLobbyBackground(bool lobbyBackground)
    {
        owner_.state_->ui.setOptionsLobbyBackground(lobbyBackground);
    }

    void ClientRuntime::UiAccess::setWorldCreateGameMode(bool sandbox)
    {
        owner_.state_->ui.setWorldCreateGameMode(sandbox);
    }

    void ClientRuntime::UiAccess::setPlayerStats(int hp, int maxHp, int hunger, int maxHunger, int thirst, int maxThirst)
    {
        owner_.state_->ui.setPlayerStats(hp, maxHp, hunger, maxHunger, thirst, maxThirst);
    }

    void ClientRuntime::UiAccess::setRadialMenu(
        const std::vector<gameplay::ItemInteractionActionMenu>& actions,
        std::optional<std::size_t> selectedActionIndex,
        std::optional<std::size_t> selectedCandidateIndex)
    {
        owner_.state_->uiBridge.setRadialMenu(actions, selectedActionIndex, selectedCandidateIndex);
    }

    void ClientRuntime::UiAccess::hideRadialMenu()
    {
        owner_.state_->uiBridge.hideRadialMenu();
    }

    void ClientRuntime::UiAccess::mouseMove(double x, double y)
    {
        owner_.renderRuntime_->uiMouseMove(x, y);
    }

    void ClientRuntime::UiAccess::mouseButton(int button, bool pressed, int modifiers)
    {
        owner_.renderRuntime_->uiMouseButton(button, pressed, modifiers);
    }

    void ClientRuntime::UiAccess::mouseWheel(double yOffset)
    {
        owner_.renderRuntime_->uiMouseWheel(yOffset);
    }

    void ClientRuntime::UiAccess::textInput(unsigned int codepoint)
    {
        owner_.renderRuntime_->uiTextInput(codepoint);
    }

    void ClientRuntime::UiAccess::key(int key, bool pressed, int modifiers)
    {
        owner_.renderRuntime_->uiKey(key, pressed, modifiers);
    }

    void ClientRuntime::UiAccess::closeInventoryInteraction()
    {
        owner_.renderRuntime_->closeInventoryInteraction();
    }

    bool ClientRuntime::UiAccess::available() const
    {
        return owner_.state_->ui.available();
    }

    std::optional<std::string> ClientRuntime::UiAccess::consumeAction()
    {
        return owner_.state_->ui.consumeAction();
    }

    ClientRuntime::DiagnosticsAccess::DiagnosticsAccess(ClientRuntime& owner) :
        owner_(owner)
    {
    }

    std::string ClientRuntime::DiagnosticsAccess::selectedBlockText() const
    {
        if (!owner_.state_->selection.hasSelectedBlock)
        {
            return "LOOKAT: none";
        }

        const BlockDefinition& definition = blockDefinition(*owner_.state_, owner_.state_->selection.selectedBlockId);
        return "LOOKAT: " + definition.name +
            "[" + std::to_string(owner_.state_->selection.selectedBlockId) + "] (x: " + std::to_string(wrapBlockCoordinate(owner_.state_->selection.selectedBlockX)) +
            ", y: " + std::to_string(owner_.state_->selection.selectedBlockY) +
            ", z: " + std::to_string(wrapBlockCoordinate(owner_.state_->selection.selectedBlockZ)) + ")";
    }

    std::string ClientRuntime::DiagnosticsAccess::climateText(DVec3 position) const
    {
        const int blockX = blockCoordinateXz(position.x);
        const int blockZ = blockCoordinateXz(position.z);
        const ClimateDebugSample climate = climateAtWorld(*owner_.state_, blockX, blockZ);

        std::ostringstream text;
        text << "CLIMATE: T[" << std::fixed << std::setprecision(3) << climate.temperature << "] P[" << climate.precipitation << "]";
        return text.str();
    }

    std::string ClientRuntime::DiagnosticsAccess::biomeText(DVec3 position) const
    {
        const int blockX = blockCoordinateXz(position.x);
        const int blockZ = blockCoordinateXz(position.z);
        const ClimateDebugSample climate = climateAtWorld(*owner_.state_, blockX, blockZ);
        const world::TerrainBuilder terrainBuilder(terrainBuilderConfig(*owner_.state_));
        const world::TerrainDebugSample terrainSample = terrainBuilder.sampleTerrainAtWorld(blockX, blockZ);

        const world::BiomeSample biome = world::classifyBiome(
            climate.temperature,
            climate.precipitation,
            terrainSample.groundness);

        std::ostringstream text;
        text << "BIOME: T[" << biome.temperatureBand <<
            "] P[" << biome.precipitationBand <<
            "] GND[" << biome.groundnessBand <<
            "] - " << world::biomeName(biome.id);
        return text.str();
    }

    std::string ClientRuntime::DiagnosticsAccess::terrainText(DVec3 position) const
    {
        const int blockX = blockCoordinateXz(position.x);
        const int blockZ = blockCoordinateXz(position.z);
        const world::TerrainBuilder terrainBuilder(terrainBuilderConfig(*owner_.state_));
        const world::TerrainDebugSample terrainSample = terrainBuilder.sampleTerrainAtWorld(blockX, blockZ);

        std::ostringstream text;
        text << "TERRAIN: GND[" << std::fixed << std::setprecision(3) << terrainSample.groundness <<
            "] SMTH[" << terrainSample.smoothness <<
            "] W[" << terrainSample.weirdness <<
            "] PV[" << terrainSample.pv << "]";
        text << "\nVALUE: RAW[" << terrainSample.rawTerrainValue <<
            "] NORM[" << terrainSample.normalizedTerrainValue <<
            "] PVW[" << terrainSample.pvWeight <<
            "] PVMUL[" << terrainSample.pvMultiplier <<
            "] BASE[" << terrainSample.baseline <<
            "] INF[" << terrainSample.influence <<
            "] VAL[" << terrainSample.terrainValue <<
            "] H[" << terrainSample.height << "]";
        return text.str();
    }

    std::string ClientRuntime::DiagnosticsAccess::performanceMaxText() const
    {
        const ClientPerfMaxStats& perf = owner_.state_->diagnostics.perfMax;
        std::ostringstream text;
        text << std::fixed << std::setprecision(2);
        text << "PERF MAX [R]\n";
        text << "TL_FINISH   " << std::setw(7) << perf.terrainLoadFinishMs << " ms\n";
        text << "TL_SNAPSHOT " << std::setw(7) << perf.terrainLoadSnapshotMs << " ms\n";
        text << "TL_INSTALL  " << std::setw(7) << perf.terrainLoadInstallMs << " ms\n";
        text << "TL_RESUME   " << std::setw(7) << perf.terrainLoadResumeMs << " ms\n";
        text << "TL_TOTAL    " << std::setw(7) << perf.terrainLoadHandleMs << " ms\n";
        text << "TL_COUNT    " << std::setw(7) << perf.terrainLoadCount << "\n";
        text << "TD_POP      " << std::setw(7) << perf.terrainPopMs << " ms\n";
        text << "TD_HANDLE   " << std::setw(7) << perf.terrainHandleMs << " ms\n";
        text << "TD_TERR_CNT " << std::setw(7) << perf.terrainCompletedCount << "\n";
        text << "TD_POP_CNT  " << std::setw(7) << perf.terrainPopCount;
        return text.str();
    }

    void ClientRuntime::DiagnosticsAccess::recordPerformanceMax(ClientPerfCounter counter, double milliseconds)
    {
        recordPerfMax(owner_.state_->diagnostics.perfMax, counter, milliseconds);
    }

    void ClientRuntime::DiagnosticsAccess::resetPerformanceMax()
    {
        owner_.state_->diagnostics.perfMax = ClientPerfMaxStats{};
    }

    ClientRuntime::AudioAccess::AudioAccess(ClientRuntime& owner) :
        owner_(owner)
    {
    }

    void ClientRuntime::AudioAccess::setVolumes(float musicVolume, float sfxVolume)
    {
        owner_.state_->audio.setMusicVolume(musicVolume);
        owner_.state_->audio.setSfxVolume(sfxVolume);
    }

    ClientRuntime::ClientRuntime(GLFWwindow* window) :
        state_(std::make_unique<ClientRuntimeState>()),
        renderAccess_(*this),
        sceneAccess_(*this),
        gameplayAccess_(*this),
        uiAccess_(*this),
        diagnosticsAccess_(*this),
        audioAccess_(*this)
    {
        state_->initializeContexts();
        renderRuntime_ = std::make_unique<ClientRenderRuntime>(window, *state_);
    }

    ClientRuntime::~ClientRuntime() = default;

    ClientRuntime::RenderAccess& ClientRuntime::render()
    {
        return renderAccess_;
    }

    ClientRuntime::SceneAccess& ClientRuntime::scene()
    {
        return sceneAccess_;
    }

    ClientRuntime::GameplayAccess& ClientRuntime::gameplay()
    {
        return gameplayAccess_;
    }

    ClientRuntime::UiAccess& ClientRuntime::ui()
    {
        return uiAccess_;
    }

    ClientRuntime::DiagnosticsAccess& ClientRuntime::diagnostics()
    {
        return diagnosticsAccess_;
    }

    ClientRuntime::AudioAccess& ClientRuntime::audio()
    {
        return audioAccess_;
    }

    world::TerrainBuilderConfig ClientRuntime::terrainConfigForWorldSeed(uint64_t worldSeed) const
    {
        world::TerrainBuilderConfig config = terrainBuilderConfig(*state_);
        config.activeWorldSeedSalt = static_cast<int>((worldSeed ^ (worldSeed >> 32u)) & 0x7fffffffu);
        return config;
    }

    const ClientRuntime::RenderAccess& ClientRuntime::render() const
    {
        return renderAccess_;
    }

    const ClientRuntime::SceneAccess& ClientRuntime::scene() const
    {
        return sceneAccess_;
    }

    const ClientRuntime::GameplayAccess& ClientRuntime::gameplay() const
    {
        return gameplayAccess_;
    }

    const ClientRuntime::UiAccess& ClientRuntime::ui() const
    {
        return uiAccess_;
    }

    const ClientRuntime::DiagnosticsAccess& ClientRuntime::diagnostics() const
    {
        return diagnosticsAccess_;
    }

    const ClientRuntime::AudioAccess& ClientRuntime::audio() const
    {
        return audioAccess_;
    }
}
