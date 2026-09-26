#include "world/periodic_noise.hpp"
#include "core/world_rules.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>
namespace sandbox {
namespace {
uint64_t mix(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
FastNoise::PeriodicLayer layer(double spacing, double yf, float weight, uint64_t seed, double smear = 0) {
    const double cells = std::round(world_size / spacing);
    if (!std::isfinite(cells) || cells > 1000000000.0)
        throw std::invalid_argument("Noise frequency exceeds safe periodic lattice range.");
    FastNoise::PeriodicLayer l;
    l.period = int(std::max(1.0, cells));
    l.frequency = double(l.period) / world_size;
    l.y_frequency = yf < 0 ? l.frequency : yf;
    l.weight = weight;
    l.smear = smear;
    l.seed = std::bit_cast<int32_t>(uint32_t(mix(seed)));
    for (int i = 0; i < 3; ++i) {
        const double v = double(mix(seed + uint64_t(i + 1) * 0x9e3779b97f4a7c15ULL) >> 11) * 0x1p-53 * 256;
        l.offset[i] = v;
        l.periodic_offset[i] = v - std::floor(v / l.period) * l.period;
    }
    return l;
}
} // namespace
double effective_noise_spacing(double requested, int family) {
    const double cells =
        std::max(1.0, std::round(world_size / requested * (family ? 1.0181268882175227 : 1.0)));
    return world_size / cells;
}
PeriodicNoise::PeriodicNoise(const NoiseSettings& n, uint32_t seed, bool two, bool py, bool pz)
    : kernel_(FastNoise::New<FastNoise::PeriodicPerlin>()), two_(two), periodic_y_(py), periodic_z_(pz) {
    const auto amplitudes = octave_weights(n), spacings = octave_spacings(n);
    int first = n.octaves, last = -1;
    for (int i = 0; i < n.octaves; ++i)
        if (amplitudes[i] != 0) {
            first = std::min(first, i);
            last = i;
        }
    const double norm = last < 0 ? 0 : (1.0 / 6) / (0.1 * (1 + 1.0 / (last - first + 1)));
    const double octave_base = std::ldexp(1.0, n.octaves - 1) / (std::ldexp(1.0, n.octaves) - 1);
    for (int family = 0; family < 2; ++family)
        for (int i = 0; i < n.octaves; ++i) {
            if (amplitudes[i] == 0 || (n.preview_octave >= 0 && n.preview_octave != i))
                continue;
            const float weight = n.preview_octave >= 0 && !n.preview_weighted
                                     ? .5f
                                     : float(amplitudes[i] * std::ldexp(octave_base, -i) * norm);
            const double spacing = spacings[i] / n.frequency_multiplier;
            const double frequency_ratio = family ? 1.0181268882175227 : 1;
            const uint64_t key = mix(uint64_t(seed) + uint32_t(n.seed_offset)) ^
                                 mix(uint64_t(family + 1) * 0x9e3779b97f4a7c15ULL + uint64_t(i));
            layers_.push_back(layer(spacing / frequency_ratio, -1, weight, key));
        }
}
void PeriodicNoise::sample(std::span<float> out, std::span<const double> x, std::span<const double> y,
                           std::span<const double> z) const {
    if (!kernel_)
        throw std::runtime_error("No compatible SIMD noise backend.");
    if (out.size() != x.size() || out.size() != z.size() || (!two_ && out.size() != y.size()))
        throw std::invalid_argument("Noise batch dimensions mismatch.");
    kernel_->SampleLayers(out, x, y, z, layers_, two_, periodic_y_, periodic_z_);
}
} // namespace sandbox
