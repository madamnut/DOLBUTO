#include "world/edit.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sandbox {
namespace {
uint32_t local_index(BlockPos p) {
    return static_cast<uint32_t>(local_coordinate(p.x) + 16 * (p.y + world_height * local_coordinate(p.z)));
}
BlockPos decode(ColumnKey key, uint32_t index) {
    return {key.x * 16 + static_cast<int>(index % 16), static_cast<int>((index / 16) % world_height),
            key.z * 16 + static_cast<int>(index / (16 * world_height))};
}
} // namespace
std::optional<BlockHit> raycast_blocks(std::array<double, 3> origin, std::array<double, 3> direction,
                                       const std::function<std::optional<Block>(BlockPos)>& sample,
                                       double reach) {
    const double length = std::hypot(direction[0], direction[1], direction[2]);
    if (!std::isfinite(length) || length <= 0 || !std::isfinite(reach) || reach < 0)
        return {};
    std::array<int, 3> cell{}, step{};
    std::array<double, 3> next{}, delta{};
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(origin[axis]) || std::abs(origin[axis]) > 1000000010.0)
            return {};
        direction[axis] /= length;
        cell[axis] = static_cast<int>(std::floor(origin[axis]));
        step[axis] = direction[axis] > 0 ? 1 : (direction[axis] < 0 ? -1 : 0);
        if (!step[axis]) {
            next[axis] = delta[axis] = std::numeric_limits<double>::infinity();
            continue;
        }
        delta[axis] = std::abs(1.0 / direction[axis]);
        next[axis] =
            (static_cast<double>(cell[axis]) + (step[axis] > 0 ? 1.0 : 0.0) - origin[axis]) / direction[axis];
    }
    double distance{};
    std::optional<BlockPos> adjacent;
    while (distance <= reach) {
        const BlockPos p{cell[0], cell[1], cell[2]};
        const auto block = sample(p);
        if (!block)
            return {};
        if (is_solid(*block)) {
            double entry = distance;
            double exit = std::min(reach, *std::min_element(next.begin(), next.end()));
            auto face = adjacent;
            bool intersects = true;
            for (int axis = 0; axis < 3; ++axis) {
                const double minimum = cell[axis];
                const double maximum = minimum + (axis == 1 ? block_height(*block) : 1.0);
                if (step[axis] == 0) {
                    if (origin[axis] < minimum || origin[axis] >= maximum) {
                        intersects = false;
                        break;
                    }
                    continue;
                }
                const double a = (minimum - origin[axis]) / direction[axis];
                const double b = (maximum - origin[axis]) / direction[axis];
                const double near = std::min(a, b);
                if (near > entry + 1e-10) {
                    entry = near;
                    auto neighbour = cell;
                    neighbour[axis] -= step[axis];
                    face = BlockPos{neighbour[0], neighbour[1], neighbour[2]};
                }
                exit = std::min(exit, std::max(a, b));
                if (entry > exit + 1e-10) {
                    intersects = false;
                    break;
                }
            }
            if (intersects && entry <= reach)
                return BlockHit{p, face, entry};
        }
        const double crossing = *std::min_element(next.begin(), next.end());
        if (!std::isfinite(crossing) || crossing > reach)
            return {};
        int face_axis = -1;
        // Step tied axes together so touching a voxel only along an edge doesn't select it.
        for (int axis = 0; axis < 3; ++axis) {
            if (next[axis] <= crossing + 1e-10) {
                cell[axis] += step[axis];
                next[axis] += delta[axis];
                if (face_axis < 0)
                    face_axis = axis;
            }
        }
        auto neighbour = cell;
        neighbour[face_axis] -= step[face_axis];
        adjacent = BlockPos{neighbour[0], neighbour[1], neighbour[2]};
        distance = crossing;
    }
    return {};
}
std::vector<ChunkKey> affected_chunks(BlockPos p) {
    std::vector<ChunkKey> result;
    if (p.y < 0 || p.y >= world_height)
        return result;
    for (int z = chunk_coordinate(p.z - 1); z <= chunk_coordinate(p.z + 1); ++z)
        for (int y = std::max(0, chunk_coordinate(p.y - 1));
             y <= std::min(chunks_per_column - 1, chunk_coordinate(p.y + 1)); ++y)
            for (int x = chunk_coordinate(p.x - 1); x <= chunk_coordinate(p.x + 1); ++x)
                result.push_back(canonical(ChunkKey{x, y, z}));
    return result;
}
Block WorldEdits::override_block(BlockPos p, Block fallback) const {
    if (p.y < 0 || p.y >= world_height)
        return Block::air;
    const auto it = columns_.find(canonical(ColumnKey{chunk_coordinate(p.x), chunk_coordinate(p.z)}));
    if (it != columns_.end()) {
        const auto block = it->second.find(local_index(p));
        if (block != it->second.end() && block->second.block)
            return *block->second.block;
    }
    return fallback;
}
bool WorldEdits::set(BlockPos p, Block block, Block base) {
    if (p.y < 0 || p.y >= world_height || (block != Block::air && !is_solid(block)) ||
        override_block(p, base) == block)
        return false;
    const auto key = canonical(ColumnKey{chunk_coordinate(p.x), chunk_coordinate(p.z)});
    auto& cells = columns_[key];
    auto [it, inserted] = cells.try_emplace(local_index(p));
    count_ += inserted;
    it->second.block = block == base ? std::nullopt : std::optional{block};
    if (!it->second.block && !it->second.fluid) {
        cells.erase(it);
        --count_;
    }
    if (cells.empty())
        columns_.erase(key);
    return true;
}
Fluid WorldEdits::override_fluid(BlockPos p, Fluid fallback) const {
    if (p.y < 0 || p.y >= world_height)
        return {};
    const auto it = columns_.find(canonical(ColumnKey{chunk_coordinate(p.x), chunk_coordinate(p.z)}));
    if (it != columns_.end()) {
        const auto cell = it->second.find(local_index(p));
        if (cell != it->second.end() && cell->second.fluid)
            return *cell->second.fluid;
    }
    return fallback;
}
bool WorldEdits::set_fluid(BlockPos p, Fluid fluid, Fluid base) {
    if (p.y < 0 || p.y >= world_height || fluid.amount > fluid_capacity ||
        (fluid.amount && fluid.kind != FluidKind::water && fluid.kind != FluidKind::lava) ||
        override_fluid(p, base) == fluid)
        return false;
    if (!fluid.amount)
        fluid = {};
    const auto key = canonical(ColumnKey{chunk_coordinate(p.x), chunk_coordinate(p.z)});
    auto& cells = columns_[key];
    auto [it, inserted] = cells.try_emplace(local_index(p));
    count_ += inserted;
    it->second.fluid = fluid == base ? std::nullopt : std::optional{fluid};
    if (!it->second.block && !it->second.fluid) {
        cells.erase(it);
        --count_;
    }
    if (cells.empty())
        columns_.erase(key);
    return true;
}
bool WorldEdits::has_nearby(ColumnKey column) const {
    for (int z = -1; z <= 1; ++z)
        for (int x = -1; x <= 1; ++x)
            if (columns_.contains(canonical(ColumnKey{column.x + x, column.z + z})))
                return true;
    return false;
}
WorldEdits WorldEdits::subset(std::span<const Column> columns) const {
    WorldEdits result;
    for (const auto& column : columns) {
        auto it = columns_.find(column.key);
        if (it != columns_.end()) {
            result.columns_.emplace(it->first, it->second);
            result.count_ += it->second.size();
        }
    }
    return result;
}
void WorldEdits::apply(Column& column) const {
    auto it = columns_.find(column.key);
    if (it == columns_.end())
        return;
    std::array<std::shared_ptr<std::array<Block, 4096>>, chunks_per_column> changed;
    std::array<std::shared_ptr<std::array<Fluid, 4096>>, chunks_per_column> fluids;
    for (const auto& [index, edit] : it->second) {
        const auto p = decode(column.key, index);
        const int cy = p.y / 16, local = local_coordinate(p.x) + 16 * (p.y % 16 + 16 * local_coordinate(p.z));
        const auto& base = column.chunks[cy];
        if (edit.block) {
            if (!changed[cy]) {
                changed[cy] = std::make_shared<std::array<Block, 4096>>();
                if (base.blocks)
                    *changed[cy] = *base.blocks;
                else
                    changed[cy]->fill(base.uniform);
            }
            (*changed[cy])[local] = *edit.block;
        }
        if (edit.fluid) {
            if (!fluids[cy]) {
                fluids[cy] = std::make_shared<std::array<Fluid, 4096>>();
                if (base.fluids)
                    *fluids[cy] = *base.fluids;
                else
                    fluids[cy]->fill(base.uniform_fluid);
            }
            (*fluids[cy])[local] = *edit.fluid;
        }
    }
    for (int cy = 0; cy < chunks_per_column; ++cy) {
        if (changed[cy])
            column.chunks[cy].blocks = std::move(changed[cy]);
        if (fluids[cy])
            column.chunks[cy].fluids = std::move(fluids[cy]);
    }
}
std::bitset<chunks_per_column> WorldEdits::affected(ColumnKey column) const {
    column = canonical(column);
    std::bitset<chunks_per_column> dirty;
    for (int z = column.z - 1; z <= column.z + 1; ++z)
        for (int x = column.x - 1; x <= column.x + 1; ++x) {
            const auto key = canonical(ColumnKey{x, z});
            const auto it = columns_.find(key);
            if (it == columns_.end())
                continue;
            for (const auto& [index, block] : it->second) {
                const auto p = decode(key, index);
                for (auto affected : affected_chunks(p))
                    if (affected.x == column.x && affected.z == column.z)
                        dirty.set(affected.y);
            }
        }
    return dirty;
}
ChunkMesh WorldEdits::mesh(ChunkKey key, ChunkHalo halo) const {
    for (int z = -1; z <= 16; ++z)
        for (int x = -1; x <= 16; ++x) {
            const int wx = key.x * 16 + x, wz = key.z * 16 + z;
            for (int y = -1; y <= 16; ++y) {
                const int wy = key.y * 16 + y;
                auto& block = halo[x + 1 + 18 * (y + 1 + 18 * (z + 1))];
                block = override_block({wx, wy, wz}, block);
                auto& fluid = halo.fluids[x + 1 + 18 * (y + 1 + 18 * (z + 1))];
                fluid = override_fluid({wx, wy, wz}, fluid);
            }
        }
    return mesh_chunk(halo);
}
} // namespace sandbox
