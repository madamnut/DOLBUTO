/*
Periodic X/Z extension of FastNoise2 v1.1.1 Perlin.inl.
MIT License

Copyright (c) 2020 Jordan Peck

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/
// Include order defines the SIMD aliases before the generator mixins. Do not sort.
// clang-format off
#include <FastNoise/Generators/Generator.inl>
#include <FastNoise/Generators/BasicGenerators.inl>
#include <FastNoise/Generators/Utils.inl>
#include "world/periodic_perlin.hpp"
#include <algorithm>
#include <cmath>
// clang-format on

template <FastSIMD::FeatureSet SIMD>
class FastSIMD::DispatchClass<PeriodicPerlin, SIMD> final
    : public virtual PeriodicPerlin,
      public DispatchClass<VariableRange<Seeded<ScalableGenerator>>, SIMD> {
  public:
    void SampleLayers(std::span<float> out, std::span<const double> x, std::span<const double> y,
                      std::span<const double> z, std::span<const PeriodicLayer> layers, bool two,
                      bool periodic_y, bool periodic_z) const override {
        if (two) {
            if (periodic_z)
                sample_layers<true, false, true>(out, x, y, z, layers);
            else
                sample_layers<true, false, false>(out, x, y, z, layers);
        } else if (periodic_y)
            sample_layers<false, true, true>(out, x, y, z, layers);
        else
            sample_layers<false, false, true>(out, x, y, z, layers);
    }

  private:
    template <bool two, bool periodic_y, bool periodic_z>
    void sample_layers(std::span<float> out, std::span<const double> x, std::span<const double> y,
                       std::span<const double> z, std::span<const PeriodicLayer> layers) const {
        constexpr size_t lanes = float32v::ElementCount;
        for (size_t start = 0; start < out.size(); start += lanes) {
            float32v sum(0);
            for (const auto& layer : layers) {
                if (layer.weight == 0)
                    continue;
                std::array<float, lanes> fx{}, fy{}, fz{}, fade_y{}, result{};
                std::array<int32_t, lanes> ax{}, ay{}, az{}, bx{}, by{}, bz{};
                for (size_t lane = 0; lane < lanes; ++lane) {
                    const size_t i = std::min(start + lane, out.size() - 1);
                    const auto coordinate = [&](double position, double frequency, double offset,
                                                bool periodic, int32_t& a, int32_t& b, float& fraction) {
                        // Canonicalize before multiplying, then wrap lattice indices, not output values.
                        if (periodic && (position < 0 || position >= 131072.0))
                            position -= std::floor(position / 131072.0) * 131072.0;
                        const double p = position * frequency + offset, floor = std::floor(p);
                        fraction = float(p - floor);
                        int64_t cell = int64_t(floor);
                        if (periodic && cell >= layer.period)
                            cell -= layer.period;
                        a = int32_t(cell);
                        b = periodic && a == layer.period - 1 ? 0 : a + 1;
                    };
                    coordinate(x[i], layer.frequency, layer.periodic_offset[0], true, ax[lane], bx[lane],
                               fx[lane]);
                    coordinate(z[i], layer.frequency, periodic_z ? layer.periodic_offset[2] : layer.offset[2],
                               periodic_z, az[lane], bz[lane], fz[lane]);
                    if (!two) {
                        coordinate(y[i], layer.y_frequency,
                                   periodic_y ? layer.periodic_offset[1] : layer.offset[1], periodic_y,
                                   ay[lane], by[lane], fy[lane]);
                        fade_y[lane] = fy[lane];
                        if (layer.smear > 0) {
                            const double fudge = y[i] * layer.y_frequency;
                            const double limit = fudge >= 0 && fudge < fy[lane] ? fudge : fy[lane];
                            fy[lane] -= float(std::floor(limit / layer.smear + 1e-7) * layer.smear);
                        }
                    }
                }
                const int32v seed(layer.seed);
                const auto x0 = FS::Load<int32v>(ax.data()) * int32v(Primes::X);
                const auto x1 = FS::Load<int32v>(bx.data()) * int32v(Primes::X);
                const auto z0 = FS::Load<int32v>(az.data()) * int32v(Primes::Z);
                const auto z1 = FS::Load<int32v>(bz.data()) * int32v(Primes::Z);
                const auto u = FS::Load<float32v>(fx.data()), v = FS::Load<float32v>(fz.data());
                const auto us = InterpQuintic(u), vs = InterpQuintic(v);
                float32v value;
                if (two) {
                    value = Lerp(
                        Lerp(GetGradientDotPerlin(HashPrimes(seed, x0, z0), u, v),
                             GetGradientDotPerlin(HashPrimes(seed, x1, z0), u - float32v(1), v), us),
                        Lerp(GetGradientDotPerlin(HashPrimes(seed, x0, z1), u, v - float32v(1)),
                             GetGradientDotPerlin(HashPrimes(seed, x1, z1), u - float32v(1), v - float32v(1)),
                             us),
                        vs);
                    // Match the sqrt(2) length of Improved-Perlin gradients.
                    value *= float32v(1.0f / 1.8477590650225735f);
                } else {
                    const auto y0 = FS::Load<int32v>(ay.data()) * int32v(Primes::Y);
                    const auto y1 = FS::Load<int32v>(by.data()) * int32v(Primes::Y);
                    const auto t = FS::Load<float32v>(fy.data());
                    const auto ts = InterpQuintic(FS::Load<float32v>(fade_y.data()));
                    const auto plane = [&](int32v zi, float32v dz) {
                        return Lerp(
                            Lerp(GetGradientDotCommon(HashPrimes(seed, x0, y0, zi), u, t, dz),
                                 GetGradientDotCommon(HashPrimes(seed, x1, y0, zi), u - float32v(1), t, dz),
                                 us),
                            Lerp(GetGradientDotCommon(HashPrimes(seed, x0, y1, zi), u, t - float32v(1), dz),
                                 GetGradientDotCommon(HashPrimes(seed, x1, y1, zi), u - float32v(1),
                                                      t - float32v(1), dz),
                                 us),
                            ts);
                    };
                    value = Lerp(plane(z0, v), plane(z1, v - float32v(1)), vs);
                }
                sum += value * float32v(layer.weight);
            }
            std::array<float, lanes> result{};
            FS::Store(result.data(), sum);
            for (size_t lane = 0; lane < lanes && start + lane < out.size(); ++lane)
                out[start + lane] = result[lane];
        }
    }

  private:
    float32v FS_VECTORCALL octave2(int32v seed, float32v x, float32v y, int mask) const {

        float32v xs = FS::Floor(x);
        float32v ys = FS::Floor(y);

        int32v x0 = (FS::Convert<int32_t>(xs) & int32v(mask)) * int32v(Primes::X);
        int32v y0 = (FS::Convert<int32_t>(ys) & int32v(mPeriodicZ ? mask : -1)) * int32v(Primes::Y);
        int32v x1 = ((FS::Convert<int32_t>(xs) + int32v(1)) & int32v(mask)) * int32v(Primes::X);
        int32v y1 =
            ((FS::Convert<int32_t>(ys) + int32v(1)) & int32v(mPeriodicZ ? mask : -1)) * int32v(Primes::Y);

        float32v xf0 = xs = x - xs;
        float32v yf0 = ys = y - ys;
        float32v xf1 = xf0 - float32v(1);
        float32v yf1 = yf0 - float32v(1);

        xs = InterpQuintic(xs);
        ys = InterpQuintic(ys);

        float32v value = Lerp(Lerp(GetGradientDotPerlin(HashPrimes(seed, x0, y0), xf0, yf0),
                                   GetGradientDotPerlin(HashPrimes(seed, x1, y0), xf1, yf0), xs),
                              Lerp(GetGradientDotPerlin(HashPrimes(seed, x0, y1), xf0, yf1),
                                   GetGradientDotPerlin(HashPrimes(seed, x1, y1), xf1, yf1), xs),
                              ys);

        constexpr float kBounding = 1.726796627044677734375f;

        return this->ScaleOutput(value, -kBounding, kBounding);
    }

    float32v FS_VECTORCALL octave3(int32v seed, float32v x, float32v y, float32v z, int mask) const {

        float32v xs = FS::Floor(x);
        float32v ys = FS::Floor(y);
        float32v zs = FS::Floor(z);

        int32v x0 = (FS::Convert<int32_t>(xs) & int32v(mask)) * int32v(Primes::X);
        int32v y0 = FS::Convert<int32_t>(ys) * int32v(Primes::Y);
        int32v z0 = (FS::Convert<int32_t>(zs) & int32v(mask)) * int32v(Primes::Z);
        int32v x1 = ((FS::Convert<int32_t>(xs) + int32v(1)) & int32v(mask)) * int32v(Primes::X);
        int32v y1 = y0 + int32v(Primes::Y);
        int32v z1 = ((FS::Convert<int32_t>(zs) + int32v(1)) & int32v(mask)) * int32v(Primes::Z);

        float32v xf0 = xs = x - xs;
        float32v yf0 = ys = y - ys;
        float32v zf0 = zs = z - zs;
        float32v xf1 = xf0 - float32v(1);
        float32v yf1 = yf0 - float32v(1);
        float32v zf1 = zf0 - float32v(1);

        xs = InterpQuintic(xs);
        ys = InterpQuintic(ys);
        zs = InterpQuintic(zs);

        float32v value =
            Lerp(Lerp(Lerp(GetGradientDotCommon(HashPrimes(seed, x0, y0, z0), xf0, yf0, zf0),
                           GetGradientDotCommon(HashPrimes(seed, x1, y0, z0), xf1, yf0, zf0), xs),
                      Lerp(GetGradientDotCommon(HashPrimes(seed, x0, y1, z0), xf0, yf1, zf0),
                           GetGradientDotCommon(HashPrimes(seed, x1, y1, z0), xf1, yf1, zf0), xs),
                      ys),
                 Lerp(Lerp(GetGradientDotCommon(HashPrimes(seed, x0, y0, z1), xf0, yf0, zf1),
                           GetGradientDotCommon(HashPrimes(seed, x1, y0, z1), xf1, yf0, zf1), xs),
                      Lerp(GetGradientDotCommon(HashPrimes(seed, x0, y1, z1), xf0, yf1, zf1),
                           GetGradientDotCommon(HashPrimes(seed, x1, y1, z1), xf1, yf1, zf1), xs),
                      ys),
                 zs);

        constexpr double kBounding = 1.0363423824310302734375;

        return this->ScaleOutput(value, -kBounding, kBounding);
    }

    float32v FS_VECTORCALL Gen(int32v seed, float32v x, float32v z) const override {
        seed += int32v(mSeedOffset);
        x *= float32v(mFrequency);
        z *= float32v(mFrequency);
        float32v sum(0);
        int period = mPeriod;
        for (int i = 0; i < mOctaves; ++i) {
            if (mWeights[i] != 0)
                sum += float32v(mWeights[i]) * octave2(seed, x, z, period - 1);
            x *= float32v(2);
            z *= float32v(2);
            period *= 2;
            seed += int32v(1);
        }
        return sum;
    }
    float32v FS_VECTORCALL Gen(int32v seed, float32v x, float32v y, float32v z) const override {
        seed += int32v(mSeedOffset);
        x *= float32v(mFrequency);
        z *= float32v(mFrequency);
        y *= float32v(mVerticalFrequency);
        float32v sum(0);
        int period = mPeriod;
        for (int i = 0; i < mOctaves; ++i) {
            if (mWeights[i] != 0)
                sum += float32v(mWeights[i]) * octave3(seed, x, y, z, period - 1);
            x *= float32v(2);
            y *= float32v(2);
            z *= float32v(2);
            period *= 2;
            seed += int32v(1);
        }
        return sum;
    }
};
template class FastSIMD::RegisterDispatchClass<FastNoise::PeriodicPerlin>;
static_assert(std::is_final_v<FastSIMD::DispatchClass<PeriodicPerlin, FastSIMD::FeatureSetDefault()>>);
