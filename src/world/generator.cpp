#include "world/generator.hpp"
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
    std::array<std::unique_ptr<PeriodicNoise>, 6> signals;
    std::unique_ptr<PeriodicNoise> shift;
    std::array<std::unique_ptr<PeriodicNoise>, 3> shape;
};
TerrainGenerator::TerrainGenerator(GenerationConfig config)
    : config_(std::move(config)), nodes_(std::make_unique<Nodes>()) {
    validate(config_);
    config_.groundness.spacings = octave_spacings(config_.groundness);
    config_.groundness.weights = octave_weights(config_.groundness);
    signature_ = 14695981039346656037ULL;
    for (unsigned char byte : generation_json(config_)) {
        signature_ ^= byte;
        signature_ *= 1099511628211ULL;
    }
    if (shape_active())
        for (int i = 0; i < 3; ++i)
            nodes_->shape[i] = std::make_unique<PeriodicNoise>(config_, i);
    nodes_->shift = std::make_unique<PeriodicNoise>(config_.warps[0].noise, config_.seed, false, true, true);
    const std::array settings{&config_.groundness,  &config_.smoothness,    &config_.weirdness,
                              &config_.temperature, &config_.precipitation, &config_.jagged};
    for (size_t i = 0; i < settings.size(); ++i)
        nodes_->signals[i] = std::make_unique<PeriodicNoise>(*settings[i], config_.seed, true, false, i != 3);
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
    const std::array settings{&config_.groundness,  &config_.smoothness,    &config_.weirdness,
                              &config_.temperature, &config_.precipitation, &config_.jagged};
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
void TerrainGenerator::terrain_inputs(std::span<float> ground, std::span<float> smooth,
                                      std::span<float> weird, std::span<const float> x,
                                      std::span<const float> z) const {
    const std::array kinds{0, 1, 2};
    std::array outputs{ground, smooth, weird};
    sample_signals(kinds, outputs, x, z);
}
void TerrainGenerator::groundness(std::span<float> out, std::span<const float> x,
                                  std::span<const float> z) const {
    const std::array kinds{0};
    std::array outputs{out};
    sample_signals(kinds, outputs, x, z);
}
void TerrainGenerator::profile(std::span<float> heights, std::span<float> squash, std::span<const float> x,
                               std::span<const float> z) const {
    if (heights.size() != squash.size())
        throw std::invalid_argument("Mismatched terrain profile dimensions.");
    spline_profile(heights, squash, x, z, true);
}
void TerrainGenerator::spline_profile(std::span<float> heights, std::span<float> squash,
                                      std::span<const float> x, std::span<const float> z, bool jagged) const {
    thread_local std::vector<float> smooth, weird, detail;
    smooth.resize(heights.size());
    weird.resize(heights.size());
    detail.resize(heights.size());
    terrain_inputs(heights, smooth, weird, x, z);
    const auto& s = config_.splines;
    jagged = jagged && s.jagged_enabled && s.jagged_scale > 0;
    if (jagged)
        map(GenerationMap::jagged_noise, detail, x, z);
    profiling::Scope spline_measure(profiling::Stage::spline);
    for (size_t i = 0; i < heights.size(); ++i) {
        const float g = heights[i], e = smooth[i], w = weird[i];
        float h = sea_level + s.height_scale * (0.0040625f + s.offset.evaluate(g, e, w));
        squash[i] = config_.squash * std::clamp(s.factor.evaluate(g, e, w), 0.01f, 64.0f) / s.height_scale;
        if (jagged) {
            const float strength = std::clamp(s.jaggedness.evaluate(g, e, w), 0.0f, 64.0f);
            h += s.jagged_scale * strength * (detail[i] < 0 ? 0.5f * detail[i] : detail[i]);
        }
        heights[i] = h;
    }
}
float TerrainGenerator::density(float noise, float height, float squash, float y) const {
    float gradient = squash * (height - y);
    // Vanilla's 4 * quarter_negative: strengthen solid-side density, retain air-side slope.
    if (gradient > 0)
        gradient *= 4;
    float value = config_.amplitude * noise + gradient;
    // Vanilla overworld top/bottom slides, stretched from 384 to 512 blocks.
    const float top = std::clamp((world_height - 64.0f * 4 / 3 - y) / (16.0f * 4 / 3), 0.0f, 1.0f);
    value = std::lerp(-.078125f, value, top);
    value = std::lerp(.1171875f, value, std::clamp(y / 32.0f, 0.0f, 1.0f));
    // Post-interpolation squeeze is monotone and preserves zero; block classification needs only sign.
    return value;
}
void TerrainGenerator::map(GenerationMap kind, std::span<float> out, std::span<const float> x,
                           std::span<const float> z) const {
    if (out.size() != x.size() || out.size() != z.size() || out.size() > INT_MAX)
        throw std::invalid_argument("Mismatched map batch dimensions.");
    if (out.empty())
        return;
    if (kind == GenerationMap::groundness) {
        groundness(out, x, z);
        return;
    }
    if (kind == GenerationMap::base_height || kind == GenerationMap::effective_height ||
        kind == GenerationMap::factor) {
        std::vector<float> squash(out.size());
        spline_profile(out, squash, x, z, kind == GenerationMap::effective_height);
        if (kind == GenerationMap::factor)
            std::copy(squash.begin(), squash.end(), out.begin());
        return;
    }
    if (kind == GenerationMap::offset || kind == GenerationMap::jaggedness) {
        std::vector<float> smooth(out.size()), weird(out.size());
        terrain_inputs(out, smooth, weird, x, z);
        const auto& grid =
            kind == GenerationMap::offset ? config_.splines.offset : config_.splines.jaggedness;
        for (size_t i = 0; i < out.size(); ++i) {
            out[i] = grid.evaluate(out[i], smooth[i], weird[i]);
            if (kind == GenerationMap::jaggedness)
                out[i] = std::clamp(out[i], 0.0f, 64.0f);
        }
        return;
    }
    const int signal = kind == GenerationMap::smoothness                                 ? 1
                       : (kind == GenerationMap::weirdness || kind == GenerationMap::pv) ? 2
                       : kind == GenerationMap::jagged_noise                             ? 5
                       : kind == GenerationMap::precipitation                            ? 4
                                                                                         : 3;
    const std::array kinds{signal};
    std::array outputs{out};
    sample_signals(kinds, outputs, x, z);
    if (kind == GenerationMap::pv)
        for (float& value : out)
            value = spline_pv(value);
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
void TerrainGenerator::shape(std::span<float> out, std::span<const float> x, std::span<const float> y,
                             std::span<const float> z) const {
    if (out.size() != x.size() || out.size() != y.size() || out.size() != z.size() || out.size() > INT_MAX)
        throw std::invalid_argument("Mismatched density batch dimensions.");
    if (out.empty())
        return;
    if (!shape_active()) {
        std::fill(out.begin(), out.end(), 0.0f);
        return;
    }
    profiling::Scope measure(profiling::within(profiling::Stage::surface_search)
                                 ? profiling::Stage::noise3d_surface
                                 : profiling::Stage::noise3d,
                             0, 0, -1, x.size());
    struct Scratch {
        std::vector<double> x, y, z, cx, cy, cz;
        std::vector<float> control, lo, hi, values;
        std::vector<size_t> indices;
    };
    thread_local Scratch s;
    s.x.resize(x.size());
    s.y.resize(x.size());
    s.z.resize(x.size());
    s.control.resize(x.size());
    s.lo.resize(x.size());
    s.hi.resize(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        s.x[i] = wrap_position(x[i]);
        s.z[i] = wrap_position(z[i]);
        s.y[i] = (double(y[i]) - sea_level) * .75 + 63;
    }
    nodes_->shape[0]->sample(s.control, s.x, s.y, s.z);
    // Like the original, evaluate only the contributing limit branch for each sample.
    for (int family = 1; family <= 2; ++family) {
        s.cx.clear();
        s.cy.clear();
        s.cz.clear();
        s.indices.clear();
        auto& destination = family == 1 ? s.lo : s.hi;
        std::fill(destination.begin(), destination.end(), 0);
        for (size_t i = 0; i < x.size(); ++i) {
            const float blend = (s.control[i] / 10 + 1) * .5f;
            if (family == 1 ? blend < 1 : blend > 0) {
                s.cx.push_back(s.x[i]);
                s.cy.push_back(s.y[i]);
                s.cz.push_back(s.z[i]);
                s.indices.push_back(i);
            }
        }
        s.values.resize(s.indices.size());
        nodes_->shape[family]->sample(s.values, s.cx, s.cy, s.cz);
        for (size_t i = 0; i < s.indices.size(); ++i)
            destination[s.indices[i]] = s.values[i];
    }
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = std::lerp(s.lo[i], s.hi[i], std::clamp((s.control[i] / 10 + 1) * .5f, 0.0f, 1.0f));
}
} // namespace sandbox
