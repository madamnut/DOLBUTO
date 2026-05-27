#pragma once

#include <cstdint>

namespace dolbuto::game
{
    inline constexpr uint32_t InvalidRadialMenuIndex = UINT32_MAX;

    struct RadialMenuRenderFrame
    {
        bool visible = false;
        uint32_t actionCount = 0;
        uint32_t candidateCount = 0;
        uint32_t selectedActionIndex = InvalidRadialMenuIndex;
        uint32_t selectedCandidateIndex = InvalidRadialMenuIndex;
    };
}
