#pragma once
#include "world/generation_config.hpp"
#include <FastNoise/Generators/BasicGenerators.h>
#include <array>
#include <span>

namespace FastNoise {
struct PeriodicLayer {
    int period{}, seed{};
    double frequency{}, y_frequency{}, smear{};
    float weight{};
    std::array<double, 3> offset{}, periodic_offset{};
};
// Project-owned SIMD node. 2D uses (X,Z), optionally nonperiodic Z for temperature.
// 3D always repeats X/Z and uses nonperiodic Y.
// Configure once before sharing with workers. Every octave spans the same physical world period.
class PeriodicPerlin : public virtual VariableRange<Seeded<ScalableGenerator>> {
  public:
    // Double coordinate reduction retains sub-block precision even in the finest Jagged octave.
    // Hashing, gradient evaluation and interpolation use the selected FastSIMD backend.
    virtual void SampleLayers(std::span<float> out, std::span<const double> x, std::span<const double> y,
                              std::span<const double> z, std::span<const PeriodicLayer> layers,
                              bool two_dimensional, bool periodic_y, bool periodic_z) const = 0;
    const Metadata& GetMetadata() const override;
    void Configure(const sandbox::NoiseSettings& settings, float vertical_scale, bool periodic_z = true);

  protected:
    std::array<float, 16> mWeights{};
    bool mPeriodicZ{true};
    int mPeriod{128}, mOctaves{4};
    float mGain{0.5f}, mNormalization{1.0f}, mVerticalFrequency{1.0f / 48};
};
} // namespace FastNoise
