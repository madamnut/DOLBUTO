#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

namespace sandbox::minecraft_reference {
// Read-only ordinary Overworld 26.2 signals. Deliberately not linked into world_core.
inline constexpr std::array<std::string_view, 10> keys{
    "groundness",    "smoothness",   "weirdness", "pv",     "temperature",
    "precipitation", "jagged_noise", "offset",    "factor", "jaggedness"};
int field_index(std::string_view name);
double peaks_valleys(double weirdness);
// Raw TerrainProvider spline output (offset excludes the router's -0.50375F bias).
float spline(int field, float continents, float erosion, float weirdness);

class Sampler {
  public:
    explicit Sampler(int64_t seed);
    ~Sampler();
    double sample(int field, int x, int z) const;
    std::array<double, 10> probe(int x, int z) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sandbox::minecraft_reference
