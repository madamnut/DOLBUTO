#pragma once
#include <chrono>
#include <cmath>
#include <cstdint>
namespace sandbox {
inline constexpr int chunk_edge = 16;
inline constexpr int world_size = 131072;
inline constexpr int world_columns = world_size / chunk_edge;
constexpr int wrap_block(int coordinate) {
    const int r = coordinate % world_size;
    return r < 0 ? r + world_size : r;
}
constexpr int wrap_column(int coordinate) {
    const int r = coordinate % world_columns;
    return r < 0 ? r + world_columns : r;
}
constexpr int column_delta(int a, int b) {
    return wrap_column(wrap_column(a) - wrap_column(b) + world_columns / 2) - world_columns / 2;
}
inline double wrap_position(double coordinate) {
    const double r = std::fmod(coordinate, double(world_size));
    return r < 0 ? r + world_size : r;
}
inline double world_delta(double a, double b) {
    return wrap_position(a - b + world_size / 2) - world_size / 2;
}
inline constexpr int world_height = 512;
inline constexpr int sea_level = 192; // Water surface plane; fluid cells occupy y < sea_level.
inline constexpr int chunks_per_column = world_height / chunk_edge;
inline constexpr int physics_tps = 20;
inline constexpr uint32_t ticks_per_day = 28800;
inline constexpr uint32_t ticks_per_hour = 1200;
inline constexpr uint32_t start_day_tick = 9 * ticks_per_hour;
inline constexpr auto physics_step = std::chrono::milliseconds(50);
// Use mathematical floor for negative block coordinates too.
constexpr int chunk_coordinate(int block) {
    const int quotient = block / chunk_edge;
    return quotient - (block % chunk_edge < 0 ? 1 : 0);
}
constexpr int local_coordinate(int block) {
    const int remainder = block % chunk_edge;
    return remainder < 0 ? remainder + chunk_edge : remainder;
}
} // namespace sandbox
