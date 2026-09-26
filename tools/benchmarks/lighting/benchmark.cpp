// Explicitly requested before/after comparison, never registered with CTest.
#include "reference.hpp"
#include "world/light_diagnostics.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

using namespace sandbox;
using Clock = std::chrono::steady_clock;
using Region = std::array<Column, 9>;
using Inputs = std::array<LightInput, 9>;
uint64_t comparisons = 0, checksum = 0;

Inputs locals(const Region& region, bool reference) {
    Inputs out;
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = {region[i], reference ? reference_local_column_light(region[i], {})
                                       : local_column_light(region[i], {})};
    return out;
}
Inputs connected(const Region& region, bool reference) {
    auto in = locals(region, reference), out = in;
    for (size_t i = 0; i < out.size(); ++i)
        out[i].light = reference ? reference_connect_column_light(region[i].key, in, {})
                                 : connect_column_light(region[i].key, in, {});
    return out;
}
void compare(const ColumnLight& a, const ColumnLight& b, const std::string& label) {
    if (a.key != b.key || a.revision != b.revision)
        throw std::runtime_error(label + ": key/revision mismatch");
    for (int cy = 0; cy < chunks_per_column; ++cy)
        for (int z = -1; z <= 16; ++z)
            for (int y = -1; y <= 16; ++y)
                for (int x = -1; x <= 16; ++x) {
                    ++comparisons;
                    const auto av = a.chunks[cy].at(x, y, z), bv = b.chunks[cy].at(x, y, z);
                    if (av != bv || a.chunks[cy].opaque_at(x, y, z) != b.chunks[cy].opaque_at(x, y, z))
                        throw std::runtime_error(label + ": mismatch column=" + std::to_string(a.key.x) +
                                                 "," + std::to_string(a.key.z) +
                                                 " local=" + std::to_string(x) + "," +
                                                 std::to_string(cy * 16 + y) + "," + std::to_string(z) +
                                                 " values=" + std::to_string(av) + "/" + std::to_string(bv));
                    if (!b.chunks[cy].has_block_light && (bv & 0xf0))
                        throw std::runtime_error(label + ": invalid empty block-light summary");
                }
}
void compare(const Inputs& a, const Inputs& b, const std::string& label) {
    for (size_t i = 0; i < a.size(); ++i)
        compare(*a[i].light, *b[i].light, label);
}
Region generated(ColumnKey centre, const TerrainGenerator& generator) {
    Region out;
    for (int z = -1; z <= 1; ++z)
        for (int x = -1; x <= 1; ++x) {
            auto& c = out[x + 1 + 3 * (z + 1)];
            c.key = canonical(ColumnKey{centre.x + x, centre.z + z});
            const auto tile = generate_tile(c.key, generator);
            for (int y = 0; y < chunks_per_column; ++y)
                c.chunks[y] = generate_chunk({c.key.x, y, c.key.z}, generator, &tile);
        }
    return out;
}
Region chamber(bool opening = false, bool air_only = false) {
    Region out;
    for (int z = -1; z <= 1; ++z)
        for (int x = -1; x <= 1; ++x) {
            auto& c = out[x + 1 + 3 * (z + 1)];
            c.key = canonical(ColumnKey{x, z});
            for (int cy = 0; cy < chunks_per_column; ++cy)
                c.chunks[cy].uniform = !air_only && cy < 6 && cy != 4 ? Block::rock : Block::air;
        }
    if (opening)
        for (int y = 80; y < 96; ++y)
            out[4].chunks[y / 16].set(8, y % 16, 8, Block::air);
    return out;
}
size_t find(const Region& region, BlockPos p) {
    const auto key = canonical(ColumnKey{chunk_coordinate(p.x), chunk_coordinate(p.z)});
    for (size_t i = 0; i < region.size(); ++i)
        if (region[i].key == key)
            return i;
    throw std::runtime_error("Missing fixture column");
}
void set(Region& region, BlockPos p, Block b) {
    auto& c = region[find(region, p)];
    c.chunks[p.y / 16].set(local_coordinate(p.x), p.y % 16, local_coordinate(p.z), b);
}
Block at(const Region& region, BlockPos p) {
    return region[find(region, p)].at(local_coordinate(p.x), p.y, local_coordinate(p.z));
}

