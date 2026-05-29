#pragma once

#include "assets/PropModelLoader.h"
#include "camera/Camera.h"
#include "world/BlockData.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dolbuto::gameplay
{
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

    struct BlockBreakingUpdate
    {
        bool breakBlock = false;
        bool spawnMiningParticle = false;
        BlockRaycastHit hit{};
        uint16_t block = 0;
        uint16_t durabilityCost = 0;
    };

    struct BlockBreakTool
    {
        uint16_t level = 1;
        std::vector<std::string> actions;
        bool durable = false;
    };

    class BlockInteractionSystem
    {
    public:
        using BlockSampler = std::function<uint16_t(int, int, int)>;
        using FluidSampler = std::function<uint16_t(int, int, int)>;
        using BlockDefinitionProvider = std::function<const BlockDefinition&(uint16_t)>;
        using PropMeshProvider = std::function<const assets::PropMesh*(uint16_t)>;
        using TerrainCollisionPredicate = std::function<bool(int, int, int)>;

        static constexpr double MaxInteractionDistance = 8.0;
        static constexpr float BlockBreakPower = 1.0f;
        static constexpr float BlockMiningParticleInterval = 0.12f;

        static int blockCoordinateXz(double worldCoordinate);
        static int blockCoordinateY(double worldCoordinate);

        static bool playerColliderIntersectsTerrain(
            DVec3 playerPosition,
            double heightScale,
            const TerrainCollisionPredicate& terrainCellBlocksPlayer);

        static bool playerColliderHasSupportBelow(
            DVec3 playerPosition,
            const TerrainCollisionPredicate& terrainCellBlocksPlayer);

        static bool playerColliderIntersectsWater(
            DVec3 playerPosition,
            double heightScale,
            const FluidSampler& fluidAtWorld);

        static bool blockIntersectsPlayerCollider(
            int x,
            int y,
            int z,
            const BlockDefinition& definition,
            DVec3 playerPosition,
            double heightScale);

        static bool raycastBlock(
            DVec3 origin,
            Vec3 direction,
            const BlockSampler& blockAtWorld,
            const BlockDefinitionProvider& blockDefinition,
            BlockRaycastHit& hit,
            const PropMeshProvider& propMesh = {});

        static BlockBreakingUpdate updateBreaking(
            BlockBreakingState& state,
            DVec3 origin,
            Vec3 direction,
            bool breaking,
            float deltaSeconds,
            bool sandboxMode,
            const BlockBreakTool& tool,
            const BlockSampler& blockAtWorld,
            const BlockDefinitionProvider& blockDefinition,
            const PropMeshProvider& propMesh = {});

        static void resetBreaking(BlockBreakingState& state);
    };
}
