#pragma once

#include "camera/Camera.h"
#include "world/BlockData.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace dolbuto::world::block_visual
{
    constexpr int TerrainTilePeriod = 65536;
    constexpr uint32_t TopFaceRotationSalt = 0x51A7E001u;
    constexpr uint32_t PlantPlacementSalt = 0x9A7D3E21u;
    constexpr float RandomBlockOffsetHalfRange = 0.2f;

    struct LocalAabb
    {
        Vec3 min{};
        Vec3 max{};
    };

    template <typename Callback>
    void forEachCrucibleLocalAabb(Callback callback)
    {
        callback(LocalAabb{Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.2f, 1.0f}});
        callback(LocalAabb{Vec3{0.0f, 0.2f, 0.0f}, Vec3{1.0f, 1.0f, 0.2f}});
        callback(LocalAabb{Vec3{0.0f, 0.2f, 0.8f}, Vec3{1.0f, 1.0f, 1.0f}});
        callback(LocalAabb{Vec3{0.0f, 0.2f, 0.2f}, Vec3{0.2f, 1.0f, 0.8f}});
        callback(LocalAabb{Vec3{0.8f, 0.2f, 0.2f}, Vec3{1.0f, 1.0f, 0.8f}});
    }

    template <typename Callback>
    void forEachCrucibleWorldAabb(int x, int y, int z, Callback callback)
    {
        forEachCrucibleLocalAabb([&](const LocalAabb& local)
        {
            callback(LocalAabb{
                Vec3{static_cast<float>(x) - 0.5f + local.min.x, static_cast<float>(y) + local.min.y, static_cast<float>(z) - 0.5f + local.min.z},
                Vec3{static_cast<float>(x) - 0.5f + local.max.x, static_cast<float>(y) + local.max.y, static_cast<float>(z) - 0.5f + local.max.z}
            });
        });
    }

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

    inline BlockAttachState attachState(uint16_t blockState)
    {
        switch (static_cast<BlockAttachState>(blockState))
        {
        case BlockAttachState::Top:
        case BlockAttachState::North:
        case BlockAttachState::South:
        case BlockAttachState::West:
        case BlockAttachState::East:
            return static_cast<BlockAttachState>(blockState);
        case BlockAttachState::Bottom:
        default:
            return BlockAttachState::Bottom;
        }
    }

    inline LocalAabb slabLocalAabb(uint16_t blockState)
    {
        switch (attachState(blockState))
        {
        case BlockAttachState::Top:
            return LocalAabb{Vec3{0.0f, 0.5f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f}};
        case BlockAttachState::North:
            return LocalAabb{Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 0.5f}};
        case BlockAttachState::South:
            return LocalAabb{Vec3{0.0f, 0.0f, 0.5f}, Vec3{1.0f, 1.0f, 1.0f}};
        case BlockAttachState::West:
            return LocalAabb{Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.5f, 1.0f, 1.0f}};
        case BlockAttachState::East:
            return LocalAabb{Vec3{0.5f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f}};
        case BlockAttachState::Bottom:
        default:
            return LocalAabb{Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.5f, 1.0f}};
        }
    }

    inline LocalAabb slabWorldAabb(int x, int y, int z, uint16_t blockState)
    {
        const LocalAabb local = slabLocalAabb(blockState);
        return LocalAabb{
            Vec3{static_cast<float>(x) - 0.5f + local.min.x, static_cast<float>(y) + local.min.y, static_cast<float>(z) - 0.5f + local.min.z},
            Vec3{static_cast<float>(x) - 0.5f + local.max.x, static_cast<float>(y) + local.max.y, static_cast<float>(z) - 0.5f + local.max.z}
        };
    }

    struct LocalAxis
    {
        int axis = 0;
        int sign = 1;
    };

    struct AttachFaceBasis
    {
        BlockAttachState face = BlockAttachState::Bottom;
        LocalAxis normal{};
        LocalAxis u{};
        LocalAxis v{};
    };

    inline uint16_t attachGridState(BlockAttachState face, int grid)
    {
        const int clampedGrid = std::clamp(grid, 1, 9);
        return static_cast<uint16_t>(static_cast<uint16_t>(face) * 9u + static_cast<uint16_t>(clampedGrid - 1));
    }

    inline BlockAttachState attachGridFace(uint16_t blockState)
    {
        const uint16_t face = static_cast<uint16_t>(blockState / 9u);
        return attachState(face);
    }

    inline int attachGridCommand(uint16_t blockState)
    {
        const int grid = static_cast<int>(blockState % 9u) + 1;
        switch (grid)
        {
        case 1:
        case 2:
        case 3:
        case 4:
        case 6:
        case 7:
        case 8:
        case 9:
            return grid;
        case 5:
        default:
            return 2;
        }
    }

    inline AttachFaceBasis attachFaceBasis(BlockAttachState face)
    {
        switch (face)
        {
        case BlockAttachState::Top:
            return AttachFaceBasis{face, LocalAxis{1, -1}, LocalAxis{0, 1}, LocalAxis{2, 1}};
        case BlockAttachState::North:
            return AttachFaceBasis{face, LocalAxis{2, 1}, LocalAxis{0, -1}, LocalAxis{1, 1}};
        case BlockAttachState::South:
            return AttachFaceBasis{face, LocalAxis{2, -1}, LocalAxis{0, 1}, LocalAxis{1, 1}};
        case BlockAttachState::West:
            return AttachFaceBasis{face, LocalAxis{0, 1}, LocalAxis{2, 1}, LocalAxis{1, 1}};
        case BlockAttachState::East:
            return AttachFaceBasis{face, LocalAxis{0, -1}, LocalAxis{2, -1}, LocalAxis{1, 1}};
        case BlockAttachState::Bottom:
        default:
            return AttachFaceBasis{BlockAttachState::Bottom, LocalAxis{1, 1}, LocalAxis{0, 1}, LocalAxis{2, -1}};
        }
    }

    inline void applyAxisRange(std::array<float, 3>& minValues, std::array<float, 3>& maxValues, LocalAxis axis, float minValue, float maxValue)
    {
        const float clampedMin = std::clamp(minValue, 0.0f, 1.0f);
        const float clampedMax = std::clamp(maxValue, clampedMin, 1.0f);
        if (axis.sign >= 0)
        {
            minValues[static_cast<size_t>(axis.axis)] = clampedMin;
            maxValues[static_cast<size_t>(axis.axis)] = clampedMax;
        }
        else
        {
            minValues[static_cast<size_t>(axis.axis)] = 1.0f - clampedMax;
            maxValues[static_cast<size_t>(axis.axis)] = 1.0f - clampedMin;
        }
    }

    inline LocalAabb halfSlabLocalAabb(uint16_t blockState)
    {
        const AttachFaceBasis basis = attachFaceBasis(attachGridFace(blockState));
        const int grid = attachGridCommand(blockState);
        std::array<float, 3> minValues{0.0f, 0.0f, 0.0f};
        std::array<float, 3> maxValues{1.0f, 1.0f, 1.0f};

        const bool corner = grid == 1 || grid == 3 || grid == 7 || grid == 9;
        const float normalMax = corner ? 1.0f : 0.5f;
        float uMin = 0.0f;
        float uMax = 1.0f;
        float vMin = 0.0f;
        float vMax = 1.0f;

        if (corner)
        {
            uMin = (grid == 3 || grid == 9) ? 0.5f : 0.0f;
            uMax = uMin + 0.5f;
            vMin = (grid == 7 || grid == 9) ? 0.5f : 0.0f;
            vMax = vMin + 0.5f;
        }
        else if (grid == 2)
        {
            vMax = 0.5f;
        }
        else if (grid == 8)
        {
            vMin = 0.5f;
        }
        else if (grid == 4)
        {
            uMax = 0.5f;
        }
        else if (grid == 6)
        {
            uMin = 0.5f;
        }

        applyAxisRange(minValues, maxValues, basis.normal, 0.0f, normalMax);
        applyAxisRange(minValues, maxValues, basis.u, uMin, uMax);
        applyAxisRange(minValues, maxValues, basis.v, vMin, vMax);
        return LocalAabb{
            Vec3{minValues[0], minValues[1], minValues[2]},
            Vec3{maxValues[0], maxValues[1], maxValues[2]}
        };
    }

    inline LocalAabb halfSlabWorldAabb(int x, int y, int z, uint16_t blockState)
    {
        const LocalAabb local = halfSlabLocalAabb(blockState);
        return LocalAabb{
            Vec3{static_cast<float>(x) - 0.5f + local.min.x, static_cast<float>(y) + local.min.y, static_cast<float>(z) - 0.5f + local.min.z},
            Vec3{static_cast<float>(x) - 0.5f + local.max.x, static_cast<float>(y) + local.max.y, static_cast<float>(z) - 0.5f + local.max.z}
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
