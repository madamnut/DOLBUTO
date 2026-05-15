#pragma once

#include "audio/AudioSystem.h"
#include "game/ClientContent.h"
#include "game/ClientSceneLifecycle.h"
#include "game/ClientTerrainSceneRuntime.h"
#include "game/ClientWorldRuntime.h"
#include "gameplay/ClientGameplayRuntime.h"
#include "ui/ClientUiBridge.h"
#include "ui/UiSystem.h"
#include "world/WorldRuntime.h"

#include <array>
#include <chrono>
#include <cstdint>

namespace dolbuto::game
{
    struct ClientSelectionState
    {
        bool hasSelectedBlock = false;
        int selectedBlockX = 0;
        int selectedBlockY = 0;
        int selectedBlockZ = 0;
        uint16_t selectedBlockId = 0;
    };

    struct ClientWorldConfigState
    {
        int loadGridScale = 0;
        int terrainWorkerCount = 4;
        int maxTerrainUploadChunksPerFrame = 8;
        int maxTerrainUnloadChunksPerFrame = 16;
        int maxTerrainRetiredDestroyPerFrame = 4;
        float terrainNoiseFeatureScale = 0.0f;
        int terrainNoiseOctaveCount = 0;
        float terrainNoiseLacunarity = 0.0f;
        float terrainNoiseGain = 0.0f;
        float terrainNoiseSimplexScale = 0.0f;
        bool terrainDomainWarpEnabled = false;
        float terrainDomainWarpAmplitude = 0.0f;
        float terrainDomainWarpFrequency = 0.0f;
        int terrainDomainWarpOctaveCount = 0;
        float terrainDomainWarpGain = 0.0f;
        float temperatureNoiseStrength = 0.12f;
        float temperatureNoiseFeatureScale = 8192.0f;
        int temperatureNoiseOctaveCount = 2;
        float temperatureNoiseLacunarity = 2.0f;
        float temperatureNoiseGain = 0.5f;
        float temperatureNoiseSimplexScale = 1.0f;
        float precipitationNoiseFeatureScale = 4096.0f;
        int precipitationNoiseOctaveCount = 3;
        float precipitationNoiseLacunarity = 2.0f;
        float precipitationNoiseGain = 0.5f;
        float precipitationNoiseSimplexScale = 1.0f;
        int seaLevel = 0;
    };

    struct ClientRenderConfigState
    {
        float fluidWaterAlpha = 0.8f;
    };

    struct ClientDiagnosticsState
    {
        std::array<uint16_t, 1024> heightLut{};
        uint32_t terrainDrawCount = 0;
        uint32_t terrainFaceCount = 0;
        uint32_t terrainVertexCount = 0;
        bool terrainDebugInitialized = false;

        std::chrono::steady_clock::time_point performanceSampleStart{};
        double accumulatedCpuFrameMs = 0.0;
        double accumulatedGpuFrameMs = 0.0;
        uint32_t performanceSampleCount = 0;
    };

    struct ClientRuntimeState
    {
        ClientRuntimeState() :
            terrainSceneRuntime(clientWorldRuntime),
            sceneLifecycle(clientWorldRuntime, terrainSceneRuntime, gameplayRuntime),
            worldRuntime(clientWorldRuntime.worldRuntime)
        {
        }

        void initializeContexts()
        {
            gameplayRuntime.setContext(&worldRuntime, &content.itemDefinitions());
            uiBridge.setContext(&ui, &gameplayRuntime, &content.itemDefinitions());
        }

        ClientSelectionState selection;
        gameplay::ClientGameplayRuntime gameplayRuntime;
        ClientWorldConfigState worldConfig;
        ClientRenderConfigState renderConfig;
        ClientWorldRuntime clientWorldRuntime;
        ClientTerrainSceneRuntime terrainSceneRuntime;
        ClientSceneLifecycle sceneLifecycle;
        world::WorldRuntime& worldRuntime;
        ClientDiagnosticsState diagnostics;
        ClientContent content;
        bool climateTemperatureOverlayReady = false;
        bool climatePrecipitationOverlayReady = false;
        ui::UiSystem ui;
        ui::ClientUiBridge uiBridge;
        audio::AudioSystem audio;
    };
}
