#include "camera/Camera.h"

#include <algorithm>
#include <cmath>

namespace dolbuto
{
    namespace
    {
        constexpr float HalfPi = 1.57079632679f;
        constexpr float MouseSensitivity = 0.0025f;
    }

    void Camera::rotate(float deltaX, float deltaY)
    {
        yaw_ -= deltaX * MouseSensitivity;
        pitch_ = std::clamp(pitch_ + deltaY * MouseSensitivity, -HalfPi + 0.01f, HalfPi - 0.01f);
    }

    void Camera::setAngles(float yaw, float pitch)
    {
        yaw_ = yaw;
        pitch_ = std::clamp(pitch, -HalfPi + 0.01f, HalfPi - 0.01f);
    }

    Vec3 Camera::forward() const
    {
        const float cosPitch = std::cos(pitch_);
        return normalize({
            cosPitch * std::cos(yaw_),
            std::sin(pitch_),
            cosPitch * std::sin(yaw_)
        });
    }

    Vec3 Camera::right() const
    {
        return normalize({-std::sin(yaw_), 0.0f, std::cos(yaw_)});
    }

    Vec3 Camera::up() const
    {
        return normalize(cross(right(), forward()));
    }

    float Camera::yaw() const
    {
        return yaw_;
    }

    float Camera::pitch() const
    {
        return pitch_;
    }

    float dot(Vec3 left, Vec3 right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    Vec3 cross(Vec3 left, Vec3 right)
    {
        return {
            left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x
        };
    }

    Vec3 normalize(Vec3 value)
    {
        const float length = std::sqrt(dot(value, value));
        if (length <= 0.0f)
        {
            return {};
        }

        return {value.x / length, value.y / length, value.z / length};
    }
}
