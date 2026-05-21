#include "game/ClientTerrainJobProcessor.h"

#include "world/SkyLightSystem.h"

#include <memory>
#include <utility>
#include <vector>

namespace dolbuto::game
{
    ClientTerrainJobProcessor::ClientTerrainJobProcessor(world::TerrainBuilderConfig terrainConfig) :
        terrainConfig_(terrainConfig)
    {
    }

    bool ClientTerrainJobProcessor::canProcess(const TerrainJob& job)
    {
        return job.type == TerrainJob::Type::BuildTerrainSource ||
            job.type == TerrainJob::Type::ResolveFeatures ||
            job.type == TerrainJob::Type::ResolveLight;
    }

    world::TerrainJobResult ClientTerrainJobProcessor::process(TerrainJob job) const
    {
        world::TerrainJobResult result{};
        const world::TerrainBuilder terrainBuilder(terrainConfig_);

        if (job.type == TerrainJob::Type::BuildTerrainSource)
        {
            std::shared_ptr<ChunkData> chunk = terrainBuilder.buildChunkData(job.chunkX, job.chunkZ);
            chunk->generation = job.generation;
            chunk->revision = 0;
            result.completedChunkData = CompletedChunkData{std::move(chunk), {}};
            return result;
        }

        if (job.type == TerrainJob::Type::ResolveFeatures)
        {
            std::shared_ptr<ChunkData> resolved = terrainBuilder.resolveFeaturesForCenter(job.meshChunks);
            if (resolved)
            {
                resolved->localLight = world::computeLocalSkyLight(*resolved, terrainConfig_.lightAttenuationTables.get());
            }
            if (resolved && !resolved->localLight.empty())
            {
                resolved->generation = job.generation;
                result.completedLocalLightChunk = std::move(resolved);
            }
            return result;
        }

        if (job.type == TerrainJob::Type::ResolveLight && job.meshChunks[4])
        {
            auto resolved = std::make_shared<ChunkData>(*job.meshChunks[4]);
            std::vector<uint8_t> light = world::resolveCenterSkyLight(job.meshChunks, terrainConfig_.lightAttenuationTables.get());
            if (!light.empty())
            {
                if (resolved->light != light)
                {
                    ++resolved->revision;
                }
                resolved->light = std::move(light);
                resolved->generation = job.generation;
                result.completedLightChunk = std::move(resolved);
            }
        }

        return result;
    }
}
