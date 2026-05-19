#pragma once

#include "camera/Camera.h"

#include <cstdint>
#include <string_view>

namespace dolbuto::game
{
    struct ClientFrame
    {
        const Camera& camera;
        DVec3 cameraPosition;
        std::string_view fpsText;
        bool debugTextVisible = false;
        bool screenshotRequested = false;
        bool showPlayer = false;
        DVec3 playerPosition;
        float playerYaw = 0.0f;
        float playerHeadYaw = 0.0f;
        float playerHeadPitch = 0.0f;
        float playerWalkPhase = 0.0f;
        float playerWalkAmount = 0.0f;
        bool showFirstPersonHand = false;
        uint16_t heldItemId = 0;
        bool terrainWireframe = false;
        int climateOverlayMode = 0;
        int menuOverlayMode = 0;
        bool hudVisible = true;
        bool worldUpdateEnabled = false;
        bool gameSceneRenderEnabled = false;
        uint64_t worldTicks = 0;
    };
}
