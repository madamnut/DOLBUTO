#include "renderer/RendererAssetStore.h"

#include "renderer/ItemSpriteMeshBuilder.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

namespace dolbuto
{
    namespace
    {
        float blockModelExtent(float explicitExtent, float fallback)
        {
            return std::clamp(explicitExtent > 0.0f ? explicitExtent : fallback, 0.0625f, 1.0f);
        }

        DroppedItemRenderPath::ItemSpriteMesh buildBlockModelItemMesh(
            const BlockTextureLayers& layers,
            BlockRenderType renderType,
            float explicitWidth,
            float explicitHeight,
            float explicitDepth,
            bool useVerticalSection)
        {
            DroppedItemRenderPath::ItemSpriteMesh mesh{};
            auto addQuad = [&](std::array<Vec3, 4> positions, std::array<std::array<float, 2>, 4> uvs, uint32_t textureLayer, float ao)
            {
                DroppedItemRenderPath::ItemSpriteQuad quad{};
                quad.positions = positions;
                quad.uvs = uvs;
                quad.ao = ao;
                quad.textureLayer = static_cast<float>(textureLayer);
                mesh.quads.push_back(quad);
            };
            auto addCuboid = [&](
                float minX,
                float minY,
                float minZ,
                float maxX,
                float maxY,
                float maxZ,
                uint32_t textureLayer)
            {
                const float width = maxX - minX;
                const float height = maxY - minY;
                const float depth = maxZ - minZ;
                const std::array<std::array<float, 2>, 4> topUvs{{{{0.0f, 0.0f}}, {{0.0f, depth}}, {{width, depth}}, {{width, 0.0f}}}};
                const std::array<std::array<float, 2>, 4> xSideUvs{{{{0.0f, 1.0f}}, {{0.0f, 1.0f - height}}, {{depth, 1.0f - height}}, {{depth, 1.0f}}}};
                const std::array<std::array<float, 2>, 4> zSideUvs{{{{0.0f, 1.0f}}, {{0.0f, 1.0f - height}}, {{width, 1.0f - height}}, {{width, 1.0f}}}};
                addQuad(
                    {{{minX, maxY, minZ}, {minX, maxY, maxZ}, {maxX, maxY, maxZ}, {maxX, maxY, minZ}}},
                    topUvs,
                    textureLayer,
                    1.0f);
                addQuad(
                    {{{minX, minY, maxZ}, {minX, minY, minZ}, {maxX, minY, minZ}, {maxX, minY, maxZ}}},
                    topUvs,
                    textureLayer,
                    0.82f);
                addQuad(
                    {{{maxX, minY, minZ}, {maxX, maxY, minZ}, {maxX, maxY, maxZ}, {maxX, minY, maxZ}}},
                    xSideUvs,
                    textureLayer,
                    0.86f);
                addQuad(
                    {{{minX, minY, maxZ}, {minX, maxY, maxZ}, {minX, maxY, minZ}, {minX, minY, minZ}}},
                    xSideUvs,
                    textureLayer,
                    0.78f);
                addQuad(
                    {{{maxX, minY, maxZ}, {maxX, maxY, maxZ}, {minX, maxY, maxZ}, {minX, minY, maxZ}}},
                    zSideUvs,
                    textureLayer,
                    0.88f);
                addQuad(
                    {{{minX, minY, minZ}, {minX, maxY, minZ}, {maxX, maxY, minZ}, {maxX, minY, minZ}}},
                    zSideUvs,
                    textureLayer,
                    0.74f);
            };

            if (renderType == BlockRenderType::Crucible)
            {
                const uint32_t textureLayer = layers.faces[0];
                addCuboid(-0.5f, -0.5f, -0.5f, 0.5f, -0.3f, 0.5f, textureLayer);
                addCuboid(-0.5f, -0.3f, -0.5f, 0.5f, 0.5f, -0.3f, textureLayer);
                addCuboid(-0.5f, -0.3f, 0.3f, 0.5f, 0.5f, 0.5f, textureLayer);
                addCuboid(-0.5f, -0.3f, -0.3f, -0.3f, 0.5f, 0.3f, textureLayer);
                addCuboid(0.3f, -0.3f, -0.3f, 0.5f, 0.5f, 0.3f, textureLayer);
                return mesh;
            }

            const float width = blockModelExtent(explicitWidth, 1.0f);
            const float height = blockModelExtent(explicitHeight, renderType == BlockRenderType::Slab ? 0.5f : 1.0f);
            const float depth = blockModelExtent(explicitDepth, 1.0f);
            const float minX = -0.5f * width;
            const float maxX = 0.5f * width;
            const float minY = -0.5f;
            const float maxY = minY + height;
            const float minZ = -0.5f * depth;
            const float maxZ = 0.5f * depth;
            const float minLocalY = 0.0f;
            const float maxLocalY = height;
            const float sideTopV = 1.0f - maxLocalY;
            const float sideBottomV = 1.0f - minLocalY;
            addQuad(
                {{{minX, maxY, minZ}, {minX, maxY, maxZ}, {maxX, maxY, maxZ}, {maxX, maxY, minZ}}},
                {{{0.0f, 0.0f}, {0.0f, depth}, {width, depth}, {width, 0.0f}}},
                layers.faces[0],
                1.0f);
            addQuad(
                {{{minX, minY, maxZ}, {minX, minY, minZ}, {maxX, minY, minZ}, {maxX, minY, maxZ}}},
                {{{0.0f, depth}, {0.0f, 0.0f}, {width, 0.0f}, {width, depth}}},
                layers.faces[1],
                0.82f);
            addQuad(
                {{{maxX, minY, minZ}, {maxX, maxY, minZ}, {maxX, maxY, maxZ}, {maxX, minY, maxZ}}},
                useVerticalSection
                    ? std::array<std::array<float, 2>, 4>{{{{0.0f, 1.0f}}, {{0.0f, 0.0f}}, {{1.0f, 0.0f}}, {{1.0f, 1.0f}}}}
                    : std::array<std::array<float, 2>, 4>{{{{0.0f, sideBottomV}}, {{0.0f, sideTopV}}, {{depth, sideTopV}}, {{depth, sideBottomV}}}},
                useVerticalSection ? layers.verticalSection : layers.faces[2],
                0.86f);
            addQuad(
                {{{minX, minY, maxZ}, {minX, maxY, maxZ}, {minX, maxY, minZ}, {minX, minY, minZ}}},
                {{{0.0f, sideBottomV}, {0.0f, sideTopV}, {depth, sideTopV}, {depth, sideBottomV}}},
                layers.faces[3],
                0.78f);
            addQuad(
                {{{maxX, minY, maxZ}, {maxX, maxY, maxZ}, {minX, maxY, maxZ}, {minX, minY, maxZ}}},
                {{{0.0f, sideBottomV}, {0.0f, sideTopV}, {width, sideTopV}, {width, sideBottomV}}},
                layers.faces[4],
                0.88f);
            addQuad(
                {{{minX, minY, minZ}, {minX, maxY, minZ}, {maxX, maxY, minZ}, {maxX, minY, minZ}}},
                {{{0.0f, sideBottomV}, {0.0f, sideTopV}, {width, sideTopV}, {width, sideBottomV}}},
                layers.faces[5],
                0.74f);
            return mesh;
        }
    }

