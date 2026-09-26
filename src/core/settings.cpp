#include "core/settings.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
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
GameSettings load_game_settings(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path))
        return {};
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("Cannot open game settings.");
    const auto size = input.tellg();
    if (size < 0 || size > 65536)
        throw std::runtime_error("Game settings exceed 64 KiB.");
    input.seekg(0);
    std::string text(static_cast<size_t>(size), '\0');
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size())))
        throw std::runtime_error("Cannot read game settings.");
    const auto json = nlohmann::json::parse(text);
    const auto& version = json.at("schema_version");
    if (!version.is_number_integer() || version < 1 || version > 10)
        throw std::runtime_error("Invalid game settings schema.");
    const auto read_int = [&](const char* key, int low, int high, bool allow_zero = false) {
        const auto& value = json.at(key);
        if (!value.is_number_integer() || !((allow_zero && value == 0) || (value >= low && value <= high)))
            throw std::runtime_error(std::string("Invalid game setting: ") + key);
        return value.get<int>();
    };
    GameSettings result;
    result.render_distance = read_int("render_distance", 1, 64);
    if (version >= 2) {
        result.field_of_view = read_int("field_of_view", 30, 110);
        result.fps_limit = read_int("fps_limit", 30, 500, true);
        if (!json.at("vsync").is_boolean())
            throw std::runtime_error("VSync must be boolean.");
        result.vsync = json.at("vsync").get<bool>();
    }
    if (version >= 10) {
        if (!json.at("view_bobbing").is_boolean())
            throw std::runtime_error("View bobbing must be boolean.");
        result.view_bobbing = json.at("view_bobbing").get<bool>();
    }
    if (version >= 3) {
        const auto& water = json.at("water");
        const auto read = [&](const char* key) {
            if (!water.at(key).is_boolean())
                throw std::runtime_error(std::string("Invalid water setting: ") + key);
            return water.at(key).get<bool>();
        };
        result.water = {read("enabled"), read("depth"), read("waves"), read("ssr")};
        if (version >= 4) {
            result.water.refraction = read("refraction");
            result.water.foam = read("foam");
            result.water.caustics = read("caustics");
        }
        if (version >= 5)
            result.water.underwater_fog = read("underwater_fog");
    }
    if (version >= 4) {
        const auto& graphics = json.at("graphics");
        for (const auto& toggle : graphics_toggles) {
            if (version < 9 && std::string_view(toggle.key) == "lod")
                continue;
            if (version < 7 && std::string_view(toggle.key) == "taa")
                continue;
            if (version < 8 &&
                (std::string_view(toggle.key) == "sun_moon" || std::string_view(toggle.key) == "stars"))
                continue;
            const auto& value = graphics.at(toggle.key);
            if (!value.is_boolean())
                throw std::runtime_error(std::string("Invalid graphics toggle: ") + toggle.key);
            result.graphics.*(toggle.member) = value.get<bool>();
        }
        for (const auto& range : graphics_ranges) {
            if (version < 9 && std::string_view(range.key) == "lod_distance")
                continue;
            const auto& value = graphics.at(range.key);
            if (!value.is_number_integer() || value < range.minimum || value > range.maximum)
                throw std::runtime_error(std::string("Invalid graphics value: ") + range.key);
            result.graphics.*(range.member) = value.get<int>();
        }
    }
    // Migrate only the former default cascade profile, without rewriting the file.
    if (version < 7 && result.graphics.shadow_distance == 256 && result.graphics.shadow_quality == 2)
        result.graphics.shadow_distance = 192;
    return result;
}
void save_game_settings(const std::filesystem::path& path, const GameSettings& settings) {
    if (settings.render_distance < 1 || settings.render_distance > 64 || settings.field_of_view < 30 ||
        settings.field_of_view > 110 ||
        (settings.fps_limit != 0 && (settings.fps_limit < 30 || settings.fps_limit > 500)))
        throw std::invalid_argument("Invalid game settings.");
    nlohmann::ordered_json graphics = nlohmann::ordered_json::object();
    for (const auto& toggle : graphics_toggles)
        graphics[toggle.key] = settings.graphics.*(toggle.member);
    for (const auto& range : graphics_ranges) {
        const int value = settings.graphics.*(range.member);
        if (value < range.minimum || value > range.maximum)
            throw std::invalid_argument(std::string("Invalid graphics value: ") + range.key);
        graphics[range.key] = value;
    }
    const auto text = nlohmann::ordered_json{{"schema_version", 10},
                                             {"render_distance", settings.render_distance},
                                             {"field_of_view", settings.field_of_view},
                                             {"fps_limit", settings.fps_limit},
                                             {"vsync", settings.vsync},
                                             {"view_bobbing", settings.view_bobbing},
                                             {"water",
                                              {{"enabled", settings.water.enabled},
                                               {"depth", settings.water.depth},
                                               {"waves", settings.water.waves},
                                               {"ssr", settings.water.ssr},
                                               {"refraction", settings.water.refraction},
                                               {"foam", settings.water.foam},
                                               {"caustics", settings.water.caustics},
                                               {"underwater_fog", settings.water.underwater_fog}}},
                                             {"graphics", graphics}}
                          .dump(2) +
                      "\n";
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        if (!output)
            throw std::runtime_error("Cannot write game settings.");
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace game settings (Windows error " +
                                 std::to_string(GetLastError()) + ").");
#else
    std::filesystem::rename(temporary, path);
#endif
}
} // namespace sandbox
