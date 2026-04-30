#pragma once

namespace dolbuto
{
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    class Camera
    {
    public:
        void rotate(float deltaX, float deltaY);
        void setAngles(float yaw, float pitch);

        Vec3 forward() const;
        Vec3 right() const;
        Vec3 up() const;
        float yaw() const;
        float pitch() const;

    private:
        float yaw_ = 0.0f;
        float pitch_ = 0.0f;
    };

    float dot(Vec3 left, Vec3 right);
    Vec3 cross(Vec3 left, Vec3 right);
    Vec3 normalize(Vec3 value);
}
