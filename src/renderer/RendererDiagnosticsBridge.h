#pragma once

#include "camera/Camera.h"
#include "world/ClimateSystem.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace dolbuto
{
    class DebugOverlayText;
    class RendererTerrainRuntimeBridge;
    class TextRenderPath;
    namespace game
    {
        struct ClientRuntimeState;
    }
    struct RendererVulkanState;

    class RendererDiagnosticsBridge
    {
    public:
        RendererDiagnosticsBridge(
            game::ClientRuntimeState& client,
            const RendererVulkanState& vulkan,
            DebugOverlayText& debugOverlayText,
            TextRenderPath& textRenderPath,
            const RendererTerrainRuntimeBridge& terrainRuntimeBridge,
            const VkDeviceSize& localMemoryHeapSize,
            const uint32_t& localMemoryHeapIndex,
            const bool& memoryBudgetSupported);

        world::ClimateSystem climateSystem() const;
        void updateDebugTextBatch(std::string_view fpsText, std::string_view perfText);
        void updatePerformanceText(double cpuFrameMs);
        void updateTerrainDebugText();
        void updateVramText();

    private:
        game::ClientRuntimeState& client_;
        const RendererVulkanState& vulkan_;
        DebugOverlayText& debugOverlayText_;
        TextRenderPath& textRenderPath_;
        const RendererTerrainRuntimeBridge& terrainRuntimeBridge_;
        const VkDeviceSize& localMemoryHeapSize_;
        const uint32_t& localMemoryHeapIndex_;
        const bool& memoryBudgetSupported_;
    };
}
