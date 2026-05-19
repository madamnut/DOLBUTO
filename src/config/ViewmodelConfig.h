#pragma once

namespace dolbuto::config
{
    struct ViewmodelHandConfig
    {
        float x = 0.46f;
        float y = -0.42f;
        float z = 0.88f;
        float scale = 1.62f;
        float rotationX = -1.0f;
        float rotationY = 0.0f;
        float rotationZ = 0.0f;
    };

    struct ViewmodelHeldItemConfig
    {
        float x = 0.28f;
        float y = -0.22f;
        float z = 0.86f;
        float scale = 1.0f;
        float rotationX = -1.57079632679f;
        float rotationY = -0.72f;
        float rotationZ = -0.24f;
    };

    struct ViewmodelConfig
    {
        ViewmodelHandConfig hand;
        ViewmodelHeldItemConfig heldItem;
    };
}
