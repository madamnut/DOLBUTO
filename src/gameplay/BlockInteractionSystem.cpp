#include "gameplay/BlockInteractionSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dolbuto::gameplay
{
    int BlockInteractionSystem::blockCoordinateXz(double worldCoordinate)
    {
        return static_cast<int>(std::floor(worldCoordinate + 0.5));
    }

    int BlockInteractionSystem::blockCoordinateY(double worldCoordinate)
    {
        return static_cast<int>(std::floor(worldCoordinate));
    }

    bool BlockInteractionSystem::playerColliderIntersectsTerrain(
        DVec3 playerPosition,
        double heightScale,
        const TerrainCollisionPredicate& terrainCellBlocksPlayer)
    {
        constexpr double HalfWidth = 0.3;
        constexpr double Height = 1.75;
        constexpr double Epsilon = 0.000001;
        const double scaledHeight = std::max(0.1, Height * heightScale);

        const double minX = playerPosition.x - HalfWidth;
        const double maxX = playerPosition.x + HalfWidth;
        const double minY = playerPosition.y;
        const double maxY = playerPosition.y + scaledHeight;
        const double minZ = playerPosition.z - HalfWidth;
        const double maxZ = playerPosition.z + HalfWidth;

        const int blockMinX = blockCoordinateXz(minX);
        const int blockMaxX = blockCoordinateXz(maxX - Epsilon);
        const int blockMinY = blockCoordinateY(minY);
        const int blockMaxY = blockCoordinateY(maxY - Epsilon);
        const int blockMinZ = blockCoordinateXz(minZ);
        const int blockMaxZ = blockCoordinateXz(maxZ - Epsilon);

        for (int y = blockMinY; y <= blockMaxY; ++y)
        {
            for (int z = blockMinZ; z <= blockMaxZ; ++z)
            {
                for (int x = blockMinX; x <= blockMaxX; ++x)
                {
                    if (terrainCellBlocksPlayer && terrainCellBlocksPlayer(x, y, z))
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    bool BlockInteractionSystem::playerColliderHasSupportBelow(
        DVec3 playerPosition,
        const TerrainCollisionPredicate& terrainCellBlocksPlayer)
    {
        constexpr double HalfWidth = 0.3;
        constexpr double SupportEpsilon = 0.03;
        constexpr double Epsilon = 0.000001;

        const double minX = playerPosition.x - HalfWidth;
        const double maxX = playerPosition.x + HalfWidth;
        const double minZ = playerPosition.z - HalfWidth;
        const double maxZ = playerPosition.z + HalfWidth;
        const int y = blockCoordinateY(playerPosition.y - SupportEpsilon);

        const int blockMinX = blockCoordinateXz(minX);
        const int blockMaxX = blockCoordinateXz(maxX - Epsilon);
        const int blockMinZ = blockCoordinateXz(minZ);
        const int blockMaxZ = blockCoordinateXz(maxZ - Epsilon);

        for (int z = blockMinZ; z <= blockMaxZ; ++z)
        {
            for (int x = blockMinX; x <= blockMaxX; ++x)
            {
                if (terrainCellBlocksPlayer && terrainCellBlocksPlayer(x, y, z))
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool BlockInteractionSystem::blockIntersectsPlayerCollider(
        int x,
        int y,
        int z,
        const BlockDefinition& definition,
        DVec3 playerPosition,
        double heightScale)
    {
        if (!definition.collision)
        {
            return false;
        }

        constexpr double HalfWidth = 0.3;
        constexpr double Height = 1.75;
        constexpr double Epsilon = 0.000001;
        const double scaledHeight = std::max(0.1, Height * heightScale);

        const double minX = playerPosition.x - HalfWidth;
        const double maxX = playerPosition.x + HalfWidth;
        const double minY = playerPosition.y;
        const double maxY = playerPosition.y + scaledHeight;
        const double minZ = playerPosition.z - HalfWidth;
        const double maxZ = playerPosition.z + HalfWidth;

        return x >= blockCoordinateXz(minX) &&
            x <= blockCoordinateXz(maxX - Epsilon) &&
            y >= blockCoordinateY(minY) &&
            y <= blockCoordinateY(maxY - Epsilon) &&
            z >= blockCoordinateXz(minZ) &&
            z <= blockCoordinateXz(maxZ - Epsilon);
    }

    bool BlockInteractionSystem::raycastBlock(
        DVec3 origin,
        Vec3 direction,
        const BlockSampler& blockAtWorld,
        const BlockDefinitionProvider& blockDefinition,
        BlockRaycastHit& hit)
    {
        constexpr double Epsilon = 0.000001;

        Vec3 normalizedDirection = normalize(direction);
        if (normalizedDirection.x == 0.0f && normalizedDirection.y == 0.0f && normalizedDirection.z == 0.0f)
        {
            return false;
        }

        int blockX = blockCoordinateXz(origin.x);
        int blockY = blockCoordinateY(origin.y);
        int blockZ = blockCoordinateXz(origin.z);
        int previousBlockX = blockX;
        int previousBlockY = blockY;
        int previousBlockZ = blockZ;

        auto axisTMax = [](double originValue, double directionValue, int block, bool vertical) -> double
        {
            if (std::abs(directionValue) <= 0.0)
            {
                return std::numeric_limits<double>::infinity();
            }

            const double boundary = vertical
                ? (directionValue > 0.0 ? static_cast<double>(block + 1) : static_cast<double>(block))
                : (directionValue > 0.0 ? static_cast<double>(block) + 0.5 : static_cast<double>(block) - 0.5);
            return (boundary - originValue) / directionValue;
        };

        const int stepX = normalizedDirection.x > 0.0f ? 1 : (normalizedDirection.x < 0.0f ? -1 : 0);
        const int stepY = normalizedDirection.y > 0.0f ? 1 : (normalizedDirection.y < 0.0f ? -1 : 0);
        const int stepZ = normalizedDirection.z > 0.0f ? 1 : (normalizedDirection.z < 0.0f ? -1 : 0);
        double tMaxX = axisTMax(origin.x, normalizedDirection.x, blockX, false);
        double tMaxY = axisTMax(origin.y, normalizedDirection.y, blockY, true);
        double tMaxZ = axisTMax(origin.z, normalizedDirection.z, blockZ, false);
        const double tDeltaX = stepX == 0 ? std::numeric_limits<double>::infinity() : 1.0 / std::abs(static_cast<double>(normalizedDirection.x));
        const double tDeltaY = stepY == 0 ? std::numeric_limits<double>::infinity() : 1.0 / std::abs(static_cast<double>(normalizedDirection.y));
        const double tDeltaZ = stepZ == 0 ? std::numeric_limits<double>::infinity() : 1.0 / std::abs(static_cast<double>(normalizedDirection.z));

        double traveled = 0.0;
        while (traveled <= MaxInteractionDistance + Epsilon)
        {
            const uint16_t block = blockAtWorld ? blockAtWorld(blockX, blockY, blockZ) : 0;
            if (block != 0 && blockDefinition && blockDefinition(block).renderType != BlockRenderType::None)
            {
                hit.blockX = blockX;
                hit.blockY = blockY;
                hit.blockZ = blockZ;
                hit.previousBlockX = previousBlockX;
                hit.previousBlockY = previousBlockY;
                hit.previousBlockZ = previousBlockZ;
                return true;
            }

            previousBlockX = blockX;
            previousBlockY = blockY;
            previousBlockZ = blockZ;
            if (tMaxX <= tMaxY && tMaxX <= tMaxZ)
            {
                blockX += stepX;
                traveled = tMaxX;
                tMaxX += tDeltaX;
            }
            else if (tMaxY <= tMaxZ)
            {
                blockY += stepY;
                traveled = tMaxY;
                tMaxY += tDeltaY;
            }
            else
            {
                blockZ += stepZ;
                traveled = tMaxZ;
                tMaxZ += tDeltaZ;
            }
        }

        return false;
    }

    BlockBreakingUpdate BlockInteractionSystem::updateBreaking(
        BlockBreakingState& state,
        DVec3 origin,
        Vec3 direction,
        bool breaking,
        float deltaSeconds,
        const BlockSampler& blockAtWorld,
        const BlockDefinitionProvider& blockDefinition)
    {
        BlockBreakingUpdate update{};
        if (!breaking || deltaSeconds <= 0.0f)
        {
            resetBreaking(state);
            return update;
        }

        BlockRaycastHit hit{};
        if (!raycastBlock(origin, direction, blockAtWorld, blockDefinition, hit))
        {
            resetBreaking(state);
            return update;
        }

        const uint16_t block = blockAtWorld ? blockAtWorld(hit.blockX, hit.blockY, hit.blockZ) : 0;
        const BlockDefinition& definition = blockDefinition(block);
        if (block == 0 || definition.renderType == BlockRenderType::None || definition.hardness < 0.0f)
        {
            resetBreaking(state);
            return update;
        }

        update.hit = hit;
        update.block = block;
        if (definition.hardness <= 0.0f)
        {
            update.breakBlock = true;
            resetBreaking(state);
            return update;
        }

        if (!state.active ||
            state.x != hit.blockX ||
            state.y != hit.blockY ||
            state.z != hit.blockZ ||
            state.block != block)
        {
            state = {};
            state.active = true;
            state.x = hit.blockX;
            state.y = hit.blockY;
            state.z = hit.blockZ;
            state.block = block;
        }

        state.progress = std::min(1.0f, state.progress + deltaSeconds * BlockBreakPower / definition.hardness);
        state.particleTimer += deltaSeconds;
        if (state.particleTimer >= BlockMiningParticleInterval)
        {
            state.particleTimer = 0.0f;
            update.spawnMiningParticle = true;
        }

        if (state.progress >= 1.0f)
        {
            update.breakBlock = true;
            resetBreaking(state);
        }

        return update;
    }

    void BlockInteractionSystem::resetBreaking(BlockBreakingState& state)
    {
        state = {};
    }
}
