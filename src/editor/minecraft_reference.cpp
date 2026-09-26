// Independent numerical implementation for the editor's 26.2 reference view.
// Algorithm/parameter provenance and comparison procedure: docs/minecraft-reference-page-2026-09-26.md.
#include "editor/minecraft_reference.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <nlohmann/json.hpp>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on

namespace sandbox::minecraft_reference {
namespace {
using Pair = std::array<uint64_t, 2>;
uint64_t mix(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
Pair name_hash(std::string_view name) {
    struct Md5 {
        BCRYPT_ALG_HANDLE handle{};
        Md5() {
            if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_MD5_ALGORITHM, nullptr, 0) < 0)
                throw std::runtime_error("Minecraft 시드 해시 초기화 실패");
        }
        ~Md5() { BCryptCloseAlgorithmProvider(handle, 0); }
    };
    static const Md5 md5;
    std::array<unsigned char, 16> bytes{};
    if (BCryptHash(md5.handle, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(name.data())),
                   ULONG(name.size()), bytes.data(), ULONG(bytes.size())) < 0)
        throw std::runtime_error("Minecraft 시드 해시 계산 실패");
    Pair result{};
    for (size_t i = 0; i < bytes.size(); ++i)
        result[i / 8] = (result[i / 8] << 8) | bytes[i];
    return result;
}
struct Random {
    Pair state;
    explicit Random(Pair s) : state(s) {
        if (!(state[0] | state[1]))
            state = {0x9e3779b97f4a7c15ULL, 0x6a09e667f3bcc909ULL};
    }
    explicit Random(int64_t seed)
        : Random(Pair{mix(uint64_t(seed) ^ 0x6a09e667f3bcc909ULL),
                      mix((uint64_t(seed) ^ 0x6a09e667f3bcc909ULL) + 0x9e3779b97f4a7c15ULL)}) {}
    uint64_t next() {
        const uint64_t a = state[0], b = state[1] ^ a;
        const uint64_t result = std::rotl(a + state[1], 17) + a;
        state = {std::rotl(a, 49) ^ b ^ (b << 21), std::rotl(b, 28)};
        return result;
    }
    Pair fork() {
        const uint64_t a = next();
        return {a, next()};
    }
    double unit() { return double(next() >> 11) * 0x1p-53; }
    unsigned bounded(unsigned n) {
        uint64_t product = uint64_t(uint32_t(next())) * n;
        const uint32_t threshold = uint32_t(0U - n) % n;
        while (uint32_t(product) < threshold)
            product = uint64_t(uint32_t(next())) * n;
        return unsigned(product >> 32);
    }
};
Random named(Pair parent, std::string_view key) {
    auto h = name_hash(key);
    return Random(Pair{parent[0] ^ h[0], parent[1] ^ h[1]});
}
double lerp(double a, double b, double t) { return a + t * (b - a); }
double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
double wrap(double x) { return x - std::floor(x / 33554432.0 + 0.5) * 33554432.0; }
struct Perlin {
    std::array<uint8_t, 256> permutation{};
    std::array<double, 3> origin{};
    explicit Perlin(Random random) {
        for (double& v : origin)
            v = random.unit() * 256;
        std::iota(permutation.begin(), permutation.end(), uint8_t(0));
        for (unsigned i = 0; i < 256; ++i)
            std::swap(permutation[i], permutation[i + random.bounded(256 - i)]);
    }
    int hash(int n) const { return permutation[unsigned(n) & 255]; }
    double value(double x, double y, double z) const {
        x += origin[0];
        y += origin[1];
        z += origin[2];
        const int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
        x -= ix;
        y -= iy;
        z -= iz;
        // Improved Perlin's 16-entry gradient set, including the repeated directions.
        static constexpr int gradients[16][3] = {
            {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
            {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};
        double corners[2][2][2];
        for (int dz = 0; dz < 2; ++dz)
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const auto& g = gradients[hash(hash(hash(ix + dx) + iy + dy) + iz + dz) & 15];
                    corners[dz][dy][dx] = g[0] * (x - dx) + g[1] * (y - dy) + g[2] * (z - dz);
                }
        const double fx = fade(x), fy = fade(y), fz = fade(z);
        return lerp(lerp(lerp(corners[0][0][0], corners[0][0][1], fx),
                         lerp(corners[0][1][0], corners[0][1][1], fx), fy),
                    lerp(lerp(corners[1][0][0], corners[1][0][1], fx),
                         lerp(corners[1][1][0], corners[1][1][1], fx), fy),
                    fz);
    }
};
struct Octaves {
    struct Layer {
        Perlin perlin;
        double frequency, weight;
    };
    std::vector<Layer> layers;
    Octaves(Random& random, int first, const std::vector<double>& weights) {
        const Pair parent = random.fork();
        double f = std::ldexp(1.0, first);
        double a = std::ldexp(1.0, int(weights.size()) - 1) / (std::ldexp(1.0, int(weights.size())) - 1);
        for (size_t i = 0; i < weights.size(); ++i, f *= 2, a *= 0.5)
            if (weights[i] != 0)
                layers.push_back(
                    {Perlin(named(parent, "octave_" + std::to_string(first + int(i)))), f, a * weights[i]});
    }
    double value(double x, double y, double z) const {
        double sum = 0;
        for (const auto& n : layers)
            sum += n.perlin.value(wrap(x * n.frequency), wrap(y * n.frequency), wrap(z * n.frequency)) *
                   n.weight;
        return sum;
    }
};
struct DoublePerlin {
    Octaves a, b;
    double gain;
    DoublePerlin(Random random, int first, const std::vector<double>& weights)
        : a(random, first, weights), b(random, first, weights) {
        size_t low = 0, high = weights.size() - 1;
        while (weights[low] == 0)
            ++low;
        while (weights[high] == 0)
            --high;
        gain = (1.0 / 6.0) / (0.1 * (1 + 1.0 / double(high - low + 1)));
    }
    double value(double x, double y, double z) const {
        constexpr double ratio = 1.0181268882175227;
        return (a.value(x, y, z) + b.value(x * ratio, y * ratio, z * ratio)) * gain;
    }
};
struct Curve {
    int axis{-1};
    float constant{};
    std::vector<float> locations, slopes;
    std::vector<Curve> children;
    explicit Curve(const nlohmann::json& j) {
        if (j.is_number()) {
            constant = j.get<float>();
            return;
        }
        axis = j.at("axis").get<int>();
        for (const auto& p : j.at("points")) {
            locations.push_back(p.at(0).get<float>());
            slopes.push_back(p.at(1).get<float>());
            children.emplace_back(p.at(2));
        }
    }
    float evaluate(const std::array<float, 4>& v) const {
        if (axis < 0)
            return constant;
        const float x = v[axis];
        const auto right = std::upper_bound(locations.begin(), locations.end(), x);
        if (right == locations.begin() || right == locations.end()) {
            const size_t i = right == locations.begin() ? 0 : locations.size() - 1;
            const float y = children[i].evaluate(v);
            return slopes[i] == 0 ? y : y + slopes[i] * (x - locations[i]);
        }
        const size_t i = size_t(right - locations.begin() - 1);
        const float dx = locations[i + 1] - locations[i], t = (x - locations[i]) / dx;
        const float a = children[i].evaluate(v), b = children[i + 1].evaluate(v);
        const float da = slopes[i] * dx - (b - a), db = -slopes[i + 1] * dx + (b - a);
        return (a + t * (b - a)) + t * (1 - t) * (da + t * (db - da));
    }
};
const std::array<Curve, 3>& curves() {
    static const auto data = nlohmann::json::parse(
#include "editor/minecraft_splines.inc"
    );
    static const std::array<Curve, 3> result{Curve(data.at("offset")), Curve(data.at("factor")),
                                             Curve(data.at("jaggedness"))};
    return result;
}
float evaluate(int field, double c, double e, double w, double pv) {
    return curves().at(size_t(field - 7)).evaluate({float(c), float(e), float(w), float(pv)});
}
} // namespace

