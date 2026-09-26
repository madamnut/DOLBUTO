#include "world/lighting.hpp"
#include "world/light_diagnostics.hpp"
#include "world/profiling.hpp"
#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace sandbox {
namespace {
bool blocks_light(Block b) { return (is_full_block(b) && b != Block::ice) || b == Block::lava; }
bool attenuates_sky(Block b) {
    return b == Block::water || b == Block::ice || (is_snow(b) && !is_full_block(b));
}
bool emits_light(Block b) { return b == Block::glow || b == Block::lava; }
// Edited/loading-edge fallback uses the same column-local + boundary algorithm.
// Known snapshots have already been reconciled at the current edit baseline.
std::unique_ptr<ColumnLight> calculate(ColumnKey key, uint64_t revision, std::array<Column, 9> columns,
                                       const WorldEdits& edits, std::stop_token stop,
                                       const std::array<std::shared_ptr<const ColumnLight>, 9>& known = {}) {
    std::array<LightInput, 9> inputs;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (stop.stop_requested())
            return {};
        edits.apply(columns[i]);
        inputs[i].light = known[i] ? known[i] : local_column_light(columns[i], stop);
        if (!inputs[i].light)
            return {};
        inputs[i].blocks = std::move(columns[i]);
    }
    auto connected = connect_column_light(key, std::move(inputs), stop);
    if (!connected)
        return {};
    auto result = std::make_unique<ColumnLight>(*connected);
    result->revision = revision;
    return result;
}

