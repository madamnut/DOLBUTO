#pragma once

#include "assets/PropModelLoader.h"
#include "game/ClientContent.h"
#include "renderer/DroppedItemRenderPath.h"
#include "renderer/RendererGpuResources.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace dolbuto
{
    struct RendererAssetStore
    {
        Texture sun;
        Texture moon;
        Texture crosshair;
        Texture white;
        Texture lobbyBackground;
        Texture lobbyTitle;
        Texture climateTemperatureOverlay;
        Texture climatePrecipitationOverlay;
        Texture terrainGroundnessOverlay;
        Texture terrainSmoothnessOverlay;
        Texture terrainWeirdnessOverlay;
        Texture terrainPvOverlay;
        Texture font;
        Texture playerTexture;
        Texture terrainTextureArray;
        Texture fluidTextureArray;
        Texture smokeParticleTextureArray;
        Texture itemTextureArray;
        std::vector<DroppedItemRenderPath::ItemSpriteMesh> itemSpriteMeshes;
        std::unordered_map<std::string, uint16_t> itemSpriteMeshIdsByTextureName;
        std::vector<DroppedItemRenderPath::ItemSpriteMesh> moltenSurfaceMeshes;
        std::unordered_map<uint16_t, assets::PropMesh> propMeshesByBlock;
        std::unordered_map<uint16_t, DroppedItemRenderPath::ItemSpriteMesh> moldMeshesByBlock;
        std::unordered_map<uint16_t, uint16_t> moldMoltenSurfaceMeshIdsByBlock;

        static RendererAssetStore load(
            const std::filesystem::path& assetDirectory,
            const game::ClientContent& content,
            const VulkanResourceManager& gpuResources);

        void destroy(const VulkanResourceManager& gpuResources);
    };
}