    RendererAssetStore RendererAssetStore::load(
        const std::filesystem::path& assetDirectory,
        const game::ClientContent& content,
        const VulkanResourceManager& gpuResources)
    {
        RendererAssetStore store{};

        const std::filesystem::path blockTextureDir = assetDirectory / "textures" / "block";
        const std::filesystem::path fluidTextureDir = assetDirectory / "textures" / "fluid";
        const std::filesystem::path itemTextureDir = assetDirectory / "textures" / "item";
        const std::filesystem::path smokeTextureDir = assetDirectory / "textures" / "particle" / "smoke";

        store.sun = gpuResources.createTexture((assetDirectory / "textures" / "sky" / "Sun.png").string());
        store.moon = gpuResources.createTexture((assetDirectory / "textures" / "sky" / "Moon.png").string());
        store.crosshair = gpuResources.createTexture((assetDirectory / "textures" / "ui" / "Crosshair.png").string());
        const std::array<unsigned char, 4> whitePixel = {255u, 255u, 255u, 255u};
        store.white = gpuResources.createTextureFromRgba(whitePixel.data(), 1, 1);
        store.lobbyBackground = gpuResources.createTexture((assetDirectory / "textures" / "block" / "rock.png").string());
        store.lobbyTitle = gpuResources.createTexture((assetDirectory / "textures" / "ui" / "Title.png").string());
        store.playerTexture = gpuResources.createTextureArray({(assetDirectory / "textures" / "character" / "Character.png").string()});

        std::vector<std::string> blockTexturePaths;
        blockTexturePaths.reserve(content.blockTextureNames().size());
        for (const std::string& textureName : content.blockTextureNames())
        {
            blockTexturePaths.push_back((blockTextureDir / (textureName + ".png")).string());
        }
        store.terrainTextureArray = gpuResources.createTextureArray(blockTexturePaths);
        store.fluidTextureArray = gpuResources.createTextureArray({(fluidTextureDir / "water.png").string()});
        store.smokeParticleTextureArray = gpuResources.createTextureArray({
            (smokeTextureDir / "smoke_0.png").string(),
            (smokeTextureDir / "smoke_1.png").string(),
            (smokeTextureDir / "smoke_2.png").string(),
            (smokeTextureDir / "smoke_3.png").string(),
            (smokeTextureDir / "smoke_4.png").string(),
            (smokeTextureDir / "smoke_5.png").string(),
            (smokeTextureDir / "smoke_6.png").string(),
            (smokeTextureDir / "smoke_7.png").string(),
            (smokeTextureDir / "smoke_darker_0.png").string(),
            (smokeTextureDir / "smoke_darker_1.png").string(),
            (smokeTextureDir / "smoke_darker_2.png").string(),
            (smokeTextureDir / "smoke_darker_3.png").string(),
            (smokeTextureDir / "smoke_darker_4.png").string(),
            (smokeTextureDir / "smoke_darker_5.png").string(),
            (smokeTextureDir / "smoke_darker_6.png").string(),
            (smokeTextureDir / "smoke_darker_7.png").string(),
            (smokeTextureDir / "smoke_lighter_0.png").string(),
            (smokeTextureDir / "smoke_lighter_1.png").string(),
            (smokeTextureDir / "smoke_lighter_2.png").string(),
            (smokeTextureDir / "smoke_lighter_3.png").string(),
            (smokeTextureDir / "smoke_lighter_4.png").string(),
            (smokeTextureDir / "smoke_lighter_5.png").string(),
            (smokeTextureDir / "smoke_lighter_6.png").string(),
            (smokeTextureDir / "smoke_lighter_7.png").string()
        });

        if (!content.itemTextureNames().empty())
        {
            std::vector<std::string> itemTexturePaths;
            itemTexturePaths.reserve(content.itemTextureNames().size());
            for (const std::string& textureName : content.itemTextureNames())
            {
                itemTexturePaths.push_back((itemTextureDir / (textureName + ".png")).string());
            }
            store.itemTextureArray = gpuResources.createTextureArray(itemTexturePaths);
        }

        store.itemSpriteMeshes.assign(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u, {});
        const std::vector<ItemDefinition>& itemDefinitions = content.itemDefinitions();
        for (size_t itemId = 0; itemId < itemDefinitions.size(); ++itemId)
        {
            const ItemDefinition& definition = itemDefinitions[itemId];
            if (definition.droppedRender == ItemRenderType::BlockModel ||
                definition.heldRender == ItemRenderType::BlockModel)
            {
                const bool usesModelTexture = definition.hasModelTexture;
                if (usesModelTexture ||
                    (definition.modelBlockId != 0 &&
                        static_cast<size_t>(definition.modelBlockId) < content.blockTextureLayers().size()))
                {
                    BlockTextureLayers itemLayers{};
                    BlockRenderType itemRenderType = BlockRenderType::Cube;
                    if (usesModelTexture)
                    {
                        itemLayers.faces.fill(definition.modelTextureLayer);
                        itemLayers.verticalSection = definition.modelTextureLayer;
                        itemLayers.horizontalSection = definition.modelTextureLayer;
                        itemRenderType = definition.useBlockModelCrucibleShape ? BlockRenderType::Crucible : BlockRenderType::Cube;
                    }
                    else
                    {
                        const BlockDefinition& blockDefinition = content.blockDefinitions()[definition.modelBlockId];
                        itemLayers = content.blockTextureLayers()[definition.modelBlockId];
                        itemRenderType = definition.useBlockModelCrucibleShape ? BlockRenderType::Crucible : blockDefinition.renderType;
                    }
                    store.itemSpriteMeshes[itemId] = buildBlockModelItemMesh(
                        itemLayers,
                        itemRenderType,
                        definition.blockModelWidth,
                        definition.blockModelHeight,
                        definition.blockModelDepth,
                        definition.useBlockModelVerticalSection);
                }
                continue;
            }
            if (definition.droppedTexture == "none")
            {
                continue;
            }
            store.itemSpriteMeshes[itemId] = ItemSpriteMeshBuilder::build(itemTextureDir / (definition.droppedTexture + ".png"));
        }

        store.propMeshesByBlock = content.propMeshesByBlock();

        return store;
    }

    void RendererAssetStore::destroy(const VulkanResourceManager& gpuResources)
    {
        gpuResources.destroyTexture(terrainTextureArray);
        gpuResources.destroyTexture(fluidTextureArray);
        gpuResources.destroyTexture(smokeParticleTextureArray);
        gpuResources.destroyTexture(playerTexture);
        gpuResources.destroyTexture(font);
        gpuResources.destroyTexture(white);
        gpuResources.destroyTexture(lobbyTitle);
        gpuResources.destroyTexture(lobbyBackground);
        gpuResources.destroyTexture(crosshair);
        gpuResources.destroyTexture(terrainPvOverlay);
        gpuResources.destroyTexture(terrainWeirdnessOverlay);
        gpuResources.destroyTexture(terrainSmoothnessOverlay);
        gpuResources.destroyTexture(terrainGroundnessOverlay);
        gpuResources.destroyTexture(climatePrecipitationOverlay);
        gpuResources.destroyTexture(climateTemperatureOverlay);
        gpuResources.destroyTexture(moon);
        gpuResources.destroyTexture(sun);
        gpuResources.destroyTexture(itemTextureArray);
        itemSpriteMeshes.clear();
        propMeshesByBlock.clear();
    }
}
