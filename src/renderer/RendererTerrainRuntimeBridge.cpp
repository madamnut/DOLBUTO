#include "renderer/RendererTerrainRuntimeBridge.h"

#include "renderer/DebugOverlayText.h"
#include "renderer/RendererAssetStore.h"
#include "game/ClientRuntimeState.h"
#include "renderer/ParticleRenderPath.h"
#include "renderer/RendererTerrainMeshBridge.h"
#include "renderer/TerrainRenderPath.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dolbuto
{
    namespace
    {
        uint32_t stableStringHash(const std::string& value)
        {
            uint32_t hash = 2166136261u;
            for (const char c : value)
            {
                hash ^= static_cast<uint8_t>(c);
                hash *= 16777619u;
            }
            return hash;
        }
    }

    namespace
    {
        constexpr int MaxFramesInFlight = 2;
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeY = 512;
        constexpr int ChunkSizeZ = 16;
        constexpr int SubchunkSize = 16;
        constexpr int SubchunksPerChunk = ChunkSizeY / SubchunkSize;
        constexpr int LoadGridUnitChunks = 16;
        constexpr int CenterGroupChunks = 2;

        int positiveModulo(int value, int divisor)
        {
            return world::WorldRuntime::positiveModulo(value, divisor);
        }

        int floorDiv(int value, int divisor)
        {
            return world::WorldRuntime::floorDiv(value, divisor);
        }

        int chunkCoordinate(double worldCoordinate)
        {
            const int blockCoordinate = static_cast<int>(std::floor(worldCoordinate + 0.5));
            return static_cast<int>(std::floor(static_cast<double>(blockCoordinate) / static_cast<double>(ChunkSizeX)));
        }

        int centerGroupCoordinate(int chunkCoordinate)
        {
            return floorDiv(chunkCoordinate, CenterGroupChunks) * CenterGroupChunks;
        }

        uint64_t chunkKey(int chunkX, int chunkZ)
        {
            return world::WorldRuntime::chunkKey(chunkX, chunkZ);
        }
    }

    RendererTerrainRuntimeBridge::RendererTerrainRuntimeBridge(
        game::ClientRuntimeState& client,
        RendererAssetStore& rendererAssets,
        TerrainRenderPath& terrainRenderPath,
        ParticleRenderPath& particleRenderPath,
        DebugOverlayText& debugOverlayText) :
        client_(client),
        rendererAssets_(rendererAssets),
        terrainRenderPath_(terrainRenderPath),
        particleRenderPath_(particleRenderPath),
        debugOverlayText_(debugOverlayText)
    {
    }

    void RendererTerrainRuntimeBridge::updateLoadedChunks(DVec3 playerPosition)
    {
        const int centerGroupChunkX = centerGroupCoordinate(chunkCoordinate(playerPosition.x));
        const int centerGroupChunkZ = centerGroupCoordinate(chunkCoordinate(playerPosition.z));
        requestTerrainLoad(centerGroupChunkX, centerGroupChunkZ);
    }

    void RendererTerrainRuntimeBridge::requestTerrainLoad(int centerGroupChunkX, int centerGroupChunkZ)
    {
        const auto perfStart = std::chrono::steady_clock::now();
        const game::ClientTerrainSceneRuntime::TerrainLoadResult loadResult = client_.terrainSceneRuntime.requestTerrainLoad(
            centerGroupChunkX,
            centerGroupChunkZ,
            client_.worldConfig.loadGridScale,
            LoadGridUnitChunks,
            terrainBuilderConfig(),
            [this](uint64_t key)
            {
                return chunkMeshReady(key);
            });

        if (!loadResult.requested)
        {
            game::recordPerfMax(
                client_.diagnostics.perfMax,
                game::ClientPerfCounter::TerrainRequest,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - perfStart).count());
            return;
        }

        client_.gameplayRuntime.reserveDroppedItemTracking(loadResult.droppedItemTrackingCapacity);
        terrainRenderPath_.reserve(loadResult.terrainRenderCapacity);
        terrainRenderPath_.retireChunksNotIn(*loadResult.desiredRenderChunks, static_cast<uint32_t>(MaxFramesInFlight + 1));

        updateTerrainStats();
        debugOverlayText_.markDirty();
        game::recordPerfMax(
            client_.diagnostics.perfMax,
            game::ClientPerfCounter::TerrainRequest,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - perfStart).count());
    }

    world::TerrainJobResult RendererTerrainRuntimeBridge::processRenderTerrainMeshJob(TerrainJob job)
    {
        world::TerrainJobResult result{};
        if (job.meshChunks[4])
        {
            if (job.revision != job.meshChunks[4]->revision)
            {
                CompletedChunkMesh mesh{};
                mesh.generation = job.generation;
                mesh.revision = job.revision;
                mesh.chunkX = job.chunkX;
                mesh.chunkZ = job.chunkZ;
                result.completedChunkMesh = std::move(mesh);
            }
            else
            {
                result.completedChunkMesh = RendererTerrainMeshBridge(client_.content, rendererAssets_).buildChunkMesh(job.meshChunks, job.generation);
            }
        }
        return result;
    }

    void RendererTerrainRuntimeBridge::markRuntimeChunkDataDirty(RuntimeChunk& chunk)
    {
        world::WorldRuntime::markDataDirty(chunk);
    }

    void RendererTerrainRuntimeBridge::processCompletedTerrainJobs()
    {
        const auto perfStart = std::chrono::steady_clock::now();
        auto sectionStart = perfStart;
        auto completionResult = client_.terrainSceneRuntime.processCompletedTerrainJobs(
            static_cast<uint32_t>(client_.worldConfig.maxTerrainUploadChunksPerFrame),
            terrainBuilderConfig(),
            [this](uint64_t key)
            {
                return chunkMeshReady(key);
            },
            [this](WorldEntity& entity)
            {
                client_.gameplayRuntime.normalizeLoadedEntity(entity);
            });
        game::recordPerfMax(client_.diagnostics.perfMax, game::ClientPerfCounter::TerrainPop, completionResult.popMs);
        game::recordPerfMax(client_.diagnostics.perfMax, game::ClientPerfCounter::TerrainHandle, completionResult.handleMs);
        game::recordPerfMax(client_.diagnostics.perfMax, game::ClientPerfCounter::TerrainLoadHandle, completionResult.loadHandleMs);
        game::recordPerfMax(client_.diagnostics.perfMax, game::ClientPerfCounter::TerrainLoadFinish, completionResult.loadFinishMs);
        game::recordPerfMax(client_.diagnostics.perfMax, game::ClientPerfCounter::TerrainLoadSnapshot, completionResult.loadSnapshotMs);
        game::recordPerfMax(client_.diagnostics.perfMax, game::ClientPerfCounter::TerrainLoadInstall, completionResult.loadInstallMs);
        game::recordPerfMax(client_.diagnostics.perfMax, game::ClientPerfCounter::TerrainLoadResume, completionResult.loadResumeMs);
        game::recordPerfMax(
            client_.diagnostics.perfMax,
            game::ClientPerfCounter::TerrainDataHandle,
            completionResult.sourceHandleMs + completionResult.localLightHandleMs + completionResult.lightHandleMs);
        game::recordPerfMax(client_.diagnostics.perfMax, game::ClientPerfCounter::TerrainMeshHandle, completionResult.meshHandleMs);
        game::recordPerfMax(client_.diagnostics.perfMax, game::ClientPerfCounter::TerrainSaveQueue, completionResult.saveQueueMs);
        game::recordPerfMax(client_.diagnostics.perfMax, game::ClientPerfCounter::TerrainSourceHandle, completionResult.sourceHandleMs);
        game::recordPerfMax(
            client_.diagnostics.perfMax,
            game::ClientPerfCounter::TerrainLightHandle,
            completionResult.localLightHandleMs + completionResult.lightHandleMs);
        game::recordPerfMax(
            client_.diagnostics.perfMax,
            game::ClientPerfCounter::TerrainDrain,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sectionStart).count());
        client_.diagnostics.perfMax.terrainPopCount = std::max<uint32_t>(
            client_.diagnostics.perfMax.terrainPopCount,
            completionResult.popCount);
        client_.diagnostics.perfMax.terrainCompletedCount = std::max<uint32_t>(
            client_.diagnostics.perfMax.terrainCompletedCount,
            completionResult.terrainCount);
        client_.diagnostics.perfMax.terrainLoadCount = std::max<uint32_t>(
            client_.diagnostics.perfMax.terrainLoadCount,
            completionResult.loadCount);
        client_.diagnostics.perfMax.terrainBuildMeshCount = std::max<uint32_t>(
            client_.diagnostics.perfMax.terrainBuildMeshCount,
            completionResult.buildMeshCount);
        client_.diagnostics.perfMax.terrainMeshes = std::max<uint32_t>(
            client_.diagnostics.perfMax.terrainMeshes,
            static_cast<uint32_t>(completionResult.meshesToInstall.size()));
        client_.diagnostics.perfMax.terrainRefreshChunks = std::max<uint32_t>(
            client_.diagnostics.perfMax.terrainRefreshChunks,
            static_cast<uint32_t>(completionResult.refreshDroppedItemChunkKeys.size()));

        sectionStart = std::chrono::steady_clock::now();
        for (uint64_t key : completionResult.refreshDroppedItemChunkKeys)
        {
            client_.gameplayRuntime.refreshDroppedItemChunkTracking(key);
        }
        game::recordPerfMax(
            client_.diagnostics.perfMax,
            game::ClientPerfCounter::TerrainTracking,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sectionStart).count());

        double installMs = 0.0;
        double markMs = 0.0;
        for (CompletedChunkMesh& mesh : completionResult.meshesToInstall)
        {
            const uint64_t key = chunkKey(mesh.chunkX, mesh.chunkZ);
            sectionStart = std::chrono::steady_clock::now();
            terrainRenderPath_.installCompletedMesh(key, mesh, static_cast<uint32_t>(MaxFramesInFlight + 1));
            installMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sectionStart).count();
            sectionStart = std::chrono::steady_clock::now();
            client_.worldRuntime.markMeshed(key);
            refreshFireEmittersForChunk(mesh.chunkX, mesh.chunkZ);
            markMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sectionStart).count();
        }
        game::recordPerfMax(client_.diagnostics.perfMax, game::ClientPerfCounter::TerrainInstall, installMs);
        game::recordPerfMax(client_.diagnostics.perfMax, game::ClientPerfCounter::TerrainMark, markMs);

        sectionStart = std::chrono::steady_clock::now();
        if (completionResult.terrainStatsDirty)
        {
            updateTerrainStats();
        }
        game::recordPerfMax(
            client_.diagnostics.perfMax,
            game::ClientPerfCounter::TerrainStats,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sectionStart).count());
        game::recordPerfMax(
            client_.diagnostics.perfMax,
            game::ClientPerfCounter::TerrainComplete,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - perfStart).count());
        processRetiredTerrainChunks();
        processPendingTerrainUnloads();
    }

    uint32_t RendererTerrainRuntimeBridge::processPendingTerrainUnloads()
    {
        const auto perfStart = std::chrono::steady_clock::now();
        uint32_t unloadedCount = 0;
        while (unloadedCount < static_cast<uint32_t>(client_.worldConfig.maxTerrainUnloadChunksPerFrame))
        {
            const std::optional<game::ClientTerrainSceneRuntime::PendingTerrainUnload> pendingUnload =
                client_.terrainSceneRuntime.processNextPendingTerrainUnload();
            if (!pendingUnload)
            {
                break;
            }

            const uint64_t key = pendingUnload->key;
            const int chunkX = static_cast<int32_t>(key >> 32u);
            const int chunkZ = static_cast<int32_t>(key & 0xFFFFFFFFu);
            particleRenderPath_.removeFireEmittersForChunk(chunkX, chunkZ);
            terrainRenderPath_.retireAndErase(key, static_cast<uint32_t>(MaxFramesInFlight + 1));
            client_.gameplayRuntime.removeDroppedItemChunkTracking(key);
            ++unloadedCount;
        }

        if (unloadedCount > 0)
        {
            updateTerrainStats();
            debugOverlayText_.markDirty();
        }
        client_.diagnostics.perfMax.terrainUnloadedChunks = std::max<uint32_t>(
            client_.diagnostics.perfMax.terrainUnloadedChunks,
            unloadedCount);

        game::recordPerfMax(
            client_.diagnostics.perfMax,
            game::ClientPerfCounter::TerrainUnload,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - perfStart).count());
        return unloadedCount;
    }

    void RendererTerrainRuntimeBridge::processRetiredTerrainChunks()
    {
        const auto perfStart = std::chrono::steady_clock::now();
        terrainRenderPath_.processRetired(static_cast<uint32_t>(client_.worldConfig.maxTerrainRetiredDestroyPerFrame));
        game::recordPerfMax(
            client_.diagnostics.perfMax,
            game::ClientPerfCounter::TerrainRetired,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - perfStart).count());
    }

    world::TerrainBuilderConfig RendererTerrainRuntimeBridge::terrainBuilderConfig() const
    {
        world::TerrainBuilderConfig config{};
        config.heightLut = client_.diagnostics.heightLut;
        config.groundnessBaselineLut = client_.diagnostics.groundnessBaselineLut;
        config.groundnessInfluenceLut = client_.diagnostics.groundnessInfluenceLut;
        config.smoothnessInfluenceLut = client_.diagnostics.smoothnessInfluenceLut;
        config.pvWeightLut = client_.diagnostics.pvWeightLut;
        config.groundnessPvWeightLut = client_.diagnostics.groundnessPvWeightLut;
        config.smoothnessPvWeightLut = client_.diagnostics.smoothnessPvWeightLut;
        config.lightAttenuationTables = client_.content.lightAttenuationTables();
        config.activeWorldSeedSalt = client_.clientWorldRuntime.activeWorldSeedSalt;
        config.seaLevel = client_.worldConfig.seaLevel;
        config.groundnessNoiseFeatureScale = client_.worldConfig.groundnessNoiseFeatureScale;
        config.groundnessNoiseOctaveCount = client_.worldConfig.groundnessNoiseOctaveCount;
        config.groundnessNoiseLacunarity = client_.worldConfig.groundnessNoiseLacunarity;
        config.groundnessNoiseGain = client_.worldConfig.groundnessNoiseGain;
        config.groundnessDomainWarpEnabled = client_.worldConfig.groundnessDomainWarpEnabled;
        config.groundnessDomainWarpAmplitude = client_.worldConfig.groundnessDomainWarpAmplitude;
        config.groundnessDomainWarpFrequency = client_.worldConfig.groundnessDomainWarpFrequency;
        config.groundnessDomainWarpOctaveCount = client_.worldConfig.groundnessDomainWarpOctaveCount;
        config.groundnessDomainWarpGain = client_.worldConfig.groundnessDomainWarpGain;
        config.baseNoiseFeatureScale = client_.worldConfig.baseNoiseFeatureScale;
        config.baseNoiseOctaveCount = client_.worldConfig.baseNoiseOctaveCount;
        config.baseNoiseLacunarity = client_.worldConfig.baseNoiseLacunarity;
        config.baseNoiseGain = client_.worldConfig.baseNoiseGain;
        config.smoothnessNoiseFeatureScale = client_.worldConfig.smoothnessNoiseFeatureScale;
        config.smoothnessNoiseOctaveCount = client_.worldConfig.smoothnessNoiseOctaveCount;
        config.smoothnessNoiseLacunarity = client_.worldConfig.smoothnessNoiseLacunarity;
        config.smoothnessNoiseGain = client_.worldConfig.smoothnessNoiseGain;
        config.weirdnessNoiseFeatureScale = client_.worldConfig.weirdnessNoiseFeatureScale;
        config.weirdnessNoiseOctaveCount = client_.worldConfig.weirdnessNoiseOctaveCount;
        config.weirdnessNoiseLacunarity = client_.worldConfig.weirdnessNoiseLacunarity;
        config.weirdnessNoiseGain = client_.worldConfig.weirdnessNoiseGain;
        config.weirdnessDomainWarpEnabled = client_.worldConfig.weirdnessDomainWarpEnabled;
        config.weirdnessDomainWarpAmplitude = client_.worldConfig.weirdnessDomainWarpAmplitude;
        config.weirdnessDomainWarpFrequency = client_.worldConfig.weirdnessDomainWarpFrequency;
        config.weirdnessDomainWarpOctaveCount = client_.worldConfig.weirdnessDomainWarpOctaveCount;
        config.weirdnessDomainWarpGain = client_.worldConfig.weirdnessDomainWarpGain;
        config.temperatureNoiseStrength = client_.worldConfig.temperatureNoiseStrength;
        config.temperatureNoiseFeatureScale = client_.worldConfig.temperatureNoiseFeatureScale;
        config.temperatureNoiseOctaveCount = client_.worldConfig.temperatureNoiseOctaveCount;
        config.temperatureNoiseLacunarity = client_.worldConfig.temperatureNoiseLacunarity;
        config.temperatureNoiseGain = client_.worldConfig.temperatureNoiseGain;
        config.temperatureNoiseSimplexScale = client_.worldConfig.temperatureNoiseSimplexScale;
        config.precipitationNoiseFeatureScale = client_.worldConfig.precipitationNoiseFeatureScale;
        config.precipitationNoiseOctaveCount = client_.worldConfig.precipitationNoiseOctaveCount;
        config.precipitationNoiseLacunarity = client_.worldConfig.precipitationNoiseLacunarity;
        config.precipitationNoiseGain = client_.worldConfig.precipitationNoiseGain;
        config.precipitationNoiseSimplexScale = client_.worldConfig.precipitationNoiseSimplexScale;
        for (const config::WorldOreFeatureConfig& ore : client_.worldConfig.oreFeatures)
        {
            if (!ore.enabled || ore.attemptsPerChunk <= 0 || ore.size <= 0)
            {
                continue;
            }

            const auto blockIt = client_.content.blockIdByName().find(ore.block);
            const auto replaceIt = client_.content.blockIdByName().find(ore.replace);
            if (blockIt == client_.content.blockIdByName().end() || replaceIt == client_.content.blockIdByName().end())
            {
                continue;
            }

            world::TerrainBuilderConfig::OreFeature feature{};
            feature.block = blockIt->second;
            feature.replace = replaceIt->second;
            feature.minY = ore.minY;
            feature.maxY = ore.maxY;
            feature.attemptsPerChunk = ore.attemptsPerChunk;
            feature.size = ore.size;
            feature.salt = stableStringHash(ore.name.empty() ? ore.block : ore.name);
            config.oreFeatures.push_back(feature);
        }
        return config;
    }

    bool RendererTerrainRuntimeBridge::setBlockAtWorld(int x, int y, int z, uint16_t block)
    {
        return client_.worldRuntime.setBlockAtWorld(x, y, z, block);
    }

    void RendererTerrainRuntimeBridge::tickFluidSimulation()
    {
        constexpr uint32_t MaxFluidTickCells = 256;
        const world::WorldRuntime::FluidTickResult result = client_.worldRuntime.tickFluidSimulation(MaxFluidTickCells);
        if (result.changedSubchunks.empty() && result.changedCells.empty())
        {
            return;
        }

        struct AffectedSubchunk
        {
            int chunkX = 0;
            int chunkZ = 0;
            int subchunkY = 0;
        };
        std::vector<AffectedSubchunk> affectedSubchunks;
        auto addAffectedSubchunk = [&](int affectedChunkX, int affectedChunkZ, int affectedSubchunkY)
        {
            if (affectedSubchunkY < 0 || affectedSubchunkY >= SubchunksPerChunk)
            {
                return;
            }
            for (const AffectedSubchunk& existing : affectedSubchunks)
            {
                if (existing.chunkX == affectedChunkX && existing.chunkZ == affectedChunkZ && existing.subchunkY == affectedSubchunkY)
                {
                    return;
                }
            }
            affectedSubchunks.push_back({affectedChunkX, affectedChunkZ, affectedSubchunkY});
        };

        for (const world::WorldRuntime::EditedSubchunk& changed : result.changedSubchunks)
        {
            addAffectedSubchunk(changed.chunkX, changed.chunkZ, changed.subchunkY);
        }
        for (const world::WorldRuntime::FluidTickCell& cell : result.lightChangedCells)
        {
            const std::vector<world::WorldRuntime::EditedSubchunk> lightChangedSubchunks =
                client_.worldRuntime.resolveEditedSkyLightAtWorld(cell.x, cell.y, cell.z);
            for (const world::WorldRuntime::EditedSubchunk& changed : lightChangedSubchunks)
            {
                addAffectedSubchunk(changed.chunkX, changed.chunkZ, changed.subchunkY);
            }
        }

        for (const AffectedSubchunk& affected : affectedSubchunks)
        {
            rebuildSubchunkMeshNow(affected.chunkX, affected.chunkZ, affected.subchunkY);
        }

        updateTerrainStats();
        debugOverlayText_.markDirty();
    }

    void RendererTerrainRuntimeBridge::rebuildSubchunkMeshNow(int chunkX, int chunkZ, int subchunkY)
    {
        if (subchunkY < 0 || subchunkY >= SubchunksPerChunk)
        {
            return;
        }

        const uint64_t key = chunkKey(chunkX, chunkZ);
        RuntimeChunk* chunk = client_.worldRuntime.find(key);
        if (chunk == nullptr || !chunk->data || !client_.clientWorldRuntime.isTerrainDesired(key))
        {
            return;
        }

        const uint64_t generation = client_.terrainSceneRuntime.terrainGeneration();
        chunk->data->generation = generation;
        const uint64_t revision = chunk->data->revision;

        std::array<std::shared_ptr<ChunkData>, 9> chunks{};
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const uint64_t sampleKey = chunkKey(chunkX + dx, chunkZ + dz);
                RuntimeChunk* sampleChunk = client_.worldRuntime.find(sampleKey);
                if (sampleChunk != nullptr && sampleChunk->data)
                {
                    chunks[static_cast<std::size_t>((dz + 1) * 3 + (dx + 1))] = sampleChunk->data;
                }
            }
        }
        chunks[4] = chunk->data;

        TerrainSubchunkBuildData mesh = RendererTerrainMeshBridge(client_.content, rendererAssets_).buildEditedSubchunkMesh(
            chunks,
            subchunkY,
            [this](int x, int y, int z)
            {
                return client_.worldRuntime.blockAtWorld(x, y, z);
            },
            [this](int x, int y, int z)
            {
                return client_.worldRuntime.lightAtWorld(x, y, z);
            });

        client_.clientWorldRuntime.clearRequestedMeshJob(key);
        if (chunk->data->revision != revision || chunk->data->generation != generation)
        {
            return;
        }

        terrainRenderPath_.replaceEditedSubchunk(
            key,
            chunkX,
            chunkZ,
            chunk->data->revision,
            subchunkY,
            mesh,
            static_cast<uint32_t>(MaxFramesInFlight + 1));
        chunk->genState = ChunkGenState::Meshed;
        refreshFireEmittersForChunk(chunkX, chunkZ);
    }

    void RendererTerrainRuntimeBridge::rebuildEditedChunkMeshes(int blockX, int blockY, int blockZ)
    {
        if (blockY < 0 || blockY >= ChunkSizeY)
        {
            return;
        }

        const int chunkX = floorDiv(blockX, ChunkSizeX);
        const int chunkZ = floorDiv(blockZ, ChunkSizeZ);
        const int subchunkY = blockY / SubchunkSize;
        std::vector<int> chunkOffsetsX = {0};
        std::vector<int> chunkOffsetsZ = {0};
        std::vector<int> subchunkYs = {subchunkY};
        if (positiveModulo(blockX, ChunkSizeX) == 0)
        {
            chunkOffsetsX.push_back(-1);
        }
        if (positiveModulo(blockX, ChunkSizeX) == ChunkSizeX - 1)
        {
            chunkOffsetsX.push_back(1);
        }
        if (positiveModulo(blockZ, ChunkSizeZ) == 0)
        {
            chunkOffsetsZ.push_back(-1);
        }
        if (positiveModulo(blockZ, ChunkSizeZ) == ChunkSizeZ - 1)
        {
            chunkOffsetsZ.push_back(1);
        }
        if (positiveModulo(blockY, SubchunkSize) == 0)
        {
            subchunkYs.push_back(subchunkY - 1);
        }
        if (positiveModulo(blockY, SubchunkSize) == SubchunkSize - 1)
        {
            subchunkYs.push_back(subchunkY + 1);
        }

        struct AffectedSubchunk
        {
            int chunkX = 0;
            int chunkZ = 0;
            int subchunkY = 0;
        };
        std::vector<AffectedSubchunk> affectedSubchunks;
        auto addAffectedSubchunk = [&](int affectedChunkX, int affectedChunkZ, int affectedSubchunkY)
        {
            if (affectedSubchunkY < 0 || affectedSubchunkY >= SubchunksPerChunk)
            {
                return;
            }
            for (const AffectedSubchunk& existing : affectedSubchunks)
            {
                if (existing.chunkX == affectedChunkX && existing.chunkZ == affectedChunkZ && existing.subchunkY == affectedSubchunkY)
                {
                    return;
                }
            }
            affectedSubchunks.push_back({affectedChunkX, affectedChunkZ, affectedSubchunkY});
        };

        for (int offsetZ : chunkOffsetsZ)
        {
            for (int offsetX : chunkOffsetsX)
            {
                for (int affectedSubchunkY : subchunkYs)
                {
                    addAffectedSubchunk(chunkX + offsetX, chunkZ + offsetZ, affectedSubchunkY);
                }
            }
        }

        const std::vector<world::WorldRuntime::EditedSubchunk> lightChangedSubchunks =
            client_.worldRuntime.resolveEditedSkyLightAtWorld(blockX, blockY, blockZ);
        for (const world::WorldRuntime::EditedSubchunk& changed : lightChangedSubchunks)
        {
            addAffectedSubchunk(changed.chunkX, changed.chunkZ, changed.subchunkY);
        }

        for (const AffectedSubchunk& affected : affectedSubchunks)
        {
            rebuildSubchunkMeshNow(affected.chunkX, affected.chunkZ, affected.subchunkY);
        }

        updateTerrainStats();
        debugOverlayText_.markDirty();
    }

    bool RendererTerrainRuntimeBridge::chunkMeshReady(uint64_t key) const
    {
        return terrainRenderPath_.chunkMeshReady(key, client_.worldRuntime.find(key));
    }

    void RendererTerrainRuntimeBridge::destroyAllTerrainChunks()
    {
        terrainRenderPath_.destroyAll();
        client_.clientWorldRuntime.resetSceneRuntime();
        client_.gameplayRuntime.resetDroppedItemTracking();
        particleRenderPath_.clear();
    }

    void RendererTerrainRuntimeBridge::updateTerrainStats()
    {
        const TerrainRenderPath::Stats stats = terrainRenderPath_.rebuildStats();
        client_.diagnostics.terrainDrawCount = stats.drawCount;
        client_.diagnostics.terrainFaceCount = stats.faceCount;
        client_.diagnostics.terrainVertexCount = stats.vertexCount;
        debugOverlayText_.setTerrainStats(client_.diagnostics.terrainDrawCount, client_.diagnostics.terrainFaceCount, client_.diagnostics.terrainVertexCount);
    }

    uint16_t RendererTerrainRuntimeBridge::fireBlockId() const
    {
        const auto it = client_.content.blockIdByName().find("fire");
        return it == client_.content.blockIdByName().end() ? 0 : it->second;
    }

    void RendererTerrainRuntimeBridge::refreshFireEmittersForChunk(int chunkX, int chunkZ)
    {
        const uint16_t fireBlock = fireBlockId();
        if (fireBlock == 0)
        {
            return;
        }

        particleRenderPath_.removeFireEmittersForChunk(chunkX, chunkZ);
        const RuntimeChunk* runtimeChunk = client_.worldRuntime.find(chunkKey(chunkX, chunkZ));
        if (runtimeChunk == nullptr || !runtimeChunk->data || runtimeChunk->data->blocks.size() != ChunkBlockCount)
        {
            return;
        }

        for (int y = 0; y < ChunkSizeY; ++y)
        {
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const std::size_t index = static_cast<std::size_t>((y * ChunkSizeZ + localZ) * ChunkSizeX + localX);
                    if (runtimeChunk->data->blocks[index] != fireBlock)
                    {
                        continue;
                    }
                    particleRenderPath_.registerFireEmitter(
                        chunkX * ChunkSizeX + localX,
                        y,
                        chunkZ * ChunkSizeZ + localZ);
                    client_.worldRuntime.ensureFireBlockEntityAtWorld(
                        chunkX * ChunkSizeX + localX,
                        y,
                        chunkZ * ChunkSizeZ + localZ,
                        InitialFireBurnTicks);
                }
            }
        }
    }

}
