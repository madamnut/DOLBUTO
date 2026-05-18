#include "game/ClientSceneLifecycle.h"

#include "platform/Log.h"

namespace dolbuto::game
{
    ClientSceneLifecycle::ClientSceneLifecycle(
        ClientWorldRuntime& worldRuntime,
        ClientTerrainSceneRuntime& terrainSceneRuntime,
        gameplay::ClientGameplayRuntime& gameplayRuntime) :
        worldRuntime_(worldRuntime),
        terrainSceneRuntime_(terrainSceneRuntime),
        gameplayRuntime_(gameplayRuntime)
    {
    }

    bool ClientSceneLifecycle::loadGameScene(
        const std::filesystem::path& worldDirectory,
        uint64_t worldSeed,
        int terrainWorkerCount,
        const TerrainConfigProvider& terrainConfigProvider,
        double timestamp,
        const LoadHooks& hooks)
    {
        if (terrainSceneRuntime_.gameSceneLoaded())
        {
            return false;
        }

        log::info("Loading game scene: " + worldDirectory.string());
        worldRuntime_.setActiveWorld(worldDirectory, worldSeed);
        const world::TerrainBuilderConfig terrainConfig = terrainConfigProvider ? terrainConfigProvider() : world::TerrainBuilderConfig{};
        run(hooks.clearParticles, timestamp);
        run(hooks.resetBlockBreaking);
        gameplayRuntime_.resetForScene(timestamp);
        run(hooks.refreshInventoryUi);
        run(hooks.refreshInventoryDebugSlots);
        run(hooks.resetClimateOverlays);
        terrainSceneRuntime_.resetLoadRequest();
        terrainSceneRuntime_.startSaveWorker();
        terrainSceneRuntime_.startChunkLoadWorker();
        terrainSceneRuntime_.startTerrainWorkers(
            terrainWorkerCount,
            terrainConfig,
            hooks.processRenderMeshJob);
        terrainSceneRuntime_.setGameSceneLoaded(true);
        log::info("Game scene loaded.");
        return true;
    }

    bool ClientSceneLifecycle::unloadGameScene(const UnloadHooks& hooks)
    {
        if (!terrainSceneRuntime_.gameSceneLoaded())
        {
            return false;
        }

        log::info("Unloading game scene.");
        terrainSceneRuntime_.stopTerrainWorkers();
        terrainSceneRuntime_.stopChunkLoadWorker();
        worldRuntime_.enqueueSaveAllRuntimeChunks();
        terrainSceneRuntime_.stopSaveWorker();
        run(hooks.waitForRendererIdle);
        run(hooks.destroyTerrainRenderData);
        worldRuntime_.saveSystem.clear();
        terrainSceneRuntime_.resetLoadRequest();
        run(hooks.clearParticles);
        run(hooks.resetBlockBreaking);
        gameplayRuntime_.resetForUnload();
        run(hooks.refreshInventoryUi);
        run(hooks.refreshInventoryDebugSlots);
        run(hooks.updateTerrainStats);
        run(hooks.markDebugDirty);
        terrainSceneRuntime_.setGameSceneLoaded(false);
        log::info("Game scene unloaded.");
        return true;
    }

    void ClientSceneLifecycle::run(const VoidHook& hook)
    {
        if (hook)
        {
            hook();
        }
    }

    void ClientSceneLifecycle::run(const TimedHook& hook, double value)
    {
        if (hook)
        {
            hook(value);
        }
    }
}
