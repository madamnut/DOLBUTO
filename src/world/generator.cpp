#include "world/generator.hpp"
#include "core/random_seed.hpp"
#include "core/world_rules.hpp"
#include "world/periodic_noise.hpp"
#include "world/profiling.hpp"
#include <FastNoise/FastNoise.h>
#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace sandbox {
struct TerrainGenerator::Nodes {
    std::array<std::unique_ptr<PeriodicNoise>, 2> signals;
    std::unique_ptr<PeriodicNoise> shift;
};
TerrainGenerator::TerrainGenerator(GenerationConfig config)
    : config_(std::move(config)), nodes_(std::make_unique<Nodes>()) {
    validate(config_);
    signature_ = 14695981039346656037ULL;
    for (unsigned char byte : generation_json(config_)) {
        signature_ ^= byte;
        signature_ *= 1099511628211ULL;
    }
    nodes_->shift = std::make_unique<PeriodicNoise>(
        config_.warps[0].noise, derive_seed(config_.seed, SeedDomain::climate_shift), false, true, true);
    const std::array settings{&config_.temperature, &config_.precipitation};
    for (size_t i = 0; i < settings.size(); ++i)
        nodes_->signals[i] = std::make_unique<PeriodicNoise>(
            *settings[i],
            derive_seed(config_.seed, i == 0 ? SeedDomain::temperature : SeedDomain::precipitation), true,
            false, i != 0);
}
TerrainGenerator::~TerrainGenerator() = default;
void TerrainGenerator::sample_signals(std::span<const int> kinds, std::span<std::span<float>> outputs,
                                      std::span<const float> x, std::span<const float> z) const {
    if (x.size() != z.size() || kinds.size() != outputs.size())
        throw std::invalid_argument("Mismatched signal dimensions.");
    for (auto output : outputs)
        if (output.size() != x.size())
            throw std::invalid_argument("Mismatched signal output dimensions.");
    if (x.empty())
        return;
    struct Scratch {
        std::vector<double> x, z, zero, dx, dz;
        std::vector<float> sx, sz;
    };
    thread_local Scratch s;
    for (auto* v : {&s.x, &s.z, &s.zero, &s.dx, &s.dz})
        v->resize(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        s.x[i] = wrap_position(x[i]);
        s.z[i] = wrap_position(z[i]);
        s.zero[i] = 0;
    }
    profiling::Scope all(profiling::Stage::noise2d, 0, 0, -1, x.size() * kinds.size());
    const auto& w = config_.warps[0];
    const std::array settings{&config_.temperature, &config_.precipitation};
    bool shifted = false;
    for (int kind : kinds)
        shifted |= !settings[kind]->warp.empty();
    shifted = shifted && w.enabled && w.strength > 0;
    if (shifted) {
        profiling::Scope warp(profiling::Stage::warp, 0, 0, -1, x.size() * 2);
        s.sx.resize(x.size());
        s.sz.resize(x.size());
        nodes_->shift->sample(s.sx, s.x, s.zero, s.z);
        nodes_->shift->sample(s.sz, s.z, s.x, s.zero);
        for (size_t i = 0; i < x.size(); ++i) {
            s.dx[i] = s.x[i] + double(s.sx[i]) * w.strength;
            s.dz[i] = s.z[i] + double(s.sz[i]) * w.strength;
        }
    }
    for (size_t i = 0; i < kinds.size(); ++i) {
        const bool apply = shifted && !settings[kinds[i]]->warp.empty();
        nodes_->signals[kinds[i]]->sample(outputs[i], apply ? s.dx : s.x, {}, apply ? s.dz : s.z);
    }
}
void TerrainGenerator::map(GenerationMap kind, std::span<float> out, std::span<const float> x,
                           std::span<const float> z) const {
    if (out.size() != x.size() || out.size() != z.size() || out.size() > INT_MAX)
        throw std::invalid_argument("Mismatched map batch dimensions.");
    if (out.empty())
        return;
    if (kind != GenerationMap::temperature && kind != GenerationMap::precipitation)
        throw std::invalid_argument("Unknown climate map.");
    const int signal = kind == GenerationMap::temperature ? 0 : 1;
    const std::array kinds{signal};
    std::array outputs{out};
    sample_signals(kinds, outputs, x, z);
    if (kind == GenerationMap::temperature) {
        const auto& b = config_.temperature_bands;
        for (size_t i = 0; i < out.size(); ++i) {
            const float latitude =
                float(0.5 - 0.5 * std::cos(2 * std::numbers::pi * wrap_position(z[i]) / world_size));
            const float belt = std::pow(latitude, b.latitude_power);
            const float warmth = std::clamp(belt + out[i] * b.variation * belt * (1 - belt), 0.0f, 1.0f);
            out[i] = std::lerp(b.poles, b.equator, warmth);
        }
    }
}
} // namespace sandbox
