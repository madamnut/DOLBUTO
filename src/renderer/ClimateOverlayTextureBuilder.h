#pragma once

#include "world/ClimateSystem.h"

#include <vector>

namespace dolbuto
{
    class ClimateOverlayTextureBuilder
    {
    public:
        static constexpr int OverlaySize = 1024;

        static std::vector<unsigned char> buildPixels(int mode, const world::ClimateSystem& climate);
    };
}
