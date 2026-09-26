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
    NoiseSettings result{integer(j, "spacing_log2"), integer(j, "octaves"), j.value("gain", 0.5f), 0, {}};
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
} // namespace
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
    for (const auto* n : {&c.temperature, &c.precipitation})
        for (const auto& w : c.warps)
            if (n->warp == w.id && w.enabled && w.strength > 0)
                return true;
    return false;
}
void remove_warp(GenerationConfig& c, const std::string& id) {
    for (auto* n : {&c.temperature, &c.precipitation})
        if (n->warp == id)
            n->warp.clear();
    std::erase_if(c.warps, [&](const auto& w) { return w.id == id; });
}
void validate(const GenerationConfig& c) {
    require(c.warps.size() == 1 && c.warps[0].id == "shift", "One shared Shift field is required.");
    std::vector<const NoiseSettings*> noises{&c.temperature, &c.precipitation};
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
    for (const auto* noise : noises) {
        require(noise->warp.empty() || std::find(ids.begin(), ids.end(), noise->warp) != ids.end(),
                "Noise references an unknown warp ID.");
        const auto& n = *noise;
        require(n.seed_offset == 0, "Climate streams derive only from the master seed.");
        require(std::isfinite(n.frequency_multiplier) && n.frequency_multiplier >= .001f &&
                    n.frequency_multiplier <= 1500,
                "Frequency multiplier must be .001..1500.");
        require(n.spacing_log2 >= 2 && n.spacing_log2 <= 17, "Horizontal spacing exponent must be 2..17.");
        require(n.octaves >= 1 && n.octaves <= 16, "Octaves must be 1..16.");
        require(n.spacings.empty(), "Climate noise uses a shared octave spacing exponent.");
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
    return Json{{"schema_version", 12},
                {"terrain", "flat_stone"},
                {"noise_2d", "periodic_double_perlin"},
                {"world_size", world_size},
                {"seed", c.seed},
                {"warps", warps},
                {"temperature", noise_json(c.temperature)},
                {"precipitation", noise_json(c.precipitation)},
                {"temperature_bands",
                 {{"equator", c.temperature_bands.equator},
                  {"poles", c.temperature_bands.poles},
                  {"latitude_power", c.temperature_bands.latitude_power},
                  {"variation", c.temperature_bands.variation}}}}
               .dump(2) +
           "\n";
}
GenerationConfig parse_generation_json(const std::string& text) {
    const auto j = Json::parse(text);
    const int version = integer(j, "schema_version");
    require(version >= 5 && version <= 12, "Unsupported world generation schema (expected 5..12).");
    require(integer(j, "world_size") == world_size, "Saved world period must be 131072 blocks.");
    GenerationConfig c;
    const auto& seed = j.at("seed");
    require(seed.is_number_integer() && seed.get<double>() >= 0 && seed.get<double>() <= UINT32_MAX,
            "Seed must be a 32-bit unsigned integer.");
    c.seed = seed.get<uint32_t>();
    // Older algorithms already migrated to defaults in schema10. Keep that policy for 5..9.
    // Retired terrain fields and individual seed offsets are ignored; never rewrite files on load.
    if (version >= 10) {
        require(j.at("noise_2d") == "periodic_double_perlin", "Unsupported climate noise algorithm.");
        if (version >= 11)
            require(j.at("terrain") == "flat_stone", "Unsupported terrain generator.");
        const auto& warps = j.at("warps");
        require(warps.is_array() && warps.size() == 1, "One climate Shift field is required.");
        c.warps.clear();
        for (const auto& w : warps)
            c.warps.push_back({w.at("id").get<std::string>(), w.at("name").get<std::string>(),
                               w.at("enabled").get<bool>(), parse_noise(w.at("noise")),
                               w.at("strength").get<float>()});
        c.temperature = parse_noise(j.at("temperature"));
        c.precipitation = parse_noise(j.at("precipitation"));
    }
    if (j.contains("temperature_bands")) {
        const auto& b = j.at("temperature_bands");
        c.temperature_bands = {b.at("equator").get<float>(), b.at("poles").get<float>(),
                               b.at("latitude_power").get<float>(), b.at("variation").get<float>()};
    }
    validate(c);
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
