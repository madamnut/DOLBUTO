#pragma once

#include "camera/Camera.h"
#include "items/ItemData.h"
#include "renderer/DroppedItemRenderPath.h"
#include "world/WorldTypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace dolbuto
{
    class DroppedItemRenderCollector
    {
    public:
        struct Input
        {
            const Camera& camera;
            Vec3 cameraPosition{};
            std::size_t loadedItemCount = 0;
            const std::unordered_map<uint64_t, std::size_t>& droppedItemCountsByChunk;
            const std::vector<ItemDefinition>& itemDefinitions;
            const std::vector<DroppedItemRenderPath::ItemSpriteMesh>& itemSpriteMeshes;
            float aspect = 1.0f;
            float fovRadians = 1.0471975512f;
            float renderAlpha = 0.0f;
            std::function<const RuntimeChunk*(uint64_t)> findChunk;
            std::function<bool(uint16_t)> meshReady;
            std::function<uint8_t(int, int, int)> lightAtWorld;
        };

        static std::vector<DroppedItemRenderPath::RenderInstance> collect(const Input& input);
    };
}
