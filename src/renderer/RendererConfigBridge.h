#pragma once

#include <filesystem>

namespace dolbuto
{
    class VulkanResourceManager;
    struct RendererAssetStore;
    namespace game
    {
        struct ClientRuntimeState;
    }

    class RendererConfigBridge
    {
    public:
        RendererConfigBridge(
            game::ClientRuntimeState& client,
            RendererAssetStore& rendererAssets,
            VulkanResourceManager& gpuResources);

        void loadContentAndAssets(const std::filesystem::path& assetDirectory);
        void loadWorldConfig(const std::filesystem::path& configDirectory);
        void loadRenderConfig(const std::filesystem::path& configDirectory);
        void loadTerrainLuts(const std::filesystem::path& assetDirectory);

    private:
        game::ClientRuntimeState& client_;
        RendererAssetStore& rendererAssets_;
        VulkanResourceManager& gpuResources_;
    };
}
