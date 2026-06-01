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
        uint32_t verticalSection = 0;
        uint32_t horizontalSection = 0;
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
        Prop,
        Fire,
        Slab,
        HalfSlab
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

    enum class BlockAttachmentFace : uint8_t
    {
        None,
        Bottom
    };

    enum class BlockStateKind : uint8_t
    {
        None,
        Attach,
        AttachGrid
    };

    enum class BlockAttachState : uint16_t
    {
        Bottom = 0,
        Top = 1,
        North = 2,
        South = 3,
        West = 4,
        East = 5
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
        uint16_t breakLevel = 0;
        std::string breakAction = "none";
        uint8_t lightAttenuation = 15;
        uint8_t lightEmission = 0;
        bool randomOffset = false;
        bool breakEffectParticles = true;
        BlockStateKind stateKind = BlockStateKind::None;
        BlockAttachmentFace attachmentFace = BlockAttachmentFace::None;
        std::vector<std::string> interactActions;
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
        std::vector<BlockRenderType> blockRenderTypes;
        std::vector<BlockStateKind> blockStateKinds;
        std::vector<uint8_t> fluid;
    };

    using LightAttenuationTablesPtr = std::shared_ptr<const LightAttenuationTables>;
}
