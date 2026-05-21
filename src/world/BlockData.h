#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dolbuto
{
    struct BlockTextureLayers
    {
        std::array<uint32_t, 6> faces{};
    };

    struct BlockDrop
    {
        uint16_t itemId = 0;
        uint16_t min = 1;
        uint16_t max = 1;
        float chance = 1.0f;
    };

    enum class BlockRenderType : uint8_t
    {
        None,
        Cube,
        Cross,
        Prop
    };

    enum class BlockFaceOcclusion : uint8_t
    {
        None,
        Opaque,
        Cutout
    };

    enum class BlockAlphaMode : uint8_t
    {
        Opaque,
        Cutout,
        Blend
    };

    struct BlockDefinition
    {
        std::string name = "unknown";
        BlockRenderType renderType = BlockRenderType::None;
        bool directional = false;
        bool collision = false;
        bool ao = false;
        BlockFaceOcclusion faceOcclusion = BlockFaceOcclusion::None;
        bool sameBlockFaceCulling = false;
        BlockAlphaMode alphaMode = BlockAlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        float alphaBlend = 1.0f;
        float mipDistanceScale = 1.0f;
        float hardness = -1.0f;
        uint8_t lightAttenuation = 15;
        uint8_t lightEmission = 0;
        bool randomOffset = false;
        std::vector<BlockDrop> drops;
    };

    struct FluidDefinition
    {
        std::string name = "none";
        uint8_t lightAttenuation = 0;
    };

    struct LightAttenuationTables
    {
        std::vector<uint8_t> block;
        std::vector<uint8_t> blockEmission;
        std::vector<uint8_t> fluid;
    };

    using LightAttenuationTablesPtr = std::shared_ptr<const LightAttenuationTables>;
}
