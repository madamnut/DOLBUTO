#pragma once

#include "camera/Camera.h"
#include "world/BlockData.h"
#include "world/BlockVisualShape.h"

namespace dolbuto::world::block_collision
{
    inline bool aabbIntersects(DVec3 min, DVec3 max, DVec3 blockMin, DVec3 blockMax)
    {
        constexpr double Epsilon = 0.000001;
        return max.x > blockMin.x + Epsilon &&
            min.x < blockMax.x - Epsilon &&
            max.y > blockMin.y + Epsilon &&
            min.y < blockMax.y - Epsilon &&
            max.z > blockMin.z + Epsilon &&
            min.z < blockMax.z - Epsilon;
    }

    inline block_visual::LocalAabb blockWorldAabb(
        int x,
        int y,
        int z,
        const BlockDefinition& definition,
        uint16_t blockState,
        const block_visual::LocalAabb* propLocalAabb = nullptr)
    {
        if (definition.renderType == BlockRenderType::Prop && propLocalAabb != nullptr)
        {
            return block_visual::transformLocalAabb(definition, x, y, z, *propLocalAabb);
        }
        if (definition.renderType == BlockRenderType::Slab)
        {
            return block_visual::slabWorldAabb(x, y, z, blockState);
        }
        if (definition.renderType == BlockRenderType::HalfSlab)
        {
            return block_visual::halfSlabWorldAabb(x, y, z, blockState);
        }
        if (definition.renderType == BlockRenderType::Mold)
        {
            return block_visual::moldWorldAabb(x, y, z);
        }

        return block_visual::LocalAabb{
            Vec3{static_cast<float>(x) - 0.5f, static_cast<float>(y), static_cast<float>(z) - 0.5f},
            Vec3{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 1.0f, static_cast<float>(z) + 0.5f}
        };
    }

    inline bool blockIntersectsAabb(
        int x,
        int y,
        int z,
        const BlockDefinition& definition,
        DVec3 min,
        DVec3 max,
        uint16_t blockState,
        const block_visual::LocalAabb* propLocalAabb = nullptr)
    {
        if (!definition.collision)
        {
            return false;
        }

        if (definition.renderType == BlockRenderType::Crucible)
        {
            bool intersects = false;
            block_visual::forEachCrucibleWorldAabb(x, y, z, [&](const block_visual::LocalAabb& aabb)
            {
                intersects = intersects || aabbIntersects(
                    min,
                    max,
                    DVec3{static_cast<double>(aabb.min.x), static_cast<double>(aabb.min.y), static_cast<double>(aabb.min.z)},
                    DVec3{static_cast<double>(aabb.max.x), static_cast<double>(aabb.max.y), static_cast<double>(aabb.max.z)});
            });
            return intersects;
        }

        const block_visual::LocalAabb aabb = blockWorldAabb(x, y, z, definition, blockState, propLocalAabb);
        return aabbIntersects(
            min,
            max,
            DVec3{static_cast<double>(aabb.min.x), static_cast<double>(aabb.min.y), static_cast<double>(aabb.min.z)},
            DVec3{static_cast<double>(aabb.max.x), static_cast<double>(aabb.max.y), static_cast<double>(aabb.max.z)});
    }
}