int field_index(std::string_view name) {
    auto it = std::find(keys.begin(), keys.end(), name);
    if (it == keys.end())
        throw std::invalid_argument("알 수 없는 Minecraft 지도 종류");
    return int(it - keys.begin());
}
double peaks_valleys(double w) { return -(std::abs(std::abs(w) - 2.0 / 3.0) - 1.0 / 3.0) * 3.0; }
float spline(int field, float c, float e, float w) {
    if (field < 7 || field > 9)
        throw std::invalid_argument("스플라인 종류 오류");
    return evaluate(field, c, e, w, peaks_valleys(w));
}
struct Sampler::Impl {
    Pair parent;
    DoublePerlin shift, continents, erosion, ridges, temperature, humidity, jagged;
    explicit Impl(int64_t seed)
        : parent(Random(seed).fork()), shift(named(parent, "minecraft:offset"), -3, {1, 1, 1, 0}),
          continents(named(parent, "minecraft:continentalness"), -9, {1, 1, 2, 2, 2, 1, 1, 1, 1}),
          erosion(named(parent, "minecraft:erosion"), -9, {1, 1, 0, 1, 1}),
          ridges(named(parent, "minecraft:ridge"), -7, {1, 2, 1, 0, 0, 0}),
          temperature(named(parent, "minecraft:temperature"), -10, {1.5, 0, 1, 0, 0, 0}),
          humidity(named(parent, "minecraft:vegetation"), -8, {1, 1, 0, 0, 0, 0}),
          jagged(named(parent, "minecraft:jagged"), -16, std::vector<double>(16, 1)) {}
    std::array<double, 2> coordinates(int x, int z) const {
        return {x * .25 + shift.value(x * .25, 0, z * .25) * 4,
                z * .25 + shift.value(z * .25, x * .25, 0) * 4};
    }
};
Sampler::Sampler(int64_t seed) : impl_(std::make_unique<Impl>(seed)) {}
Sampler::~Sampler() = default;
double Sampler::sample(int field, int x, int z) const {
    const auto& n = *impl_;
    if (field == 6)
        return n.jagged.value(x * 1500.0, 0, z * 1500.0);
    const auto [a, b] = n.coordinates(x, z);
    switch (field) {
    case 0:
        return n.continents.value(a, 0, b);
    case 1:
        return n.erosion.value(a, 0, b);
    case 2:
        return n.ridges.value(a, 0, b);
    case 3:
        return peaks_valleys(n.ridges.value(a, 0, b));
    case 4:
        return n.temperature.value(a, 0, b);
    case 5:
        return n.humidity.value(a, 0, b);
    default:
        const double c = n.continents.value(a, 0, b), e = n.erosion.value(a, 0, b),
                     w = n.ridges.value(a, 0, b);
        return evaluate(field, c, e, w, peaks_valleys(w));
    }
}
std::array<double, 10> Sampler::probe(int x, int z) const {
    const auto& n = *impl_;
    const auto [a, b] = n.coordinates(x, z);
    std::array<double, 10> v{n.continents.value(a, 0, b), n.erosion.value(a, 0, b), n.ridges.value(a, 0, b)};
    v[3] = peaks_valleys(v[2]);
    v[4] = n.temperature.value(a, 0, b);
    v[5] = n.humidity.value(a, 0, b);
    v[6] = n.jagged.value(x * 1500.0, 0, z * 1500.0);
    for (int i = 7; i < 10; ++i)
        v[i] = evaluate(i, v[0], v[1], v[2], v[3]);
    return v;
}
} // namespace sandbox::minecraft_reference
