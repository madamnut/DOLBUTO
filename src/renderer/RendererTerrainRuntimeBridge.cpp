#include "renderer/RendererTerrainRuntimeBridge.h"

#include "renderer/DebugOverlayText.h"
#include "renderer/RendererAssetStore.h"
#include "game/ClientRuntimeState.h"
#include "renderer/RendererTerrainMeshBridge.h"
#include "renderer/TerrainRenderPath.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace dolbuto
{
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
        DebugOverlayText& debugOverlayText) :
        client_(client),
        rendererAssets_(rendererAssets),
        terrainRenderPath_(terrainRenderPath),
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
            return;
        }

        client_.gameplayRuntime.reserveDroppedItemTracking(loadResult.droppedItemTrackingCapacity);
        terrainRenderPath_.reserve(loadResult.terrainRenderCapacity);
        terrainRenderPath_.retireChunksNotIn(*loadResult.desiredRenderChunks, static_cast<uint32_t>(MaxFramesInFlight + 1));

        updateTerrainStats();
        debugOverlayText_.markDirty();
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

        for (uint64_t key : completionResult.refreshDroppedItemChunkKeys)
        {
            client_.gameplayRuntime.refreshDroppedItemChunkTracking(key);
        }

        for (CompletedChunkMesh& mesh : completionResult.meshesToInstall)
        {
            const uint64_t key = chunkKey(mesh.chunkX, mesh.chunkZ);
            terrainRenderPath_.installCompletedMesh(key, mesh, static_cast<uint32_t>(MaxFramesInFlight + 1));
            client_.worldRuntime.markMeshed(key);
        }

        if (completionResult.terrainStatsDirty)
        {
            updateTerrainStats();
        }
        processRetiredTerrainChunks();
        processPendingTerrainUnloads();
    }

    uint32_t RendererTerrainRuntimeBridge::processPendingTerrainUnloads()
    {
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
            terrainRenderPath_.retireAndErase(key, static_cast<uint32_t>(MaxFramesInFlight + 1));
            client_.gameplayRuntime.removeDroppedItemChunkTracking(key);
            ++unloadedCount;
        }

        if (unloadedCount > 0)
        {
            updateTerrainStats();
            debugOverlayText_.markDirty();
        }

        return unloadedCount;
    }

    void RendererTerrainRuntimeBridge::processRetiredTerrainChunks()
    {
        terrainRenderPath_.processRetired(static_cast<uint32_t>(client_.worldConfig.maxTerrainRetiredDestroyPerFrame));
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
        return config;
    }

    bool RendererTerrainRuntimeBridge::setBlockAtWorld(int x, int y, int z, uint16_t block)
    {
        return client_.worldRuntime.setBlockAtWorld(x, y, z, block);
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
        TerrainBuildData mesh = RendererTerrainMeshBridge(client_.content, rendererAssets_).buildEditedSubchunkMesh(
            chunk->data,
            subchunkY,
            [this](int x, int y, int z)
            {
                return client_.worldRuntime.blockAtWorld(x, y, z);
            });

        client_.clientWorldRuntime.clearRequestedMeshJob(key);
        if (chunk->data->revision != revision || chunk->data->generation != generation)
        {
            return;
        }

        terrainRenderPath_.replaceEditedSolidSubchunk(
            key,
            chunkX,
            chunkZ,
            chunk->data->revision,
            subchunkY,
            mesh,
            static_cast<uint32_t>(MaxFramesInFlight + 1));
        chunk->genState = ChunkGenState::Meshed;
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
    }

    void RendererTerrainRuntimeBridge::updateTerrainStats()
    {
        const TerrainRenderPath::Stats stats = terrainRenderPath_.rebuildStats();
        client_.diagnostics.terrainDrawCount = stats.drawCount;
        client_.diagnostics.terrainFaceCount = stats.faceCount;
        client_.diagnostics.terrainVertexCount = stats.vertexCount;
        debugOverlayText_.setTerrainStats(client_.diagnostics.terrainDrawCount, client_.diagnostics.terrainFaceCount, client_.diagnostics.terrainVertexCount);
    }

}
