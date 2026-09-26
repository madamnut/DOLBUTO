#pragma once
#include "world/terrain.hpp"
#include <bitset>
#include <compare>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>

namespace sandbox {
struct BlockPos {
    int x{}, y{}, z{};
    auto operator<=>(const BlockPos&) const = default;
};
inline constexpr double edit_reach = 6.0;
struct BlockHit {
    BlockPos block;
    std::optional<BlockPos> adjacent;
    double distance{};
};
// nullopt from the sampler means unloaded: rays stop rather than selecting invisible terrain.
std::optional<BlockHit> raycast_blocks(std::array<double, 3> origin, std::array<double, 3> direction,
                                       const std::function<std::optional<Block>(BlockPos)>& sample,
                                       double reach = edit_reach);
std::vector<ChunkKey> affected_chunks(BlockPos block);

// Main-thread session state. Worker-generated base terrain never reads this map.
class WorldEdits {
  public:
    Block override_block(BlockPos position, Block fallback) const;
    bool set(BlockPos position, Block block, Block base);
    Fluid override_fluid(BlockPos position, Fluid fallback) const;
    bool set_fluid(BlockPos position, Fluid fluid, Fluid base);
    size_t count() const { return count_; }
    bool has_nearby(ColumnKey column) const;
    WorldEdits subset(std::span<const Column> columns) const;
    void apply(Column& column) const;
    std::bitset<chunks_per_column> affected(ColumnKey column) const;
    ChunkMesh mesh(ChunkKey key, ChunkHalo halo) const;

  private:
    size_t count_{};
    struct CellEdit {
        std::optional<Block> block;
        std::optional<Fluid> fluid;
    };
    std::unordered_map<ColumnKey, std::unordered_map<uint32_t, CellEdit>, ColumnHash> columns_;
};
} // namespace sandbox
