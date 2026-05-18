#include "renderer/RendererAssetStore.h"

#include "platform/Log.h"
#include "renderer/ItemSpriteMeshBuilder.h"

#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace dolbuto
{
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
            if (definition.droppedTexture == "none")
            {
                continue;
            }
            store.itemSpriteMeshes[itemId] = ItemSpriteMeshBuilder::build(itemTextureDir / (definition.droppedTexture + ".png"));
        }

        const std::filesystem::path propModelDirectory = assetDirectory / "textures" / "block" / "model";
        std::unordered_set<std::string> checkedPropModels;
        for (const game::PropModelBinding& binding : content.propModelBindings())
        {
            if (checkedPropModels.insert(binding.modelName).second)
            {
                assets::ensurePropModelBinary(propModelDirectory, binding.modelName);
            }
        }

        for (const game::PropModelBinding& binding : content.propModelBindings())
        {
            const std::filesystem::path dpmPath = propModelDirectory / (binding.modelName + ".dpm");
            assets::PropMesh mesh = assets::loadDpmRenderMesh(dpmPath);
            if (mesh.quads.empty())
            {
                log::warn("Prop model dpm could not be loaded: " + dpmPath.string());
                continue;
            }
            store.propMeshesByBlock[binding.blockId] = std::move(mesh);
        }

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
