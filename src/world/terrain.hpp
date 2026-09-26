#pragma once
#include "core/world_rules.hpp"
#include "world/generator.hpp"
#include <array>
#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sandbox {
// Fluid block values are compatibility query results; storage uses the separate fluid layer.
enum class Block : uint8_t {
    air,
    dirt,
    grass,
    rock,
    water,
    sand,
    glow,
    lava,
    ice,
    snow = 16,
    snow_full = 31
};
constexpr bool is_snow(Block block) { return block >= Block::snow && block <= Block::snow_full; }
constexpr unsigned snow_layers(Block block) {
    return is_snow(block) ? unsigned(block) - unsigned(Block::snow) + 1 : 0;
}
constexpr Block snow_block(unsigned layers) {
    return layers >= 1 && layers <= 16 ? Block(unsigned(Block::snow) + layers - 1) : Block::air;
}
constexpr bool is_fluid(Block block) { return block == Block::water || block == Block::lava; }
constexpr bool is_solid(Block block) {
    return (block >= Block::dirt && block <= Block::rock) || block == Block::sand || block == Block::glow ||
           block == Block::ice || is_snow(block);
}
constexpr double block_height(Block block) {
    return is_snow(block) ? snow_layers(block) / 16.0 : is_solid(block) ? 1.0 : 0.0;
}
constexpr bool is_full_block(Block block) { return block_height(block) == 1.0; }
// Empty slots are selectable; air here is an inventory placeholder, never a placement.
inline constexpr std::array<Block, 10> hotbar_blocks{Block::dirt, Block::grass, Block::rock, Block::sand,
                                                     Block::glow, Block::water, Block::lava, Block::ice,
                                                     Block::snow, Block::air};
