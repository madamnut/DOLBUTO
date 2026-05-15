#pragma once

#include <cstdint>
#include <filesystem>

namespace dolbuto
{
    class DebugOverlayText;
    class ParticleRenderPath;
    class RendererGameplayBridge;
    class RendererTerrainRuntimeBridge;
    class RendererUiRuntimeBridge;
    namespace game
    {
        struct ClientRuntimeState;
    }
    struct RendererVulkanState;

    class RendererSceneLifecycleBridge
    {
    public:
        RendererSceneLifecycleBridge(
            game::ClientRuntimeState& client,
            RendererVulkanState& vulkan,
            ParticleRenderPath& particleRenderPath,
            DebugOverlayText& debugOverlayText,
            RendererTerrainRuntimeBridge& terrainRuntimeBridge,
            RendererGameplayBridge& gameplayBridge,
            RendererUiRuntimeBridge& uiRuntimeBridge);

        void loadGameScene(const std::filesystem::path& worldDirectory, uint64_t worldSeed);
        void unloadGameScene();

    private:
        game::ClientRuntimeState& client_;
        RendererVulkanState& vulkan_;
        ParticleRenderPath& particleRenderPath_;
        DebugOverlayText& debugOverlayText_;
        RendererTerrainRuntimeBridge& terrainRuntimeBridge_;
        RendererGameplayBridge& gameplayBridge_;
        RendererUiRuntimeBridge& uiRuntimeBridge_;
    };
}
