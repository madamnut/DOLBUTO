#include "camera/CameraViewBob.h"

#include <algorithm>
#include <cmath>

namespace dolbuto
{
    namespace
    {
        constexpr float HorizontalAmplitude = 0.045f;
        constexpr float VerticalAmplitude = 0.065f;
        constexpr float MaxWalkAmount = 1.35f;
    }

    Vec3 CameraViewBob::offset(CameraViewBobInput input)
    {
        if (!input.enabled)
        {
            return {};
        }

        const float amount = std::clamp(input.walkAmount, 0.0f, MaxWalkAmount);
        if (amount <= 0.001f)
        {
            return {};
        }

        const float horizontal = std::sin(input.walkPhase) * amount * HorizontalAmplitude;
        const float vertical = -std::abs(std::cos(input.walkPhase)) * amount * VerticalAmplitude;
        const Vec3 right{std::sin(input.yaw), 0.0f, -std::cos(input.yaw)};
        return {
            right.x * horizontal,
            vertical,
            right.z * horizontal
        };
    }
}
