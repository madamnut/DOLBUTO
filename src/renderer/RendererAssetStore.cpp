#include "renderer/RendererAssetStore.h"

#include "renderer/ItemSpriteMeshBuilder.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace dolbuto
{
    namespace
    {
        float blockModelExtent(float explicitExtent, float fallback)
        {
            return std::clamp(explicitExtent > 0.0f ? explicitExtent : fallback, 0.0625f, 1.0f);
        }

        std::filesystem::path itemTexturePathOrDefault(const std::filesystem::path& itemTextureDir, const std::string& textureName)
        {
            const std::filesystem::path texturePath = itemTextureDir / (textureName + ".png");
            if (std::filesystem::exists(texturePath))
            {
                return texturePath;
            }
            return itemTextureDir / "default.png";
        }

        void generateCastPartSprites(const std::filesystem::path& itemTextureDir)
        {
            struct Cell
            {
                const char* form;
                int x = 0;
                int y = 0;
            };
            static constexpr std::array<const char*, 7> Metals{
                "tin",
                "zinc",
                "silver",
                "gold",
                "copper",
                "iron",
                "bronze"
            };
            static constexpr std::array<Cell, 9> Cells{{
                {"small_plate", 0, 0},
                {"short_rod", 1, 0},
                {"long_rod", 2, 0},
                {"large_preform", 0, 1},
                {"large_plate", 1, 1},
                {"rod", 2, 1},
                {"small_preform", 0, 2},
                {"plate", 1, 2},
                {"preform", 2, 2}
            }};

            const std::filesystem::path generatedDir = itemTextureDir / "generated";
            std::filesystem::create_directories(generatedDir);
            for (const char* metal : Metals)
            {
                const std::filesystem::path atlasPath = itemTextureDir / ("cast_parts_" + std::string(metal) + ".png");
                if (!std::filesystem::exists(atlasPath))
                {
                    continue;
                }

                int width = 0;
                int height = 0;
                int channels = 0;
                stbi_uc* pixels = stbi_load(atlasPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
                if (pixels == nullptr || width < 96 || height < 96)
                {
                    if (pixels != nullptr)
                    {
                        stbi_image_free(pixels);
                    }
                    continue;
                }

                const auto atlasWriteTime = std::filesystem::last_write_time(atlasPath);
                for (const Cell& cell : Cells)
                {
                    const std::string key = std::string(metal) + "_" + cell.form;
                    const std::filesystem::path outputPath = generatedDir / (key + ".png");
                    if (std::filesystem::exists(outputPath) &&
                        std::filesystem::last_write_time(outputPath) >= atlasWriteTime)
                    {
                        continue;
                    }

                    std::array<unsigned char, 32 * 32 * 4> output{};
                    for (int y = 0; y < 32; ++y)
                    {
                        for (int x = 0; x < 32; ++x)
                        {
                            const int sourceX = cell.x * 32 + x;
                            const int sourceY = cell.y * 32 + y;
                            const size_t sourceIndex = (static_cast<size_t>(sourceY) * static_cast<size_t>(width) + static_cast<size_t>(sourceX)) * 4u;
                            const size_t targetIndex = (static_cast<size_t>(y) * 32u + static_cast<size_t>(x)) * 4u;
                            output[targetIndex + 0u] = pixels[sourceIndex + 0u];
                            output[targetIndex + 1u] = pixels[sourceIndex + 1u];
                            output[targetIndex + 2u] = pixels[sourceIndex + 2u];
                            output[targetIndex + 3u] = pixels[sourceIndex + 3u];
                        }
                    }
                    stbi_write_png(outputPath.string().c_str(), 32, 32, 4, output.data(), 32 * 4);
                }
                stbi_image_free(pixels);
            }
        }

        DroppedItemRenderPath::ItemSpriteMesh buildBlockModelItemMesh(
            const BlockTextureLayers& layers,
            BlockRenderType renderType,
            float explicitWidth,
            float explicitHeight,
            float explicitDepth,
            bool useVerticalSection,
            const assets::PropMesh* propMesh)
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
            auto propAo = [](const std::array<Vec3, 4>& positions)
            {
                const Vec3 ab{
                    positions[1].x - positions[0].x,
                    positions[1].y - positions[0].y,
                    positions[1].z - positions[0].z
                };
                const Vec3 ac{
                    positions[2].x - positions[0].x,
                    positions[2].y - positions[0].y,
                    positions[2].z - positions[0].z
                };
                Vec3 normal{
                    ab.y * ac.z - ab.z * ac.y,
                    ab.z * ac.x - ab.x * ac.z,
                    ab.x * ac.y - ab.y * ac.x
                };
                const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                if (length > 0.0001f)
                {
                    normal.x /= length;
                    normal.y /= length;
                    normal.z /= length;
                }
                return std::clamp(0.68f + std::max(normal.y, 0.0f) * 0.24f + std::max(normal.x, 0.0f) * 0.06f, 0.62f, 1.0f);
            };
            if (renderType == BlockRenderType::Prop && propMesh != nullptr && !propMesh->quads.empty())
            {
                const uint32_t textureLayer = layers.faces[0];
                mesh.quads.reserve(propMesh->quads.size() / assets::PropQuadRenderFloatCount);
                for (size_t offset = 0; offset + assets::PropQuadRenderFloatCount <= propMesh->quads.size(); offset += assets::PropQuadRenderFloatCount)
                {
                    std::array<Vec3, 4> positions{};
                    std::array<std::array<float, 2>, 4> uvs{};
                    for (size_t vertex = 0; vertex < 4u; ++vertex)
                    {
                        const size_t positionOffset = offset + vertex * 3u;
                        positions[vertex] = Vec3{
                            propMesh->quads[positionOffset + 0u] - 0.5f,
                            propMesh->quads[positionOffset + 1u] - 0.5f,
                            propMesh->quads[positionOffset + 2u] - 0.5f
                        };
                    }
                    const size_t uvBase = offset + 12u;
                    for (size_t vertex = 0; vertex < 4u; ++vertex)
                    {
                        const size_t uvOffset = uvBase + vertex * 2u;
                        uvs[vertex] = {{
                            propMesh->quads[uvOffset + 0u],
                            propMesh->quads[uvOffset + 1u]
                        }};
                    }
                    addQuad(positions, uvs, textureLayer, propAo(positions));
                }
                return mesh;
            }
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
                const float width = blockModelExtent(explicitWidth, 1.0f);
                const float height = blockModelExtent(explicitHeight, 1.0f);
                const float depth = blockModelExtent(explicitDepth, 1.0f);
                const float minX = -0.5f * width;
                const float maxX = 0.5f * width;
                const float minY = -0.5f;
                const float maxY = minY + height;
                const float minZ = -0.5f * depth;
                const float maxZ = 0.5f * depth;
                const float floorTop = minY + height * 0.2f;
                const float wallThicknessX = width * 0.2f;
                const float wallThicknessZ = depth * 0.2f;
                addCuboid(minX, minY, minZ, maxX, floorTop, maxZ, textureLayer);
                addCuboid(minX, floorTop, minZ, maxX, maxY, minZ + wallThicknessZ, textureLayer);
                addCuboid(minX, floorTop, maxZ - wallThicknessZ, maxX, maxY, maxZ, textureLayer);
                addCuboid(minX, floorTop, minZ + wallThicknessZ, minX + wallThicknessX, maxY, maxZ - wallThicknessZ, textureLayer);
                addCuboid(maxX - wallThicknessX, floorTop, minZ + wallThicknessZ, maxX, maxY, maxZ - wallThicknessZ, textureLayer);
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
        generateCastPartSprites(itemTextureDir);
        store.moltenSurfaceMeshes.resize(2);

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
            constexpr std::string_view ItemTexturePrefix = "item/";
            if (textureName.rfind(ItemTexturePrefix, 0) == 0)
            {
                blockTexturePaths.push_back(itemTexturePathOrDefault(itemTextureDir, textureName.substr(ItemTexturePrefix.size())).string());
            }
            else
            {
                blockTexturePaths.push_back((blockTextureDir / (textureName + ".png")).string());
            }
        }
        store.terrainTextureArray = gpuResources.createTextureArray(blockTexturePaths);
        std::vector<std::string> fluidTexturePaths;
        fluidTexturePaths.reserve(content.fluidTextureNames().size());
        for (const std::string& textureName : content.fluidTextureNames())
        {
            fluidTexturePaths.push_back((fluidTextureDir / (textureName + ".png")).string());
        }
        if (fluidTexturePaths.empty())
        {
            fluidTexturePaths.push_back((fluidTextureDir / "water.png").string());
        }
        store.fluidTextureArray = gpuResources.createTextureArray(fluidTexturePaths);
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
                itemTexturePaths.push_back(itemTexturePathOrDefault(itemTextureDir, textureName).string());
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
                        definition.useBlockModelVerticalSection,
                        itemRenderType == BlockRenderType::Prop
                            ? content.propMeshForBlock(definition.modelBlockId)
                            : nullptr);
                }
                continue;
            }
            const bool hasDroppedLayeredTexture = definition.droppedBottomTexture != "none" && definition.droppedTopTexture != "none";
            const bool hasHeldLayeredTexture = definition.heldBottomTexture != "none" && definition.heldTopTexture != "none";
            if (definition.droppedTexture == "none" && !hasDroppedLayeredTexture && !hasHeldLayeredTexture)
            {
                continue;
            }
            if (hasDroppedLayeredTexture)
            {
                store.itemSpriteMeshes[itemId] = ItemSpriteMeshBuilder::buildLayered(
                    itemTexturePathOrDefault(itemTextureDir, definition.droppedBottomTexture),
                    definition.droppedBottomTextureLayer,
                    itemTexturePathOrDefault(itemTextureDir, definition.droppedTopTexture),
                    definition.droppedTopTextureLayer);
            }
            else if (hasHeldLayeredTexture)
            {
                store.itemSpriteMeshes[itemId] = ItemSpriteMeshBuilder::buildLayered(
                    itemTexturePathOrDefault(itemTextureDir, definition.heldBottomTexture),
                    definition.heldBottomTextureLayer,
                    itemTexturePathOrDefault(itemTextureDir, definition.heldTopTexture),
                    definition.heldTopTextureLayer);
            }
            else
            {
                store.itemSpriteMeshes[itemId] = ItemSpriteMeshBuilder::build(itemTexturePathOrDefault(itemTextureDir, definition.droppedTexture));
            }
        }

        store.propMeshesByBlock = content.propMeshesByBlock();

        auto texturePathForBlockLayer = [&](uint32_t textureLayer)
        {
            const std::vector<std::string>& textureNames = content.blockTextureNames();
            if (static_cast<size_t>(textureLayer) >= textureNames.size())
            {
                return blockTextureDir / "rock.png";
            }

            constexpr std::string_view ItemTexturePrefix = "item/";
            const std::string& textureName = textureNames[textureLayer];
            if (textureName.rfind(ItemTexturePrefix, 0) == 0)
            {
                return itemTexturePathOrDefault(itemTextureDir, textureName.substr(ItemTexturePrefix.size()));
            }
            return blockTextureDir / (textureName + ".png");
        };

        const std::vector<BlockDefinition>& blockDefinitions = content.blockDefinitions();
        const std::vector<BlockTextureLayers>& blockTextureLayers = content.blockTextureLayers();
        for (uint32_t blockId = 0; blockId < blockDefinitions.size() && blockId < blockTextureLayers.size(); ++blockId)
        {
            if (blockDefinitions[blockId].renderType != BlockRenderType::Mold)
            {
                continue;
            }

            const uint32_t topTextureLayer = blockTextureLayers[blockId].faces[0];
            const uint32_t bottomTextureLayer = blockTextureLayers[blockId].faces[1];
            store.moldMeshesByBlock[static_cast<uint16_t>(blockId)] = ItemSpriteMeshBuilder::buildBlockMold(
                texturePathForBlockLayer(bottomTextureLayer),
                bottomTextureLayer,
                texturePathForBlockLayer(topTextureLayer),
                topTextureLayer);
            const uint16_t moltenMeshId = static_cast<uint16_t>(store.moltenSurfaceMeshes.size());
            store.moldMoltenSurfaceMeshIdsByBlock[static_cast<uint16_t>(blockId)] = moltenMeshId;
            store.moltenSurfaceMeshes.push_back(ItemSpriteMeshBuilder::buildMoldCavitySurface(texturePathForBlockLayer(topTextureLayer)));
        }

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
        moldMeshesByBlock.clear();
    }
}
