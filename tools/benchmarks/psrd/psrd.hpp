#pragma once
#include <FastNoise/Generators/BasicGenerators.h>
namespace FastNoise {
// Benchmark-only 2D static PSRD, period 131072 world blocks. No game registration.
class BenchmarkPsrd : public virtual Generator {
  public:
    const Metadata& GetMetadata() const override;
    void Configure(int octaves);

  protected:
    int mOctaves{1};
    float mNormalization{1};
};
} // namespace FastNoise
