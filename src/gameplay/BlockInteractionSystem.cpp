#include "gameplay/BlockInteractionSystem.h"

#include "world/BlockVisualShape.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace dolbuto::gameplay
{
    namespace
    {
        constexpr float LevelMultiplierBase = 1.5f;

        bool actionMatches(const BlockDefinition& definition, const BlockBreakTool& tool)
        {
            return definition.breakAction.empty() ||
                definition.breakAction == "none" ||
                std::find(tool.actions.begin(), tool.actions.end(), definition.breakAction) != tool.actions.end();
        }

        uint16_t durabilityCostFor(const BlockDefinition& definition, const BlockBreakTool& tool)
        {
            if (!tool.durable || definition.breakLevel == 0)
            {
                return 0;
            }
            return actionMatches(definition, tool) ? 1u : 3u;
        }

        float breakPowerFor(const BlockDefinition& definition, const BlockBreakTool& tool)
        {
            if (tool.level < definition.breakLevel)
            {
                return 0.0f;
            }

            const int levelDelta = static_cast<int>(tool.level) - static_cast<int>(definition.breakLevel);
            const float levelMultiplier = std::pow(LevelMultiplierBase, static_cast<float>(levelDelta));
            const float actionMultiplier = actionMatches(definition, tool) ? 1.0f : 0.5f;
            return BlockInteractionSystem::BlockBreakPower * levelMultiplier * actionMultiplier;
        }

        Vec3 subtract(Vec3 left, Vec3 right)
        {
            return {left.x - right.x, left.y - right.y, left.z - right.z};
        }

        Vec3 rayPoint(DVec3 origin, Vec3 direction, double distance)
        {
            return {
                static_cast<float>(origin.x + static_cast<double>(direction.x) * distance),
                static_cast<float>(origin.y + static_cast<double>(direction.y) * distance),
                static_cast<float>(origin.z + static_cast<double>(direction.z) * distance)
            };
        }

        bool rayIntersectsTriangle(DVec3 origin, Vec3 direction, Vec3 a, Vec3 b, Vec3 c, double& distance)
        {
            constexpr double Epsilon = 0.0000001;
            const Vec3 edge1 = subtract(b, a);
            const Vec3 edge2 = subtract(c, a);
            const Vec3 p = cross(direction, edge2);
            const double determinant = static_cast<double>(dot(edge1, p));
            if (std::abs(determinant) < Epsilon)
            {
                return false;
            }

            const double invDeterminant = 1.0 / determinant;
            const Vec3 t{
                static_cast<float>(origin.x - static_cast<double>(a.x)),
                static_cast<float>(origin.y - static_cast<double>(a.y)),
                static_cast<float>(origin.z - static_cast<double>(a.z))
            };
            const double u = static_cast<double>(dot(t, p)) * invDeterminant;
            if (u < -Epsilon || u > 1.0 + Epsilon)
            {
                return false;
            }

            const Vec3 q = cross(t, edge1);
            const double v = static_cast<double>(dot(direction, q)) * invDeterminant;
            if (v < -Epsilon || u + v > 1.0 + Epsilon)
            {
                return false;
            }

            const double hitDistance = static_cast<double>(dot(edge2, q)) * invDeterminant;
            if (hitDistance < -Epsilon || hitDistance > BlockInteractionSystem::MaxInteractionDistance + Epsilon)
            {
                return false;
            }

            distance = hitDistance;
            return true;
        }

        bool rayIntersectsQuad(DVec3 origin, Vec3 direction, Vec3 a, Vec3 b, Vec3 c, Vec3 d, double& distance)
        {
            double bestDistance = std::numeric_limits<double>::infinity();
            double triangleDistance = 0.0;
            bool hit = false;
            if (rayIntersectsTriangle(origin, direction, a, b, c, triangleDistance))
            {
                bestDistance = std::min(bestDistance, triangleDistance);
                hit = true;
            }
            if (rayIntersectsTriangle(origin, direction, a, c, d, triangleDistance))
            {
                bestDistance = std::min(bestDistance, triangleDistance);
                hit = true;
            }
            distance = bestDistance;
            return hit;
        }

        bool pointInBlockCell(Vec3 point, int x, int y, int z)
        {
            constexpr float Epsilon = 0.0001f;
            return point.x >= static_cast<float>(x) - 0.5f - Epsilon &&
                point.x <= static_cast<float>(x) + 0.5f + Epsilon &&
                point.y >= static_cast<float>(y) - Epsilon &&
                point.y <= static_cast<float>(y + 1) + Epsilon &&
                point.z >= static_cast<float>(z) - 0.5f - Epsilon &&
                point.z <= static_cast<float>(z) + 0.5f + Epsilon;
        }

        bool rayIntersectsCrossBlock(DVec3 origin, Vec3 direction, int x, int y, int z, const BlockDefinition& definition)
        {
            bool hit = false;
            world::block_visual::forEachCrossQuad(
                definition,
                x,
                y,
                z,
                [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d)
                {
                    double distance = 0.0;
                    if (rayIntersectsQuad(origin, direction, a, b, c, d, distance) &&
                        pointInBlockCell(rayPoint(origin, direction, distance), x, y, z))
                    {
                        hit = true;
                    }
                });
            return hit;
        }

        bool rayIntersectsPropBlock(
            DVec3 origin,
            Vec3 direction,
            int x,
            int y,
            int z,
            const BlockDefinition& definition,
            const assets::PropMesh& mesh)
        {
            for (size_t offset = 0; offset + assets::PropQuadRenderFloatCount <= mesh.quads.size(); offset += assets::PropQuadRenderFloatCount)
            {
                size_t cursor = offset;
                std::array<Vec3, 4> quad{};
                for (Vec3& vertex : quad)
                {
                    const float localX = mesh.quads[cursor++];
                    const float localY = mesh.quads[cursor++];
                    const float localZ = mesh.quads[cursor++];
                    vertex = world::block_visual::transformLocalPoint(definition, x, y, z, localX, localY, localZ);
                }

                double distance = 0.0;
                if (rayIntersectsQuad(origin, direction, quad[0], quad[1], quad[2], quad[3], distance) &&
                    pointInBlockCell(rayPoint(origin, direction, distance), x, y, z))
                {
                    return true;
                }
            }
            return false;
        }

        bool rayIntersectsBlockShape(
            DVec3 origin,
            Vec3 direction,
            int x,
            int y,
            int z,
            uint16_t block,
            const BlockDefinition& definition,
            const BlockInteractionSystem::PropMeshProvider& propMesh)
        {
            if (definition.renderType == BlockRenderType::Cube)
            {
                return true;
            }
            if (definition.renderType == BlockRenderType::Cross)
            {
                return rayIntersectsCrossBlock(origin, direction, x, y, z, definition);
            }
            if (definition.renderType == BlockRenderType::Prop)
            {
                if (!propMesh)
                {
                    return true;
                }
                const assets::PropMesh* mesh = propMesh(block);
                return mesh == nullptr || mesh->quads.empty()
                    ? true
                    : rayIntersectsPropBlock(origin, direction, x, y, z, definition, *mesh);
            }
            return false;
        }
    }

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
        BlockRaycastHit& hit,
        const PropMeshProvider& propMesh)
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
            if (block != 0 && blockDefinition)
            {
                const BlockDefinition& definition = blockDefinition(block);
                if (definition.renderType != BlockRenderType::None &&
                    rayIntersectsBlockShape(origin, normalizedDirection, blockX, blockY, blockZ, block, definition, propMesh))
                {
                    hit.blockX = blockX;
                    hit.blockY = blockY;
                    hit.blockZ = blockZ;
                    hit.previousBlockX = previousBlockX;
                    hit.previousBlockY = previousBlockY;
                    hit.previousBlockZ = previousBlockZ;
                    return true;
                }
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
        bool sandboxMode,
        const BlockBreakTool& tool,
        const BlockSampler& blockAtWorld,
        const BlockDefinitionProvider& blockDefinition,
        const PropMeshProvider& propMesh)
    {
        BlockBreakingUpdate update{};
        if (!breaking || deltaSeconds <= 0.0f)
        {
            resetBreaking(state);
            return update;
        }

        BlockRaycastHit hit{};
        if (!raycastBlock(origin, direction, blockAtWorld, blockDefinition, hit, propMesh))
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
        const float breakPower = sandboxMode ? 1.0f : breakPowerFor(definition, tool);
        if (!sandboxMode && breakPower <= 0.0f)
        {
            resetBreaking(state);
            return update;
        }
        update.durabilityCost = sandboxMode ? 0 : durabilityCostFor(definition, tool);
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

        const float effectiveHardness = sandboxMode ? std::min(definition.hardness, 0.5f) : definition.hardness;
        state.progress = std::min(1.0f, state.progress + deltaSeconds * breakPower / effectiveHardness);
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
