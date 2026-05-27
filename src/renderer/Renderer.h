#pragma once

#include "camera/Camera.h"
#include "game/ClientRuntimeState.h"
#include "game/ClientUiTypes.h"
#include "gameplay/PlayerInventory.h"
#include "items/ItemData.h"
#include "renderer/CloudRenderPath.h"
#include "renderer/DroppedItemRenderPath.h"
#include "renderer/DebugOverlayText.h"
#include "renderer/ParticleRenderPath.h"
#include "renderer/RendererAssetStore.h"
#include "renderer/RendererFrame.h"
#include "renderer/RendererGpuResources.h"
#include "renderer/RendererTypes.h"
#include "renderer/RendererVulkanState.h"
#include "renderer/PlayerMeshRenderPath.h"
#include "renderer/RadialMenuRenderPath.h"
#include "renderer/ScreenPresentation.h"
#include "renderer/SkyRenderPath.h"
#include "renderer/SpriteRenderPath.h"
#include "renderer/TerrainRenderPath.h"
#include "renderer/TerrainTypes.h"
#include "renderer/TextRenderPath.h"
#include "world/WorldTypes.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dolbuto
{
    namespace game
    {
        class ClientRenderRuntime;
    }

    class RendererRmlUiBackend;
    class RendererAudioBridge;
    class RendererConfigBridge;
    class RendererGameplayBridge;
    class RendererSceneLifecycleBridge;
    class RendererTerrainRuntimeBridge;
    class RendererDiagnosticsBridge;
    class RendererUiRuntimeBridge;

    class Renderer
    {
    public:
        Renderer(GLFWwindow* window, game::ClientRuntimeState& client);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        void drawFrame(const RendererFrame& frame);
        void setFramebufferResized();

    private:
        friend class game::ClientRenderRuntime;

        #include "renderer/RendererVulkanMethods.inc"
        #include "renderer/RendererRenderMethods.inc"

        GLFWwindow* window_ = nullptr;

        RendererVulkanState vulkan_;
        game::ClientRuntimeState& client_;
        DebugOverlayText debugOverlayText_;
        VulkanResourceManager gpuResources_;
        TerrainRenderPath terrainRenderPath_;
        TextRenderPath textRenderPath_;
        SkyRenderPath skyRenderPath_;
        CloudRenderPath cloudRenderPath_;
        SpriteRenderPath spriteRenderPath_;
        ScreenPresentation screenPresentation_;
        PlayerMeshRenderPath playerMeshRenderPath_;
        ParticleRenderPath particleRenderPath_;
        DroppedItemRenderPath droppedItemRenderPath_;
        RadialMenuRenderPath radialMenuRenderPath_;
        VkDeviceSize localMemoryHeapSize_ = 0;
        uint32_t localMemoryHeapIndex_ = UINT32_MAX;
        bool memoryBudgetSupported_ = false;
        RendererAssetStore rendererAssets_;
        std::unique_ptr<RendererAudioBridge> audioBridge_;
        std::unique_ptr<RendererConfigBridge> configBridge_;
        std::unique_ptr<RendererTerrainRuntimeBridge> terrainRuntimeBridge_;
        std::unique_ptr<RendererGameplayBridge> gameplayBridge_;
        std::unique_ptr<RendererSceneLifecycleBridge> sceneLifecycleBridge_;
        std::unique_ptr<RendererDiagnosticsBridge> diagnosticsBridge_;
        std::unique_ptr<RendererRmlUiBackend> rmlUiBackend_;
        std::unique_ptr<RendererUiRuntimeBridge> uiRuntimeBridge_;
        std::vector<Texture> sceneColorTargets_;
        std::vector<Texture> sceneDepthTargets_;
    };
}
