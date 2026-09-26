#include "inspection.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
// Independent scalar/double transcription of the vendored original, alpha=0, derivatives omitted.
double mod(double v, double period) { return v - period * std::floor(v / period); }
double reference(double x, double y, int seed, int octaves) {
    x /= 1024;
    y /= 1024;
    double total = 0, weight = 1, normalization = 0, p = 128;
    for (int octave = 0; octave < octaves; ++octave) {
        double u = x + .5 * y, iu = std::floor(u), iv = std::floor(y);
        double ox = u - iu >= y - iv ? 1 : 0, oy = 1 - ox, vx = iu - .5 * iv;
        double sum = 0;
        const int salt = ((seed ^ (seed >> 16)) & 65535) % 289;
        for (auto v : std::array<std::array<double, 2>, 3>{
                 {{vx, iv}, {vx + ox - .5 * oy, iv + oy}, {vx + .5, iv + 1}}}) {
            double xw = mod(v[0], p), yw = mod(v[1], p);
            double a = std::floor(xw + .5 * yw + .5), b = std::floor(yw + .5);
            double hash = mod(a + salt, 289);
            hash = mod((hash * 51 + 2) * hash + b, 289);
            hash = mod((hash * 34 + 10) * hash, 289);
            // Original float literal, evaluated accurately here; SIMD uses approximate trig.
            double angle = hash * double(.07482f), dx = x - v[0], dy = y - v[1];
            double w = std::max(double(.8f) - dx * dx - dy * dy, 0.0);
            sum += w * w * w * w * (std::cos(angle) * dx + std::sin(angle) * dy);
        }
        total += double(10.9f) * weight * sum;
        normalization += weight;
        x *= 2;
        y *= 2;
        p *= 2;
        weight *= .5;
        seed = static_cast<int>(static_cast<unsigned>(seed) + 1);
    }
    return total / normalization;
}
void evaluate(const FastNoise::Generator* node, const std::vector<float>& x, const std::vector<float>& z,
              std::vector<float>& out, int seed = 1337) {
    out.resize(x.size());
    node->GenPositionArray2D(out.data(), int(out.size()), x.data(), z.data(), 0, 0, seed);
    for (float v : out)
        if (!std::isfinite(v))
            throw std::runtime_error("Nonfinite inspection value");
}
double max_difference(const std::vector<float>& a, const std::vector<float>& b) {
    double result = 0;
    for (size_t i = 0; i < a.size(); ++i)
        result = std::max(result, double(std::abs(a[i] - b[i])));
    return result;
}
} // namespace
void inspect_psrd(const std::filesystem::path& folder, const FastNoise::Generator* perlin,
                  const FastNoise::Generator* psrd, int octaves) {
    constexpr int count = 4096;
    std::vector<float> x(count), z(count), a, b;
    std::mt19937 rng(9147);
    for (int i = 0; i < count; ++i) {
        x[i] = float(int(rng() % 2097152) - 1048576) / 16;
        z[i] = float(int(rng() % 2097152) - 1048576) / 16;
    }
    double reference_error = 0;
    for (int seed : {0, 1337, -7, 2147483647}) {
        evaluate(psrd, x, z, a, seed);
        for (int i = 0; i < count; ++i)
            reference_error =
                std::max(reference_error, std::abs(double(a[i]) - reference(x[i], z[i], seed, octaves)));
    }
    std::ofstream report(folder / ("inspection-o" + std::to_string(octaves) + ".txt"));
    report << std::setprecision(12) << "psrd_scalar_reference_max_error=" << reference_error << '\n';
    if (reference_error > 0.0001)
        throw std::runtime_error("PSRD does not agree with scalar reference");
    for (int kind = 0; kind < 2; ++kind) {
        auto* node = kind == 0 ? perlin : psrd;
        const char* name = kind == 0 ? "perlin" : "psrd";
        evaluate(node, x, z, a);
        for (int axis = 0; axis < 2; ++axis) {
            auto shifted = axis == 0 ? x : z;
            for (float& v : shifted)
                v += 131072;
            evaluate(node, axis == 0 ? shifted : x, axis == 1 ? shifted : z, b);
            double error = max_difference(a, b);
            report << name << "_period_" << axis << "_max_error=" << error << '\n';
            if (error > 0.0001)
                throw std::runtime_error("Period translation does not match");
            std::vector<float> near(count, -.25f), far(count, 131072 - .25f), left, right, farleft, farright;
            evaluate(node, axis == 0 ? near : x, axis == 1 ? near : z, left);
            evaluate(node, axis == 0 ? far : x, axis == 1 ? far : z, farleft);
            std::fill(near.begin(), near.end(), .25f);
            std::fill(far.begin(), far.end(), 131072 + .25f);
            evaluate(node, axis == 0 ? near : x, axis == 1 ? near : z, right);
            evaluate(node, axis == 0 ? far : x, axis == 1 ? far : z, farright);
            double gradient_error = 0;
            for (int i = 0; i < count; ++i)
                gradient_error = std::max(
                    gradient_error, std::abs(double(right[i] - left[i] - farright[i] + farleft[i]) / .5));
            report << name << "_seam_gradient_" << axis << "_max_error=" << gradient_error << '\n';
            if (gradient_error > 0.0001)
                throw std::runtime_error("Seam gradient does not match");
        }
        // Same local 8192x8192 region, sample centres, fixed physical feature scale1024.
        constexpr int side = 1024;
        std::vector<float> px(side), pz(side), values;
        std::ofstream image(folder / (std::string(name) + "-o" + std::to_string(octaves) + ".f32"),
                            std::ios::binary);
        for (int row = 0; row < side; ++row) {
            for (int col = 0; col < side; ++col) {
                px[col] = 8192 + (col + .5f) * 8;
                pz[col] = 16384 + (row + .5f) * 8;
            }
            evaluate(node, px, pz, values);
            image.write(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(float));
        }
    }
    std::cout << "PSRD reference/period/seam diagnostics passed octaves=" << octaves
              << " max_reference_error=" << reference_error << std::endl;
}