// Incremental jobs read immutable, mutually consistent column snapshots. Only touched
// chunks are copied; column-local generation snapshots remain immutable.
class LightUpdate {
    static constexpr int column_cells = 16 * 16 * world_height;
    using Samples = std::array<uint8_t, 18 * 18 * 18>;
    struct Access {
        const Block* blocks{};
        Block uniform{Block::air};
        const uint8_t* light{};
        uint8_t level{};
        bool has_glow{true};
        const Fluid* fluids{};
        Fluid uniform_fluid{};
    };
    struct Entry {
        LightInput input;
        std::array<std::shared_ptr<Samples>, chunks_per_column> changed;
        std::bitset<chunks_per_column> dirty;
        std::bitset<chunks_per_column> interior_dirty;
        std::array<Access, chunks_per_column> access;
        std::array<std::unique_ptr<std::bitset<4096>>, chunks_per_column> queued;
        std::array<int, 4> adjacent{-1, -1, -1, -1};
        std::array<int, 9> nearby{-1, -1, -1, -1, -1, -1, -1, -1, -1};
    };
    std::vector<Entry> entries_;
    std::unordered_map<ColumnKey, int, ColumnHash> lookup_;
    std::map<int, std::pair<Block, Block>> changes_;
    std::unordered_map<int, std::array<uint8_t, world_height>> direct_;
    std::stop_token stop_;
    uint64_t revision_;
    static int offset(int x, int y, int z) { return x + 16 * z + 256 * y; }
    static int sample_index(int x, int y, int z) { return x + 1 + 18 * (y % 16 + 1 + 18 * (z + 1)); }
    Block block(int id) const {
        const int local = id % column_cells;
        const auto& a = entries_[id / column_cells].access[local / 4096];
        const int index = local % 16 + 16 * ((local / 256 % 16) + 16 * (local / 16 % 16));
        return effective_block(a.blocks ? a.blocks[index] : a.uniform,
                               a.fluids ? a.fluids[index] : a.uniform_fluid);
    }
    uint8_t packed(int id) const {
        const auto& entry = entries_[id / column_cells];
        const int local = id % column_cells, y = local / 256;
        const int x = local % 16, z = local / 16 % 16;
        const auto& a = entry.access[y / 16];
        return a.light ? a.light[sample_index(x, y, z)] : a.level;
    }
    int value(int id, int shift) const { return (packed(id) >> shift) & 15; }
    template <class Visit> void neighbours(int id, Visit visit) const {
        const int column = id / column_cells, local = id % column_cells;
        const int x = local % 16, z = local / 16 % 16, y = local / 256;
        const auto& adjacent = entries_[column].adjacent;
        if (x > 0)
            visit(id - 1);
        else if (adjacent[0] >= 0)
            visit(adjacent[0] * column_cells + offset(15, y, z));
        if (x < 15)
            visit(id + 1);
        else if (adjacent[1] >= 0)
            visit(adjacent[1] * column_cells + offset(0, y, z));
        if (z > 0)
            visit(id - 16);
        else if (adjacent[2] >= 0)
            visit(adjacent[2] * column_cells + offset(x, y, 15));
        if (z < 15)
            visit(id + 16);
        else if (adjacent[3] >= 0)
            visit(adjacent[3] * column_cells + offset(x, y, 0));
        if (y > 0)
            visit(id - 256);
        if (y + 1 < world_height)
            visit(id + 256);
    }
    void set(int id, int shift, int level) {
        const uint8_t previous = packed(id);
        const auto next = static_cast<uint8_t>((previous & ~(15 << shift)) | (level << shift));
        if (next == previous)
            return;
        light_count(&LightWorkStats::updates);
        const int column = id / column_cells, local = id % column_cells, y = local / 256;
        const int x = local % 16, z = local / 16 % 16;
        auto& entry = entries_[column];
        auto& changed = entry.changed[y / 16];
        if (!changed) {
            changed = std::make_shared<Samples>();
            const auto& original = entry.input.light->chunks[y / 16];
            if (original.values)
                *changed = *original.values;
            else
                changed->fill(original.uniform);
            entry.access[y / 16].light = changed->data();
        }
        (*changed)[sample_index(x, y, z)] = next;
        if (next & 0xf0)
            entry.access[y / 16].has_glow = true;
        mark_dirty(id);
    }
    void mark_dirty(int id) {
        const int column = id / column_cells, local = id % column_cells, y = local / 256;
        const int x = local % 16, z = local / 16 % 16;
        auto& entry = entries_[column];
        entry.interior_dirty.set(y / 16);
        // Every chunk whose one-cell sampling halo contains this cell must be repacked.
        for (int dz = (z == 0 ? -1 : 0); dz <= (z == 15 ? 1 : 0); ++dz)
            for (int dx = (x == 0 ? -1 : 0); dx <= (x == 15 ? 1 : 0); ++dx) {
                int neighbour = column;
                if (dx || dz) {
                    neighbour = entry.nearby[dx + 1 + 3 * (dz + 1)];
                    if (neighbour < 0)
                        continue;
                }
                for (int dy = (y % 16 == 0 ? -1 : 0); dy <= (y % 16 == 15 ? 1 : 0); ++dy) {
                    const int cy = y / 16 + dy;
                    if (cy >= 0 && cy < chunks_per_column)
                        entries_[neighbour].dirty.set(cy);
                }
            }
    }
    const std::array<uint8_t, world_height>& direct(int id) {
        const int stack = id - (id % column_cells / 256) * 256;
        auto [it, inserted] = direct_.try_emplace(stack);
        if (inserted) {
            it->second.fill(0);
            int light = 15;
            for (int y = world_height - 1; y >= 0; --y) {
                const auto b = block(stack + y * 256);
                if (blocks_light(b))
                    light = 0;
                else if (attenuates_sky(b))
                    light = std::max(0, light - 1);
                it->second[y] = static_cast<uint8_t>(light);
                if (!light)
                    break; // Direct sky cannot recover below an opaque/fully attenuated cell.
            }
        }
        return it->second;
    }
    int source(int id, int shift) {
        return shift == 4 ? (emits_light(block(id)) ? 15 : 0) : direct(id)[id % column_cells / 256];
    }
    bool propagate(int shift, const std::vector<int>& seeds, bool reset_sources = true) {
        std::vector<std::pair<int, int>> removal;
        std::vector<int> addition;
        const auto enqueue = [&](int id) {
            const int local = id % column_cells;
            auto& queued = entries_[id / column_cells].queued[local / 4096];
            if (!queued)
                queued = std::make_unique<std::bitset<4096>>();
            if (!queued->test(local % 4096)) {
                queued->set(local % 4096);
                addition.push_back(id);
                light_count(&LightWorkStats::pushes);
            }
        };
        for (int id : seeds) {
            if (!reset_sources) {
                enqueue(id);
                continue;
            }
            const int old = value(id, shift), intrinsic = source(id, shift);
            set(id, shift, intrinsic);
            if (old > intrinsic) {
                removal.emplace_back(id, old);
                light_count(&LightWorkStats::pushes);
            }
            enqueue(id);
            neighbours(id, [&](int n) { enqueue(n); });
        }
        size_t iterations = 0;
        for (size_t cursor = 0; cursor < removal.size(); ++cursor) {
            if ((++iterations & 4095) == 0 && stop_.stop_requested())
                return false;
            const auto [id, old] = removal[cursor];
            light_count(&LightWorkStats::pops);
            neighbours(id, [&](int n) {
                const int level = value(n, shift);
                if (!level)
                    return;
                if (level < old) {
                    const int intrinsic = source(n, shift);
                    if (level > intrinsic) {
                        set(n, shift, intrinsic);
                        removal.emplace_back(n, level);
                        light_count(&LightWorkStats::pushes);
                    }
                    if (intrinsic)
                        enqueue(n);
                } else {
                    // Equal/stronger light can belong to an independent source.
                    enqueue(n);
                }
            });
        }
        // Removal must finish before sorting additions: old queued levels may have decreased.
        std::array<std::vector<int>, 16> buckets;
        for (int id : addition) {
            const int local = id % column_cells;
            entries_[id / column_cells].queued[local / 4096]->reset(local % 4096);
            const int level = value(id, shift);
            if (level > 1)
                buckets[level].push_back(id);
            else
                light_count(&LightWorkStats::pops); // Discard a zero/non-propagating candidate.
        }
        for (int level = 15; level > 1; --level)
            while (!buckets[level].empty()) {
                if ((++iterations & 4095) == 0 && stop_.stop_requested())
                    return false;
                const int id = buckets[level].back();
                buckets[level].pop_back();
                light_count(&LightWorkStats::pops);
                if (value(id, shift) != level) {
                    light_count(&LightWorkStats::stale);
                    continue;
                }
                const int next = level - 1;
                neighbours(id, [&](int n) {
                    if (!blocks_light(block(n)) && value(n, shift) < next) {
                        set(n, shift, next);
                        if (next > 1) {
                            buckets[next].push_back(n);
                            light_count(&LightWorkStats::pushes);
                        }
                    }
                });
            }
        return !stop_.stop_requested();
    }

