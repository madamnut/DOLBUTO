#pragma once

#include "audio/AudioSystem.h"
#include "config/ViewmodelConfig.h"
#include "game/ClientContent.h"
#include "game/ClientSceneLifecycle.h"
#include "game/ClientTerrainSceneRuntime.h"
#include "game/ClientWorldRuntime.h"
#include "gameplay/ClientGameplayRuntime.h"
#include "ui/ClientUiBridge.h"
#include "ui/UiSystem.h"
#include "world/WorldRuntime.h"

#include <algorithm>
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
        float groundnessNoiseFeatureScale = 0.0f;
        int groundnessNoiseOctaveCount = 0;
        float groundnessNoiseLacunarity = 0.0f;
        float groundnessNoiseGain = 0.0f;
        bool groundnessDomainWarpEnabled = false;
        float groundnessDomainWarpAmplitude = 0.0f;
        float groundnessDomainWarpFrequency = 0.0f;
        int groundnessDomainWarpOctaveCount = 0;
        float groundnessDomainWarpGain = 0.0f;
        float baseNoiseFeatureScale = 0.0f;
        int baseNoiseOctaveCount = 0;
        float baseNoiseLacunarity = 0.0f;
        float baseNoiseGain = 0.0f;
        float smoothnessNoiseFeatureScale = 0.0f;
        int smoothnessNoiseOctaveCount = 0;
        float smoothnessNoiseLacunarity = 0.0f;
        float smoothnessNoiseGain = 0.0f;
        float weirdnessNoiseFeatureScale = 0.0f;
        int weirdnessNoiseOctaveCount = 0;
        float weirdnessNoiseLacunarity = 0.0f;
        float weirdnessNoiseGain = 0.0f;
        bool weirdnessDomainWarpEnabled = false;
        float weirdnessDomainWarpAmplitude = 0.0f;
        float weirdnessDomainWarpFrequency = 0.0f;
        int weirdnessDomainWarpOctaveCount = 0;
        float weirdnessDomainWarpGain = 0.0f;
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
        bool fluidWaterScreenBlurEnabled = true;
        float fluidWaterScreenBlurSpread = 1.0f;
        float fluidWaterScreenBlurIntensity = 0.75f;
        float fluidWaterScreenBlurTint = 0.025f;
    };

    enum class ClientPerfCounter
    {
        Frame,
        Poll,
        UiActions,
        Physics,
        DebugText,
        RenderCall,
        TerrainRequest,
        TerrainComplete,
        TerrainDrain,
        TerrainTracking,
        TerrainInstall,
        TerrainMark,
        TerrainStats,
        TerrainUnload,
        TerrainRetired,
        TerrainPop,
        TerrainHandle,
        TerrainLoadHandle,
        TerrainDataHandle,
        TerrainMeshHandle,
        TerrainSaveQueue,
        TerrainSourceHandle,
        TerrainLightHandle,
        TerrainLoadFinish,
        TerrainLoadSnapshot,
        TerrainLoadInstall,
        TerrainLoadResume,
        RenderFenceWait,
        RenderAcquire,
        RenderRecord,
        RenderSubmit,
        RenderPresent,
        RenderCpu
    };

    struct ClientPerfMaxStats
    {
        double frameMs = 0.0;
        double pollMs = 0.0;
        double uiActionsMs = 0.0;
        double physicsMs = 0.0;
        double debugTextMs = 0.0;
        double renderCallMs = 0.0;
        double terrainRequestMs = 0.0;
        double terrainCompleteMs = 0.0;
        double terrainDrainMs = 0.0;
        double terrainTrackingMs = 0.0;
        double terrainInstallMs = 0.0;
        double terrainMarkMs = 0.0;
        double terrainStatsMs = 0.0;
        double terrainUnloadMs = 0.0;
        double terrainRetiredMs = 0.0;
        double terrainPopMs = 0.0;
        double terrainHandleMs = 0.0;
        double terrainLoadHandleMs = 0.0;
        double terrainDataHandleMs = 0.0;
        double terrainMeshHandleMs = 0.0;
        double terrainSaveQueueMs = 0.0;
        double terrainSourceHandleMs = 0.0;
        double terrainLightHandleMs = 0.0;
        double terrainLoadFinishMs = 0.0;
        double terrainLoadSnapshotMs = 0.0;
        double terrainLoadInstallMs = 0.0;
        double terrainLoadResumeMs = 0.0;
        double renderFenceWaitMs = 0.0;
        double renderAcquireMs = 0.0;
        double renderRecordMs = 0.0;
        double renderSubmitMs = 0.0;
        double renderPresentMs = 0.0;
        double renderCpuMs = 0.0;
        uint32_t terrainMeshes = 0;
        uint32_t terrainRefreshChunks = 0;
        uint32_t terrainUnloadedChunks = 0;
        uint32_t terrainPopCount = 0;
        uint32_t terrainCompletedCount = 0;
        uint32_t terrainLoadCount = 0;
        uint32_t terrainBuildMeshCount = 0;
    };

    inline void recordPerfMax(ClientPerfMaxStats& stats, ClientPerfCounter counter, double milliseconds)
    {
        switch (counter)
        {
        case ClientPerfCounter::Frame:
            stats.frameMs = std::max(stats.frameMs, milliseconds);
            break;
        case ClientPerfCounter::Poll:
            stats.pollMs = std::max(stats.pollMs, milliseconds);
            break;
        case ClientPerfCounter::UiActions:
            stats.uiActionsMs = std::max(stats.uiActionsMs, milliseconds);
            break;
        case ClientPerfCounter::Physics:
            stats.physicsMs = std::max(stats.physicsMs, milliseconds);
            break;
        case ClientPerfCounter::DebugText:
            stats.debugTextMs = std::max(stats.debugTextMs, milliseconds);
            break;
        case ClientPerfCounter::RenderCall:
            stats.renderCallMs = std::max(stats.renderCallMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainRequest:
            stats.terrainRequestMs = std::max(stats.terrainRequestMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainComplete:
            stats.terrainCompleteMs = std::max(stats.terrainCompleteMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainDrain:
            stats.terrainDrainMs = std::max(stats.terrainDrainMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainTracking:
            stats.terrainTrackingMs = std::max(stats.terrainTrackingMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainInstall:
            stats.terrainInstallMs = std::max(stats.terrainInstallMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainMark:
            stats.terrainMarkMs = std::max(stats.terrainMarkMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainStats:
            stats.terrainStatsMs = std::max(stats.terrainStatsMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainUnload:
            stats.terrainUnloadMs = std::max(stats.terrainUnloadMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainRetired:
            stats.terrainRetiredMs = std::max(stats.terrainRetiredMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainPop:
            stats.terrainPopMs = std::max(stats.terrainPopMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainHandle:
            stats.terrainHandleMs = std::max(stats.terrainHandleMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainLoadHandle:
            stats.terrainLoadHandleMs = std::max(stats.terrainLoadHandleMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainDataHandle:
            stats.terrainDataHandleMs = std::max(stats.terrainDataHandleMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainMeshHandle:
            stats.terrainMeshHandleMs = std::max(stats.terrainMeshHandleMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainSaveQueue:
            stats.terrainSaveQueueMs = std::max(stats.terrainSaveQueueMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainSourceHandle:
            stats.terrainSourceHandleMs = std::max(stats.terrainSourceHandleMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainLightHandle:
            stats.terrainLightHandleMs = std::max(stats.terrainLightHandleMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainLoadFinish:
            stats.terrainLoadFinishMs = std::max(stats.terrainLoadFinishMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainLoadSnapshot:
            stats.terrainLoadSnapshotMs = std::max(stats.terrainLoadSnapshotMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainLoadInstall:
            stats.terrainLoadInstallMs = std::max(stats.terrainLoadInstallMs, milliseconds);
            break;
        case ClientPerfCounter::TerrainLoadResume:
            stats.terrainLoadResumeMs = std::max(stats.terrainLoadResumeMs, milliseconds);
            break;
        case ClientPerfCounter::RenderFenceWait:
            stats.renderFenceWaitMs = std::max(stats.renderFenceWaitMs, milliseconds);
            break;
        case ClientPerfCounter::RenderAcquire:
            stats.renderAcquireMs = std::max(stats.renderAcquireMs, milliseconds);
            break;
        case ClientPerfCounter::RenderRecord:
            stats.renderRecordMs = std::max(stats.renderRecordMs, milliseconds);
            break;
        case ClientPerfCounter::RenderSubmit:
            stats.renderSubmitMs = std::max(stats.renderSubmitMs, milliseconds);
            break;
        case ClientPerfCounter::RenderPresent:
            stats.renderPresentMs = std::max(stats.renderPresentMs, milliseconds);
            break;
        case ClientPerfCounter::RenderCpu:
            stats.renderCpuMs = std::max(stats.renderCpuMs, milliseconds);
            break;
        }
    }

    struct ClientDiagnosticsState
    {
        std::array<float, 1024> heightLut{};
        std::array<float, 1024> groundnessBaselineLut{};
        std::array<float, 1024> groundnessInfluenceLut{};
        std::array<float, 1024> smoothnessInfluenceLut{};
        std::array<float, 1024> pvWeightLut{};
        std::array<float, 1024> groundnessPvWeightLut{};
        std::array<float, 1024> smoothnessPvWeightLut{};
        uint32_t terrainDrawCount = 0;
        uint32_t terrainFaceCount = 0;
        uint32_t terrainVertexCount = 0;
        bool terrainDebugInitialized = false;

        std::chrono::steady_clock::time_point performanceSampleStart{};
        double accumulatedCpuFrameMs = 0.0;
        double accumulatedGpuFrameMs = 0.0;
        uint32_t performanceSampleCount = 0;
        ClientPerfMaxStats perfMax;
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
        config::ViewmodelConfig viewmodelConfig;
        ClientWorldRuntime clientWorldRuntime;
        ClientTerrainSceneRuntime terrainSceneRuntime;
        ClientSceneLifecycle sceneLifecycle;
        world::WorldRuntime& worldRuntime;
        ClientDiagnosticsState diagnostics;
        ClientContent content;
        bool climateTemperatureOverlayReady = false;
        bool climatePrecipitationOverlayReady = false;
        bool terrainGroundnessOverlayReady = false;
        bool terrainSmoothnessOverlayReady = false;
        bool terrainWeirdnessOverlayReady = false;
        bool terrainPvOverlayReady = false;
        ui::UiSystem ui;
        ui::ClientUiBridge uiBridge;
        audio::AudioSystem audio;
    };
}
