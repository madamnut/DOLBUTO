#include "renderer/RendererSceneLifecycleBridge.h"

#include "renderer/DebugOverlayText.h"
#include "renderer/ParticleRenderPath.h"
#include "game/ClientRuntimeState.h"
#include "renderer/RendererGameplayBridge.h"
#include "renderer/RendererTerrainRuntimeBridge.h"
#include "renderer/RendererUiRuntimeBridge.h"
#include "renderer/RendererVulkanState.h"
#include "world/WorldTypes.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <filesystem>
#include <utility>

namespace dolbuto
{
    RendererSceneLifecycleBridge::RendererSceneLifecycleBridge(
        game::ClientRuntimeState& client,
        RendererVulkanState& vulkan,
        ParticleRenderPath& particleRenderPath,
        DebugOverlayText& debugOverlayText,
        RendererTerrainRuntimeBridge& terrainRuntimeBridge,
        RendererGameplayBridge& gameplayBridge,
        RendererUiRuntimeBridge& uiRuntimeBridge) :
        client_(client),
        vulkan_(vulkan),
        particleRenderPath_(particleRenderPath),
        debugOverlayText_(debugOverlayText),
        terrainRuntimeBridge_(terrainRuntimeBridge),
        gameplayBridge_(gameplayBridge),
        uiRuntimeBridge_(uiRuntimeBridge)
    {
    }

    void RendererSceneLifecycleBridge::loadGameScene(const std::filesystem::path& worldDirectory, uint64_t worldSeed)
    {
        game::ClientSceneLifecycle::LoadHooks hooks{};
        hooks.clearParticles = [this](double timestamp)
        {
            particleRenderPath_.clear(timestamp);
        };
        hooks.resetBlockBreaking = [this]
        {
            gameplayBridge_.resetBlockBreaking();
        };
        hooks.refreshInventoryUi = [this]
        {
            uiRuntimeBridge_.updateInventoryUi();
        };
        hooks.refreshInventoryDebugSlots = [this]
        {
            client_.uiBridge.updateInventoryDebugSlots();
        };
        hooks.resetClimateOverlays = [this]
        {
            client_.climateTemperatureOverlayReady = false;
            client_.climatePrecipitationOverlayReady = false;
        };
        hooks.processRenderMeshJob = [this](TerrainJob job)
        {
            return terrainRuntimeBridge_.processRenderTerrainMeshJob(std::move(job));
        };

        client_.sceneLifecycle.loadGameScene(
            worldDirectory,
            worldSeed,
            client_.worldConfig.terrainWorkerCount,
            terrainRuntimeBridge_.terrainBuilderConfig(),
            glfwGetTime(),
            hooks);
    }

    void RendererSceneLifecycleBridge::unloadGameScene()
    {
        game::ClientSceneLifecycle::UnloadHooks hooks{};
        hooks.waitForRendererIdle = [this]
        {
            vkDeviceWaitIdle(vulkan_.device);
        };
        hooks.destroyTerrainRenderData = [this]
        {
            terrainRuntimeBridge_.destroyAllTerrainChunks();
        };
        hooks.clearParticles = [this]
        {
            particleRenderPath_.clear();
        };
        hooks.resetBlockBreaking = [this]
        {
            gameplayBridge_.resetBlockBreaking();
        };
        hooks.refreshInventoryUi = [this]
        {
            uiRuntimeBridge_.updateInventoryUi();
        };
        hooks.refreshInventoryDebugSlots = [this]
        {
            client_.uiBridge.updateInventoryDebugSlots();
        };
        hooks.updateTerrainStats = [this]
        {
            terrainRuntimeBridge_.updateTerrainStats();
        };
        hooks.markDebugDirty = [this]
        {
            debugOverlayText_.markDirty();
        };

        client_.sceneLifecycle.unloadGameScene(hooks);
    }
}
