#include "world/terrain.hpp"
#include "world/profiling.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>

namespace sandbox {
namespace {
uint32_t material(Block block, int axis, int sign) {
    if (block == Block::ice)
        return 1; // Extended atlas tile9.
    if (is_snow(block))
        return 2; // Extended atlas tile10.
    if (block == Block::water)
        return 5;
    if (block == Block::sand)
        return 6;
    if (block == Block::glow)
        return 0; // Extended solid material: bit27 marks the white emissive tile.
    if (block == Block::lava)
        return 7;
    if (block == Block::rock)
        return 4;
    if (block == Block::dirt)
        return 0;
    return axis == 1 ? (sign > 0 ? 1u : 3u) : 2u;
}
} // namespace
TerrainTile generate_tile(ColumnKey key, const TerrainGenerator& generator) {
    TerrainTile tile{canonical(key), generator.config().seed, {}, generator.signature()};
    for (auto& site : tile.sites)
        site.surface_y = flat_surface_y - 1;
    return tile;
}
Chunk generate_chunk(ChunkKey key, const TerrainGenerator& generator, const TerrainTile* prepared) {
    profiling::Scope measure(profiling::Stage::blocks);
    key = canonical(key);
    if (key.y < 0 || key.y >= chunks_per_column)
        throw std::out_of_range("Cannot generate a chunk outside world height.");
    if (prepared && (prepared->key != ColumnKey{key.x, key.z} || prepared->seed != generator.config().seed ||
                     prepared->signature != generator.signature()))
        throw std::invalid_argument("Terrain tile does not match chunk/generator.");
    static_assert(flat_surface_y % chunk_edge == 0);
    static_assert(flat_surface_y > 0 && flat_surface_y < world_height);
    Chunk result;
    result.uniform = key.y * chunk_edge < flat_surface_y ? Block::rock : Block::air;
    return result;
}
int terrain_spawn_height(int, int, const TerrainGenerator&) { return flat_surface_y; }
// Convenience entry points for deterministic standalone generation and existing callers.
TerrainTile generate_tile(ColumnKey key, uint32_t seed) {
    GenerationConfig config;
    config.seed = seed;
    return generate_tile(key, TerrainGenerator(config));
}
Chunk generate_chunk(ChunkKey key, uint32_t seed, const TerrainTile* tile) {
    GenerationConfig config;
    config.seed = seed;
    return generate_chunk(key, TerrainGenerator(config), tile);
}
int terrain_spawn_height(int x, int z, uint32_t seed) {
    GenerationConfig config;
    config.seed = seed;
    return terrain_spawn_height(x, z, TerrainGenerator(config));
}
void Chunk::separate_fluids() {
    if (!blocks) {
        uniform_fluid = uniform == Block::water ? water_amount(fluid_capacity) : Fluid{};
        if (uniform == Block::water)
            uniform = Block::air;
        return;
    }
    if (std::find(blocks->begin(), blocks->end(), Block::water) == blocks->end())
        return;
    auto dry = std::make_shared<std::array<Block, 4096>>(*blocks);
    auto wet = std::make_shared<std::array<Fluid, 4096>>();
    for (size_t i = 0; i < dry->size(); ++i) {
        if ((*dry)[i] == Block::water) {
            (*dry)[i] = Block::air;
            (*wet)[i] = water_amount(fluid_capacity);
        }
    }
    uniform = (*dry)[0];
    uniform_fluid = (*wet)[0];
    blocks = std::all_of(dry->begin(), dry->end(), [&](auto b) { return b == uniform; }) ? nullptr : dry;
    fluids =
        std::all_of(wet->begin(), wet->end(), [&](auto f) { return f == uniform_fluid; }) ? nullptr : wet;
}
ChunkHalo make_halo(ChunkKey key, const ChunkNeighbours& neighbours) {
    ChunkHalo halo{};
    for (int z = -1; z <= 16; ++z)
        for (int y = -1; y <= 16; ++y)
            for (int x = -1; x <= 16; ++x) {
                const int cy = key.y + chunk_coordinate(y);
                if (cy < 0 || cy >= chunks_per_column)
                    continue;
                const int i =
                    chunk_coordinate(x) + 1 + 3 * (chunk_coordinate(y) + 1 + 3 * (chunk_coordinate(z) + 1));
                if (!neighbours[i])
                    throw std::logic_error("Meshing before neighbour data is ready.");
                halo[x + 1 + 18 * (y + 1 + 18 * (z + 1))] =
                    neighbours[i]->block_at(local_coordinate(x), local_coordinate(y), local_coordinate(z));
                halo.fluids[x + 1 + 18 * (y + 1 + 18 * (z + 1))] =
                    neighbours[i]->fluid_at(local_coordinate(x), local_coordinate(y), local_coordinate(z));
            }
    return halo;
}
FluidSurface fluid_surface(const std::array<FluidSurfaceSample, 9>& samples) {
    const auto& centre = samples[4];
    const float base = fluid_height(centre.fluid, centre.above);
    FluidSurface result{base, base, base, base};
    if (!centre.fluid.amount || is_solid(centre.block))
        return {};
    constexpr int corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int c = 0; c < 4; ++c) {
        const int cx = corners[c][0], cz = corners[c][1];
        std::array<const FluidSurfaceSample*, 4> cells{};
        for (int z = 0; z < 2; ++z)
            for (int x = 0; x < 2; ++x)
                cells[x + 2 * z] = &samples[cx + x + 3 * (cz + z)];
        const int owner = (1 - cx) + 2 * (1 - cz);
        unsigned component = 1u << owner;
        bool touches_other = false;
        // Only face-connected wet cells participate; diagonally isolated water cannot cross a wall.
        for (int pass = 0; pass < 3; ++pass)
            for (int i = 0; i < 4; ++i) {
                if (!(component & (1u << i)))
                    continue;
                for (int j : {i ^ 1, i ^ 2})
                    if (!is_solid(cells[j]->block) && cells[j]->fluid.amount) {
                        if (cells[j]->fluid.kind == centre.fluid.kind)
                            component |= 1u << j;
                        else
                            touches_other = true;
                    }
            }
        unsigned low = fluid_display_steps, high = 0;
        bool covered = false;
        for (int i = 0; i < 4; ++i)
            if (component & (1u << i)) {
                const auto& cell = *cells[i];
                const unsigned level = fluid_display_level(cell.fluid);
                low = std::min(low, level);
                high = std::max(high, level);
                covered |= cell.above.amount != 0;
            }
        // A shared midrange gives identical half-step corners on both sides of every eligible edge.
        // Large drops or vertically stacked/falling water retain flat corners for the whole component.
        if (!covered && !touches_other && high - low <= 1)
            result[c] = float(low + high) * (0.875f / (2 * fluid_display_steps));
    }
    return result;
}
float fluid_surface_height(const FluidSurface& h, float x, float z) {
    x = std::clamp(x, 0.0f, 1.0f);
    z = std::clamp(z, 0.0f, 1.0f);
    return x >= z ? h[0] + (h[1] - h[0]) * x + (h[2] - h[1]) * z
                  : h[0] + (h[2] - h[3]) * x + (h[3] - h[0]) * z;
}
ChunkMesh mesh_chunk(const ChunkHalo& halo) {
    ChunkMesh mesh;
    if (std::all_of(halo.begin(), halo.end(), [&](Block block) { return block == halo[0]; }) &&
        std::all_of(halo.fluids.begin(), halo.fluids.end(), [&](Fluid f) { return f == halo.fluids[0]; }) &&
        (halo.fluids[0].amount == 0 || halo.fluids[0].amount == fluid_capacity) &&
        (!is_snow(halo[0]) || is_full_block(halo[0])))
        return mesh;
    const auto block_at = [&](const std::array<int, 3>& p) {
        return halo[p[0] + 1 + 18 * (p[1] + 1 + 18 * (p[2] + 1))];
    };
    const auto fluid_at = [&](const std::array<int, 3>& p) {
        return halo.fluids[p[0] + 1 + 18 * (p[1] + 1 + 18 * (p[2] + 1))];
    };
    const auto solid = [&](const std::array<int, 3>& p) {
        return block_at(p) != Block::ice && is_full_block(block_at(p));
    };
    for (int z = 0; z < 16; ++z)
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const std::array<int, 3> p{x, y, z};
                const Block block = effective_block(block_at(p), fluid_at(p));
                if (block == Block::air)
                    continue;
                FluidSurface surface{};
                bool surface_ready = false;
                const auto surface_at = [&](int cx, int cz) {
                    if (!surface_ready) {
                        std::array<FluidSurfaceSample, 9> samples{};
                        for (int dz = -1; dz <= 1; ++dz)
                            for (int dx = -1; dx <= 1; ++dx)
                                samples[dx + 1 + 3 * (dz + 1)] = {block_at({x + dx, y, z + dz}),
                                                                  fluid_at({x + dx, y, z + dz}),
                                                                  fluid_at({x + dx, y + 1, z + dz})};
                        surface = fluid_surface(samples);
                        surface_ready = true;
                    }
                    return surface[cz ? (cx ? 2 : 3) : cx];
                };
                for (int axis = 0; axis < 3; ++axis) {
                    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
                    for (int sign : {-1, 1}) {
                        auto outside = p;
                        outside[axis] += sign;
                        // Water is not an AO occluder; solid faces remain visible below water.
                        float top = 1.0f, lower = 0.0f;
                        const auto fluid = fluid_at(p);
                        if (is_fluid(block)) {
                            top = fluid_height(fluid, fluid_at({x, y + 1, z}));
                            if (axis != 1) {
                                if (block_at(outside) != Block::ice && block_height(block_at(outside)) >= top)
                                    continue;
                                auto neighbour_above = outside;
                                ++neighbour_above[1];
                                lower = fluid_height(fluid_at(outside), fluid_at(neighbour_above));
                                // Opaque lava retains the interface against transparent water.
                                if (block == Block::lava && fluid_at(outside).kind == FluidKind::water)
                                    lower = 0;
                                if (lower >= top)
                                    continue;
                            } else if (sign > 0) {
                                if (top == 1.0f &&
                                    ((is_solid(block_at(outside)) && block_at(outside) != Block::ice) ||
                                     (fluid_at(outside).amount &&
                                      (block == Block::water || fluid_at(outside).kind == fluid.kind))))
                                    continue;
                            } else if (solid(outside) || (fluid_at(outside).amount == fluid_capacity &&
                                                          fluid_at(outside).kind == fluid.kind))
                                continue;
                        } else {
                            const double own_height = block_height(block);
                            // Hide ice/ice interfaces, but retain opaque surfaces seen through ice.
                            const double neighbour_height =
                                block_at(outside) == Block::ice && block != Block::ice
                                    ? 0.0
                                    : block_height(block_at(outside));
                            if (axis != 1  ? neighbour_height >= own_height
                                : sign > 0 ? own_height == 1.0 && neighbour_height > 0
                                           : neighbour_height == 1.0)
                                continue;
                        }
                        uint32_t packed = static_cast<uint32_t>(x | (y << 4) | (z << 8) | (axis << 12) |
                                                                ((sign > 0 ? 1 : 0) << 14));
                        packed |= material(block, axis, sign) << 15;
                        if (is_fluid(block)) {
                            const unsigned level = fluid_display_level(fluid);
                            packed |= uint32_t(level - 1) << 18;
                            if (top == 1.0f)
                                packed |= 1u << 30;
                            const auto offset_code = [&](float height) -> uint32_t {
                                return height < top ? 1u : height > top ? 2u : 0u;
                            };
                            if (axis == 1) {
                                // Face UV runs along Z then X, unlike the surface's X/Z corner order.
                                constexpr int face_corners[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
                                if (sign > 0)
                                    for (int c = 0; c < 4; ++c)
                                        packed |=
                                            offset_code(surface_at(face_corners[c][0], face_corners[c][1]))
                                            << (22 + 2 * c);
                            } else {
                                const float end0 =
                                    axis == 0 ? surface_at(sign > 0, 0) : surface_at(0, sign > 0);
                                const float end1 =
                                    axis == 0 ? surface_at(sign > 0, 1) : surface_at(1, sign > 0);
                                packed |= offset_code(end0) << 26;
                                packed |= offset_code(end1) << 28;
                                if (lower > 0) {
                                    const unsigned neighbour_level = fluid_display_level(fluid_at(outside));
                                    packed |= uint32_t(neighbour_level - 1) << 22;
                                    packed |= 1u << 31;
                                    // A smoothed endpoint meets its neighbour exactly; the other endpoint
                                    // may retain a cliff corner, producing a triangular transition side.
                                    const bool shared0 =
                                        end0 != top && top < 1.0f && level == neighbour_level + 1;
                                    const bool shared1 =
                                        end1 != top && top < 1.0f && level == neighbour_level + 1;
                                    if (shared0 && shared1)
                                        continue;
                                }
                            }
                            (block == Block::water ? mesh.water : mesh.faces).push_back(packed);
                            continue;
                        }
                        if (block == Block::glow || block == Block::ice || is_snow(block))
                            packed |= 1u << 27;
                        if (is_snow(block)) {
                            const unsigned level = snow_layers(block) - 1;
                            packed |= (level & 7u) << 29;
                            packed |= (level >> 3) << 26;
                        }
                        if (is_solid(block) && fluid_at(outside).amount &&
                            fluid_at(outside).kind == FluidKind::water)
                            packed |= 1u << 28;
                        std::array<uint32_t, 4> ao{};
                        constexpr std::array<std::array<int, 2>, 4> corners{
                            {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
                        for (size_t c = 0; c < 4; ++c) {
                            auto a = outside, b = outside, d = outside;
                            a[u] += corners[c][0];
                            b[v] += corners[c][1];
                            d[u] += corners[c][0];
                            d[v] += corners[c][1];
                            ao[c] = vertex_ao(solid(a), solid(b), solid(d));
                            packed |= ao[c] << (18 + 2 * c);
                        }
                        if (!is_snow(block) && ao[0] + ao[2] > ao[1] + ao[3])
                            packed |= 1u << 26;
                        (block == Block::ice ? mesh.ice : mesh.faces).push_back(packed);
                    }
                }
            }
    return mesh;
}
} // namespace sandbox
