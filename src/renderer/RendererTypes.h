#pragma once

#include <cstdint>
#include <vector>

namespace dolbuto
{
    struct QueueFamilyIndices
    {
        uint32_t graphics = UINT32_MAX;
        uint32_t present = UINT32_MAX;

        bool complete() const;
    };

    struct UiVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        float u = 0.0f;
        float v = 0.0f;
    };

    struct UiPush
    {
        float viewportWidth = 1.0f;
        float viewportHeight = 1.0f;
        float translateX = 0.0f;
        float translateY = 0.0f;
    };

    struct UiGeometry
    {
        std::vector<UiVertex> vertices;
        std::vector<uint32_t> indices;
    };

    struct LineVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct TerrainPush
    {
        float mvp[16]{};
        float cameraPosition[4]{};
        float fluidWaterParams[4]{};
        float dynamicLightParams[4]{};
    };
}
