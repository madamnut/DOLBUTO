#pragma once

#include "assets/PropModelLoader.h"
#include "game/ClientContent.h"
#include "renderer/DroppedItemRenderPath.h"
#include "renderer/RendererGpuResources.h"

#include <cstdint>
#include <filesystem>
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
        Texture itemTextureArray;
        std::vector<DroppedItemRenderPath::ItemSpriteMesh> itemSpriteMeshes;
        std::unordered_map<uint16_t, assets::PropMesh> propMeshesByBlock;

        static RendererAssetStore load(
            const std::filesystem::path& assetDirectory,
            const game::ClientContent& content,
            const VulkanResourceManager& gpuResources);

        void destroy(const VulkanResourceManager& gpuResources);
    };
}
