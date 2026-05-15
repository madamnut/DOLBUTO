#pragma once

#include "game/ClientTerrainSceneRuntime.h"
#include "game/ClientWorldRuntime.h"
#include "gameplay/ClientGameplayRuntime.h"
#include "world/TerrainBuilder.h"
#include "world/TerrainJobSystem.h"
#include "world/WorldTypes.h"

#include <cstdint>
#include <filesystem>
#include <functional>

namespace dolbuto::game
{
    class ClientSceneLifecycle
    {
    public:
        using RenderMeshJobProcessor = std::function<world::TerrainJobResult(TerrainJob)>;
        using VoidHook = std::function<void()>;
        using TimedHook = std::function<void(double)>;

        struct LoadHooks
        {
            TimedHook clearParticles;
            VoidHook resetBlockBreaking;
            VoidHook refreshInventoryUi;
            VoidHook refreshInventoryDebugSlots;
            VoidHook resetClimateOverlays;
            RenderMeshJobProcessor processRenderMeshJob;
        };

        struct UnloadHooks
        {
            VoidHook waitForRendererIdle;
            VoidHook destroyTerrainRenderData;
            VoidHook clearParticles;
            VoidHook resetBlockBreaking;
            VoidHook refreshInventoryUi;
            VoidHook refreshInventoryDebugSlots;
            VoidHook updateTerrainStats;
            VoidHook markDebugDirty;
        };

        ClientSceneLifecycle(
            ClientWorldRuntime& worldRuntime,
            ClientTerrainSceneRuntime& terrainSceneRuntime,
            gameplay::ClientGameplayRuntime& gameplayRuntime);

        bool loadGameScene(
            const std::filesystem::path& worldDirectory,
            uint64_t worldSeed,
            int terrainWorkerCount,
            world::TerrainBuilderConfig terrainConfig,
            double timestamp,
            const LoadHooks& hooks);

        bool unloadGameScene(const UnloadHooks& hooks);

    private:
        static void run(const VoidHook& hook);
        static void run(const TimedHook& hook, double value);

        ClientWorldRuntime& worldRuntime_;
        ClientTerrainSceneRuntime& terrainSceneRuntime_;
        gameplay::ClientGameplayRuntime& gameplayRuntime_;
    };
}
