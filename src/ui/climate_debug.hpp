#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace sandbox {
// F3 climate labels. These do not select biomes or change generation rules.
inline constexpr std::array<const char*, 9> temperature_stage_names{
    "극한", "한대", "아한대", "냉온대", "온대", "난온대", "아열대", "열대", "극열"};
inline constexpr std::array<const char*, 9> precipitation_stage_names{
    "극건조", "매우 건조", "건조", "반건조", "중간", "약습윤", "습윤", "매우 습윤", "극습윤"};
// Equal value intervals, not equal land areas. A boundary belongs to the higher stage.
inline constexpr std::array<float, 8> climate_stage_boundaries{-7.0f / 9, -5.0f / 9, -3.0f / 9, -1.0f / 9,
                                                               1.0f / 9,  3.0f / 9,  5.0f / 9,  7.0f / 9};
inline int climate_debug_stage(float value) {
    if (!std::isfinite(value))
        return -1;
    return static_cast<int>(
        std::upper_bound(climate_stage_boundaries.begin(), climate_stage_boundaries.end(), value) -
        climate_stage_boundaries.begin());
}
} // namespace sandbox
