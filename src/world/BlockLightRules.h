#pragma once

#include "world/BlockData.h"

#include <cstdint>

namespace dolbuto::world::block_light
{
    enum class Direction : uint8_t
    {
        PosX,
        NegX,
        PosY,
        NegY,
        PosZ,
        NegZ
    };

    inline Direction opposite(Direction direction)
    {
        switch (direction)
        {
        case Direction::PosX: return Direction::NegX;
        case Direction::NegX: return Direction::PosX;
        case Direction::PosY: return Direction::NegY;
        case Direction::NegY: return Direction::PosY;
        case Direction::PosZ: return Direction::NegZ;
        case Direction::NegZ: return Direction::PosZ;
        default: return Direction::PosY;
        }
    }

    inline uint8_t directionalAttenuation(
        const LightAttenuationTables* tables,
        uint16_t block,
        uint16_t blockState,
        Direction direction,
        uint8_t baseAttenuation)
    {
        (void)tables;
        (void)block;
        (void)blockState;
        (void)direction;
        return baseAttenuation;
    }
}
