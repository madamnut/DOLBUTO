#pragma once

#include "world/BlockData.h"

#include <cstddef>
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

    inline Direction attachDirection(uint16_t blockState)
    {
        switch (static_cast<BlockAttachState>(blockState))
        {
        case BlockAttachState::Top: return Direction::PosY;
        case BlockAttachState::North: return Direction::NegZ;
        case BlockAttachState::South: return Direction::PosZ;
        case BlockAttachState::West: return Direction::NegX;
        case BlockAttachState::East: return Direction::PosX;
        case BlockAttachState::Bottom:
        default: return Direction::NegY;
        }
    }

    inline Direction attachGridDirection(uint16_t blockState)
    {
        return attachDirection(static_cast<uint16_t>(blockState / 9u));
    }

    inline bool isDirectionalAttachBlock(const LightAttenuationTables* tables, uint16_t block)
    {
        if (tables == nullptr ||
            static_cast<std::size_t>(block) >= tables->blockRenderTypes.size() ||
            static_cast<std::size_t>(block) >= tables->blockStateKinds.size())
        {
            return false;
        }

        return (tables->blockRenderTypes[block] == BlockRenderType::Slab &&
                tables->blockStateKinds[block] == BlockStateKind::Attach) ||
            (tables->blockRenderTypes[block] == BlockRenderType::HalfSlab &&
                tables->blockStateKinds[block] == BlockStateKind::AttachGrid);
    }

    inline uint8_t directionalAttenuation(
        const LightAttenuationTables* tables,
        uint16_t block,
        uint16_t blockState,
        Direction direction,
        uint8_t baseAttenuation)
    {
        (void)blockState;
        (void)direction;
        if (isDirectionalAttachBlock(tables, block))
        {
            return 1;
        }
        return baseAttenuation;
    }
}
