#pragma once
#include <array>
#include <filesystem>

namespace sandbox {
struct WaterSettings {
    bool enabled{true}, depth{true}, waves{true}, ssr{true};
    bool refraction{true}, foam{true}, caustics{true}, underwater_fog{true};
    bool active() const { return enabled && (depth || waves || ssr); }
    bool operator==(const WaterSettings&) const = default;
};
struct GraphicsSettings {
    bool clouds{true}, cloud_shadows{true}, shadows{true}, atmosphere{true};
    bool fog{true}, shafts{true}, bloom{true}, taa{true};
    bool sun_moon{true}, stars{true};
    bool lod{true};
    int lod_distance{128};
    int shadow_distance{192}, shadow_quality{2}, cloud_quality{2}, shaft_quality{2};
    int cloud_altitude{640}, cloud_coverage{50}, cloud_speed{4}, bloom_strength{12};
    bool operator==(const GraphicsSettings&) const = default;
};
struct GraphicsToggle {
    const char* key;
    bool GraphicsSettings::* member;
};
inline constexpr std::array graphics_toggles{
    GraphicsToggle{"lod", &GraphicsSettings::lod},
    GraphicsToggle{"clouds", &GraphicsSettings::clouds},
    GraphicsToggle{"cloud_shadows", &GraphicsSettings::cloud_shadows},
    GraphicsToggle{"shadows", &GraphicsSettings::shadows},
    GraphicsToggle{"atmosphere", &GraphicsSettings::atmosphere},
    GraphicsToggle{"fog", &GraphicsSettings::fog},
    GraphicsToggle{"shafts", &GraphicsSettings::shafts},
    GraphicsToggle{"bloom", &GraphicsSettings::bloom},
    GraphicsToggle{"taa", &GraphicsSettings::taa},
    GraphicsToggle{"sun_moon", &GraphicsSettings::sun_moon},
    GraphicsToggle{"stars", &GraphicsSettings::stars}};
struct GraphicsRange {
    const char* key;
    const char* label;
    int GraphicsSettings::* member;
    int minimum, maximum;
};
inline constexpr std::array graphics_ranges{
    GraphicsRange{"lod_distance", "먼 지형 거리 (컬럼)", &GraphicsSettings::lod_distance, 16, 256},
    GraphicsRange{"shadow_distance", "그림자 거리 (블록)", &GraphicsSettings::shadow_distance, 32, 512},
    GraphicsRange{"shadow_quality", "그림자 품질", &GraphicsSettings::shadow_quality, 1, 3},
    GraphicsRange{"cloud_quality", "구름 품질", &GraphicsSettings::cloud_quality, 1, 3},
    GraphicsRange{"shaft_quality", "빛줄기 품질", &GraphicsSettings::shaft_quality, 1, 3},
    GraphicsRange{"cloud_altitude", "구름 중심 높이 (블록)", &GraphicsSettings::cloud_altitude, 320, 1024},
    GraphicsRange{"cloud_coverage", "구름 양 (%)", &GraphicsSettings::cloud_coverage, 0, 100},
    GraphicsRange{"cloud_speed", "구름 이동 속도 (블록/초)", &GraphicsSettings::cloud_speed, 0, 20},
    GraphicsRange{"bloom_strength", "블룸 강도 (%)", &GraphicsSettings::bloom_strength, 1, 40}};
struct GameSettings {
    int render_distance{12};
    int field_of_view{75}; // Vertical degrees, shared by all camera modes.
    int fps_limit{};       // 0 = unlimited; retained while VSync is enabled.
    bool vsync{};
    bool view_bobbing{true};
    WaterSettings water;
    GraphicsSettings graphics;
    bool operator==(const GameSettings&) const = default;
};
// Missing file uses defaults. Older schemas keep their values and enable new effects.
// Invalid/unreadable files throw without overwriting them.
GameSettings load_game_settings(const std::filesystem::path& path);
void save_game_settings(const std::filesystem::path& path, const GameSettings& settings);
} // namespace sandbox