  public:
    LightUpdate(std::vector<LightInput> inputs, const WorldEdits& edits,
                const std::vector<LightChange>& changes, uint64_t revision, std::stop_token stop)
        : stop_(stop), revision_(revision) {
        entries_.reserve(inputs.size());
        for (auto& input : inputs) {
            edits.apply(input.blocks);
            lookup_.emplace(input.blocks.key, static_cast<int>(entries_.size()));
            Entry entry;
            entry.input = std::move(input);
            for (int cy = 0; cy < chunks_per_column; ++cy) {
                const auto& b = entry.input.blocks.chunks[cy];
                const auto& l = entry.input.light->chunks[cy];
                entry.access[cy] = {b.blocks ? b.blocks->data() : nullptr,
                                    b.uniform,
                                    l.values ? l.values->data() : nullptr,
                                    l.uniform,
                                    l.has_block_light,
                                    b.fluids ? b.fluids->data() : nullptr,
                                    b.uniform_fluid};
            }
            entries_.push_back(std::move(entry));
        }
        for (auto& entry : entries_) {
            for (int z = -1; z <= 1; ++z)
                for (int x = -1; x <= 1; ++x) {
                    const auto key = entry.input.blocks.key;
                    const auto it = lookup_.find(canonical(ColumnKey{key.x + x, key.z + z}));
                    if (it != lookup_.end())
                        entry.nearby[x + 1 + 3 * (z + 1)] = it->second;
                }
            entry.adjacent = {entry.nearby[3], entry.nearby[5], entry.nearby[1], entry.nearby[7]};
        }
        for (const auto& change : changes) {
            const auto p = change.position;
            const auto it = lookup_.find(canonical(ColumnKey{chunk_coordinate(p.x), chunk_coordinate(p.z)}));
            if (it == lookup_.end())
                throw std::runtime_error("Missing edited lighting column.");
            const int id =
                it->second * column_cells + offset(local_coordinate(p.x), p.y, local_coordinate(p.z));
            auto [entry, inserted] = changes_.try_emplace(id, change.before, change.after);
            if (!inserted)
                entry->second.second = change.after;
        }
    }
    std::unique_ptr<LightResult> run() {
        std::vector<int> sky_seeds, block_seeds;
        std::set<int> stacks;
        for (const auto& [id, change] : changes_) {
            if (change.first == change.second)
                continue;
            // Opaque material changes with equal emission have no lighting effect.
            if (blocks_light(change.first) == blocks_light(change.second) &&
                emits_light(change.first) == emits_light(change.second) &&
                attenuates_sky(change.first) == attenuates_sky(change.second))
                continue;
            // Occupancy can change while both light channels remain zero.
            if (blocks_light(change.first) != blocks_light(change.second))
                mark_dirty(id);
            sky_seeds.push_back(id);
            block_seeds.push_back(id);
            stacks.insert(id - (id % column_cells / 256) * 256);
        }
        for (int stack : stacks) {
            if (stop_.stop_requested())
                return {};
            const auto& now = direct(stack);
            int previous = 15;
            for (int y = world_height - 1; y >= 0; --y) {
                const int id = stack + y * 256;
                const auto changed = changes_.find(id);
                const auto before = changed == changes_.end() ? block(id) : changed->second.first;
                if (blocks_light(before))
                    previous = 0;
                else if (attenuates_sky(before))
                    previous = std::max(0, previous - 1);
                if (previous != now[y])
                    sky_seeds.push_back(id);
                if (!previous && !now[y])
                    break;
            }
        }
        std::sort(sky_seeds.begin(), sky_seeds.end());
        sky_seeds.erase(std::unique(sky_seeds.begin(), sky_seeds.end()), sky_seeds.end());
        if (!propagate(0, sky_seeds) || !propagate(4, block_seeds))
            return {};
        return finish();
    }
    std::unique_ptr<LightResult> connect(ColumnKey target) {
        std::vector<int> sky_seeds, block_seeds;
        {
            profiling::Scope measure(profiling::Stage::boundary_scan);
            // Compare each shared X/Z face once. Only an actual cross-face increase is seeded.
            for (int column = 0; column < static_cast<int>(entries_.size()); ++column) {
                auto& entry = entries_[column];
                if (entry.input.blocks.key == target)
                    entry.dirty.set(); // Also replace all temporary local halos, including diagonals.
                for (int direction : {1, 3}) {
                    const int adjacent = entry.adjacent[direction];
                    if (adjacent < 0)
                        continue;
                    for (int cy = 0; cy < chunks_per_column; ++cy) {
                        const auto& a_chunk = entry.access[cy];
                        const auto& b_chunk = entries_[adjacent].access[cy];
                        if (!a_chunk.light && !b_chunk.light &&
                            std::abs(int(a_chunk.level & 15) - int(b_chunk.level & 15)) <= 1 &&
                            std::abs(int(a_chunk.level >> 4) - int(b_chunk.level >> 4)) <= 1)
                            continue;
                        if (!a_chunk.blocks && !b_chunk.blocks && blocks_light(a_chunk.uniform) &&
                            blocks_light(b_chunk.uniform))
                            continue;
                        const bool glow = a_chunk.has_glow || b_chunk.has_glow;
                        for (int y = cy * 16; y < cy * 16 + 16; ++y) {
                            if (stop_.stop_requested())
                                return {};
                            for (int t = 0; t < 16; ++t) {
                                light_count(&LightWorkStats::boundary_cells);
                                const int a = column * column_cells +
                                              (direction == 1 ? offset(15, y, t) : offset(t, y, 15));
                                const int b = adjacent * column_cells +
                                              (direction == 1 ? offset(0, y, t) : offset(t, y, 0));
                                const auto seed = [&](int shift, std::vector<int>& seeds) {
                                    if (!blocks_light(block(b)) && value(a, shift) > value(b, shift) + 1)
                                        seeds.push_back(a);
                                    if (!blocks_light(block(a)) && value(b, shift) > value(a, shift) + 1)
                                        seeds.push_back(b);
                                };
                                seed(0, sky_seeds);
                                if (glow)
                                    seed(4, block_seeds);
                            }
                        }
                    }
                }
            }
        }
        {
            profiling::Scope measure(profiling::Stage::boundary_flood);
            if (!propagate(0, sky_seeds, false) || !propagate(4, block_seeds, false))
                return {};
        }
        return finish(target);
    }

