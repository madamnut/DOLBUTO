#pragma once
#include "core/world_rules.hpp"
#include "world/edit.hpp"
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace sandbox {
enum class MovementMode { fly, walk };
struct PlayerBounds {
    glm::dvec3 min, max;
};
struct Player {
    // Position is the centre of the feet. Rendering and collision share these dimensions.
    static constexpr double width = 0.7, depth = 0.7, height = 1.75;
    static constexpr double eye_height = 1.6;
    // Minecraft ordinary-air rules at 20 TPS. Stored velocities remain blocks/second.
    static constexpr double jump_speed = 0.42 * physics_tps;
    static constexpr double gravity_per_tick = 0.08 * physics_tps;
    static constexpr double ground_acceleration = 0.1 * physics_tps;
    static constexpr double air_acceleration = 0.02 * physics_tps;
    static constexpr double ground_drag = 0.6 * 0.91, air_drag = 0.91, vertical_drag = 0.98;
    static constexpr double input_drag = 0.98, sprint_multiplier = 1.3;
    static constexpr double sprint_jump_boost = 0.2 * physics_tps;
    static constexpr double stop_speed = 0.003 * physics_tps;
    glm::dvec3 position{8.0, 150.0, 8.0};
    MovementMode movement_mode{MovementMode::fly};
    glm::dvec2 horizontal_velocity{}; // World X/Z, independent of current camera yaw.
    double vertical_velocity{};
    bool grounded{};
    template <class Sampler>
    void walk_tick(glm::dvec2 input, glm::dvec2 facing, bool sprinting, bool jump, const Sampler& sample) {
        // Threshold, jump/impulse, acceleration, collision-aware movement, then gravity/drag.
        if (glm::dot(horizontal_velocity, horizontal_velocity) < stop_speed * stop_speed)
            horizontal_velocity = {};
        if (std::abs(vertical_velocity) < stop_speed)
            vertical_velocity = 0;
        const bool started_grounded = supported(sample);
        if (jump && started_grounded) {
            vertical_velocity = std::max(jump_speed, vertical_velocity);
            if (sprinting)
                horizontal_velocity += facing * sprint_jump_boost;
        }
        const double acceleration = (started_grounded ? ground_acceleration : air_acceleration) *
                                    (sprinting ? sprint_multiplier : 1.0);
        horizontal_velocity += input * (acceleration * input_drag);
        const auto blocked = move(
            glm::dvec3(horizontal_velocity.x, vertical_velocity, horizontal_velocity.y) / double(physics_tps),
            sample);
        if (blocked.x)
            horizontal_velocity.x = 0;
        if (blocked.z)
            horizontal_velocity.y = 0;
        if (blocked.y)
            vertical_velocity = 0;
        horizontal_velocity *= started_grounded ? ground_drag : air_drag;
        vertical_velocity = (vertical_velocity - gravity_per_tick) * vertical_drag;
        grounded = supported(sample);
    }
    // Sweep every crossed voxel plane, then slide along the remaining axes.
    // Unloaded cells stop movement. Existing overlaps are left in place after regeneration.
    template <class Sampler> glm::bvec3 move(glm::dvec3 displacement, const Sampler& sample) {
        glm::bvec3 blocked_axes(false);
        constexpr double epsilon = 1e-9;
        for (int axis = 0; axis < 3; ++axis) {
            double distance = displacement[axis];
            if (distance == 0)
                continue;
            const auto box = bounds();
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            const bool positive = distance > 0;
            const int step = positive ? 1 : -1;
            const double edge = positive ? box.max[axis] : box.min[axis];
            // Include the cell containing the leading edge: a snow top can be inside that cell.
            const int first = static_cast<int>(std::floor(edge + (positive ? -epsilon : epsilon)));
            const int last = static_cast<int>(std::floor(edge + distance + (positive ? -epsilon : epsilon)));
            bool blocked = false;
            for (int cell = first; positive ? cell <= last : cell >= last; cell += step) {
                for (int a = static_cast<int>(std::floor(box.min[u] + epsilon));
                     a < static_cast<int>(std::ceil(box.max[u] - epsilon)); ++a) {
                    for (int b = static_cast<int>(std::floor(box.min[v] + epsilon));
                         b < static_cast<int>(std::ceil(box.max[v] - epsilon)); ++b) {
                        int coordinates[3]{};
                        coordinates[axis] = cell;
                        coordinates[u] = a;
                        coordinates[v] = b;
                        const auto block = sample(BlockPos{coordinates[0], coordinates[1], coordinates[2]});
                        if (!block || is_solid(*block)) {
                            const double h = block ? block_height(*block) : 1.0;
                            if (axis != 1 && (box.min.y >= coordinates[1] + h - epsilon ||
                                              box.max.y <= coordinates[1] + epsilon))
                                continue;
                            const double face = positive ? double(cell) : cell + (axis == 1 ? h : 1.0);
                            const double gap = face - edge;
                            if (positive ? gap >= -epsilon && gap <= distance + epsilon
                                         : gap <= epsilon && gap >= distance - epsilon) {
                                distance = positive ? std::max(0.0, gap) : std::min(0.0, gap);
                                blocked = true;
                            }
                        }
                    }
                }
                if (blocked)
                    break;
            }
            blocked_axes[axis] = blocked;
            position[axis] += distance;
        }
        position.x = wrap_position(position.x);
        position.z = wrap_position(position.z);
        const double limited_y = std::clamp(position.y, -512.0, 4096.0);
        blocked_axes.y = blocked_axes.y || limited_y != position.y;
        position.y = limited_y;
        return blocked_axes;
    }
    template <class Sampler> bool supported(const Sampler& sample) const {
        // Only an actual solid top face provides a jump surface; unloaded cells never do.
        const int cell_y = static_cast<int>(std::floor(position.y - 1e-9));
        const auto box = bounds();
        for (int z = static_cast<int>(std::floor(box.min.z + 1e-9));
             z < static_cast<int>(std::ceil(box.max.z - 1e-9)); ++z) {
            for (int x = static_cast<int>(std::floor(box.min.x + 1e-9));
                 x < static_cast<int>(std::ceil(box.max.x - 1e-9)); ++x) {
                const auto block = sample(BlockPos{x, cell_y, z});
                if (block && is_solid(*block) &&
                    std::abs(position.y - (cell_y + block_height(*block))) <= 1e-7)
                    return true;
            }
        }
        return false;
    }
    PlayerBounds bounds() const {
        return {position - glm::dvec3(width * 0.5, 0, depth * 0.5),
                position + glm::dvec3(width * 0.5, height, depth * 0.5)};
    }
    bool overlaps(BlockPos block, double cell_height = 1.0) const {
        // Resolve the block to the nearest image before testing across a world seam.
        const glm::dvec3 min{world_delta(block.x + 0.5, position.x) - 0.5, block.y - position.y,
                             world_delta(block.z + 0.5, position.z) - 0.5};
        return min.x < width * 0.5 && min.x + 1 > -width * 0.5 && min.z < depth * 0.5 &&
               min.z + 1 > -depth * 0.5 && min.y < height && min.y + cell_height > 0;
    }
};
} // namespace sandbox
