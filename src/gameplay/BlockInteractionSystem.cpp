#include "gameplay/BlockInteractionSystem.h"

#include "world/BlockCollisionShape.h"
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
        constexpr int FluidAmountBits = 7;
        constexpr uint16_t FluidAmountMask = (1u << FluidAmountBits) - 1u;
        constexpr uint16_t FluidWater = 1;
        constexpr uint16_t FluidFullAmount = 100;
        constexpr uint16_t FluidHeightStepAmount = 10;
        constexpr uint16_t FluidHeightLevels = 10;
        constexpr double FluidSurfaceMaxHeight = 0.8;
        constexpr float LevelMultiplierBase = 1.5f;

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

        bool rayIntersectsAabb(DVec3 origin, Vec3 direction, Vec3 min, Vec3 max, double& distance)
        {
            constexpr double Epsilon = 0.0000001;
            double tMin = 0.0;
            double tMax = BlockInteractionSystem::MaxInteractionDistance;

            auto updateAxis = [&](double originValue, double directionValue, double minValue, double maxValue)
            {
                if (std::abs(directionValue) < Epsilon)
                {
                    return originValue >= minValue - Epsilon && originValue <= maxValue + Epsilon;
                }

                double nearT = (minValue - originValue) / directionValue;
                double farT = (maxValue - originValue) / directionValue;
                if (nearT > farT)
                {
                    std::swap(nearT, farT);
                }
                tMin = std::max(tMin, nearT);
                tMax = std::min(tMax, farT);
                return tMin <= tMax + Epsilon;
            };

            const bool intersects = updateAxis(origin.x, direction.x, min.x, max.x) &&
                updateAxis(origin.y, direction.y, min.y, max.y) &&
                updateAxis(origin.z, direction.z, min.z, max.z);
            distance = std::max(0.0, tMin);
            return intersects;
        }

        bool rayIntersectsFireBlock(DVec3 origin, Vec3 direction, int x, int y, int z, double& distance)
        {
            return rayIntersectsAabb(
                origin,
                direction,
                Vec3{static_cast<float>(x) - 0.4f, static_cast<float>(y), static_cast<float>(z) - 0.4f},
                Vec3{static_cast<float>(x) + 0.4f, static_cast<float>(y) + 0.1f, static_cast<float>(z) + 0.4f},
                distance);
        }

        bool rayIntersectsSlabBlock(DVec3 origin, Vec3 direction, int x, int y, int z, uint16_t blockState, double& distance)
        {
            const world::block_visual::LocalAabb aabb = world::block_visual::slabWorldAabb(x, y, z, blockState);
            return rayIntersectsAabb(origin, direction, aabb.min, aabb.max, distance);
        }

        bool rayIntersectsHalfSlabBlock(DVec3 origin, Vec3 direction, int x, int y, int z, uint16_t blockState, double& distance)
        {
            const world::block_visual::LocalAabb aabb = world::block_visual::halfSlabWorldAabb(x, y, z, blockState);
            return rayIntersectsAabb(origin, direction, aabb.min, aabb.max, distance);
        }

        bool rayIntersectsCrucibleBlock(DVec3 origin, Vec3 direction, int x, int y, int z, double& distance)
        {
            bool hit = false;
            double bestDistance = std::numeric_limits<double>::infinity();
            world::block_visual::forEachCrucibleWorldAabb(x, y, z, [&](const world::block_visual::LocalAabb& aabb)
            {
                double aabbDistance = 0.0;
                if (rayIntersectsAabb(origin, direction, aabb.min, aabb.max, aabbDistance))
                {
                    bestDistance = std::min(bestDistance, aabbDistance);
                    hit = true;
                }
            });
            distance = bestDistance;
            return hit;
        }

        bool rayIntersectsMoldBlock(DVec3 origin, Vec3 direction, int x, int y, int z, double& distance)
        {
            const world::block_visual::LocalAabb aabb = world::block_visual::moldWorldAabb(x, y, z);
            return rayIntersectsAabb(origin, direction, aabb.min, aabb.max, distance);
        }

        bool rayIntersectsCrossBlock(DVec3 origin, Vec3 direction, int x, int y, int z, const BlockDefinition& definition, double& distance)
        {
            bool hit = false;
            double bestDistance = std::numeric_limits<double>::infinity();
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
                        bestDistance = std::min(bestDistance, distance);
                        hit = true;
                    }
                });
            distance = bestDistance;
            return hit;
        }

        bool rayIntersectsPropBlock(
            DVec3 origin,
            Vec3 direction,
            int x,
            int y,
            int z,
            const BlockDefinition& definition,
            const assets::PropMesh& mesh,
            double& distance)
        {
            bool hit = false;
            double bestDistance = std::numeric_limits<double>::infinity();
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
                    bestDistance = std::min(bestDistance, distance);
                    hit = true;
                }
            }
            distance = bestDistance;
            return hit;
        }

        bool rayIntersectsBlockShape(
            DVec3 origin,
            Vec3 direction,
            int x,
            int y,
            int z,
            uint16_t block,
            const BlockDefinition& definition,
            uint16_t blockState,
            const BlockInteractionSystem::PropMeshProvider& propMesh,
            double& distance)
        {
            if (definition.renderType == BlockRenderType::Cube)
            {
                return true;
            }
            if (definition.renderType == BlockRenderType::Cross)
            {
                return rayIntersectsCrossBlock(origin, direction, x, y, z, definition, distance);
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
                    : rayIntersectsPropBlock(origin, direction, x, y, z, definition, *mesh, distance);
            }
            if (definition.renderType == BlockRenderType::Fire)
            {
                return rayIntersectsFireBlock(origin, direction, x, y, z, distance);
            }
            if (definition.renderType == BlockRenderType::Slab)
            {
                return rayIntersectsSlabBlock(origin, direction, x, y, z, blockState, distance);
            }
            if (definition.renderType == BlockRenderType::HalfSlab)
            {
                return rayIntersectsHalfSlabBlock(origin, direction, x, y, z, blockState, distance);
            }
            if (definition.renderType == BlockRenderType::Crucible)
            {
                return rayIntersectsCrucibleBlock(origin, direction, x, y, z, distance);
            }
            if (definition.renderType == BlockRenderType::Mold)
            {
                return rayIntersectsMoldBlock(origin, direction, x, y, z, distance);
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
        const TerrainAabbCollisionPredicate& terrainCellIntersectsPlayer)
    {
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

        for (int y = blockMinY; y <= blockMaxY; ++y)
        {
            for (int z = blockMinZ; z <= blockMaxZ; ++z)
            {
                for (int x = blockMinX; x <= blockMaxX; ++x)
                {
                    if (terrainCellIntersectsPlayer && terrainCellIntersectsPlayer(x, y, z, min, max))
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
        const TerrainAabbCollisionPredicate& terrainCellIntersectsPlayer)
    {
        constexpr double HalfWidth = 0.3;
        constexpr double SupportEpsilon = 0.03;
        constexpr double Epsilon = 0.000001;

        const DVec3 min{playerPosition.x - HalfWidth, playerPosition.y - SupportEpsilon, playerPosition.z - HalfWidth};
        const DVec3 max{playerPosition.x + HalfWidth, playerPosition.y, playerPosition.z + HalfWidth};
        const int y = blockCoordinateY(min.y);

        const int blockMinX = blockCoordinateXz(min.x);
        const int blockMaxX = blockCoordinateXz(max.x - Epsilon);
        const int blockMinZ = blockCoordinateXz(min.z);
        const int blockMaxZ = blockCoordinateXz(max.z - Epsilon);

        for (int z = blockMinZ; z <= blockMaxZ; ++z)
        {
            for (int x = blockMinX; x <= blockMaxX; ++x)
            {
                if (terrainCellIntersectsPlayer && terrainCellIntersectsPlayer(x, y, z, min, max))
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool BlockInteractionSystem::playerColliderIntersectsWater(
        DVec3 playerPosition,
        double heightScale,
        const FluidSampler& fluidAtWorld)
    {
        if (!fluidAtWorld)
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
                    const uint16_t fluid = fluidAtWorld(x, y, z);
                    if (!isWater(fluid))
                    {
                        continue;
                    }

                    const bool hasWaterAbove = isWater(fluidAtWorld(x, y + 1, z));
                    const double waterTop = static_cast<double>(y) + (hasWaterAbove ? 1.0 : fluidSurfaceHeight(fluidAmount(fluid)));
                    if (maxY > static_cast<double>(y) + Epsilon && minY < waterTop - Epsilon)
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    bool BlockInteractionSystem::blockIntersectsAabb(
        int x,
        int y,
        int z,
        const BlockDefinition& definition,
        DVec3 min,
        DVec3 max,
        uint16_t blockState)
    {
        return world::block_collision::blockIntersectsAabb(x, y, z, definition, min, max, blockState);
    }

    bool BlockInteractionSystem::blockIntersectsPlayerCollider(
        int x,
        int y,
        int z,
        const BlockDefinition& definition,
        DVec3 playerPosition,
        double heightScale,
        uint16_t blockState)
    {
        if (!definition.collision)
        {
            return false;
        }

        constexpr double HalfWidth = 0.3;
        constexpr double Height = 1.75;
        const double scaledHeight = std::max(0.1, Height * heightScale);

        const double minX = playerPosition.x - HalfWidth;
        const double maxX = playerPosition.x + HalfWidth;
        const double minY = playerPosition.y;
        const double maxY = playerPosition.y + scaledHeight;
        const double minZ = playerPosition.z - HalfWidth;
        const double maxZ = playerPosition.z + HalfWidth;

        return blockIntersectsAabb(
            x,
            y,
            z,
            definition,
            DVec3{minX, minY, minZ},
            DVec3{maxX, maxY, maxZ},
            blockState);
    }

    bool BlockInteractionSystem::raycastBlock(
        DVec3 origin,
        Vec3 direction,
        const BlockSampler& blockAtWorld,
        const BlockDefinitionProvider& blockDefinition,
        BlockRaycastHit& hit,
        const PropMeshProvider& propMesh,
        const BlockStateSampler& blockStateAtWorld)
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
                double hitDistance = traveled;
                if (definition.renderType != BlockRenderType::None &&
                    rayIntersectsBlockShape(
                        origin,
                        normalizedDirection,
                        blockX,
                        blockY,
                        blockZ,
                        block,
                        definition,
                        blockStateAtWorld ? blockStateAtWorld(blockX, blockY, blockZ) : 0,
                        propMesh,
                        hitDistance))
                {
                    const Vec3 hitPoint = rayPoint(origin, normalizedDirection, hitDistance);
                    hit.blockX = blockX;
                    hit.blockY = blockY;
                    hit.blockZ = blockZ;
                    hit.previousBlockX = previousBlockX;
                    hit.previousBlockY = previousBlockY;
                    hit.previousBlockZ = previousBlockZ;
                    hit.hitPosition = DVec3{
                        static_cast<double>(hitPoint.x),
                        static_cast<double>(hitPoint.y),
                        static_cast<double>(hitPoint.z)
                    };
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
        const PropMeshProvider& propMesh,
        const BlockStateSampler& blockStateAtWorld)
    {
        BlockBreakingUpdate update{};
        if (!breaking || deltaSeconds <= 0.0f)
        {
            resetBreaking(state);
            return update;
        }

        BlockRaycastHit hit{};
        if (!raycastBlock(origin, direction, blockAtWorld, blockDefinition, hit, propMesh, blockStateAtWorld))
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

        if (sandboxMode)
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

        state.progress = std::min(1.0f, state.progress + deltaSeconds * breakPower / definition.hardness);
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
