#pragma once

#include "world/ClimateSystem.h"

#include <vector>

namespace dolbuto
{
    class ClimateOverlayTextureBuilder
    {
    public:
        static constexpr int OverlaySize = 1024;

        enum Mode
        {
            Off = 0,
            Temperature = 1,
            Precipitation = 2,
            Groundness = 3,
            Smoothness = 4,
            Weirdness = 5,
            Pv = 6,
            Count = 7
        };

        static std::vector<unsigned char> buildPixels(int mode, const world::ClimateSystem& climate);
    };
}
