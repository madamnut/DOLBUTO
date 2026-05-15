#pragma once

#include "world/TerrainBuilder.h"
#include "world/TerrainJobSystem.h"
#include "world/WorldTypes.h"

namespace dolbuto::game
{
    class ClientTerrainJobProcessor
    {
    public:
        explicit ClientTerrainJobProcessor(world::TerrainBuilderConfig terrainConfig);

        static bool canProcess(const TerrainJob& job);
        world::TerrainJobResult process(TerrainJob job) const;

    private:
        world::TerrainBuilderConfig terrainConfig_;
    };
}
