#include "psrd.hpp"
#include <FastNoise/FastNoise.h>
#include <FastNoise/Metadata.h>
#include <cmath>
#include <stdexcept>
namespace FastNoise {
void BenchmarkPsrd::Configure(int octaves) {
    if (octaves < 1 || octaves > 8)
        throw std::invalid_argument("PSRD benchmark supports 1..8 octaves");
    mOctaves = octaves;
    mNormalization = 1 / (2.0f * (1 - std::pow(.5f, float(octaves))));
}
const Metadata& BenchmarkPsrd::GetMetadata() const {
    struct Description final : Metadata {
        Description() {
            name = "BenchmarkPsrd";
            description = "Static periodic 2D PSRD benchmark port";
        }
        SmartNode<> CreateNode(FastSIMD::FeatureSet feature) const override {
            return New<BenchmarkPsrd>(feature);
        }
    };
    static const Description description;
    return description;
}
} // namespace FastNoise
