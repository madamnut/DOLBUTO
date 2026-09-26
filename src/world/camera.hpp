#pragma once
#include "core/world_rules.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace sandbox {
enum class CameraMode { first_person, third_person_back, third_person_front };
struct FlyCamera {
    static constexpr double third_person_distance = 6.0;
    static constexpr float near_clip = 0.1f;
    float field_of_view{75.0f};
    bool zoomed{};
    glm::mat4 visual_rotation{1.0f}; // Render-only gait rotation; logical aim/yaw/pitch stay unchanged.
    float effective_field_of_view() const { return field_of_view * (zoomed ? 0.25f : 1.0f); }
    glm::dvec3 position{8.0, 150.0, 8.0};
    double yaw{-90.0}, pitch{-22.0};
    CameraMode mode{CameraMode::first_person};
    void cycle_mode() {
        switch (mode) {
        case CameraMode::first_person:
            mode = CameraMode::third_person_back;
            break;
        case CameraMode::third_person_back:
            mode = CameraMode::third_person_front;
            break;
        case CameraMode::third_person_front:
            mode = CameraMode::first_person;
            break;
        }
    }
    glm::dvec3 forward() const {
        const auto y = glm::radians(yaw), p = glm::radians(pitch);
        return {std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)};
    }
    glm::dvec3 view_forward() const {
        return mode == CameraMode::third_person_front ? -forward() : forward();
    }
    void look(float dx, float dy) {
        const double sensitivity = zoomed ? 0.03 : 0.12;
        yaw = std::remainder(yaw + dx * sensitivity, 360.0);
        pitch = std::clamp(pitch - dy * sensitivity, -89.0, 89.0);
    }
    glm::dvec3 movement_velocity(const bool* keys, bool flying, bool allow_ascent, double speed) const {
        // Movement follows yaw only; pitch affects the view, never WASD height or speed.
        const auto heading = glm::radians(yaw);
        const glm::dvec3 f{std::cos(heading), 0.0, std::sin(heading)};
        const glm::dvec3 right{-std::sin(heading), 0.0, std::cos(heading)};
        glm::dvec3 direction{};
        if (keys[SDL_SCANCODE_W])
            direction += f;
        if (keys[SDL_SCANCODE_S])
            direction -= f;
        if (keys[SDL_SCANCODE_D])
            direction += right;
        if (keys[SDL_SCANCODE_A])
            direction -= right;
        if (flying && allow_ascent && keys[SDL_SCANCODE_SPACE])
            direction.y += 1;
        if (flying && (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]))
            direction.y -= 1;
        if (glm::dot(direction, direction) == 0.0)
            return {};
        return glm::normalize(direction) * speed;
    }
    glm::mat4 view_projection(float aspect, float distance) const {
        auto projection =
            glm::perspective(glm::radians(effective_field_of_view()), aspect, near_clip, distance);
        projection[1][1] *= -1;
        // Mesh translations are relative to the double-precision camera position.
        return projection * visual_rotation *
               glm::lookAt(glm::vec3(0), glm::vec3(view_forward()), glm::vec3(0, 1, 0));
    }
};
} // namespace sandbox