struct ChunkKey {
    int x{}, y{}, z{};
    auto operator<=>(const ChunkKey&) const = default;
};
struct ColumnKey {
    int x{}, z{};
    bool operator==(const ColumnKey&) const = default;
};
struct ColumnHash {
    size_t operator()(ColumnKey key) const noexcept {
        const auto bits = (uint64_t{static_cast<uint32_t>(key.x)} << 32) | static_cast<uint32_t>(key.z);
        return static_cast<size_t>((bits ^ (bits >> 33)) * 0xff51afd7ed558ccdULL);
    }
};
constexpr ColumnKey canonical(ColumnKey key) { return {wrap_column(key.x), wrap_column(key.z)}; }
constexpr ChunkKey canonical(ChunkKey key) { return {wrap_column(key.x), key.y, wrap_column(key.z)}; }
inline bool within_radius(ColumnKey a, ColumnKey b, int radius) {
    const int64_t dx = column_delta(a.x, b.x), dz = column_delta(a.z, b.z);
    return dx * dx + dz * dz <= int64_t{radius} * radius;
}
inline constexpr int fluid_capacity = 256;
inline constexpr int fluid_display_steps = 16;
inline constexpr int fluid_horizontal_limit = 16;
enum class FluidKind : uint16_t { none, water, lava };
struct Fluid {
    FluidKind kind : 7 {FluidKind::none};
    uint16_t amount : 9 {};
    bool operator==(const Fluid&) const = default;
};
static_assert(sizeof(Fluid) == 2);
constexpr Fluid fluid_amount(FluidKind kind, unsigned amount) {
    return amount ? Fluid{kind, static_cast<uint16_t>(amount)} : Fluid{};
}
constexpr Fluid water_amount(unsigned amount) { return fluid_amount(FluidKind::water, amount); }
constexpr Fluid lava_amount(unsigned amount) { return fluid_amount(FluidKind::lava, amount); }
static_assert(water_amount(fluid_capacity).amount == fluid_capacity);
constexpr Block effective_block(Block block, Fluid fluid) {
    return block == Block::air && fluid.amount ? (fluid.kind == FluidKind::lava ? Block::lava : Block::water)
                                               : block;
}
// Round nonempty liquid upward so sub-display-step amounts remain visible.
constexpr unsigned fluid_display_level(Fluid fluid) {
    constexpr unsigned units = fluid_capacity / fluid_display_steps;
    return (fluid.amount + units - 1) / units;
}
inline float fluid_height(Fluid fluid, Fluid above) {
    return fluid.amount == fluid_capacity && above.amount
               ? 1.0f
               : float(fluid_display_level(fluid)) * (0.875f / fluid_display_steps);
}
struct FluidSurfaceSample {
    Block block{Block::air};
    Fluid fluid{}, above{};
};
// X/Z corner order: (0,0),(1,0),(1,1),(0,1). Same 00--11 diagonal as water triangles.
using FluidSurface = std::array<float, 4>;
// 3x3 horizontal neighbourhood, X fastest, at one Y. Missing data must be a solid barrier.
FluidSurface fluid_surface(const std::array<FluidSurfaceSample, 9>& samples);
float fluid_surface_height(const FluidSurface& surface, float x, float z);
struct Chunk {
    Block uniform{Block::air};
    std::shared_ptr<const std::array<Block, chunk_edge * chunk_edge * chunk_edge>> blocks;
    Fluid uniform_fluid{};
    std::shared_ptr<const std::array<Fluid, 4096>> fluids;
    Block block_at(int x, int y, int z) const { return blocks ? (*blocks)[x + 16 * (y + 16 * z)] : uniform; }
    Fluid fluid_at(int x, int y, int z) const {
        return fluids ? (*fluids)[x + 16 * (y + 16 * z)] : uniform_fluid;
    }
    Block at(int x, int y, int z) const { return effective_block(block_at(x, y, z), fluid_at(x, y, z)); }
    // Convert the generator's temporary material buffer into independent immutable layers.
    void separate_fluids();
    void set(int x, int y, int z, Block block) {
        if (block_at(x, y, z) == block)
            return;
        auto replacement = std::make_shared<std::array<Block, chunk_edge * chunk_edge * chunk_edge>>();
        if (blocks)
            *replacement = *blocks;
        else
            replacement->fill(uniform);
        (*replacement)[x + chunk_edge * (y + chunk_edge * z)] = block;
        blocks = std::move(replacement);
    }
};
// GPU ABI: xyz 4 bits each, axis 2, positive 1, material 3, four AO values 2 each,
// diagonal 1 (bit26), extended solid escape bit27, solid touches water 1 (bit28).
// Material5=water,7=lava. Material0/1/2+bit27=glow/ice/snow (atlas tiles8/9/10).
// Snow uses bits29..31 as low3 bits and bit26 as high1 bit of layers-1, fixed 00--11 diagonal.
// Fluids: bits18..21=base display level-1. Horizontal faces: bits22..29=four corner offsets
// (0=flat,1=-half step,2=+half step) in face-corner order. Vertical faces: bits22..25=lower
// neighbour level-1,26..29=two top endpoint offsets. Bit30=full cell height,31=wet neighbour.
// Fluids always use the 00--11 diagonal; solids keep their original AO and diagonal layout.
// Keep shaders/world.vert in sync with this layout.
using PackedFace = uint32_t;
constexpr uint32_t vertex_ao(bool side1, bool side2, bool corner) {
    return side1 && side2 ? 0u
                          : 3u - static_cast<uint32_t>(side1) - static_cast<uint32_t>(side2) -
                                static_cast<uint32_t>(corner);
}
struct ChunkMesh {
    std::vector<PackedFace> faces;
    std::vector<PackedFace> ice;
    std::vector<PackedFace> water;
};
struct Column {
    ColumnKey key;
    std::array<Chunk, chunks_per_column> chunks;
    Fluid fluid_at(int x, int y, int z) const {
        return y < 0 || y >= world_height ? Fluid{} : chunks[y / 16].fluid_at(x, y % 16, z);
    }
    Block block_at(int x, int y, int z) const {
        return y < 0 || y >= world_height ? Block::air : chunks[y / 16].block_at(x, y % 16, z);
    }
    Block at(int x, int y, int z) const {
        if (y < 0 || y >= world_height)
            return Block::air;
        return chunks[y / chunk_edge].at(x, y % chunk_edge, z);
    }
};
struct ColumnLight;
struct BuiltColumn {
    Column data;
    std::array<ChunkMesh, chunks_per_column> meshes;
    std::shared_ptr<const ColumnLight> light;
    double generation_ms{}, lighting_ms{}, meshing_ms{};
};
struct TerrainSite {
    int surface_y{-1}; // Highest natural solid cell in this X/Z stack; -1 means no solid.
};
struct TerrainTile {
    ColumnKey key;
    uint32_t seed{};
    std::array<TerrainSite, chunk_edge * chunk_edge> sites;
    uint64_t signature{};
};
TerrainTile generate_tile(ColumnKey key, const TerrainGenerator& generator);
Chunk generate_chunk(ChunkKey key, const TerrainGenerator& generator, const TerrainTile* tile = nullptr);
int terrain_spawn_height(int x, int z, const TerrainGenerator& generator);
TerrainTile generate_tile(ColumnKey key, uint32_t seed);
// Independent generation: the optional tile reuses column-local surface metadata.
Chunk generate_chunk(ChunkKey key, uint32_t seed, const TerrainTile* tile = nullptr);
int terrain_spawn_height(int x, int z, uint32_t seed);
struct ChunkHalo : std::array<Block, 18 * 18 * 18> {
    std::array<Fluid, 18 * 18 * 18> fluids{};
};
// Index x+1 + 3*(y+1 + 3*(z+1)); null neighbours inside the world are NOT air.
using ChunkNeighbours = std::array<std::shared_ptr<const Chunk>, 27>;
ChunkHalo make_halo(ChunkKey key, const ChunkNeighbours& neighbours);
// Halo is an 18^3 array: local coordinates [-1,16], x is the fastest axis.
ChunkMesh mesh_chunk(const ChunkHalo& halo);
} // namespace sandbox