struct EditCase {
    std::string name;
    Region region;
    std::vector<std::pair<BlockPos, Block>> changes;
};
struct Prepared {
    WorldEdits edits;
    std::vector<LightChange> changes;
    Prepared(const Region& region, const std::vector<std::pair<BlockPos, Block>>& operations) {
        for (auto [p, b] : operations) {
            const auto base = at(region, p), before = edits.override_block(p, base);
            if (before == b)
                continue;
            if (!edits.set(p, b, base))
                throw std::runtime_error("Fixture edit rejected by game rules");
            changes.push_back({p, before, b});
        }
    }
};
std::unique_ptr<LightResult> update(const Inputs& in, const Prepared& edit, bool reference,
                                    uint64_t revision = 1) {
    std::vector<LightInput> inputs(in.begin(), in.end());
    return reference
               ? reference_update_column_lights(revision, std::move(inputs), edit.edits, edit.changes, {})
               : update_column_lights(revision, std::move(inputs), edit.edits, edit.changes, {});
}
Inputs merge(Inputs in, const LightResult& result, const Prepared& edit) {
    for (auto& input : in) {
        edit.edits.apply(input.blocks);
        for (const auto& c : result.columns)
            if (c->key == input.blocks.key)
                input.light = c;
    }
    return in;
}
void counters(std::ofstream& out, const std::string& name, bool ref, const LightWorkStats& s) {
    out << name << ',' << ref << ',' << s.pushes << ',' << s.pops << ',' << s.stale << ',' << s.updates << ','
        << s.boundary_cells << ',' << s.halo_cells << '\n';
}
template <class F> void measure(std::ofstream& out, const std::string& name, F invoke) {
    for (int warm = 0; warm < 3; ++warm)
        for (bool ref : {false, true})
            invoke(ref);
    for (int run = 0; run < 31; ++run)
        for (int mode = 0; mode < 2; ++mode) {
            const bool ref = (run + mode) % 2 == 0;
            const auto start = Clock::now();
            invoke(ref);
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            out << name << ',' << ref << ',' << run << ',' << ms << '\n';
        }
}
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: lighting_benchmark worldgen.json output_directory");
        const std::filesystem::path output = argv[2];
        std::filesystem::create_directories(output);
        TerrainGenerator generator(load_generation_config(argv[1]));
        std::ofstream times(output / "timings.csv"), work(output / "work.csv");
        times << "case,reference,run,ms\n";
        work << "case,reference,pushes,pops,stale,updates,boundary_cells,halo_cells\n";
        std::vector<std::pair<std::string, Region>> regions;
        regions.emplace_back("initial_ocean", generated({7040, 5504}, generator));
        regions.emplace_back("initial_land", generated({3456, 1152}, generator));
        regions.emplace_back("initial_relief", generated({8064, 3456}, generator));
        regions.emplace_back("uniform_air", chamber(false, true));
        auto solid = chamber();
        for (auto& c : solid)
            for (auto& chunk : c.chunks)
                chunk.uniform = Block::rock;
        regions.emplace_back("uniform_solid", std::move(solid));
        auto water = chamber(false, true);
        for (auto& c : water)
            for (int y = 0; y < 12; ++y)
                c.chunks[y].uniform = Block::water;
        regions.emplace_back("uniform_water", std::move(water));
        for (const auto& [name, region] : regions) {
            auto old = locals(region, true), now = locals(region, false);
            compare(old, now, name + " local");
            const auto old_connected = reference_connect_column_light(region[4].key, old, {}),
                       now_connected = connect_column_light(region[4].key, now, {});
            compare(*old_connected, *now_connected, name + " connected");
            auto invoke = [&](bool ref) {
                auto in = locals(region, ref);
                auto c = ref ? reference_connect_column_light(region[4].key, std::move(in), {})
                             : connect_column_light(region[4].key, std::move(in), {});
                checksum += c->chunks[4].at(8, 8, 8);
            };
            for (bool ref : {true, false}) {
                LightWorkStats s;
                light_work_stats = &s;
                invoke(ref);
                light_work_stats = nullptr;
                counters(work, name, ref, s);
            }
            measure(times, name, invoke);
            std::cout << name << " exact-match\n" << std::flush;
        }
        std::vector<EditCase> cases;
        cases.push_back({"glow_add", chamber(), {{{8, 70, 8}, Block::glow}}});
        auto lit = chamber();
        set(lit, {8, 70, 8}, Block::glow);
        cases.push_back({"glow_remove", lit, {{{8, 70, 8}, Block::air}}});
        auto overlap = lit;
        set(overlap, {14, 70, 8}, Block::glow);
        cases.push_back({"overlap_remove", overlap, {{{8, 70, 8}, Block::air}}});
        cases.push_back(
            {"boundary_add", chamber(), {{{15, 70, 15}, Block::glow}, {{0, 70, 0}, Block::glow}}});
        auto boundary = chamber();
        set(boundary, {15, 70, 15}, Block::glow);
        set(boundary, {0, 70, 0}, Block::glow);
        cases.push_back(
            {"boundary_remove", boundary, {{{15, 70, 15}, Block::air}, {{0, 70, 0}, Block::air}}});
        EditCase open{"cave_open", chamber(), {}};
        for (int y = 80; y < 96; ++y)
            open.changes.push_back({{8, y, 8}, Block::air});
        cases.push_back(std::move(open));
        cases.push_back({"cave_close", chamber(true), {{{8, 80, 8}, Block::rock}}});
        auto flooded = chamber(true);
        for (int y = 84; y <= 86; ++y)
            set(flooded, {8, y, 8}, Block::water);
        cases.push_back({"water_remove",
                         flooded,
                         {{{8, 84, 8}, Block::air}, {{8, 85, 8}, Block::air}, {{8, 86, 8}, Block::air}}});
        cases.push_back(
            {"world_y_limits", chamber(false, true), {{{8, 0, 8}, Block::glow}, {{8, 511, 8}, Block::glow}}});
        for (const auto& c : cases) {
            auto before_old = connected(c.region, true), before_new = connected(c.region, false);
            compare(before_old, before_new, c.name + " baseline");
            Prepared edit(c.region, c.changes);
            auto old = update(before_old, edit, true), now = update(before_new, edit, false);
            if (!old || !now)
                throw std::runtime_error("Unexpected cancellation");
            if (old->columns.empty() || now->columns.empty())
                throw std::runtime_error(c.name + ": fixture produced no light/occlusion change");
            compare(merge(before_old, *old, edit), merge(before_new, *now, edit), c.name);
            auto invoke = [&](bool ref) {
                auto r = update(ref ? before_old : before_new, edit, ref);
                checksum += r->columns.size();
            };
            for (bool ref : {true, false}) {
                LightWorkStats s;
                light_work_stats = &s;
                invoke(ref);
                light_work_stats = nullptr;
                counters(work, c.name, ref, s);
            }
            measure(times, c.name, invoke);
            std::cout << c.name << " exact-match\n" << std::flush;
        }
        {
            const auto region = chamber();
            const auto old = connected(region, true), now = connected(region, false);
            Prepared edit(region, {{{15, 70, 8}, Block::glow}});
            auto a = reference_update_column_lights(1, {old[4], old[5]}, edit.edits, edit.changes, {});
            auto b = update_column_lights(1, {now[4], now[5]}, edit.edits, edit.changes, {});
            compare(merge(old, *a, edit), merge(now, *b, edit), "sparse neighbour region");
        }
        // Repeated mixed edits exercise immutable snapshots, removals, overlapping sources, and X/Z wrap.
        auto region = chamber(true);
        auto old = connected(region, true), now = connected(region, false);
        std::mt19937 random(1337);
        for (int batch = 0; batch < 48; ++batch) {
            std::vector<std::pair<BlockPos, Block>> operations;
            for (int n = 0; n < 12; ++n) {
                const int x = int(random() % 32) - 8, z = int(random() % 32) - 8, y = 64 + int(random() % 32);
                constexpr Block choices[]{Block::air, Block::rock, Block::glow};
                operations.push_back({{x, y, z}, choices[random() % 3]});
            }
            Prepared edit(region, operations);
            auto a = update(old, edit, true, batch + 1), b = update(now, edit, false, batch + 1);
            old = merge(std::move(old), *a, edit);
            now = merge(std::move(now), *b, edit);
            compare(old, now, "mixed batch " + std::to_string(batch));
            for (size_t i = 0; i < region.size(); ++i)
                region[i] = old[i].blocks;
        }
        std::ofstream report(output / "verification.txt");
        report << "exact compared halo cells=" << comparisons
               << "\nchannels=sky,block,occlusion\nrandom batches=48\nchecksum=" << checksum << "\n";
        std::cout << "Compared " << comparisons << " cells; all exact. checksum=" << checksum << '\n';
        return 0;
    } catch (const std::exception& e) {
        light_work_stats = nullptr;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
