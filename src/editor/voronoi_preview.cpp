#include "editor/voronoi_preview.hpp"
#include "core/random_seed.hpp"
#include "core/world_rules.hpp"
#include "world/periodic_noise.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>

namespace sandbox::editor {
namespace {
using Json = nlohmann::ordered_json;
double number(const Json& j, const char* key, double lo, double hi, bool integral = false) {
    const auto& v = j.at(key);
    if (!v.is_number())
        throw std::invalid_argument(std::string("숫자가 필요합니다: ") + key);
    const double r = v.get<double>();
    if (!std::isfinite(r) || r < lo || r > hi || (integral && r != std::floor(r)))
        throw std::invalid_argument(std::string("보로노이 값 범위 확인: ") + key);
    return r;
}
struct Point {
    double x, z;
};
struct Hit {
    uint32_t id{};
    Point site{};
    double distance{std::numeric_limits<double>::infinity()};
};
int wrap(int v, int period) { return (v % period + period) % period; }
class Grid {
  public:
    int count;
    double spacing, jitter;
    uint32_t seed;
    Point point(int x, int z) const {
        const uint32_t id = uint32_t(wrap(x, count) + count * wrap(z, count));
        const double a = (double(mix_seed(seed ^ id ^ 0xa511e9b3U)) + .5) / 4294967296.0;
        const double b = (double(mix_seed(seed ^ id ^ 0x63d83595U)) + .5) / 4294967296.0;
        return {(x + .5 + (a - .5) * jitter) * spacing, (z + .5 + (b - .5) * jitter) * spacing};
    }
    Hit nearest(Point p) const {
        const int bx = int(std::floor(p.x / spacing)), bz = int(std::floor(p.z / spacing));
        Hit h;
        // Home site <sqrt(2) spacings; anything outside these strata is >=2 spacings away.
        for (int z = bz - 2; z <= bz + 2; ++z)
            for (int x = bx - 2; x <= bx + 2; ++x) {
                const auto s = point(x, z);
                const double dx = p.x - s.x, dz = p.z - s.z, d = dx * dx + dz * dz;
                const uint32_t id = uint32_t(wrap(x, count) + count * wrap(z, count));
                if (d < h.distance || (d == h.distance && id < h.id))
                    h = {id, s, d};
            }
        h.distance = std::sqrt(h.distance);
        return h;
    }
};
NoiseSettings broad_noise(double spacing) {
    NoiseSettings n{15, 3, .5f, 0, {}};
    n.spacings = {float(spacing), float(spacing / 2), float(spacing / 4)};
    return n;
}
// Continuous distance-gap proxy to the union's exterior, NOT distance to each parent-cell edge.
// Internal archipelago/archipelago boundaries never enter the exterior candidate set.
double edge_weight(Point p, const Grid& grid, const std::vector<int>& regions, double fade) {
    const auto nearest = grid.nearest(p);
    if (regions[nearest.id] != 2)
        return -1; // Fine site's center is outside the archipelago union.
    if (fade <= 0)
        return 1;
    const int bx = int(std::floor(p.x / grid.spacing)), bz = int(std::floor(p.z / grid.spacing));
    double exterior = std::numeric_limits<double>::infinity();
    // fade <= half a spacing. Beyond radius3 the distance gap already saturates this fade.
    for (int z = bz - 3; z <= bz + 3; ++z)
        for (int x = bx - 3; x <= bx + 3; ++x) {
            const int id = wrap(x, grid.count) + grid.count * wrap(z, grid.count);
            if (regions[id] == 2)
                continue;
            const auto s = grid.point(x, z);
            exterior = std::min(exterior, std::hypot(p.x - s.x, p.z - s.z));
        }
    const double t = std::clamp((exterior - nearest.distance) / (2 * fade), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
} // namespace
std::vector<float> voronoi_preview(const Json& request) {
    const auto start = std::chrono::steady_clock::now();
    const auto& p = request.at("parameters");
    const uint32_t seed = uint32_t(number(p, "seed", 0, UINT32_MAX, true));
    const int count = std::clamp(int(std::lround(world_size / number(p, "spacing", 512, 32768))), 4, 256);
    const double jitter = number(p, "jitter", 0, 1);
    const Grid coarse{count, double(world_size) / count, jitter, derive_seed(seed, SeedDomain::region_sites)};
    const int subdivisions = int(number(p, "subdivisions", 2, 8, true));
    const Grid fine{count * subdivisions, coarse.spacing / subdivisions, jitter,
                    derive_seed(seed, SeedDomain::fine_sites)};
    const bool enabled = p.at("arch_enabled").get<bool>(), warped = p.at("warp_enabled").get<bool>();
    const double land_spacing = number(p, "land_spacing", 512, world_size),
                 land_threshold = number(p, "land_threshold", -1, 1);
    const double arch_spacing = number(p, "arch_spacing", 512, world_size),
                 arch_threshold = number(p, "arch_threshold", -1, 1);
    const double island_spacing = number(p, "island_spacing", 64, world_size),
                 island_threshold = number(p, "island_threshold", -1, 1);
    const double fade = number(p, "arch_edge_fade", 0, .5) * coarse.spacing;
    const double strength = number(p, "warp_strength", 0, 8192), gain = number(p, "warp_gain", 0, 1);
    const int exponent = int(number(p, "warp_spacing_log2", 2, 17, true)),
              octaves = int(number(p, "warp_octaves", 1, 8, true));
    const int resolution = int(number(request, "resolution", 32, 1024, true));
    const double x0 = number(request, "x0", -world_size, 2 * world_size),
                 z0 = number(request, "z0", -world_size, 2 * world_size);
    const double x1 = number(request, "x1", -world_size, 2 * world_size),
                 z1 = number(request, "z1", -world_size, 2 * world_size);
    const double dx = x1 - x0, dz = z1 - z0;
    if (dx < 1 || dz < 1 || dx > world_size || dz > world_size)
        throw std::invalid_argument("범위 길이는 1~131072블록이어야 합니다.");
    const int width = std::max(2, int(resolution * dx / std::max(dx, dz))),
              height = std::max(2, int(resolution * dz / std::max(dx, dz)));
    const size_t sites = size_t(count) * count, pixels = size_t(width) * height;
    std::vector<double> cx(sites), cz(sites);
    std::vector<float> land(sites), arch(sites);
    std::vector<int> regions(sites);
    for (int z = 0; z < count; ++z)
        for (int x = 0; x < count; ++x) {
            const auto q = coarse.point(x, z);
            const auto id = size_t(x + count * z);
            cx[id] = q.x;
            cz[id] = q.z;
        }
    PeriodicNoise(broad_noise(land_spacing), derive_seed(seed, SeedDomain::continent))
        .sample(land, cx, {}, cz);
    if (enabled)
        PeriodicNoise(broad_noise(arch_spacing), derive_seed(seed, SeedDomain::archipelago))
            .sample(arch, cx, {}, cz);
    for (size_t i = 0; i < sites; ++i)
        regions[i] = land[i] > land_threshold ? 1 : (enabled && arch[i] > arch_threshold ? 2 : 0);
    std::unique_ptr<PeriodicNoise> warp_x, warp_z;
    if (warped && strength > 0) {
        const NoiseSettings n{exponent, octaves, float(gain), 0, {}};
        warp_x = std::make_unique<PeriodicNoise>(n, derive_seed(seed, SeedDomain::boundary_x));
        warp_z = std::make_unique<PeriodicNoise>(n, derive_seed(seed, SeedDomain::boundary_z));
    }
    struct Leaf {
        uint32_t id;
        Point center;
    };
    std::vector<Leaf> leaves;
    std::unordered_map<uint32_t, uint32_t> leaf_indices;
    std::vector<float> pixel_data(pixels * 4);
    std::vector<double> xs(width), zs(width);
    std::vector<float> wx(width), wz(width);
    for (int x = 0; x < width; ++x)
        xs[x] = wrap_position(x0 + dx * x / (width - 1));
    for (int z = 0; z < height; ++z) {
        const double raw_z = wrap_position(z0 + dz * z / (height - 1));
        if (warp_x) {
            std::fill(zs.begin(), zs.end(), raw_z);
            warp_x->sample(wx, xs, {}, zs);
            warp_z->sample(wz, xs, {}, zs);
        }
        for (int x = 0; x < width; ++x) {
            const Point q{wrap_position(xs[x] + wx[x] * strength), wrap_position(raw_z + wz[x] * strength)};
            const auto parent = coarse.nearest(q);
            const size_t at = 4 * (size_t(z) * width + x);
            pixel_data[at] = float(parent.id);
            pixel_data[at + 1] = -1;
            pixel_data[at + 2] = float(parent.distance / coarse.spacing);
            pixel_data[at + 3] = float(regions[parent.id]);
            if (regions[parent.id] != 2)
                continue;
            const auto child = fine.nearest(q);
            const auto [entry, inserted] = leaf_indices.try_emplace(child.id, uint32_t(leaves.size()));
            if (inserted)
                leaves.push_back(
                    {child.id, fine.point(int(child.id % fine.count), int(child.id / fine.count))});
            pixel_data[at + 1] = float(entry->second);
            pixel_data[at + 2] = float(child.distance / fine.spacing);
        }
    }
    std::vector<double> fx(leaves.size()), fz(leaves.size());
    std::vector<float> island(leaves.size()), weights(leaves.size());
    std::vector<bool> solid(leaves.size());
    for (size_t i = 0; i < leaves.size(); ++i) {
        fx[i] = leaves[i].center.x;
        fz[i] = leaves[i].center.z;
    }
    if (!leaves.empty())
        PeriodicNoise(broad_noise(island_spacing), derive_seed(seed, SeedDomain::island))
            .sample(island, fx, {}, fz);
    for (size_t i = 0; i < leaves.size(); ++i) {
        weights[i] = float(edge_weight(leaves[i].center, coarse, regions, fade));
        // Raise the threshold toward the exterior. Exact boundary and exterior centers remain water.
        solid[i] = weights[i] > 0 && island[i] > island_threshold + (1 - weights[i]) * 2;
    }
    for (size_t i = 0; i < pixels; ++i)
        if (pixel_data[4 * i + 1] >= 0)
            pixel_data[4 * i + 3] = solid[size_t(pixel_data[4 * i + 1])] ? 2.0f : 3.0f;
    const size_t coarse_offset = 16, leaf_offset = coarse_offset + 6 * sites,
                 pixel_offset = leaf_offset + 6 * leaves.size();
    std::vector<float> result(pixel_offset + pixel_data.size());
    const std::array<float, 16> header{3,
                                       float(width),
                                       float(height),
                                       float(count),
                                       float(coarse.spacing),
                                       float(sites),
                                       float(fine.count),
                                       float(fine.spacing),
                                       float(leaves.size()),
                                       0,
                                       6,
                                       6,
                                       4,
                                       float(coarse_offset),
                                       float(leaf_offset),
                                       float(pixel_offset)};
    std::copy(header.begin(), header.end(), result.begin());
    for (size_t i = 0; i < sites; ++i) {
        const size_t a = coarse_offset + 6 * i;
        result[a] = float(cx[i]);
        result[a + 1] = float(cz[i]);
        result[a + 2] = land[i];
        result[a + 3] = float(regions[i]);
        result[a + 4] = arch[i];
    }
    for (size_t i = 0; i < leaves.size(); ++i) {
        const size_t a = leaf_offset + 6 * i;
        result[a] = float(leaves[i].id);
        result[a + 1] = float(fx[i]);
        result[a + 2] = float(fz[i]);
        result[a + 3] = island[i];
        result[a + 4] = weights[i];
        result[a + 5] = solid[i] ? 1.0f : 0.0f;
    }
    std::copy(pixel_data.begin(), pixel_data.end(), result.begin() + pixel_offset);
    result[9] =
        float(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    return result;
}
} // namespace sandbox::editor
