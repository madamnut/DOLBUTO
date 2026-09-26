#pragma once
#include "world/periodic_perlin.hpp"
#include <FastNoise/FastNoise.h>
namespace sandbox {
class PeriodicNoise {
  public:
    PeriodicNoise(const NoiseSettings& settings, uint32_t seed, bool two = true, bool periodic_y = false,
                  bool periodic_z = true);
    PeriodicNoise(const GenerationConfig& config, int family);
    void sample(std::span<float> out, std::span<const double> x, std::span<const double> y,
                std::span<const double> z) const;

  private:
    FastNoise::SmartNode<FastNoise::PeriodicPerlin> kernel_;
    std::vector<FastNoise::PeriodicLayer> layers_;
    bool two_{}, periodic_y_{}, periodic_z_{true};
};
// Rounded integer cell counts for both independent Double-Perlin families.
double effective_noise_spacing(double requested, int family);
} // namespace sandbox
