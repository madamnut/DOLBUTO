#include "world/fluid.hpp"
#include <algorithm>
#include <unordered_map>

namespace sandbox {
namespace {
constexpr std::array<BlockPos, 6> directions{
    {{0, -1, 0}, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}, {0, 0, -1}}};
BlockPos adjacent(BlockPos p, BlockPos d) {
    return {wrap_block(p.x + d.x), p.y + d.y, wrap_block(p.z + d.z)};
}
bool open(const std::optional<FluidCell>& c) { return c && !is_solid(c->block); }
bool accepts(const std::optional<FluidCell>& c, FluidKind kind) {
    return open(c) && (!c->fluid.amount || c->fluid.kind == kind);
}
uint64_t mix(uint64_t n) {
    n ^= n >> 30;
    n *= 0xbf58476d1ce4e5b9ULL;
    n ^= n >> 27;
    n *= 0x94d049bb133111ebULL;
    return n ^ (n >> 31);
}
} // namespace
uint64_t FluidSimulation::key(BlockPos p) {
    return uint64_t(wrap_block(p.x)) | (uint64_t(wrap_block(p.z)) << 17) | (uint64_t(p.y) << 34);
}
BlockPos FluidSimulation::position(uint64_t k) {
    return {int(k & 131071), int(k >> 34), int((k >> 17) & 131071)};
}
void FluidSimulation::wake(BlockPos p) {
    if (p.y >= 0 && p.y < world_height)
        active_.insert(key(p));
    for (auto d : directions) {
        auto q = adjacent(p, d);
        if (q.y >= 0 && q.y < world_height)
            active_.insert(key(q));
    }
}
void FluidSimulation::seed(const Column& column, const Sample& sample) {
    // The new column is already a consistent edited snapshot; avoid world-map lookups for its interior.
    const auto nearby = [&](BlockPos p) -> std::optional<FluidCell> {
        if (p.y < 0 || p.y >= world_height)
            return {};
        const auto k = canonical(ColumnKey{chunk_coordinate(p.x), chunk_coordinate(p.z)});
        if (k == column.key)
            return FluidCell{column.block_at(local_coordinate(p.x), p.y, local_coordinate(p.z)),
                             column.fluid_at(local_coordinate(p.x), p.y, local_coordinate(p.z))};
        return sample(p);
    };
    const auto consider = [&](BlockPos p) {
        const auto c = nearby(p);
        if (!open(c) || !c->fluid.amount)
            return;
        const auto below = nearby(adjacent(p, directions[0]));
        bool movable = accepts(below, c->fluid.kind) && below->fluid.amount < fluid_capacity;
        for (int i = 2; !movable && i < 6; ++i) {
            const auto n = nearby(adjacent(p, directions[i]));
            movable = accepts(n, c->fluid.kind) && c->fluid.amount > n->fluid.amount + 1;
        }
        if (movable)
            active_.insert(key(p));
    };
    for (int cy = 0; cy < chunks_per_column; ++cy) {
        const auto& chunk = column.chunks[cy];
        if (!chunk.fluids && !chunk.uniform_fluid.amount)
            continue;
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    if (chunk.fluid_at(x, y, z).amount)
                        consider({column.key.x * 16 + x, cy * 16 + y, column.key.z * 16 + z});
                }
    }
    // Previously unloaded faces were closed. Reconsider the resident cells across each new face.
    for (int y = 0; y < world_height; ++y)
        for (int i = 0; i < 16; ++i) {
            consider({column.key.x * 16 - 1, y, column.key.z * 16 + i});
            consider({column.key.x * 16 + 16, y, column.key.z * 16 + i});
            consider({column.key.x * 16 + i, y, column.key.z * 16 - 1});
            consider({column.key.x * 16 + i, y, column.key.z * 16 + 16});
        }
}
std::vector<FluidChange> FluidSimulation::tick(const Sample& sample) {
    ++tick_;
    std::vector<uint64_t> candidates(active_.begin(), active_.end());
    active_.clear();
    // Stable ordering independent of hash insertion, chunk worker completion, and iteration order.
    std::sort(candidates.begin(), candidates.end(),
              [&](auto a, auto b) { return mix(a + tick_) < mix(b + tick_); });
    struct Reservation {
        FluidCell cell;
        int incoming{}, outgoing{};
        FluidKind incoming_kind{FluidKind::none};
    };
    std::unordered_map<uint64_t, Reservation> reservations;
    const auto get = [&](uint64_t id) -> Reservation* {
        if (auto it = reservations.find(id); it != reservations.end())
            return &it->second;
        const auto p = position(id);
        if (p.y < 0 || p.y >= world_height)
            return nullptr;
        const auto c = sample(p);
        if (!open(c))
            return nullptr;
        return &reservations.emplace(id, Reservation{*c}).first->second;
    };
    const auto due = [&](FluidKind kind) { return kind != FluidKind::lava || tick_ % 20 == 0; };
    const auto compatible = [](const Reservation* target, FluidKind kind) {
        return target && (!target->cell.fluid.amount || target->cell.fluid.kind == kind) &&
               (!target->incoming || target->incoming_kind == kind);
    };
    // Only the tick-start amount may leave. Incoming liquid cannot cascade again in this tick.
    for (auto id : candidates) {
        auto* source = get(id);
        const auto p = position(id);
        if (!source || !source->cell.fluid.amount)
            continue;
        if (!due(source->cell.fluid.kind)) {
            active_.insert(id); // Keep pending lava awake until the next 20-tick update.
            continue;
        }
        if (p.y == 0)
            continue;
        auto* target = get(key(adjacent(p, directions[0])));
        if (!compatible(target, source->cell.fluid.kind))
            continue;
        const int amount = std::min(int(source->cell.fluid.amount),
                                    fluid_capacity - int(target->cell.fluid.amount) - target->incoming);
        source->outgoing += amount;
        target->incoming += amount;
        if (amount)
            target->incoming_kind = source->cell.fluid.kind;
    }
    // Downward reservations have priority. The total horizontal budget is shared by all four faces.
    for (auto id : candidates) {
        auto* source = get(id);
        if (!source || !due(source->cell.fluid.kind) || int(source->cell.fluid.amount) - source->outgoing < 2)
            continue;
        const unsigned start = unsigned(mix(id) + tick_) & 3u;
        std::array<Reservation*, 4> neighbours{};
        for (unsigned i = 0; i < 4; ++i)
            neighbours[i] = get(key(adjacent(position(id), directions[2 + ((start + i) & 3u)])));
        // Small integer transfers equalize recipients without sending more than half a pair's difference.
        // This loop is bounded by16 regardless of the amount stored in the cell.
        for (int unit = 0; unit < fluid_horizontal_limit; ++unit) {
            const int remaining = int(source->cell.fluid.amount) - source->outgoing;
            if (remaining < 2)
                break;
            Reservation* best = nullptr;
            int best_level = remaining - 1;
            for (auto* target : neighbours) {
                if (!compatible(target, source->cell.fluid.kind))
                    continue;
                // Do not spend capacity freed in this tick or re-send incoming liquid.
                const int level = int(target->cell.fluid.amount) + target->incoming;
                if (level < best_level && level < fluid_capacity) {
                    best = target;
                    best_level = level;
                }
            }
            if (!best)
                break;
            ++source->outgoing;
            ++best->incoming;
            best->incoming_kind = source->cell.fluid.kind;
        }
    }
    std::vector<FluidChange> result;
    for (const auto& [id, r] : reservations) {
        if (r.incoming == r.outgoing)
            continue;
        const int amount = int(r.cell.fluid.amount) + r.incoming - r.outgoing;
        const auto p = position(id);
        const auto kind = r.cell.fluid.amount ? r.cell.fluid.kind : r.incoming_kind;
        result.push_back({p, r.cell.fluid, fluid_amount(kind, unsigned(amount))});
        wake(p);
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.position < b.position; });
    return result;
}
} // namespace sandbox
