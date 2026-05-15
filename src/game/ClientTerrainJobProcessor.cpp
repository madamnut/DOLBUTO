#include "game/ClientTerrainJobProcessor.h"

#include <array>
#include <memory>
#include <utility>

namespace dolbuto::game
{
    ClientTerrainJobProcessor::ClientTerrainJobProcessor(world::TerrainBuilderConfig terrainConfig) :
        terrainConfig_(terrainConfig)
    {
    }

    bool ClientTerrainJobProcessor::canProcess(const TerrainJob& job)
    {
        return job.type == TerrainJob::Type::BuildFeaturing ||
            job.type == TerrainJob::Type::FinalizeFeatures;
    }

    world::TerrainJobResult ClientTerrainJobProcessor::process(TerrainJob job) const
    {
        world::TerrainJobResult result{};
        const world::TerrainBuilder terrainBuilder(terrainConfig_);

        if (job.type == TerrainJob::Type::BuildFeaturing)
        {
            std::shared_ptr<ChunkData> chunk = terrainBuilder.buildChunkData(job.chunkX, job.chunkZ);
            chunk->generation = job.generation;
            chunk->revision = 0;
            const std::array<int, ChunkColumnCount> heights = terrainBuilder.buildChunkHeightmap(job.chunkX, job.chunkZ);
            std::array<FeatureWriteListPtr, FeatureNeighborCount> outgoingFeatureSlots = terrainBuilder.buildTreeFeatures(chunk, heights);
            result.completedChunkData = CompletedChunkData{std::move(chunk), std::move(outgoingFeatureSlots)};
            return result;
        }

        if (job.type == TerrainJob::Type::FinalizeFeatures && job.chunk)
        {
            terrainBuilder.applyFeatureWrites(job.chunk, job.incomingFeatureSlots);
            result.completedMergedChunk = std::move(job.chunk);
        }

        return result;
    }
}
