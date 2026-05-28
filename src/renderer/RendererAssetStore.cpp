#include "renderer/RendererAssetStore.h"

#include "renderer/ItemSpriteMeshBuilder.h"

#include <array>
#include <limits>
#include <string>
#include <utility>

namespace dolbuto
{
    namespace
    {
        DroppedItemRenderPath::ItemSpriteMesh buildBlockModelItemMesh(const BlockTextureLayers& layers)
        {
            DroppedItemRenderPath::ItemSpriteMesh mesh{};
            auto addQuad = [&](std::array<Vec3, 4> positions, uint32_t textureLayer, float ao)
            {
                DroppedItemRenderPath::ItemSpriteQuad quad{};
                quad.positions = positions;
                quad.uvs = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
                quad.ao = ao;
                quad.textureLayer = static_cast<float>(textureLayer);
                mesh.quads.push_back(quad);
            };

            addQuad({{{-0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}}}, layers.faces[0], 1.0f);
            addQuad({{{-0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}}}, layers.faces[1], 0.82f);
            addQuad({{{0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}}}, layers.faces[2], 0.86f);
            addQuad({{{-0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}}}, layers.faces[3], 0.78f);
            addQuad({{{0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}}}, layers.faces[4], 0.88f);
            addQuad({{{-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}}}, layers.faces[5], 0.74f);
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
                if (definition.placeBlockId != 0 &&
                    static_cast<size_t>(definition.placeBlockId) < content.blockTextureLayers().size())
                {
                    store.itemSpriteMeshes[itemId] = buildBlockModelItemMesh(content.blockTextureLayers()[definition.placeBlockId]);
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
