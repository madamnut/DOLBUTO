#include "editor/voronoi_preview.hpp"
#include "core/random_seed.hpp"
#include "core/world_rules.hpp"
#include "world/periodic_noise.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sandbox::editor {
namespace {
using Json = nlohmann::ordered_json;
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
double number(const Json& j, const char* key, double lo, double hi, bool integer = false) {
    const auto& v = j.at(key);
    if (!v.is_number())
        throw std::invalid_argument(std::string("숫자가 필요합니다: ") + key);
    const double n = v.get<double>();
    if (!std::isfinite(n) || n < lo || n > hi || (integer && n != std::floor(n)))
        throw std::invalid_argument(std::string("값 범위 확인: ") + key);
    return n;
}
Json parameters(const Json& input, bool legacy) {
    Json p;
    struct Limit {
        const char* key;
        double lo, hi;
        bool integer = false;
    };
    for (const auto& l :
         std::array{Limit{"seed", 0, UINT32_MAX, true}, Limit{"spacing", 512, 32768}, Limit{"jitter", 0, 1},
                    Limit{"warp_strength", 0, 8192}, Limit{"warp_spacing_log2", 2, 17, true},
                    Limit{"warp_octaves", 1, 8, true}, Limit{"warp_gain", 0, 1}})
        p[l.key] = number(input, l.key, l.lo, l.hi, l.integer);
    if (!input.at("warp_enabled").is_boolean())
        throw std::invalid_argument("워핑 값은 켜기/끄기여야 합니다.");
    p["warp_enabled"] = input.at("warp_enabled");
    p["growth_seed_count"] = legacy ? 32 : int(number(input, "growth_seed_count", 1, 4096, true));
    p["growth_percent"] = legacy ? 50.0 : number(input, "growth_percent", 0, 100);
    p["growth_steps"] = legacy ? 12 : int(number(input, "growth_steps", 0, 100, true));
    for (const auto& l :
         std::array{Limit{"growth_noise_strength", 0, 24}, Limit{"growth_large_log2", 9, 17, true},
                    Limit{"growth_small_log2", 9, 17, true}, Limit{"growth_detail_mix", 0, 1}}) {
        const double fallback = std::string_view(l.key) == "growth_noise_strength" ? 8
                                : std::string_view(l.key) == "growth_large_log2"   ? 14
                                : std::string_view(l.key) == "growth_small_log2"   ? 12
                                                                                   : .25;
        const double value = input.contains(l.key) ? number(input, l.key, l.lo, l.hi, l.integer) : fallback;
        if (l.integer)
            p[l.key] = int(value);
        else
            p[l.key] = value;
    }
    if (input.contains("growth_noise_enabled") && !input.at("growth_noise_enabled").is_boolean())
        throw std::invalid_argument("성장 지도 사용 값은 켜기/끄기여야 합니다.");
    p["growth_noise_enabled"] = input.value("growth_noise_enabled", true);
    struct TrendLimit {
        const char* key;
        double fallback, lo, hi;
        bool integer = false;
    };
    for (const auto& h : std::array{
             TrendLimit{"trend_distance", 8192, 1, 131072}, TrendLimit{"trend_distance_strength", .45, 0, 1},
             TrendLimit{"trend_shared_strength", .65, 0, 2}, TrendLimit{"trend_local_strength", .15, 0, 1},
             TrendLimit{"trend_spacing_log2", 14, 9, 17, true}, TrendLimit{"trend_neighbor_mix", .55, 0, 1},
             TrendLimit{"trend_neighbor_passes", 3, 0, 12, true}, TrendLimit{"trend_compression", .45, 0, 1},
             TrendLimit{"trend_compression_variation", .35, 0, 1},
             TrendLimit{"trend_transition", .65, .05, 1}}) {
        const double v = input.contains(h.key) ? number(input, h.key, h.lo, h.hi, h.integer) : h.fallback;
        if (h.integer)
            p[h.key] = int(v);
        else
            p[h.key] = v;
    }
    p["seed"] = uint32_t(p["seed"].get<double>());
    for (const auto key : {"warp_spacing_log2", "warp_octaves", "growth_steps"})
        p[key] = int(p[key].get<double>());
    return p;
}
struct Point {
    double x, z;
};
int wrap(int v, int n) { return (v % n + n) % n; }
struct Grid {
    int count;
    double spacing, jitter;
    uint32_t seed;
    std::vector<Point> points; // canonical positions in unit stratum coordinates
    Grid(int n, double j, uint32_t s) : count(n), spacing(double(world_size) / n), jitter(j), seed(s) {
        points.resize(size_t(n) * n);
        const auto stream = derive_seed(seed, SeedDomain::region_sites);
        for (int z = 0; z < n; ++z)
            for (int x = 0; x < n; ++x) {
                const uint32_t id = uint32_t(x + n * z);
                const double a = (double(mix_seed(stream ^ id ^ 0xa511e9b3U)) + .5) / 4294967296.0;
                const double b = (double(mix_seed(stream ^ id ^ 0x63d83595U)) + .5) / 4294967296.0;
                points[id] = {x + .5 + (a - .5) * j, z + .5 + (b - .5) * j};
            }
    }
    uint32_t id(int x, int z) const { return uint32_t(wrap(x, count) + count * wrap(z, count)); }
    Point point(int x, int z) const {
        const int wx = wrap(x, count), wz = wrap(z, count);
        const auto p = points[size_t(wx + count * wz)];
        return {p.x + x - wx, p.z + z - wz};
    }
    std::pair<uint32_t, double> nearest(Point p) const {
        p.x /= spacing;
        p.z /= spacing;
        const int bx = int(std::floor(p.x)), bz = int(std::floor(p.z));
        uint32_t best = 0;
        double distance = INFINITY;
        for (int z = bz - 2; z <= bz + 2; ++z)
            for (int x = bx - 2; x <= bx + 2; ++x) {
                const auto q = point(x, z);
                const double dx = p.x - q.x, dz = p.z - q.z, d = dx * dx + dz * dz;
                const auto i = id(x, z);
                if (d < distance || (d == distance && i < best)) {
                    best = i;
                    distance = d;
                }
            }
        return {best, std::sqrt(distance)};
    }
};
struct BoundarySegment {
    Point a, b;
    uint32_t cell, neighbor;
};
struct Geometry {
    std::vector<BoundarySegment> boundaries;
    Grid grid;
    std::vector<std::vector<uint32_t>> neighbors;
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    std::vector<double> areas;
    Geometry(int n, double jitter, uint32_t seed)
        : grid(n, jitter, seed), neighbors(size_t(n) * n), areas(size_t(n) * n) {
        // Every location has a site in its own stratum at distance <=sqrt(2).
        // Thus a Voronoi cell lies within sqrt(2) of its site and only sites <2sqrt(2)
        // away can bound it. Strata outside +/-3 are at least3 away, so7x7 is sufficient.
        constexpr uint32_t none = UINT32_MAX;
        struct Vertex {
            Point p;
            uint32_t incoming;
        };
        std::vector<Vertex> polygon, next;
        polygon.reserve(24);
        next.reserve(24);
        for (int z = 0; z < n; ++z)
            for (int x = 0; x < n; ++x) {
                const auto id = grid.id(x, z);
                const auto center = grid.point(x, z);
                polygon = {{{-2, -2}, none}, {{2, -2}, none}, {{2, 2}, none}, {{-2, 2}, none}};
                for (int oz = -3; oz <= 3; ++oz)
                    for (int ox = -3; ox <= 3; ++ox) {
                        if (ox == 0 && oz == 0)
                            continue;
                        const auto other = grid.point(x + ox, z + oz);
                        const Point d{other.x - center.x, other.z - center.z};
                        const double distance = d.x * d.x + d.z * d.z;
                        if (distance > 8.000000001)
                            continue;
                        const double limit = distance * .5;
                        const auto neighbor = grid.id(x + ox, z + oz);
                        next.clear();
                        auto previous = polygon.back();
                        double ps = previous.p.x * d.x + previous.p.z * d.z - limit;
                        for (const auto& v : polygon) {
                            const double s = v.p.x * d.x + v.p.z * d.z - limit;
                            if ((ps <= 0) != (s <= 0)) {
                                const double t = ps / (ps - s);
                                next.push_back({{previous.p.x + (v.p.x - previous.p.x) * t,
                                                 previous.p.z + (v.p.z - previous.p.z) * t},
                                                ps <= 0 ? v.incoming : neighbor});
                            }
                            if (s <= 0)
                                next.push_back(v);
                            previous = v;
                            ps = s;
                        }
                        polygon.swap(next);
                        if (polygon.empty())
                            throw std::runtime_error("보로노이 셀 구성 실패");
                    }
                double twice_area = 0;
                auto previous = polygon.back();
                for (const auto& v : polygon) {
                    twice_area += previous.p.x * v.p.z - v.p.x * previous.p.z;
                    const double length = std::hypot(v.p.x - previous.p.x, v.p.z - previous.p.z);
                    if (length > 1e-9) {
                        if (v.incoming == none)
                            throw std::runtime_error("보로노이 경계 구성 실패");
                        if (v.incoming != id)
                            edges.emplace_back(std::min(id, v.incoming), std::max(id, v.incoming));
                        if (id < v.incoming)
                            boundaries.push_back(
                                {{(center.x + previous.p.x) * grid.spacing,
                                  (center.z + previous.p.z) * grid.spacing},
                                 {(center.x + v.p.x) * grid.spacing, (center.z + v.p.z) * grid.spacing},
                                 id,
                                 v.incoming});
                    }
                    previous = v;
                }
                areas[id] = std::abs(twice_area) * .5 / (double(n) * n);
            }
        std::sort(edges.begin(), edges.end());
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
        for (const auto& [a, b] : edges) {
            neighbors[a].push_back(b);
            neighbors[b].push_back(a);
        }
        for (const auto& list : neighbors)
            if (list.empty())
                throw std::runtime_error("이웃이 없는 보로노이 셀");
    }
};
// Exact Euclidean point-to-coast distance on the unwarped periodic polygon map.
struct CoastMap {
    struct Segment {
        Point a, b;
    };
    struct Node {
        Point lo, hi;
        size_t begin, end;
        int left = -1, right = -1;
    };
    std::vector<Segment> segments;
    std::vector<Node> nodes;
    std::vector<float> site_distances;
    double max_land = 0, max_sea = 0;
    static double box_distance(Point p, const Node& n) {
        const double x = std::max({n.lo.x - p.x, 0.0, p.x - n.hi.x});
        const double z = std::max({n.lo.z - p.z, 0.0, p.z - n.hi.z});
        return x * x + z * z;
    }
    int build(size_t begin, size_t end) {
        Node node{{INFINITY, INFINITY}, {-INFINITY, -INFINITY}, begin, end};
        for (size_t i = begin; i < end; ++i)
            for (auto p : {segments[i].a, segments[i].b}) {
                node.lo.x = std::min(node.lo.x, p.x);
                node.lo.z = std::min(node.lo.z, p.z);
                node.hi.x = std::max(node.hi.x, p.x);
                node.hi.z = std::max(node.hi.z, p.z);
            }
        const int id = int(nodes.size());
        nodes.push_back(node);
        if (end - begin > 8) {
            const bool x = node.hi.x - node.lo.x >= node.hi.z - node.lo.z;
            const size_t middle = (begin + end) / 2;
            std::nth_element(segments.begin() + begin, segments.begin() + middle, segments.begin() + end,
                             [&](const auto& a, const auto& b) {
                                 return x ? a.a.x + a.b.x < b.a.x + b.b.x : a.a.z + a.b.z < b.a.z + b.b.z;
                             });
            const int left = build(begin, middle), right = build(middle, end);
            nodes[id].left = left;
            nodes[id].right = right;
        }
        return id;
    }
    void nearest(int id, Point p, double& best) const {
        const auto& node = nodes[size_t(id)];
        if (box_distance(p, node) >= best)
            return;
        if (node.left >= 0) {
            int first = node.left, second = node.right;
            if (box_distance(p, nodes[size_t(second)]) < box_distance(p, nodes[size_t(first)]))
                std::swap(first, second);
            nearest(first, p, best);
            nearest(second, p, best);
            return;
        }
        for (size_t i = node.begin; i < node.end; ++i) {
            const auto& s = segments[i];
            const double dx = s.b.x - s.a.x, dz = s.b.z - s.a.z;
            const double t =
                std::clamp(((p.x - s.a.x) * dx + (p.z - s.a.z) * dz) / (dx * dx + dz * dz), 0.0, 1.0);
            const double x = p.x - s.a.x - t * dx, z = p.z - s.a.z - t * dz;
            best = std::min(best, x * x + z * z);
        }
    }
    double distance(Point p) const {
        if (nodes.empty())
            return -1; // no coastline: undefined, never a fabricated zero
        p = {wrap_position(p.x), wrap_position(p.z)};
        double best = INFINITY;
        nearest(0, p, best);
        for (int z = -1; z <= 1; ++z)
            for (int x = -1; x <= 1; ++x)
                if (x || z)
                    nearest(0, {p.x + x * world_size, p.z + z * world_size}, best);
        return std::sqrt(best);
    }
    void prepare(const Geometry& g, const std::vector<uint32_t>& owners) {
        segments.clear();
        nodes.clear();
        max_land = max_sea = 0;
        for (const auto& edge : g.boundaries)
            if (bool(owners[edge.cell]) != bool(owners[edge.neighbor])) {
                Point a = edge.a, b = edge.b;
                // Center each segment's periodic image in the canonical tile. Nearest copies are +/-one tile.
                const double sx = std::floor((a.x + b.x) * .5 / world_size) * world_size;
                const double sz = std::floor((a.z + b.z) * .5 / world_size) * world_size;
                a.x -= sx;
                b.x -= sx;
                a.z -= sz;
                b.z -= sz;
                segments.push_back({a, b});
            }
        if (!segments.empty()) {
            nodes.reserve(segments.size() * 2);
            build(0, segments.size());
        }
        site_distances.resize(owners.size());
        for (size_t i = 0; i < owners.size(); ++i) {
            const auto p = g.grid.points[i];
            const double d = distance({p.x * g.grid.spacing, p.z * g.grid.spacing});
            site_distances[i] = float(d);
            if (d >= 0) {
                auto& maximum = owners[i] ? max_land : max_sea;
                maximum = std::max(maximum, d);
            }
        }
    }
};
// Cached center values and common boundary constraints, independent of viewport/warp.
struct TrendMap {
    struct Value {
        double altitude = 0, compression = 0;
    };
    struct Vertex {
        Point p;
        std::array<Value, 2> value; // sea and land constraints at the same position
    };
    struct Edge {
        uint32_t cell, neighbor;
        size_t a, b;
        Point middle;
        std::array<Value, 2> value; // sea and land constraints at the same position
    };
    std::vector<Value> centers;
    std::vector<Vertex> vertices;
    std::vector<Edge> edges;
    std::vector<std::vector<size_t>> cell_edges;
    static double delta(double x) { return x - std::round(x / world_size) * world_size; }
    static double canonical(double x) { return x - std::floor(x / world_size) * world_size; }
    static Value blend(Value a, Value b, double t) {
        return {a.altitude + (b.altitude - a.altitude) * t,
                a.compression + (b.compression - a.compression) * t};
    }
    Value vertex_value(const Geometry& g, Point p, bool land) const {
        // Smooth compact support; support radius2 cells, search +/-3 strata.
        // Same-type cells share values; sea and land retain independent constraints.
        Value v;
        double sum = 0;
        const int x = int(std::floor(p.x / g.grid.spacing)), z = int(std::floor(p.z / g.grid.spacing));
        for (int oz = -3; oz <= 3; ++oz)
            for (int ox = -3; ox <= 3; ++ox) {
                const auto q = g.grid.point(x + ox, z + oz);
                const double r = std::hypot(q.x - p.x / g.grid.spacing, q.z - p.z / g.grid.spacing) / 2;
                if (r >= 1)
                    continue;
                const double w = std::pow(1 - r, 4) * (1 + 4 * r);
                const auto c = centers[g.grid.id(x + ox, z + oz)];
                if ((c.altitude > 0) != land)
                    continue;
                sum += w;
                v.altitude += w * c.altitude;
                v.compression += w * c.compression;
            }
        // A type absent from this support cannot own a cell meeting this vertex.
        return sum > 0 ? Value{v.altitude / sum, v.compression / sum} : Value{};
    }
    void prepare(const Geometry& g, const std::vector<uint32_t>& owners, const CoastMap& coast,
                 const Json& p) {
        const auto n = owners.size();
        const auto seed = p["seed"].get<uint32_t>();
        PeriodicNoise broad(NoiseSettings{p["trend_spacing_log2"].get<int>(), 2, .5f, 0, {}},
                            derive_seed(seed, SeedDomain::terrain_tendency));
        PeriodicNoise sea_broad(NoiseSettings{p["trend_spacing_log2"].get<int>(), 2, .5f, 0, {}},
                                derive_seed(seed, SeedDomain::sea_tendency));
        PeriodicNoise compression(NoiseSettings{p["trend_spacing_log2"].get<int>(), 2, .5f, 0, {}},
                                  derive_seed(seed, SeedDomain::terrain_compression));
        std::vector<double> xs(n), zs(n);
        std::vector<float> noise(n), sea_noise(n), squash(n);
        for (size_t i = 0; i < n; ++i) {
            xs[i] = g.grid.points[i].x * g.grid.spacing;
            zs[i] = g.grid.points[i].z * g.grid.spacing;
        }
        broad.sample(noise, xs, {}, zs);
        sea_broad.sample(sea_noise, xs, {}, zs);
        compression.sample(squash, xs, {}, zs);
        centers.resize(n);
        const auto local = derive_seed(seed, SeedDomain::terrain_local);
        const auto sea_local = derive_seed(seed, SeedDomain::sea_local);
        for (size_t i = 0; i < n; ++i) {
            const double d = coast.site_distances[i],
                         distance = d < 0 ? 1 : 1 - std::exp(-d / p["trend_distance"].get<double>());
            const double jitter =
                double(mix_seed((owners[i] ? local : sea_local) ^ uint32_t(i)) >> 8) * 0x1p-23 - 1;
            centers[i] = {std::clamp(.12 + p["trend_distance_strength"].get<double>() * distance +
                                         p["trend_shared_strength"].get<double>() *
                                             (owners[i] ? noise[i] : sea_noise[i]) +
                                         p["trend_local_strength"].get<double>() * jitter,
                                     .01, 1.0),
                          std::clamp(p["trend_compression"].get<double>() +
                                         p["trend_compression_variation"].get<double>() * squash[i],
                                     0.0, 1.0)};
        }
        const double mix = p["trend_neighbor_mix"].get<double>();
        std::vector<Value> next(n);
        for (int pass = 0; pass < p["trend_neighbor_passes"].get<int>(); ++pass) {
            for (size_t i = 0; i < n; ++i) {
                Value mean;
                size_t count = 0;
                for (auto j : g.neighbors[i])
                    if (bool(owners[i]) == bool(owners[j])) {
                        mean.altitude += centers[j].altitude;
                        mean.compression += centers[j].compression;
                        ++count;
                    }
                next[i] = count ? blend(centers[i], {mean.altitude / count, mean.compression / count}, mix)
                                : centers[i];
            }
            centers.swap(next);
        }
        for (size_t i = 0; i < n; ++i)
            if (!owners[i])
                centers[i].altitude = -centers[i].altitude;
        vertices.clear();
        edges.clear();
        cell_edges.assign(n, {});
        // Quantization only identifies coincident clipped vertices, not stored values.
        // Search adjacent buckets so rounding cannot split a shared endpoint.
        constexpr double quantum = .001;
        const int64_t buckets = int64_t(world_size / quantum);
        std::map<std::pair<int64_t, int64_t>, std::vector<size_t>> lookup;
        auto vertex = [&](Point point) {
            point = {canonical(point.x), canonical(point.z)};
            const int64_t kx = int64_t(std::floor(point.x / quantum)),
                          kz = int64_t(std::floor(point.z / quantum));
            for (int oz = -1; oz <= 1; ++oz)
                for (int ox = -1; ox <= 1; ++ox) {
                    auto it = lookup.find({(kx + ox + buckets) % buckets, (kz + oz + buckets) % buckets});
                    if (it == lookup.end())
                        continue;
                    for (auto id : it->second)
                        if (std::hypot(delta(vertices[id].p.x - point.x), delta(vertices[id].p.z - point.z)) <
                            1e-5)
                            return id;
                }
            const size_t id = vertices.size();
            vertices.push_back({point, {vertex_value(g, point, false), vertex_value(g, point, true)}});
            lookup[{kx, kz}].push_back(id);
            return id;
        };
        for (const auto& b : g.boundaries) {
            const auto a = vertex(b.a), c = vertex(b.b);
            const auto aa = vertices[a].p, bb = vertices[c].p;
            const Point middle{canonical(aa.x + delta(bb.x - aa.x) * .5),
                               canonical(aa.z + delta(bb.z - aa.z) * .5)};
            const auto index = edges.size();
            std::array<Value, 2> values{};
            const auto left = centers[b.cell], right = centers[b.neighbor];
            const bool left_land = left.altitude > 0, right_land = right.altitude > 0;
            if (left_land == right_land)
                values[left_land] = blend(left, right, .5);
            else {
                values[left_land] = left;
                values[right_land] = right;
            }
            edges.push_back({b.cell, b.neighbor, a, c, middle, values});
            cell_edges[b.cell].push_back(index);
            cell_edges[b.neighbor].push_back(index);
        }
    }
    Value sample(const Geometry& g, uint32_t id, Point p, double width) const {
        const Point center{g.grid.points[id].x * g.grid.spacing, g.grid.points[id].z * g.grid.spacing};
        const bool land = centers[id].altitude > 0;
        const Point q{delta(p.x - center.x), delta(p.z - center.z)};
        if (std::hypot(q.x, q.z) < 1e-9)
            return centers[id];
        // Fan triangles: center -> edge midpoint -> each common vertex.
        for (auto index : cell_edges[id]) {
            const auto& e = edges[index];
            const Point a{delta(e.middle.x - center.x), delta(e.middle.z - center.z)};
            for (auto vi : {e.a, e.b}) {
                const auto& v = vertices[vi];
                const Point b{delta(v.p.x - center.x), delta(v.p.z - center.z)};
                const double det = a.x * b.z - a.z * b.x;
                if (std::abs(det) < 1e-12)
                    continue;
                const double u = (q.x * b.z - q.z * b.x) / det, t = (a.x * q.z - a.z * q.x) / det,
                             c = 1 - u - t;
                if (u < -1e-7 || t < -1e-7 || c < -1e-7)
                    continue;
                const double sum = std::max(1e-12, u + t), fraction = std::clamp(t / sum, 0.0, 1.0);
                double f = std::clamp(c / width, 0.0, 1.0);
                f = f * f * (3 - 2 * f);
                return blend(blend(e.value[land], v.value[land], fraction), centers[id], f);
            }
        }
        return vertex_value(g, p, land); // numerical degeneracy fallback, continuous shared field
    }
    size_t bytes() const {
        size_t total = centers.size() * sizeof(Value) + vertices.size() * sizeof(Vertex) +
                       edges.size() * sizeof(Edge) + cell_edges.size() * sizeof(std::vector<size_t>);
        for (const auto& v : cell_edges)
            total += v.size() * sizeof(size_t);
        return total;
    }
};
struct GrowthState {
    // Zero is sea. Nonzero owner is a representative original seed cell ID + 1.
    std::vector<uint32_t> owner;
    std::vector<uint32_t> seeds;
};
struct Cache {
    std::unique_ptr<Geometry> geometry;
    CoastMap coast;
    TrendMap trends;
    Json trend_key;
    int coast_step = -1;
    int seed_count = -1;
    double growth = -1;
    Json modulation;
    std::vector<double> probabilities;
    std::vector<float> large_field, small_field;
    std::vector<uint32_t> priority;
    std::vector<GrowthState> history;
};
void prepare_growth_map(Cache& c, const Geometry& g, const Json& settings, double base) {
    const auto size = g.grid.points.size();
    std::vector<double> xs(size), zs(size);
    for (size_t i = 0; i < size; ++i) {
        xs[i] = g.grid.points[i].x * g.grid.spacing;
        zs[i] = g.grid.points[i].z * g.grid.spacing;
    }
    c.large_field.resize(size);
    c.small_field.resize(size);
    c.probabilities.resize(size);
    const NoiseSettings large{settings["growth_large_log2"].get<int>(), 2, .5f, 0, {}};
    const NoiseSettings small{settings["growth_small_log2"].get<int>(), 2, .5f, 0, {}};
    PeriodicNoise(large, derive_seed(g.grid.seed, SeedDomain::growth_large))
        .sample(c.large_field, xs, {}, zs);
    PeriodicNoise(small, derive_seed(g.grid.seed, SeedDomain::growth_small))
        .sample(c.small_field, xs, {}, zs);
    const double strength =
        settings["growth_noise_enabled"].get<bool>() ? settings["growth_noise_strength"].get<double>() : 0;
    const double detail = settings["growth_detail_mix"].get<double>();
    for (size_t i = 0; i < size; ++i) {
        if (base <= 0 || base >= 100 || strength == 0) {
            c.probabilities[i] = base / 100;
            continue;
        }
        const double signal = (1 - detail) * c.large_field[i] + detail * c.small_field[i];
        // Shift the odds smoothly: all interior probabilities stay positive, no permanent barriers.
        const double p = base / 100;
        c.probabilities[i] = p / (p + (1 - p) * std::exp(-strength * signal));
    }
}
bool preferred(uint32_t a, uint32_t b, const std::vector<uint32_t>& priority) {
    return priority[a - 1] < priority[b - 1] || (priority[a - 1] == priority[b - 1] && a < b);
}
GrowthState initial_growth(const Geometry& g, int requested, const std::vector<uint32_t>& priority) {
    const size_t size = g.grid.points.size();
    GrowthState state{std::vector<uint32_t>(size), std::vector<uint32_t>(size + 1)};
    std::vector<uint32_t> order(size);
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return preferred(a + 1, b + 1, priority); });
    int placed = 0;
    for (auto i : order) {
        if (placed == requested)
            break;
        // A sea-cell gap prevents initial unprocessed continent contacts, including seams.
        if (std::any_of(g.neighbors[i].begin(), g.neighbors[i].end(),
                        [&](auto n) { return state.owner[n] != 0; }))
            continue;
        state.owner[i] = i + 1;
        state.seeds[i + 1] = 1;
        ++placed;
    }
    return state;
}
GrowthState advance_growth(const Geometry& g, const GrowthState& previous,
                           std::span<const double> probabilities, uint32_t seed, uint32_t step,
                           const std::vector<uint32_t>& priority) {
    GrowthState next = previous;
    const size_t size = previous.owner.size();
    const auto stream = derive_seed(seed, SeedDomain::land_growth) ^ mix_seed(step);
    // Proposals read only previous owners: no within-step multi-cell growth.
    for (size_t i = 0; i < size; ++i) {
        if (previous.owner[i] || probabilities[i] <= 0 ||
            double(mix_seed(stream ^ uint32_t(i)) >> 8) * 0x1p-24 >= probabilities[i])
            continue;
        uint32_t donor = 0;
        for (auto n : g.neighbors[i]) {
            const auto candidate = previous.owner[n];
            if (candidate && (!donor || preferred(candidate, donor, priority)))
                donor = candidate;
        }
        next.owner[i] = donor;
    }
    std::vector<uint32_t> parent(size + 1);
    std::iota(parent.begin(), parent.end(), 0U);
    const auto find = [&](uint32_t id) {
        while (parent[id] != id) {
            parent[id] = parent[parent[id]];
            id = parent[id];
        }
        return id;
    };
    // All touching land merges; representative priority only keeps IDs stable.
    for (const auto& [a, b] : g.edges) {
        if (!next.owner[a] || !next.owner[b])
            continue;
        auto x = find(next.owner[a]), y = find(next.owner[b]);
        if (x == y)
            continue;
        if (preferred(y, x, priority))
            std::swap(x, y);
        parent[y] = x;
        next.seeds[x] += next.seeds[y];
        next.seeds[y] = 0;
    }
    for (auto& id : next.owner)
        if (id)
            id = find(id);
    return next;
}
// The editor server handles requests serially. Cache is editor-local and never touches the game.
Cache cache;
} // namespace
Json voronoi_settings(const Json& input) {
    if (input.at("format") != "dolbuto-voronoi-experiment")
        throw std::invalid_argument("보로노이 실험 파일이 아닙니다.");
    const int version = int(number(input, "version", 1, 6, true));
    auto p = parameters(input.at("parameters"), version < 5);
    Json range;
    for (const auto key : {"x0", "z0", "x1", "z1"})
        range[key] = number(input.at("range"), key, -world_size, 2 * world_size);
    for (const auto axis : {"x", "z"}) {
        const double length =
            range[std::string(axis) + "1"].get<double>() - range[std::string(axis) + "0"].get<double>();
        if (length < 1 || length > world_size)
            throw std::invalid_argument("범위 길이는 1~131072입니다.");
    }
    const int res = int(number(input, "resolution", 256, 1024, true));
    if (res != 256 && res != 512 && res != 1024)
        throw std::invalid_argument("해상도는 256/512/1024입니다.");
    auto display =
        input.value("display", Json{{"view", "land"}, {"edges", true}, {"sites", true}, {"auto", true}});
    const std::array views{"land",
                           "initial",
                           "random",
                           "neighbors",
                           "cells",
                           "edges",
                           "distance",
                           "continents",
                           "seed-count",
                           "growth-rate",
                           "growth-large",
                           "growth-small",
                           "coast-land",
                           "coast-sea",
                           "coast-both",
                           "trend-altitude",
                           "trend-compression",
                           "trend-altitude-smooth",
                           "trend-compression-smooth"};
    if (display.at("view") == "height")
        display["view"] = "trend-altitude";
    if (std::none_of(views.begin(), views.end(), [&](auto v) { return display.at("view") == v; })) {
        if (version < 5)
            display["view"] = "land";
        else
            throw std::invalid_argument("지원하지 않는 지도 표시입니다.");
    }
    Json clean;
    clean["view"] = display.at("view");
    clean["distance_scale"] =
        display.contains("distance_scale") ? number(display, "distance_scale", 1, world_size) : 8192.0;
    for (const auto key : {"edges", "sites", "auto"}) {
        if (!display.at(key).is_boolean())
            throw std::invalid_argument("잘못된 지도 표시 설정");
        clean[key] = display[key];
    }
    return {{"format", "dolbuto-voronoi-experiment"},
            {"version", 6},
            {"parameters", p},
            {"range", range},
            {"resolution", res},
            {"display", clean}};
}
std::vector<float> voronoi_preview(const Json& request) {
    const auto started = Clock::now();
    const auto p = parameters(request.at("parameters"), false);
    const auto seed = uint32_t(p["seed"].get<double>());
    const double jitter = p["jitter"].get<double>();
    const int count = std::clamp(int(std::lround(world_size / p["spacing"].get<double>())), 4, 256);
    const int seed_count = p["growth_seed_count"].get<int>();
    const double growth = p["growth_percent"].get<double>();
    const int steps = p["growth_steps"].get<int>(),
              resolution = int(number(request, "resolution", 32, 1024, true));
    const double x0 = number(request, "x0", -world_size, 2 * world_size),
                 z0 = number(request, "z0", -world_size, 2 * world_size),
                 x1 = number(request, "x1", -world_size, 2 * world_size),
                 z1 = number(request, "z1", -world_size, 2 * world_size);
    const double dx = x1 - x0, dz = z1 - z0;
    const bool with_trends = request.value("trend_map", false);
    const bool interpolate = request.value("interpolate", false);
    if (dx < 1 || dz < 1 || dx > world_size || dz > world_size)
        throw std::invalid_argument("범위 길이는 1~131072입니다.");
    const bool geometry_hit = cache.geometry && cache.geometry->grid.count == count &&
                              cache.geometry->grid.jitter == jitter && cache.geometry->grid.seed == seed;
    double geometry_ms = 0, ca_ms = 0;
    if (!geometry_hit) {
        const auto t = Clock::now();
        cache.geometry = std::make_unique<Geometry>(count, jitter, seed);
        cache.history.clear();
        cache.coast_step = -1;
        geometry_ms = elapsed(t);
    }
    const auto& g = *cache.geometry;
    const size_t sites = g.grid.points.size();
    Json modulation;
    for (const auto key : {"growth_noise_enabled", "growth_noise_strength", "growth_large_log2",
                           "growth_small_log2", "growth_detail_mix"})
        modulation[key] = p[key];
    const bool same_rules = cache.seed_count == seed_count && cache.growth == growth &&
                            cache.modulation == modulation && !cache.history.empty();
    const bool ca_hit = same_rules && cache.history.size() > size_t(steps);
    if (!ca_hit) {
        const auto t = Clock::now();
        if (!same_rules) {
            cache.seed_count = seed_count;
            cache.growth = growth;
            cache.modulation = modulation;
            prepare_growth_map(cache, g, modulation, growth);
            cache.history.clear();
            cache.coast_step = -1;
            cache.priority.resize(sites);
            const auto stream = derive_seed(seed, SeedDomain::land_seeds);
            for (size_t i = 0; i < sites; ++i)
                cache.priority[i] = mix_seed(stream ^ uint32_t(i));
            cache.history.push_back(initial_growth(g, seed_count, cache.priority));
        }
        while (cache.history.size() <= size_t(steps))
            cache.history.push_back(advance_growth(g, cache.history.back(), cache.probabilities, seed,
                                                   uint32_t(cache.history.size()), cache.priority));
        ca_ms = elapsed(t);
    }
    const auto& frame = cache.history[size_t(steps)];
    const auto& state = frame.owner;
    const bool with_coast = request.value("coast_distance", true);
    const bool need_coast = with_trends || with_coast;
    const bool coast_hit = need_coast && cache.coast_step == steps;
    double coast_ms = 0;
    if (need_coast && !coast_hit) {
        const auto t = Clock::now();
        cache.coast.prepare(g, state);
        cache.coast_step = steps;
        coast_ms = elapsed(t);
    }
    Json trend_key = {{"seed", seed},
                      {"count", count},
                      {"jitter", jitter},
                      {"steps", steps},
                      {"seed_count", seed_count},
                      {"growth", growth},
                      {"modulation", modulation}};
    for (auto it = p.begin(); it != p.end(); ++it)
        if (it.key().starts_with("trend_") && it.key() != "trend_transition")
            trend_key[it.key()] = it.value();
    const bool trend_hit = with_trends && cache.trend_key == trend_key;
    double trend_ms = 0;
    if (with_trends && !trend_hit) {
        const auto t = Clock::now();
        cache.trends.prepare(g, state, cache.coast, p);
        cache.trend_key = trend_key;
        trend_ms = elapsed(t);
    }
    const auto& first = cache.history.front().owner;
    const auto initial_seeds =
        std::accumulate(cache.history.front().seeds.begin(), cache.history.front().seeds.end(), 0U);
    const auto surviving_seeds = std::accumulate(frame.seeds.begin(), frame.seeds.end(), 0U);
    size_t land_cells = 0, components = 0;
    double land_area = 0, largest_area = 0;
    std::vector<uint8_t> visited(sites);
    std::vector<uint32_t> queue;
    for (uint32_t i = 0; i < sites; ++i) {
        if (state[i]) {
            ++land_cells;
            land_area += g.areas[i];
        }
        if (!state[i] || visited[i])
            continue;
        ++components;
        queue.clear();
        queue.push_back(i);
        visited[i] = 1;
        double area = 0;
        for (size_t at = 0; at < queue.size(); ++at) {
            const auto v = queue[at];
            area += g.areas[v];
            for (auto n : g.neighbors[v])
                if (state[n] && !visited[n]) {
                    visited[n] = 1;
                    queue.push_back(n);
                }
        }
        largest_area = std::max(largest_area, area);
    }
    const int width = std::max(2, int(resolution * dx / std::max(dx, dz))),
              height = std::max(2, int(resolution * dz / std::max(dx, dz)));
    const size_t site_offset = 40, pixel_offset = site_offset + sites * 16,
                 edge_offset = pixel_offset + size_t(width) * height * 5;
    const size_t boundary_offset = edge_offset + g.edges.size() * 2,
                 boundary_count = with_trends ? cache.trends.edges.size() : 0;
    std::vector<float> result(boundary_offset + boundary_count * 20);
    const std::array<float, 40> header{10,
                                       float(width),
                                       float(height),
                                       float(count),
                                       float(g.grid.spacing),
                                       float(sites),
                                       float(steps),
                                       0,
                                       float(geometry_ms),
                                       float(ca_ms),
                                       float(components),
                                       float(land_cells),
                                       float(land_area),
                                       float(land_area > 0 ? largest_area / land_area : 0),
                                       float(2.0 * g.edges.size() / sites),
                                       geometry_hit ? 1.0f : 0.0f,
                                       16,
                                       5,
                                       float(site_offset),
                                       float(pixel_offset),
                                       float(edge_offset),
                                       float(g.edges.size()),
                                       ca_hit ? 1.0f : 0.0f,
                                       float(std::accumulate(g.areas.begin(), g.areas.end(), 0.0)),
                                       float(initial_seeds),
                                       float(surviving_seeds),
                                       float(coast_ms),
                                       coast_hit ? 1.0f : 0.0f,
                                       need_coast ? float(cache.coast.segments.size()) : -1.0f,
                                       need_coast ? float(cache.coast.max_land) : -1.0f,
                                       need_coast ? float(cache.coast.max_sea) : -1.0f,
                                       with_coast ? 1.0f : 0.0f, // distance fields present
                                       with_trends ? 1.0f : 0.0f,
                                       float(trend_ms),
                                       trend_hit ? 1.0f : 0.0f,
                                       with_trends ? float(cache.trends.bytes()) : 0.0f,
                                       float(boundary_offset),
                                       float(boundary_count),
                                       20,
                                       float(p["trend_transition"].get<double>())};
    std::copy(header.begin(), header.end(), result.begin());
    for (size_t i = 0; i < sites; ++i) {
        const auto at = site_offset + 16 * i;
        unsigned land = 0;
        for (auto n : g.neighbors[i])
            land += state[n] != 0;
        result[at] = float(g.grid.points[i].x * g.grid.spacing);
        result[at + 1] = float(g.grid.points[i].z * g.grid.spacing);
        result[at + 2] = float(cache.priority[i] >> 8) * 0x1p-24f;
        result[at + 3] = float(first[i] != 0);
        result[at + 4] = float(state[i] != 0);
        result[at + 5] = float(g.neighbors[i].size());
        result[at + 6] = float(g.areas[i]);
        result[at + 7] = float(double(land) / g.neighbors[i].size());
        result[at + 8] = float(state[i]);
        result[at + 9] = float(frame.seeds[state[i]]);
        result[at + 10] = float(cache.probabilities[i]);
        result[at + 11] = cache.large_field[i];
        result[at + 12] = cache.small_field[i];
        result[at + 13] = need_coast ? cache.coast.site_distances[i] : -1.0f;
        result[at + 14] = with_trends ? float(cache.trends.centers[i].altitude) : 0;
        result[at + 15] = with_trends ? float(cache.trends.centers[i].compression) : 0;
    }
    for (size_t i = 0; i < g.edges.size(); ++i) {
        result[edge_offset + 2 * i] = float(g.edges[i].first);
        result[edge_offset + 2 * i + 1] = float(g.edges[i].second);
    }
    for (size_t i = 0; i < boundary_count; ++i) {
        const auto& e = cache.trends.edges[i];
        const auto a = cache.trends.vertices[e.a], b = cache.trends.vertices[e.b];
        const bool left = cache.trends.centers[e.cell].altitude > 0,
                   right = cache.trends.centers[e.neighbor].altitude > 0;
        const auto av = a.value[left], bv = b.value[left], mv = e.value[left];
        const auto ar = a.value[right], br = b.value[right], mr = e.value[right];
        const std::array<float, 20> record{
            float(e.cell),      float(e.neighbor),     float(a.p.x),       float(a.p.z),
            float(av.altitude), float(av.compression), float(b.p.x),       float(b.p.z),
            float(bv.altitude), float(bv.compression), float(e.middle.x),  float(e.middle.z),
            float(mv.altitude), float(mv.compression), float(ar.altitude), float(ar.compression),
            float(br.altitude), float(br.compression), float(mr.altitude), float(mr.compression)};
        std::copy(record.begin(), record.end(), result.begin() + boundary_offset + i * 20);
    }
    std::unique_ptr<PeriodicNoise> warp_x, warp_z;
    const double strength = p["warp_strength"].get<double>();
    if (p["warp_enabled"].get<bool>() && strength > 0) {
        const NoiseSettings n{int(p["warp_spacing_log2"].get<double>()),
                              int(p["warp_octaves"].get<double>()),
                              float(p["warp_gain"].get<double>()),
                              0,
                              {}};
        warp_x = std::make_unique<PeriodicNoise>(n, derive_seed(seed, SeedDomain::boundary_x));
        warp_z = std::make_unique<PeriodicNoise>(n, derive_seed(seed, SeedDomain::boundary_z));
    }
    std::vector<double> xs(width), zs(width), qx(width), qz(width);
    std::vector<float> wx(width), wz(width);
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            const double t = double(x) / (width - 1);
            xs[x] = wrap_position(x0 + dx * t);
            zs[x] = wrap_position(z0 + dz * z / (height - 1));
        }
        if (warp_x) {
            warp_x->sample(wx, xs, {}, zs);
            warp_z->sample(wz, xs, {}, zs);
        }
        for (int x = 0; x < width; ++x) {
            qx[x] = wrap_position(xs[x] + wx[x] * strength);
            qz[x] = wrap_position(zs[x] + wz[x] * strength);
        }
        for (int x = 0; x < width; ++x) {
            const Point query{qx[x], qz[x]};
            const auto [id, distance] = g.grid.nearest(query);
            const auto at = pixel_offset + 5 * (size_t(z) * width + x);
            const double coast = with_coast ? cache.coast.distance(query) : -1;
            result[at] = float(id);
            result[at + 1] = float(distance);
            result[at + 2] = float(coast);
            const auto trend =
                with_trends
                    ? (interpolate ? cache.trends.sample(g, id, query, p["trend_transition"].get<double>())
                                   : cache.trends.centers[id])
                    : TrendMap::Value{};
            result[at + 3] = float(trend.altitude);
            result[at + 4] = float(trend.compression);
        }
    }
    result[7] = float(elapsed(started));
    return result;
}
} // namespace sandbox::editor
