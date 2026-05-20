#pragma once

#include "camera/Camera.h"

namespace dolbuto
{
    struct CameraViewBobInput
    {
        bool enabled = false;
        float yaw = 0.0f;
        float walkPhase = 0.0f;
        float walkAmount = 0.0f;
    };

    class CameraViewBob
    {
    public:
        static Vec3 offset(CameraViewBobInput input);
    };
}
