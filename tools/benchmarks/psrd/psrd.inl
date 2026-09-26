/*
Adapted from psrdnoise2.glsl, Stefan Gustavson and Ian McEwan, 2021-12-02.
Copyright (c) 2021 Stefan Gustavson and Ian McEwan.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
// clang-format off
#include <FastNoise/Generators/Generator.inl>
#include "psrd.hpp"
// clang-format on
#include <stdexcept>

template <FastSIMD::FeatureSet SIMD>
class FastSIMD::DispatchClass<BenchmarkPsrd, SIMD> final : public virtual BenchmarkPsrd,
                                                           public DispatchClass<Generator, SIMD> {
    float32v FS_VECTORCALL Gen(int32v, float32v, float32v, float32v) const override {
        throw std::logic_error("This benchmark PSRD node supports 2D only");
    }
    static float32v mod(float32v x, float p) { return x - FS::Floor(x * float32v(1 / p)) * float32v(p); }
    static float32v corner(float32v x, float32v y, float32v vx, float32v vy, float p, float32v salt) {
        // Corners, not sample coordinates, are wrapped. The original support stays continuous.
        float32v xw = mod(vx, p), yw = mod(vy, p);
        float32v iu = FS::Floor(xw + float32v(.5f) * yw + float32v(.5f));
        float32v iv = FS::Floor(yw + float32v(.5f));
        // Original source has no seed. Salt=0 exactly preserves its polynomial hash.
        float32v hash = mod(iu + salt, 289);
        hash = mod((hash * float32v(51) + float32v(2)) * hash + iv, 289);
        hash = mod((hash * float32v(34) + float32v(10)) * hash, 289);
        float32v angle = hash * float32v(.07482f);
        float32v dx = x - vx, dy = y - vy;
        float32v weight = FS::Max(float32v(.8f) - dx * dx - dy * dy, float32v(0));
        weight *= weight;
        weight *= weight;
        return weight * (FS::Cos(angle) * dx + FS::Sin(angle) * dy);
    }
    float32v FS_VECTORCALL Gen(int32v seed, float32v x, float32v y) const override {
        x *= float32v(1.0f / 1024);
        y *= float32v(1.0f / 1024);
        float32v total(0), weight(mNormalization);
        float p = 128;
        for (int octave = 0; octave < mOctaves; ++octave) {
            float32v u = x + float32v(.5f) * y;
            float32v iu = FS::Floor(u), iv = FS::Floor(y);
            float32v ox = FS::Select((u - iu) >= (y - iv), float32v(1), float32v(0));
            float32v oy = float32v(1) - ox;
            float32v vx = iu - float32v(.5f) * iv;
            float32v salt = mod(FS::Convert<float>((seed ^ (seed >> 16)) & int32v(65535)), 289);
            float32v value = corner(x, y, vx, iv, p, salt) +
                             corner(x, y, vx + ox - float32v(.5f) * oy, iv + oy, p, salt) +
                             corner(x, y, vx + float32v(.5f), iv + float32v(1), p, salt);
            total += float32v(10.9f) * weight * value;
            x *= float32v(2);
            y *= float32v(2);
            p *= 2;
            weight *= float32v(.5f);
            seed += int32v(1);
        }
        return total;
    }
};
template class FastSIMD::RegisterDispatchClass<BenchmarkPsrd>;
