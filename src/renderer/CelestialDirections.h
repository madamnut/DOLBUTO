#pragma once

#include "camera/Camera.h"

#include <cmath>
#include <cstdint>

namespace dolbuto
{
    namespace celestial
    {
        constexpr uint64_t TicksPerDay = 28800;
        constexpr double TwoPi = 6.283185307179586;
        constexpr double HalfPi = 1.5707963267948966;

        inline double dayPhase(uint64_t worldTicks)
        {
            return static_cast<double>(worldTicks % TicksPerDay) / static_cast<double>(TicksPerDay);
        }

        inline Vec3 sunPositionDirection(uint64_t worldTicks)
        {
            const double phase = dayPhase(worldTicks);
            const double skyAngle = HalfPi - phase * TwoPi;
            return normalize({
                static_cast<float>(std::cos(skyAngle)),
                static_cast<float>(-std::sin(skyAngle)),
                0.0f
            });
        }

        inline Vec3 moonPositionDirection(uint64_t worldTicks)
        {
            const Vec3 sun = sunPositionDirection(worldTicks);
            return {-sun.x, -sun.y, -sun.z};
        }

        inline Vec3 sunlightTravelDirection(Vec3 sunPosition)
        {
            return normalize({-sunPosition.x, -sunPosition.y, -sunPosition.z});
        }
    }
}
