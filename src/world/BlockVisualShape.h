#pragma once

#include "camera/Camera.h"
#include "world/BlockData.h"

#include <array>
#include <cstdint>

namespace dolbuto::world::block_visual
{
    constexpr int TerrainTilePeriod = 65536;
    constexpr uint32_t TopFaceRotationSalt = 0x51A7E001u;
    constexpr uint32_t PlantPlacementSalt = 0x9A7D3E21u;
    constexpr float RandomBlockOffsetHalfRange = 0.2f;

    inline int wrapBlockCoordinate(int coordinate)
    {
        int wrapped = coordinate % TerrainTilePeriod;
        if (wrapped < 0)
        {
            wrapped += TerrainTilePeriod;
        }
        return wrapped;
    }

    inline uint32_t worldRandomHash(int x, int y, int z, uint32_t salt)
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

    inline uint8_t worldRandom8(int x, int y, int z, uint32_t salt)
    {
        return static_cast<uint8_t>(worldRandomHash(wrapBlockCoordinate(x), y, wrapBlockCoordinate(z), salt) & 255u);
    }

    inline std::array<float, 2> randomBlockOffset(const BlockDefinition& definition, int x, int y, int z)
    {
        if (!definition.randomOffset)
        {
            return {0.0f, 0.0f};
        }

        const auto offsetFromByte = [](uint8_t value)
        {
            return (static_cast<float>(value) / 255.0f) * (RandomBlockOffsetHalfRange * 2.0f) - RandomBlockOffsetHalfRange;
        };
        return {
            offsetFromByte(worldRandom8(x, y, z, PlantPlacementSalt)),
            offsetFromByte(worldRandom8(z, y, x, PlantPlacementSalt))
        };
    }

    inline uint8_t rotation(const BlockDefinition& definition, int x, int y, int z)
    {
        if (definition.directional)
        {
            return 0;
        }
        return static_cast<uint8_t>(worldRandom8(x, y, z, TopFaceRotationSalt) & 3u);
    }

    inline std::array<float, 2> rotateLocalXz(float localX, float localZ, uint8_t rotation)
    {
        switch (rotation & 3u)
        {
        case 1: return {1.0f - localZ, localX};
        case 2: return {1.0f - localX, 1.0f - localZ};
        case 3: return {localZ, 1.0f - localX};
        default: return {localX, localZ};
        }
    }

    inline Vec3 transformLocalPoint(const BlockDefinition& definition, int x, int y, int z, float localX, float localY, float localZ)
    {
        const std::array<float, 2> offset = randomBlockOffset(definition, x, y, z);
        const std::array<float, 2> rotated = rotateLocalXz(localX, localZ, rotation(definition, x, y, z));
        return {
            static_cast<float>(x) - 0.5f + offset[0] + rotated[0],
            static_cast<float>(y) + localY,
            static_cast<float>(z) - 0.5f + offset[1] + rotated[1]
        };
    }

    template <typename Callback>
    void forEachCrossQuad(const BlockDefinition& definition, int x, int y, int z, Callback callback)
    {
        callback(
            transformLocalPoint(definition, x, y, z, 0.0f, 0.0f, 0.0f),
            transformLocalPoint(definition, x, y, z, 0.0f, 1.0f, 0.0f),
            transformLocalPoint(definition, x, y, z, 1.0f, 1.0f, 1.0f),
            transformLocalPoint(definition, x, y, z, 1.0f, 0.0f, 1.0f));
        callback(
            transformLocalPoint(definition, x, y, z, 1.0f, 0.0f, 0.0f),
            transformLocalPoint(definition, x, y, z, 1.0f, 1.0f, 0.0f),
            transformLocalPoint(definition, x, y, z, 0.0f, 1.0f, 1.0f),
            transformLocalPoint(definition, x, y, z, 0.0f, 0.0f, 1.0f));
    }
}
