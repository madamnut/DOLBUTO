#pragma once
#include "world/terrain_spline.hpp"
#include <string>

namespace sandbox {
class TerrainSplineEditor {
  public:
    bool open{};
    void draw(TerrainSplines& splines);

  private:
    int output_{}, row_{}, column_{};
    float weirdness_{};
    std::optional<TerrainSpline> clipboard_;
    std::string error_;
};
} // namespace sandbox
