// Explicit diagnostics for the approved 4D transition. Never registered with CTest.
#include "world/generator.hpp"
#include "world/periodic_perlin.hpp"
#include "world/terrain.hpp"
#include <FastNoise/FastNoise.h>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <thread>

using namespace sandbox;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
float sample(const TerrainGenerator& g, GenerationMap map, float x, float z) {
    float value;
    g.map(map, std::span(&value, 1), std::span(&x, 1), std::span(&z, 1));
    require(std::isfinite(value), "Nonfinite sample");
    return value;
}
volatile float observable{};
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: torus_benchmark worldgen.json results.csv");
        auto config = load_generation_config(argv[1]);
        TerrainGenerator generator(config);
        require(parse_generation_json(generation_json(config)) == config, "Config roundtrip mismatch");
        // Include unused-by-default assignments, shared fields, disabled fields and zero weights.
        auto all_warped = config;
        require(!all_warped.warps.empty(), "Diagnostic needs an initial warp");
        for (auto* n : {&all_warped.groundness, &all_warped.smoothness, &all_warped.weirdness,
                        &all_warped.temperature, &all_warped.precipitation, &all_warped.jagged})
            n->warp = all_warped.warps.front().id;
        TerrainGenerator warped(all_warped);
        size_t pairs = 0;
        float seam_difference = 0;
        for (const auto* g : {&generator, &warped})
            for (int m = 0; m < 12; ++m)
                for (int i = 0; i < 40; ++i) {
                    const float x = float(i * 3001) + .25f, z = float(i * 1987) + .5f;
                    const auto map = static_cast<GenerationMap>(m);
                    const float v = sample(*g, map, x, z);
                    require(v == sample(*g, map, x + world_size, z), "X period mismatch");
                    require(v == sample(*g, map, x, z + world_size), "World Z wrap mismatch");
                    require(sample(*g, map, -.25f, z) == sample(*g, map, world_size - .25f, z),
                            "Negative wrap mismatch");
                    // Neighbours across the seam must be nearby, not merely equal at endpoints.
                    if (m == 0 || m == 1 || m == 2 || m == 6 || m == 10) {
                        const float d = std::abs(sample(*g, map, .015625f, z) -
                                                 sample(*g, map, world_size - .015625f, z));
                        seam_difference = std::max(seam_difference, d);
                        require(d < .08f, "Discontinuous X seam");
                        require(std::abs(sample(*g, map, x, .015625f) -
                                         sample(*g, map, x, world_size - .015625f)) < .08f,
                                "Discontinuous Z seam");
                    }
                    ++pairs;
                }
        for (float x : {0.f, 1234.f, 65536.f, 131071.f}) {
            require(sample(warped, GenerationMap::temperature, x, 0) == config.temperature_bands.poles,
                    "Polar band moved");
            require(sample(warped, GenerationMap::temperature, x, 65536) == config.temperature_bands.equator,
                    "Equator moved");
        }
        constexpr int count = 256;
        std::array<float, count> xs{}, zs{}, ys{}, g{}, e{}, w{}, expected{}, shape{};
        for (int i = 0; i < count; ++i) {
            xs[i] = 65520.5f + i % 16;
            zs[i] = 131056.5f + i / 16;
            ys[i] = 192.5f + i % 32;
        }
        generator.terrain_inputs(g, e, w, xs, zs);
        const std::array maps{GenerationMap::groundness, GenerationMap::smoothness, GenerationMap::weirdness};
        const std::array outputs{g, e, w};
        for (int m = 0; m < 3; ++m) {
            generator.map(maps[m], expected, xs, zs);
            require(expected == outputs[m], "Batch and standalone map mismatch");
        }
        // Direct comparison against the retained Perlin implementation, same settings/seed/input.
        auto original_shape = FastNoise::New<FastNoise::PeriodicPerlin>();
        original_shape->Configure(config.shape, config.vertical_scale);
        if (generator.shape_active())
            original_shape->GenPositionArray3D(expected.data(), count, xs.data(), ys.data(), zs.data(), 0, 0,
                                               0, std::bit_cast<int32_t>(config.seed));
        else
            expected.fill(0);
        generator.shape(shape, xs, ys, zs);
        require(shape == expected, "3D shape changed");
        std::array<std::thread, 4> threads;
        std::array<bool, 4> same{};
        for (int i = 0; i < 4; ++i)
            threads[i] = std::thread([&, i] {
                std::array<float, count> a{}, b{}, c{};
                generator.terrain_inputs(a, b, c, xs, zs);
                same[i] = a == g && b == e && c == w;
            });
        for (auto& t : threads)
            t.join();
        require(std::all_of(same.begin(), same.end(), [](bool v) { return v; }), "Worker sampling mismatch");
        auto off = config;
        disable_warps(off);
        auto none = off;
        for (auto* n : {&none.groundness, &none.smoothness, &none.weirdness, &none.temperature,
                        &none.precipitation, &none.jagged})
            n->warp.clear();
        TerrainGenerator disabled(off), unassigned(none);
        for (int m = 0; m < 12; ++m)
            require(sample(disabled, static_cast<GenerationMap>(m), 187.5f, 123.5f) ==
                        sample(unassigned, static_cast<GenerationMap>(m), 187.5f, 123.5f),
                    "Disabled warp differs");
        auto zero = config;
        zero.groundness.weights.assign(zero.groundness.octaves, 0);
        TerrainGenerator zero_generator(zero);
        require(sample(zero_generator, GenerationMap::groundness, 100, 100) == 0,
                "Zero weights are not zero");
        auto removed = config;
        const auto removed_id = removed.warps.front().id;
        remove_warp(removed, removed_id);
        validate(removed);
        auto invalid = config;
        invalid.groundness.warp = "does-not-exist";
        bool rejected = false;
        try {
            validate(invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Dangling warp accepted");
        // Independent chunk generation must agree with the shared column tile, including wrapped columns.
        size_t blocks = 0;
        for (ColumnKey key : {ColumnKey{0, 0}, ColumnKey{8191, 8191}}) {
            const auto tile = generate_tile(key, generator);
            for (int cy : {0, 11, 12, 16, 24, 31}) {
                const auto shared = generate_chunk({key.x, cy, key.z}, generator, &tile);
                const auto isolated = generate_chunk({key.x, cy, key.z}, generator);
                for (int z = 0; z < 16; ++z)
                    for (int y = 0; y < 16; ++y)
                        for (int x = 0; x < 16; ++x) {
                            require(shared.at(x, y, z) == isolated.at(x, y, z), "Independent chunk differs");
                            ++blocks;
                        }
            }
        }
        std::ofstream csv(argv[2]);
        csv << "run,kind,ns_per_sample\n";
        auto perlin = FastNoise::New<FastNoise::PeriodicPerlin>();
        perlin->Configure(config.groundness, 1);
        const auto begin = std::chrono::steady_clock::now;
        constexpr int iterations = 2000;
        for (int run = 0; run < 11; ++run)
            for (int slot = 0; slot < 3; ++slot) {
                const int kind = (slot + run) % 3;
                const auto start = begin();
                for (int i = 0; i < iterations; ++i) {
                    if (kind == 0)
                        perlin->GenPositionArray2D(g.data(), count, xs.data(), zs.data(), 0, 0,
                                                   std::bit_cast<int32_t>(config.seed));
                    else
                        (kind == 1 ? disabled : generator).groundness(g, xs, zs);
                    observable = g[i % count];
                }
                const double ns =
                    std::chrono::duration<double, std::nano>(begin() - start).count() / (iterations * count);
                csv << run << ','
                    << (kind == 0   ? "perlin2d_no_warp"
                        : kind == 1 ? "simplex4d_no_warp"
                                    : "simplex4d_with_warp")
                    << ',' << ns << '\n';
            }
        csv.close();
        require(bool(csv), "Cannot save timings");
        std::cout << "period_pairs=" << pairs << " max_x_seam_neighbour_difference=" << seam_difference
                  << " independent_blocks=" << blocks << " shape_samples_equal=" << count
                  << " worker_batches_equal=4 roundtrip=ok invalid_reference=rejected\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
