#include "world/spawn.hpp"
#include <random>

namespace sandbox {
std::optional<BlockPos> find_land_spawn(const TerrainGenerator& generator, uint64_t random_seed) {
    std::mt19937_64 random(random_seed);
    std::uniform_int_distribution<int> longitude(0, world_size - 1), band(0, 1),
        latitude(0, spawn_band_width - 1);
    for (unsigned attempt = 0; attempt < spawn_attempt_limit; ++attempt) {
        const int x = longitude(random);
        const int z = spawn_band_begin[band(random)] + latitude(random);
        const auto tile = generate_tile({chunk_coordinate(x), chunk_coordinate(z)}, generator);
        const int lx = local_coordinate(x), lz = local_coordinate(z);
        const int feet = tile.sites[lx + chunk_edge * lz].surface_y + 1;
        // Natural water occupies cells below sea_level. Reserve two empty cells for the 1.75-block body.
        if (feet < sea_level || feet > world_height - 2)
            continue;
        std::optional<Chunk> chunk;
        int cy = -1;
        bool safe = true;
        for (int y = feet - 1; y <= feet + 1; ++y) {
            if (cy != y / chunk_edge) {
                cy = y / chunk_edge;
                chunk = generate_chunk({tile.key.x, cy, tile.key.z}, generator, &tile);
            }
            const Block block = chunk->block_at(lx, y % chunk_edge, lz);
            const bool dry = chunk->fluid_at(lx, y % chunk_edge, lz).amount == 0;
            if (!dry || (y < feet ? !is_full_block(block) : block != Block::air)) {
                safe = false;
                break;
            }
        }
        if (safe)
            return BlockPos{x, feet, z};
    }
    return std::nullopt;
}
} // namespace sandbox
