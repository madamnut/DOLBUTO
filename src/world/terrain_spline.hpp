#pragma once
#include <optional>
#include <vector>

namespace sandbox {
enum class SplineAxis { constant, weirdness, pv, groundness, smoothness };
// Values can themselves be splines. Parallel arrays keep the tree value-owned and easy to serialize.
struct TerrainSpline {
    SplineAxis axis{SplineAxis::constant};
    float constant{};
    std::vector<float> locations, derivatives;
    std::vector<TerrainSpline> values;
    float evaluate(float weirdness) const;
    float evaluate(float ground, float smooth, float weird) const;
    bool operator==(const TerrainSpline&) const = default;
};
struct SplineGrid {
    std::optional<TerrainSpline> tree; // Authoritative nested Hermite tree; grids are legacy imports only.
    std::vector<float> groundness, smoothness;
    // Row-major [groundness][smoothness]; unset cells interpolate from defined neighbours.
    std::vector<std::optional<TerrainSpline>> cells;
    float evaluate(float ground, float smooth, float weird) const;
    bool operator==(const SplineGrid&) const = default;
};
struct TerrainSplines {
    SplineGrid offset, factor, jaggedness;
    float height_scale{512.0f / 3};
    float jagged_scale{512.0f / 3};
    bool jagged_enabled{true};
    bool operator==(const TerrainSplines&) const = default;
};
TerrainSplines default_terrain_splines();
void validate(const TerrainSplines& splines);
float spline_pv(float weirdness);
} // namespace sandbox