  private:
    std::unique_ptr<LightResult> finish(std::optional<ColumnKey> only = {}) {
        profiling::Scope measure(profiling::Stage::light_halo);
        auto result = std::make_unique<LightResult>();
        for (int column = 0; column < static_cast<int>(entries_.size()); ++column) {
            const auto& entry = entries_[column];
            if ((only && entry.input.blocks.key != *only) || !entry.dirty.any())
                continue;
            auto output = std::make_shared<ColumnLight>(*entry.input.light);
            output->revision = revision_;
            bool different = false;
            for (int cy = 0; cy < chunks_per_column; ++cy) {
                if (!entry.dirty.test(cy))
                    continue;
                if (stop_.stop_requested())
                    return {};
                const auto& original = entry.input.light->chunks[cy];
                const Access below{nullptr, Block::air, nullptr, 0, false};
                const Access above{nullptr, Block::air, nullptr, 15, false};
                std::array<const Access*, 27> sources{};
                bool uniform = true, first = true, opaque = false, possible_glow = false;
                uint8_t level = 0;
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int near = entry.nearby[dx + 1 + 3 * (dz + 1)];
                            const Access* source = cy + dy < 0                    ? &below
                                                   : cy + dy >= chunks_per_column ? &above
                                                   : near < 0 ? nullptr
                                                              : &entries_[near].access[cy + dy];
                            sources[dx + 1 + 3 * (dy + 1 + 3 * (dz + 1))] = source;
                            if (!source) {
                                uniform = false;
                                possible_glow |= original.has_block_light;
                                continue;
                            }
                            possible_glow |= source->has_glow;
                            if (source->light || source->blocks || source->fluids)
                                uniform = false;
                            if (first) {
                                first = false;
                                level = source->level;
                                opaque =
                                    blocks_light(effective_block(source->uniform, source->uniform_fluid));
                            } else if (level != source->level ||
                                       opaque != blocks_light(
                                                     effective_block(source->uniform, source->uniform_fluid)))
                                uniform = false;
                        }
                auto& next = output->chunks[cy];
                if (uniform) {
                    next.has_block_light = (level & 0xf0) != 0;
                    if (original.values || original.occluders || original.uniform != level ||
                        original.uniform_opaque != opaque) {
                        next.uniform = level;
                        next.values.reset();
                        next.uniform_opaque = opaque;
                        next.occluders.reset();
                        different = true;
                    }
                    continue;
                }
                auto samples = std::make_shared<Samples>();
                auto occluders = std::make_shared<std::bitset<18 * 18 * 18>>();
                const bool interior_changed = entry.interior_dirty.test(cy);
                if (!interior_changed) {
                    if (original.values)
                        *samples = *original.values;
                    else
                        samples->fill(original.uniform);
                    if (original.occluders)
                        *occluders = *original.occluders;
                    else if (original.uniform_opaque)
                        occluders->set();
                }
                bool changed = false;
                for (int z = -1; z <= 16; ++z)
                    for (int y = -1; y <= 16; ++y)
                        for (int x = -1; x <= 16; ++x) {
                            if (!interior_changed && x >= 0 && x < 16 && y >= 0 && y < 16 && z >= 0 && z < 16)
                                continue;
                            light_count(&LightWorkStats::halo_cells);
                            const int dx = x < 0    ? -1
                                           : x > 15 ? 1
                                                    : 0,
                                      dy = y < 0    ? -1
                                           : y > 15 ? 1
                                                    : 0,
                                      dz = z < 0    ? -1
                                           : z > 15 ? 1
                                                    : 0;
                            const auto* source = sources[dx + 1 + 3 * (dy + 1 + 3 * (dz + 1))];
                            const int lx = (x + 16) % 16, ly = (y + 16) % 16, lz = (z + 16) % 16;
                            const uint8_t value = !source ? original.at(x, y, z)
                                                  : source->light
                                                      ? source->light[lx + 1 + 18 * (ly + 1 + 18 * (lz + 1))]
                                                      : source->level;
                            const bool blocked =
                                !source ? original.opaque_at(x, y, z)
                                        : blocks_light(effective_block(
                                              source->blocks ? source->blocks[lx + 16 * (ly + 16 * lz)]
                                                             : source->uniform,
                                              source->fluids ? source->fluids[lx + 16 * (ly + 16 * lz)]
                                                             : source->uniform_fluid));
                            const int index = x + 1 + 18 * (y + 1 + 18 * (z + 1));
                            (*samples)[index] = value;
                            occluders->set(index, blocked);
                            changed |=
                                value != original.at(x, y, z) || blocked != original.opaque_at(x, y, z);
                        }
                if (!changed)
                    continue;
                different = true;
                next.uniform = (*samples)[0];
                next.values.reset();
                if (!std::all_of(samples->begin(), samples->end(),
                                 [&](uint8_t v) { return v == next.uniform; }))
                    next.values = std::move(samples);
                next.uniform_opaque = occluders->all();
                next.occluders.reset();
                if (!occluders->none() && !next.uniform_opaque)
                    next.occluders = std::move(occluders);
                next.has_block_light =
                    possible_glow && (next.values ? std::any_of(next.values->begin(), next.values->end(),
                                                                [](uint8_t v) { return (v & 0xf0) != 0; })
                                                  : (next.uniform & 0xf0) != 0);
            }
            if (different || only)
                result->columns.push_back(std::move(output));
        }
        return result;
    }
};
} // namespace
std::shared_ptr<const ColumnLight> local_column_light(const Column& column, std::stop_token stop) {
    constexpr int count = 16 * 16 * world_height;
    thread_local std::vector<Block> blocks(count);
    thread_local std::vector<uint8_t> sky(count), glow(count);
    thread_local std::array<std::vector<int>, 16> buckets;
    bool any_glow = false;
    {
        profiling::Scope measure(profiling::Stage::light_init);
        std::array<uint8_t, 256> direct;
        direct.fill(15);
        for (int cy = chunks_per_column - 1; cy >= 0; --cy) {
            if (stop.stop_requested())
                return {};
            const auto& chunk = column.chunks[cy];
            const int base = cy * 4096;
            if (!chunk.blocks && blocks_light(chunk.uniform)) {
                std::fill_n(blocks.begin() + base, 4096, chunk.uniform);
                std::fill_n(sky.begin() + base, 4096, 0);
                const uint8_t emission = emits_light(chunk.uniform) ? 15 : 0;
                std::fill_n(glow.begin() + base, 4096, emission);
                any_glow |= emission != 0;
                direct.fill(0);
                continue;
            }
            if (!chunk.blocks && !chunk.fluids && !chunk.uniform_fluid.amount &&
                chunk.uniform == Block::air &&
                std::all_of(direct.begin(), direct.end(), [&](auto v) { return v == direct[0]; })) {
                std::fill_n(blocks.begin() + base, 4096, Block::air);
                std::fill_n(sky.begin() + base, 4096, direct[0]);
                std::fill_n(glow.begin() + base, 4096, 0);
                continue;
            }
            // X runs contiguously; 256 independent vertical skylight columns share a small state array.
            for (int y = 15; y >= 0; --y)
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x) {
                        const int stack = x + 16 * z, i = base + y * 256 + stack;
                        const Block b = chunk.at(x, y, z);
                        blocks[i] = b;
                        if (blocks_light(b))
                            direct[stack] = 0;
                        else if (attenuates_sky(b) && direct[stack])
                            --direct[stack];
                        sky[i] = direct[stack];
                        glow[i] = emits_light(b) ? 15 : 0;
                        any_glow |= glow[i] != 0;
                    }
        }
    }
    const auto neighbours = [](int i, auto visit) {
        const int x = i % 16, z = i / 16 % 16, y = i / 256;
        if (x > 0)
            visit(i - 1);
        if (x < 15)
            visit(i + 1);
        if (z > 0)
            visit(i - 16);
        if (z < 15)
            visit(i + 16);
        if (y > 0)
            visit(i - 256);
        if (y + 1 < world_height)
            visit(i + 256);
    };
    for (auto* channel : {&sky, &glow}) {
        if (channel == &glow && !any_glow)
            continue;
        auto& values = *channel;
        for (auto& bucket : buckets)
            bucket.clear();
        const auto enqueue = [&](int i) {
            buckets[values[i]].push_back(i);
            light_count(&LightWorkStats::pushes);
        };
        {
            profiling::Scope measure(profiling::Stage::light_seeds);
            for (int cy = 0; cy < chunks_per_column; ++cy) {
                if (stop.stop_requested())
                    return {};
                const int base = cy * 4096;
                const auto v = values[base];
                if (std::all_of(values.begin() + base, values.begin() + base + 4096,
                                [&](auto n) { return n == v; })) {
                    if (v <= 1)
                        continue;
                    // A uniform interior can only send light across its top/bottom within this column.
                    for (int side : {-1, 1}) {
                        if (cy + side < 0 || cy + side >= chunks_per_column)
                            continue;
                        const int row = base + (side < 0 ? 0 : 15 * 256);
                        for (int t = 0; t < 256; ++t) {
                            const int i = row + t, n = i + side * 256;
                            if (!blocks_light(blocks[n]) && values[n] + 1 < v)
                                enqueue(i);
                        }
                    }
                    continue;
                }
                for (int i = base; i < base + 4096; ++i) {
                    if (values[i] <= 1)
                        continue;
                    bool front = false;
                    neighbours(
                        i, [&](int n) { front |= !blocks_light(blocks[n]) && values[n] + 1 < values[i]; });
                    if (front)
                        enqueue(i);
                }
            }
        }
        profiling::Scope measure(profiling::Stage::light_flood);
        size_t iterations = 0;
        for (int level = 15; level > 1; --level)
            while (!buckets[level].empty()) {
                if ((++iterations & 4095) == 0 && stop.stop_requested())
                    return {};
                const int i = buckets[level].back();
                buckets[level].pop_back();
                light_count(&LightWorkStats::pops);
                if (values[i] != level) {
                    light_count(&LightWorkStats::stale);
                    continue;
                }
                const int next = level - 1;
                neighbours(i, [&](int n) {
                    if (!blocks_light(blocks[n]) && values[n] < next) {
                        values[n] = static_cast<uint8_t>(next);
                        light_count(&LightWorkStats::updates);
                        if (next > 1)
                            enqueue(n);
                    }
                });
            }
    }
    profiling::Scope measure(profiling::Stage::light_halo);
    std::array<std::optional<uint8_t>, chunks_per_column> uniform;
    std::array<std::optional<bool>, chunks_per_column> opaque;
    for (int cy = 0; cy < chunks_per_column; ++cy) {
        const int base = cy * 4096;
        const uint8_t value = sky[base] | (glow[base] << 4);
        const bool solid = blocks_light(blocks[base]);
        bool same = true, same_opaque = true;
        for (int i = base; i < base + 4096 && (same || same_opaque); ++i) {
            same &= (sky[i] | (glow[i] << 4)) == value;
            same_opaque &= blocks_light(blocks[i]) == solid;
        }
        if (same)
            uniform[cy] = value;
        if (same_opaque)
            opaque[cy] = solid;
    }
    auto result = std::make_shared<ColumnLight>();
    result->key = column.key;
    for (int cy = 0; cy < chunks_per_column; ++cy) {
        if (stop.stop_requested())
            return {};
        auto& out = result->chunks[cy];
        bool same = uniform[cy].has_value() && opaque[cy].has_value();
        for (int side : {-1, 1}) {
            const int n = cy + side;
            if (n < 0 || n >= chunks_per_column)
                same &= uniform[cy] == (n < 0 ? 0 : 15) && opaque[cy] == false;
            else
                same &= uniform[n] == uniform[cy] && opaque[n] == opaque[cy];
        }
        if (same) {
            out.uniform = *uniform[cy];
            out.uniform_opaque = *opaque[cy];
            out.has_block_light = (out.uniform & 0xf0) != 0;
            continue;
        }
        auto samples = std::make_shared<std::array<uint8_t, 18 * 18 * 18>>();
        auto occluders = std::make_shared<std::bitset<18 * 18 * 18>>();
        for (int z = -1; z <= 16; ++z)
            for (int y = -1; y <= 16; ++y)
                for (int x = -1; x <= 16; ++x) {
                    light_count(&LightWorkStats::halo_cells);
                    const int wy = cy * 16 + y, index = x + 1 + 18 * (y + 1 + 18 * (z + 1));
                    if (wy < 0 || wy >= world_height) {
                        (*samples)[index] = wy < 0 ? 0 : 15;
                        continue;
                    }
                    const int i = std::clamp(x, 0, 15) + 16 * std::clamp(z, 0, 15) + 256 * wy;
                    (*samples)[index] = sky[i] | (glow[i] << 4);
                    occluders->set(index, blocks_light(blocks[i]));
                }
        out.uniform = (*samples)[0];
        out.has_block_light =
            any_glow && std::any_of(samples->begin(), samples->end(), [](auto v) { return (v & 0xf0) != 0; });
        if (!std::all_of(samples->begin(), samples->end(), [&](auto v) { return v == out.uniform; }))
            out.values = std::move(samples);
        out.uniform_opaque = occluders->all();
        if (!out.uniform_opaque && !occluders->none())
            out.occluders = std::move(occluders);
    }
    return result;
}
std::shared_ptr<const ColumnLight> connect_column_light(ColumnKey key, std::array<LightInput, 9> columns,
                                                        std::stop_token stop) {
    std::vector<LightInput> inputs;
    inputs.reserve(columns.size());
    for (auto& column : columns)
        inputs.push_back(std::move(column));
    LightUpdate update(std::move(inputs), {}, {}, 0, stop);
    auto result = update.connect(key);
    if (!result)
        return {};
    if (result->columns.size() != 1 || result->columns.front()->key != key)
        throw std::logic_error("Missing connected column light.");
    return std::move(result->columns.front());
}
std::unique_ptr<LightResult> update_column_lights(uint64_t revision, std::vector<LightInput> inputs,
                                                  const WorldEdits& edits,
                                                  const std::vector<LightChange>& changes,
                                                  std::stop_token stop) {
    LightUpdate update(std::move(inputs), edits, changes, revision, stop);
    return update.run();
}
uint32_t face_light(PackedFace face, const LightChunk& light) {
    std::array<int, 3> p{int(face & 15), int((face >> 4) & 15), int((face >> 8) & 15)};
    const int axis = (face >> 12) & 3, sign = (face & (1u << 14)) ? 1 : -1;
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    p[axis] += sign;
    constexpr int corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    uint32_t packed = 0;
    for (int c = 0; c < 4; ++c) {
        auto side_u = p, side_v = p, diagonal = p;
        side_u[u] += corners[c][0];
        side_v[v] += corners[c][1];
        diagonal[u] += corners[c][0];
        diagonal[v] += corners[c][1];
        const auto open = [&](const std::array<int, 3>& q) { return !light.opaque_at(q[0], q[1], q[2]); };
        const bool open_u = open(side_u), open_v = open(side_v);
        int sky = 0, glow = 0, count = 0;
        const auto sample = [&](const std::array<int, 3>& q) {
            if (!open(q))
                return;
            const uint8_t value = light.at(q[0], q[1], q[2]);
            sky += value & 15;
            glow += value >> 4;
            ++count;
        };
        sample(p);
        if (open_u)
            sample(side_u);
        if (open_v)
            sample(side_v);
        // A diagonal needs an open side path. This is geometry, not an AO-value test.
        if (open_u || open_v)
            sample(diagonal);
        // A newly edited face can briefly precede its matching light snapshot.
        const uint8_t value =
            count ? static_cast<uint8_t>(((sky + count / 2) / count) | (((glow + count / 2) / count) << 4))
                  : light.at(p[0], p[1], p[2]);
        packed |= uint32_t(value) << (c * 8);
    }
    return packed;
}
MeshLightWorker::MeshLightWorker() : thread_([this](std::stop_token stop) { run(stop); }) {}
MeshLightWorker::~MeshLightWorker() {
    thread_.request_stop();
    wake_.notify_all();
    thread_.join();
}
void MeshLightWorker::submit(PackedMesh request, ChunkMesh source,
                             std::shared_ptr<const std::vector<uint32_t>> previous, bool urgent) {
    std::lock_guard lock(mutex_);
    std::erase_if(jobs_, [&](const Job& job) { return job.request.key == request.key; });
    Job job{std::move(request), std::move(source), std::move(previous), urgent};
    if (urgent)
        jobs_.insert(
            std::find_if(jobs_.begin(), jobs_.end(), [](const Job& pending) { return !pending.urgent; }),
            std::move(job));
    else
        jobs_.push_back(std::move(job));
    wake_.notify_one();
}
std::deque<PackedMesh> MeshLightWorker::take() {
    std::lock_guard lock(mutex_);
    if (failure_)
        std::rethrow_exception(failure_);
    std::deque<PackedMesh> result;
    result.swap(ready_);
    return result;
}
void MeshLightWorker::run(std::stop_token stop) {
    try {
        while (!stop.stop_requested()) {
            Job job;
            {
                std::unique_lock lock(mutex_);
                if (!wake_.wait(lock, stop, [&] { return !jobs_.empty(); }))
                    return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            auto& result = job.request;
            profiling::Scope measure(profiling::Stage::packing, result.key.x, result.key.z, result.key.y);
            if (!result.light_only) {
                auto geometry = std::make_shared<std::vector<PackedFace>>(std::move(job.source.faces));
                result.solid_count = static_cast<uint32_t>(geometry->size());
                result.ice_count = static_cast<uint32_t>(job.source.ice.size());
                geometry->insert(geometry->end(), job.source.ice.begin(), job.source.ice.end());
                geometry->insert(geometry->end(), job.source.water.begin(), job.source.water.end());
                result.geometry = std::move(geometry);
            }
            auto values = std::make_shared<std::vector<uint32_t>>();
            values->reserve(result.geometry->size());
            for (size_t i = 0; i < result.geometry->size(); ++i) {
                if ((i & 1023) == 0 && stop.stop_requested())
                    return;
                values->push_back(face_light((*result.geometry)[i], result.light));
            }
            result.unchanged = job.previous && *job.previous == *values;
            result.values = result.unchanged ? std::move(job.previous) : std::move(values);
            {
                std::lock_guard lock(mutex_);
                ready_.push_back(std::move(result));
            }
        }
    } catch (...) {
        std::lock_guard lock(mutex_);
        failure_ = std::current_exception();
    }
}
LightWorker::LightWorker() : thread_([this](std::stop_token stop) { run(stop); }) {}
LightWorker::~LightWorker() {
    thread_.request_stop();
    wake_.notify_all();
    thread_.join();
}
void LightWorker::submit(ColumnKey key, uint64_t revision, std::array<Column, 9> columns, WorldEdits edits,
                         std::array<std::shared_ptr<const ColumnLight>, 9> known) {
    std::lock_guard lock(mutex_);
    Job job{};
    job.key = key;
    job.known = std::move(known);
    job.revision = revision;
    job.columns = std::move(columns);
    job.edits = std::move(edits);
    pending_ = std::move(job);
    wake_.notify_one();
}
void LightWorker::submit_update(uint64_t revision, std::vector<LightInput> inputs, WorldEdits edits,
                                std::vector<LightChange> changes, std::vector<LightRebuild> rebuilds) {
    Job job{};
    job.revision = revision;
    job.inputs = std::move(inputs);
    job.edits = std::move(edits);
    job.changes = std::move(changes);
    job.rebuilds = std::move(rebuilds);
    job.update = true;
    std::lock_guard lock(mutex_);
    pending_ = std::move(job);
    wake_.notify_one();
}
std::unique_ptr<LightResult> LightWorker::take() {
    std::lock_guard lock(mutex_);
    if (failure_)
        std::rethrow_exception(failure_);
    return std::move(ready_);
}
void LightWorker::run(std::stop_token stop) {
    try {
        while (!stop.stop_requested()) {
            std::optional<Job> job;
            {
                std::unique_lock lock(mutex_);
                if (!wake_.wait(lock, stop, [&] { return pending_.has_value(); }))
                    return;
                job = std::move(pending_);
                pending_.reset();
            }
            std::unique_ptr<LightResult> result;
            if (!job->update) {
                result = std::make_unique<LightResult>();
                if (auto column = calculate(job->key, job->revision, std::move(job->columns), job->edits,
                                            stop, job->known))
                    result->columns.push_back(std::move(column));
            } else if (!job->rebuilds.empty()) {
                // Loading boundaries may not yet have all nine baseline light columns.
                // Rebuild affected visible outputs as one transaction in that case only.
                result = std::make_unique<LightResult>();
                for (auto& rebuild : job->rebuilds) {
                    if (stop.stop_requested())
                        return;
                    if (auto column = calculate(rebuild.key, job->revision, std::move(rebuild.columns),
                                                rebuild.edits, stop)) {
                        bool changed = false;
                        for (int y = 0; y < chunks_per_column; ++y) {
                            auto& next = column->chunks[y];
                            const auto& previous = rebuild.previous->chunks[y];
                            const bool same_occluders = next.uniform_opaque == previous.uniform_opaque &&
                                                        ((!next.occluders && !previous.occluders) ||
                                                         (next.occluders && previous.occluders &&
                                                          *next.occluders == *previous.occluders));
                            const bool equal =
                                same_occluders && next.uniform == previous.uniform &&
                                ((!next.values && !previous.values) ||
                                 (next.values && previous.values && *next.values == *previous.values));
                            if (equal)
                                next = previous;
                            else
                                changed = true;
                        }
                        if (changed)
                            result->columns.push_back(std::move(column));
                    }
                }
            } else {
                result = update_column_lights(job->revision, std::move(job->inputs), job->edits, job->changes,
                                              stop);
            }
            if (stop.stop_requested())
                return;
            {
                std::lock_guard lock(mutex_);
                ready_ = std::move(result);
            }
        }
    } catch (...) {
        std::lock_guard lock(mutex_);
        failure_ = std::current_exception();
    }
}
} // namespace sandbox
