#pragma once
#include "world/terrain_spline.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sandbox {
struct NoiseSettings {
    int spacing_log2{10}; // Horizontal lattice spacing in blocks: 2^n, divides the world period.
    int octaves{4};
    float gain{0.5f};
    int seed_offset{};
    std::vector<float> weights;    // Empty means geometric gain weights.
    std::string warp{};            // Stable warp ID; empty disables coordinate displacement.
    std::vector<float> spacings{}; // Groundness only: per-octave block scale; empty migrates legacy spacing.
    float frequency_multiplier{1}; // Jagged original input scale.
    int preview_octave{-1};        // Transient diagnostic selection.
    bool preview_weighted{false};
    bool operator==(const NoiseSettings&) const = default;
};
std::vector<float> octave_weights(const NoiseSettings& settings);
std::vector<float> octave_spacings(const NoiseSettings& settings);
struct TemperatureSettings {
    float equator{1}, poles{-1}, latitude_power{1}, variation{0.35f};
    bool operator==(const TemperatureSettings&) const = default;
};
struct DomainWarpSettings {
    std::string id, name;
    bool enabled{true};
    NoiseSettings noise{11, 3, .5f, 8191, {1, .5f, .25f}};
    float strength{192}; // Blocks per displacement component; normalized weights.
    bool operator==(const DomainWarpSettings&) const = default;
};
struct GenerationConfig {
    std::vector<DomainWarpSettings> warps{{"shift", "공유 Shift", true, {5, 4, .5f, 8191, {1, 1, 1, 0}}, 16}};
    uint32_t seed{1337};
    NoiseSettings groundness{11, 9, .5f, 0, {1, 1, 2, 2, 2, 1, 1, 1, 1}, "shift"};
    NoiseSettings smoothness{11, 5, .5f, 30103, {1, 1, 0, 1, 1}, "shift"};
    NoiseSettings weirdness{9, 6, .5f, 41507, {1, 2, 1, 0, 0, 0}, "shift"};
    NoiseSettings temperature{12, 6, .5f, 52711, {1.5f, 0, 1, 0, 0, 0}, "shift"};
    NoiseSettings precipitation{10, 6, .5f, 63823, {1, 1, 0, 0, 0, 0}, "shift"};
    NoiseSettings jagged{16, 16, .5f, 74929, std::vector<float>(16, 1), "", {}, 1500};
    TerrainSplines splines{default_terrain_splines()};
    TemperatureSettings temperature_bands;
    NoiseSettings shape{6, 2, 0.5f, 16883, {}};
    bool shape_enabled{true};
    float vertical_scale{48.0f};
    float amplitude{1.0f};
    float squash{1.0f};
    struct BlendedSettings {
        float xz_scale{.25f}, y_scale{.125f}, xz_factor{80}, y_factor{160}, smear{8};
        bool operator==(const BlendedSettings&) const = default;
    } blended;
    bool operator==(const GenerationConfig&) const = default;
};
// Throws on invalid input. No silent parameter coercion when reading saved defaults.
void validate(const GenerationConfig& config);
void disable_warps(GenerationConfig& config);
bool has_active_warp(const GenerationConfig& config);
void remove_warp(GenerationConfig& config, const std::string& id);
std::string generation_json(const GenerationConfig& config);
GenerationConfig parse_generation_json(const std::string& text);
GenerationConfig load_generation_config(const std::filesystem::path& path);
// Temporary sibling file, checked close, atomic replacement on Windows. Never truncates the old default.
void save_generation_config(const std::filesystem::path& path, const GenerationConfig& config);
} // namespace sandbox
