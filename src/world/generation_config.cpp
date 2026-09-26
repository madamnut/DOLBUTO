#include "world/generation_config.hpp"
#include "core/world_rules.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace sandbox {
namespace {
void require(bool valid, const char* message) {
    if (!valid)
        throw std::invalid_argument(message);
}
using Json = nlohmann::ordered_json;
Json noise_json(const NoiseSettings& n, bool individual = false) {
    Json result{{"spacing_log2", n.spacing_log2},
                {"octaves", n.octaves},
                {"gain", n.gain},
                {"seed_offset", n.seed_offset},
                {"warp", n.warp},
                {"frequency_multiplier", n.frequency_multiplier}};
    if (individual || !n.weights.empty())
        result["weights"] = octave_weights(n);
    if (!n.spacings.empty())
        result["spacings"] = n.spacings;
    return result;
}
int integer(const Json& j, const char* key) {
    const auto& v = j.at(key);
    require(v.is_number_integer(), "Expected an integer parameter.");
    if (v.is_number_unsigned())
        require(v.get<uint64_t>() <= uint64_t(std::numeric_limits<int>::max()),
                "Integer parameter is out of range.");
    const auto number = v.get<int64_t>();
    require(number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max(),
            "Integer parameter is out of range.");
    return static_cast<int>(number);
}
NoiseSettings parse_noise(const Json& j) {
    NoiseSettings result{integer(j, "spacing_log2"),
                         integer(j, "octaves"),
                         j.value("gain", 0.5f),
                         integer(j, "seed_offset"),
                         {}};
    if (j.contains("weights")) {
        const auto& weights = j.at("weights");
        require(weights.is_array() && !weights.empty() && weights.size() <= 16,
                "Noise weights must contain 1..16 values.");
        result.weights = weights.get<std::vector<float>>();
    } else
        require(j.contains("gain"), "Noise needs gain or weights.");
    if (j.contains("spacings")) {
        require(j.at("spacings").is_array() && j.at("spacings").size() <= 16, "Invalid octave spacings.");
        result.spacings = j.at("spacings").get<std::vector<float>>();
    }
    result.frequency_multiplier = j.value("frequency_multiplier", 1.0f);
    result.warp = j.value("warp", std::string{});
    return result;
}
Json node_json(const TerrainSpline& node) {
    if (node.axis == SplineAxis::constant)
        return node.constant;
    Json points = Json::array();
    for (size_t i = 0; i < node.locations.size(); ++i)
        points.push_back({{"location", node.locations[i]},
                          {"derivative", node.derivatives[i]},
                          {"value", node_json(node.values[i])}});
    return {{"axis", node.axis == SplineAxis::groundness   ? "groundness"
                     : node.axis == SplineAxis::smoothness ? "smoothness"
                     : node.axis == SplineAxis::pv         ? "pv"
                                                           : "weirdness"},
            {"points", points}};
}
TerrainSpline parse_node(const Json& j, int depth, size_t& budget) {
    require(depth <= 8 && budget > 0, "Spline tree exceeds depth/node limits.");
    --budget;
    TerrainSpline result;
    if (j.is_number()) {
        result.constant = j.get<float>();
        return result;
    }
    const auto axis = j.at("axis").get<std::string>();
    require(axis == "pv" || axis == "weirdness" || axis == "groundness" || axis == "smoothness",
            "Unknown spline coordinate.");
    result.axis = axis == "groundness"   ? SplineAxis::groundness
                  : axis == "smoothness" ? SplineAxis::smoothness
                  : axis == "pv"         ? SplineAxis::pv
                                         : SplineAxis::weirdness;
    const auto& points = j.at("points");
    require(points.is_array() && !points.empty() && points.size() <= 64, "Invalid spline points.");
    for (const auto& p : points) {
        result.locations.push_back(p.at("location").get<float>());
        result.derivatives.push_back(p.at("derivative").get<float>());
        result.values.push_back(parse_node(p.at("value"), depth + 1, budget));
    }
    return result;
}
Json grid_json(const SplineGrid& grid) {
    if (grid.tree)
        return {{"tree", node_json(*grid.tree)}};
    Json cells = Json::array();
    for (const auto& cell : grid.cells)
        cells.push_back(cell ? node_json(*cell) : Json(nullptr));
    return {{"groundness", grid.groundness}, {"smoothness", grid.smoothness}, {"cells", cells}};
}
SplineGrid parse_grid(const Json& j, size_t& budget) {
    SplineGrid result;
    if (j.contains("tree")) {
        result.tree = parse_node(j.at("tree"), 0, budget);
        return result;
    }
    for (const char* key : {"groundness", "smoothness"})
        require(j.at(key).is_array() && !j.at(key).empty() && j.at(key).size() <= 24, "Invalid grid axis.");
    result.groundness = j.at("groundness").get<std::vector<float>>();
    result.smoothness = j.at("smoothness").get<std::vector<float>>();
    const auto& cells = j.at("cells");
    require(cells.is_array() && cells.size() == result.groundness.size() * result.smoothness.size(),
            "Invalid grid cells.");
    for (const auto& cell : cells) {
        if (cell.is_null())
            result.cells.push_back(std::nullopt);
        else
            result.cells.push_back(parse_node(cell, 0, budget));
    }
    return result;
}
TerrainSplines parse_splines(const Json& j) {
    TerrainSplines result;
    result.jagged_enabled = j.at("jagged_enabled").get<bool>();
    result.height_scale = j.at("height_scale").get<float>();
    result.jagged_scale = j.at("jagged_scale").get<float>();
    size_t budget = 16384;
    result.offset = parse_grid(j.at("offset"), budget);
    result.factor = parse_grid(j.at("factor"), budget);
    result.jaggedness = parse_grid(j.at("jaggedness"), budget);
    validate(result);
    return result;
}
Json splines_json(const TerrainSplines& s) {
    return {{"height_scale", s.height_scale},     {"jagged_scale", s.jagged_scale},
            {"jagged_enabled", s.jagged_enabled}, {"offset", grid_json(s.offset)},
            {"factor", grid_json(s.factor)},      {"jaggedness", grid_json(s.jaggedness)}};
}
} // namespace
TerrainSplines default_terrain_splines() {
    static const auto preset = parse_splines(Json::parse(
#include "world/periodic_spline_preset.inc"
        ));
    return preset;
}
std::vector<float> octave_spacings(const NoiseSettings& settings) {
    if (!settings.spacings.empty())
        return settings.spacings;
    std::vector<float> result;
    for (int i = 0; i < settings.octaves; ++i)
        result.push_back(std::ldexp(1.0f, settings.spacing_log2 - i));
    return result;
}
std::vector<float> octave_weights(const NoiseSettings& settings) {
    if (!settings.weights.empty())
        return settings.weights;
    std::vector<float> result;
    float weight = 1;
    for (int i = 0; i < settings.octaves; ++i) {
        result.push_back(weight);
        weight *= settings.gain;
    }
    return result;
}
void disable_warps(GenerationConfig& c) {
    for (auto& w : c.warps)
        w.enabled = false;
}
bool has_active_warp(const GenerationConfig& c) {
    for (const auto* n :
         {&c.groundness, &c.smoothness, &c.weirdness, &c.temperature, &c.precipitation, &c.jagged})
        for (const auto& w : c.warps)
            if (n->warp == w.id && w.enabled && w.strength > 0)
                return true;
    return false;
}
void remove_warp(GenerationConfig& c, const std::string& id) {
    for (auto* n : {&c.groundness, &c.smoothness, &c.weirdness, &c.temperature, &c.precipitation, &c.jagged})
        if (n->warp == id)
            n->warp.clear();
    std::erase_if(c.warps, [&](const auto& w) { return w.id == id; });
}
void validate(const GenerationConfig& c) {
    require(c.warps.size() == 1 && c.warps[0].id == "shift", "One shared Shift field is required.");
    std::vector<const NoiseSettings*> noises{&c.groundness,  &c.shape,         &c.smoothness, &c.weirdness,
                                             &c.temperature, &c.precipitation, &c.jagged};
    std::vector<std::string> ids;
    for (const auto& w : c.warps) {
        require(
            !w.id.empty() && w.id.size() <= 64 &&
                w.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
                    std::string::npos,
            "Warp ID must use 1..64 letters, numbers, underscores or hyphens.");
        require(std::find(ids.begin(), ids.end(), w.id) == ids.end(), "Duplicate warp ID.");
        ids.push_back(w.id);
        require(!w.name.empty() && w.name.size() <= 128, "Warp name needs 1..128 UTF-8 bytes.");
        require(std::isfinite(w.strength) && w.strength >= 0 && w.strength <= 8192,
                "Warp strength must be 0..8192 blocks.");
        require(w.noise.warp.empty(), "Warp fields cannot reference other warps.");
        noises.push_back(&w.noise);
    }
    require(c.jagged.warp.empty(), "Jagged must remain unwarped.");
    require(c.shape.warp.empty(), "3D shape does not support domain warping.");
    for (const auto* noise : noises) {
        require(noise->warp.empty() || std::find(ids.begin(), ids.end(), noise->warp) != ids.end(),
                "Noise references an unknown warp ID.");
        const auto& n = *noise;
        require(std::isfinite(n.frequency_multiplier) && n.frequency_multiplier >= .001f &&
                    n.frequency_multiplier <= 1500,
                "Frequency multiplier must be .001..1500.");
        require(n.spacing_log2 >= 2 && n.spacing_log2 <= 17, "Horizontal spacing exponent must be 2..17.");
        require(n.octaves >= 1 && n.octaves <= 16, "Octaves must be 1..16.");
        require(noise == &c.groundness || n.spacings.empty(), "Only Groundness supports individual spacing.");
        require(n.spacings.empty() || n.spacings.size() == size_t(n.octaves),
                "Groundness needs one spacing per octave.");
        for (float spacing : n.spacings)
            require(std::isfinite(spacing) && spacing >= .001f && spacing <= world_size,
                    "Groundness octave spacing must be .001..131072 blocks.");
        for (float spacing : octave_spacings(n))
            require(double(world_size) / spacing * n.frequency_multiplier * 1.0181268882175227 <=
                        1000000000.0,
                    "Noise frequency exceeds safe periodic lattice range.");
        require(std::isfinite(n.gain) && n.gain >= 0 && n.gain <= 1, "Gain must be 0..1.");
        require(n.weights.empty() || n.weights.size() == static_cast<size_t>(n.octaves),
                "Noise needs one weight per octave.");
        for (float weight : n.weights)
            require(std::isfinite(weight) && weight >= 0, "Octave weights must be finite and nonnegative.");
    }
    for (float v : {c.blended.xz_scale, c.blended.y_scale, c.blended.xz_factor, c.blended.y_factor})
        require(std::isfinite(v) && v >= .001f && v <= 1000, "Blended scale/factor must be .001..1000.");
    require(684.412 * c.blended.xz_scale * world_size / std::min(1.0f, c.blended.xz_factor) <= 1000000000.0,
            "Blended horizontal frequency exceeds safe lattice range.");
    require(std::isfinite(c.blended.smear) && c.blended.smear >= 1 && c.blended.smear <= 8,
            "Smear must be 1..8.");
    require(std::isfinite(c.amplitude) && c.amplitude >= 0 && c.amplitude <= 512,
            "3D amplitude must be 0..512.");
    require(std::isfinite(c.squash) && c.squash >= 0.01f && c.squash <= 64, "Squash must be 0.01..64.");
    validate(c.splines);
    const auto bounded = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
    const auto& b = c.temperature_bands;
    require(bounded(b.poles, -1, 1) && bounded(b.equator, b.poles, 1),
            "Poles must be colder than the equator.");
    require(bounded(b.latitude_power, 1, 8) && bounded(b.variation, 0, 1), "Invalid latitude parameters.");
}
std::string generation_json(const GenerationConfig& c) {
    validate(c);
    Json warps = Json::array();
    for (const auto& w : c.warps)
        warps.push_back({{"id", w.id},
                         {"name", w.name},
                         {"enabled", w.enabled},
                         {"strength", w.strength},
                         {"noise", noise_json(w.noise, true)}});
    auto ground = noise_json(c.groundness, true);
    ground["spacings"] = octave_spacings(c.groundness);
    return Json{{"schema_version", 10},
                {"noise_2d", "periodic_double_perlin"},
                {"blended",
                 {{"xz_scale", c.blended.xz_scale},
                  {"y_scale", c.blended.y_scale},
                  {"xz_factor", c.blended.xz_factor},
                  {"y_factor", c.blended.y_factor},
                  {"smear", c.blended.smear}}},
                {"warps", warps},
                {"world_size", world_size},
                {"seed", c.seed},
                {"groundness", ground},
                {"shape", {{"seed_offset", c.shape.seed_offset}}},
                {"smoothness", noise_json(c.smoothness)},
                {"weirdness", noise_json(c.weirdness)},
                {"jagged", noise_json(c.jagged)},
                {"splines", splines_json(c.splines)},
                {"temperature", noise_json(c.temperature)},
                {"precipitation", noise_json(c.precipitation)},
                {"temperature_bands",
                 {{"equator", c.temperature_bands.equator},
                  {"poles", c.temperature_bands.poles},
                  {"latitude_power", c.temperature_bands.latitude_power},
                  {"variation", c.temperature_bands.variation}}},
                {"shape_enabled", c.shape_enabled},
                {"amplitude", c.amplitude},
                {"squash", c.squash}}
               .dump(2) +
           "\n";
}
GenerationConfig parse_generation_json(const std::string& text) {
    const auto j = Json::parse(text);
    const int version = integer(j, "schema_version");
    require(version >= 5 && version <= 10,
            "Unsupported world generation schema. The legacy groundness-height "
            "generator was removed; load spline-based settings (schema 5..10).");
    if (version < 9)
        require(j.at("splines").at("enabled").get<bool>(),
                "The legacy groundness-height generator was removed. Load settings with the multivariable "
                "splines enabled.");
    require(integer(j, "world_size") == world_size, "Saved world period must be 131072 blocks.");
    GenerationConfig c;
    if (version < 10) {
        const auto seed = j.at("seed");
        require(seed.is_number_integer() && seed.get<double>() >= 0 && seed.get<double>() <= UINT32_MAX,
                "Invalid seed.");
        c.seed = seed.get<uint32_t>();
        if (j.contains("temperature_bands")) {
            const auto& b = j.at("temperature_bands");
            c.temperature_bands = {b.at("equator").get<float>(), b.at("poles").get<float>(),
                                   b.at("latitude_power").get<float>(), b.at("variation").get<float>()};
        }
        validate(c);
        return c;
    }
    const auto& b3 = j.at("blended");
    c.blended = {b3.at("xz_scale").get<float>(), b3.at("y_scale").get<float>(),
                 b3.at("xz_factor").get<float>(), b3.at("y_factor").get<float>(),
                 b3.at("smear").get<float>()};
    c.warps.clear();
    if (version >= 7) {
        require(j.at("noise_2d") == "periodic_double_perlin", "Unsupported 2D noise algorithm.");
        const auto& warps = j.at("warps");
        require(warps.is_array() && warps.size() <= 16, "Invalid warp list.");
        for (const auto& w : warps)
            c.warps.push_back({w.at("id").get<std::string>(), w.at("name").get<std::string>(),
                               w.at("enabled").get<bool>(), parse_noise(w.at("noise")),
                               w.at("strength").get<float>()});
    } else if (version == 6) {
        const auto& w = j.at("domain_warp");
        c.warps.push_back({"legacy",
                           "이전 공유 워핑",
                           w.at("enabled").get<bool>(),
                           {integer(w, "spacing_log2"), 1, .5f, 8191, {1}},
                           w.at("strength").get<float>()});
    }
    const auto& seed = j.at("seed");
    require(seed.is_number_integer() && seed.get<double>() >= 0 && seed.get<double>() <= UINT32_MAX,
            "Seed must be a 32-bit unsigned integer.");
    c.seed = seed.get<uint32_t>();
    const auto& ground = j.at("groundness");
    {
        c.groundness.spacing_log2 = integer(ground, "spacing_log2");
        c.groundness.octaves = integer(ground, "octaves");
        c.groundness.seed_offset = integer(ground, "seed_offset");
        const auto& weights = ground.at("weights");
        require(weights.is_array() && !weights.empty() && weights.size() <= 16,
                "Groundness weights must be an array of 1..16 values.");
        c.groundness.weights = weights.get<std::vector<float>>();
    }
    if (version >= 7)
        c.groundness.warp = ground.value("warp", std::string{});
    if (version >= 8) {
        const auto& spacings = ground.at("spacings");
        require(spacings.is_array() && !spacings.empty() && spacings.size() <= 16,
                "Groundness spacings need 1..16 values.");
        c.groundness.spacings = spacings.get<std::vector<float>>();
    }
    c.groundness.frequency_multiplier = ground.value("frequency_multiplier", 1.0f);
    c.shape.seed_offset = integer(j.at("shape"), "seed_offset");
    c.jagged = parse_noise(j.at("jagged"));
    for (const char* key : {"offset", "factor", "jaggedness"})
        require(j.at("splines").at(key).contains("tree"), "Schema 10 requires nested spline trees.");
    c.splines = parse_splines(j.at("splines"));
    c.smoothness = parse_noise(j.at("smoothness"));
    c.weirdness = parse_noise(j.at("weirdness"));
    c.temperature = parse_noise(j.at("temperature"));
    c.precipitation = parse_noise(j.at("precipitation"));
    const auto& b = j.at("temperature_bands");
    c.temperature_bands.equator = b.at("equator").get<float>();
    c.temperature_bands.poles = b.at("poles").get<float>();
    c.temperature_bands.latitude_power = b.at("latitude_power").get<float>();
    c.temperature_bands.variation = b.at("variation").get<float>();
    require(j.at("shape_enabled").is_boolean(), "3D noise enabled must be a boolean.");
    c.shape_enabled = j.at("shape_enabled").get<bool>();
    c.amplitude = j.at("amplitude").get<float>();
    c.squash = j.at("squash").get<float>();
    if (version < 7) {
        for (auto* n : {&c.groundness, &c.smoothness, &c.weirdness, &c.temperature, &c.precipitation,
                        &c.jagged, &c.shape})
            n->warp.clear();
        if (version == 6)
            c.groundness.warp = c.weirdness.warp = "legacy";
    }
    validate(c);
    c.groundness.spacings = octave_spacings(c.groundness);
    c.groundness.weights = octave_weights(c.groundness);
    c.groundness.gain = 0.5f;
    return c;
}
GenerationConfig load_generation_config(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open world generation settings.");
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || size > 4194304)
        throw std::runtime_error("World generation settings exceed 4 MiB.");
    input.seekg(0);
    std::string text(static_cast<size_t>(size), '\0');
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size())))
        throw std::runtime_error("Cannot read world generation settings.");
    return parse_generation_json(text);
}
void save_generation_config(const std::filesystem::path& path, const GenerationConfig& config) {
    const auto text = generation_json(config);
    if (text.size() > 4194304)
        throw std::runtime_error("World generation settings exceed 4 MiB.");
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        if (!output)
            throw std::runtime_error("Cannot write world generation settings.");
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace saved world generation settings (Windows error " +
                                 std::to_string(GetLastError()) + ").");
#else
    std::filesystem::rename(temporary, path);
#endif
}
} // namespace sandbox
