#include "world/periodic_perlin.hpp"
#include "core/world_rules.hpp"
#include <FastNoise/FastNoise.h>
#include <FastNoise/Metadata.h>

namespace FastNoise {
void PeriodicPerlin::Configure(const sandbox::NoiseSettings& s, float vertical_scale, bool periodic_z) {
    mPeriodicZ = periodic_z;
    SetScale(static_cast<float>(1 << s.spacing_log2));
    SetSeedOffset(s.seed_offset);
    mPeriod = sandbox::world_size >> s.spacing_log2;
    mOctaves = s.octaves;
    mGain = s.gain;
    mVerticalFrequency = 1.0f / vertical_scale;
    float sum = 0, weight = 1;
    for (int i = 0; i < mOctaves; ++i) {
        sum += weight;
        weight *= mGain;
    }
    mNormalization = 1.0f / sum;
    mWeights.fill(0);
    if (s.weights.empty()) {
        // Preserve the old gain recurrence for legacy/default callers and the 3D graph.
        weight = mNormalization;
        for (int i = 0; i < mOctaves; ++i) {
            mWeights[i] = weight;
            weight *= mGain;
        }
    } else {
        double total = 0; // Finite float weights cannot overflow this double sum.
        for (float value : s.weights)
            total += value;
        for (int i = 0; i < mOctaves; ++i)
            mWeights[i] = total > 0 ? static_cast<float>(s.weights[i] / total) : 0;
    }
}
const Metadata& PeriodicPerlin::GetMetadata() const {
    // Project settings use versioned JSON, not FastNoise's encoded node-tree format.
    struct Description final : Metadata {
        Description() {
            name = "SandboxPeriodicPerlin";
            description = "Periodic X/Z Perlin fBm; configured by Sandbox JSON.";
        }
        SmartNode<> CreateNode(FastSIMD::FeatureSet feature) const override {
            return New<PeriodicPerlin>(feature);
        }
    };
    static const Description description;
    return description;
}
} // namespace FastNoise
