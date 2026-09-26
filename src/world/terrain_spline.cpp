#include "world/terrain_spline.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sandbox {
namespace {
float blend(float t, float a, float b) { return std::lerp(a, b, t * t * (3 - 2 * t)); }
void require(bool valid, const char* message) {
    if (!valid)
        throw std::invalid_argument(message);
}
void check_axis(const std::vector<float>& axis, size_t maximum) {
    require(!axis.empty() && axis.size() <= maximum, "Invalid spline axis size.");
    float previous = -3;
    for (float v : axis) {
        require(std::isfinite(v) && v >= -2 && v <= 2 && v - previous >= 0.0001f,
                "Spline coordinates must increase within -2..2.");
        previous = v;
    }
}
void check_node(const TerrainSpline& node, int depth, size_t& budget) {
    require(depth <= 8 && budget > 0, "Spline tree exceeds depth/node limits.");
    --budget;
    require(std::isfinite(node.constant) && std::abs(node.constant) <= 64, "Invalid spline value.");
    if (node.axis == SplineAxis::constant) {
        require(node.locations.empty() && node.derivatives.empty() && node.values.empty(),
                "Constant spline has children.");
        return;
    }
    require(node.axis == SplineAxis::weirdness || node.axis == SplineAxis::pv ||
                node.axis == SplineAxis::groundness || node.axis == SplineAxis::smoothness,
            "Unknown spline axis.");
    check_axis(node.locations, 64);
    require(node.locations.size() == node.derivatives.size() && node.locations.size() == node.values.size(),
            "Mismatched spline arrays.");
    for (size_t i = 0; i < node.values.size(); ++i) {
        require(std::isfinite(node.derivatives[i]) && std::abs(node.derivatives[i]) <= 64,
                "Invalid spline derivative.");
        check_node(node.values[i], depth + 1, budget);
    }
}
} // namespace
float spline_pv(float w) { return -(std::abs(std::abs(w) - 2.0f / 3) - 1.0f / 3) * 3; }
float TerrainSpline::evaluate(float weird) const { return evaluate(0, 0, weird); }
float TerrainSpline::evaluate(float ground, float smooth, float weird) const {
    if (axis == SplineAxis::constant)
        return constant;
    const float x = axis == SplineAxis::groundness   ? ground
                    : axis == SplineAxis::smoothness ? smooth
                    : axis == SplineAxis::pv         ? spline_pv(weird)
                                                     : weird;
    const auto upper = std::upper_bound(locations.begin(), locations.end(), x);
    if (upper == locations.begin())
        return values.front().evaluate(ground, smooth, weird) + derivatives.front() * (x - locations.front());
    if (upper == locations.end())
        return values.back().evaluate(ground, smooth, weird) + derivatives.back() * (x - locations.back());
    const size_t i = size_t(upper - locations.begin() - 1);
    const float width = locations[i + 1] - locations[i], t = (x - locations[i]) / width;
    const float a = values[i].evaluate(ground, smooth, weird),
                b = values[i + 1].evaluate(ground, smooth, weird);
    const float da = derivatives[i] * width - (b - a), db = -derivatives[i + 1] * width + (b - a);
    return std::lerp(a, b, t) + t * (1 - t) * std::lerp(da, db, t);
}
float SplineGrid::evaluate(float ground, float smooth, float weird) const {
    if (tree)
        return tree->evaluate(ground, smooth, weird);
    const size_t count = smoothness.size();
    const auto row_value = [&](size_t row) {
        size_t left = count, right = count;
        for (size_t e = 0; e < count; ++e) {
            if (!cells[row * count + e])
                continue;
            if (smoothness[e] < smooth)
                left = e;
            else {
                right = e;
                break;
            }
        }
        if (left == count && right == count)
            return std::optional<float>{};
        if (left == count)
            return std::optional<float>{cells[row * count + right]->evaluate(weird)};
        if (right == count)
            return std::optional<float>{cells[row * count + left]->evaluate(weird)};
        return std::optional<float>{
            blend((smooth - smoothness[left]) / (smoothness[right] - smoothness[left]),
                  cells[row * count + left]->evaluate(weird), cells[row * count + right]->evaluate(weird))};
    };
    const size_t rows = groundness.size();
    size_t left = rows, right = rows;
    for (size_t c = 0; c < rows; ++c) {
        bool filled = false;
        for (size_t e = 0; e < count; ++e)
            filled |= cells[c * count + e].has_value();
        if (!filled)
            continue;
        if (groundness[c] < ground)
            left = c;
        else {
            right = c;
            break;
        }
    }
    if (left == rows)
        return right == rows ? 0 : *row_value(right);
    if (right == rows)
        return *row_value(left);
    return blend((ground - groundness[left]) / (groundness[right] - groundness[left]), *row_value(left),
                 *row_value(right));
}
void validate(const TerrainSplines& s) {
    size_t budget = 16384;
    for (const auto* grid : {&s.offset, &s.factor, &s.jaggedness}) {
        if (grid->tree) {
            check_node(*grid->tree, 0, budget);
            continue;
        }
        check_axis(grid->groundness, 24);
        check_axis(grid->smoothness, 24);
        require(grid->cells.size() == grid->groundness.size() * grid->smoothness.size(),
                "Mismatched spline grid.");
        bool defined = false;
        for (const auto& cell : grid->cells)
            if (cell) {
                defined = true;
                check_node(*cell, 0, budget);
            }
        require(defined, "A spline grid needs at least one defined cell.");
    }
    require(std::isfinite(s.height_scale) && s.height_scale >= 1 && s.height_scale <= 512,
            "Spline height scale must be 1..512.");
    require(std::isfinite(s.jagged_scale) && s.jagged_scale >= 0 && s.jagged_scale <= 512,
            "Jagged scale must be 0..512.");
}
} // namespace sandbox
