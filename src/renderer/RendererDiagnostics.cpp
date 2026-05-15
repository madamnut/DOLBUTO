#include "renderer/RendererDiagnosticsBridge.h"

#include "renderer/DebugOverlayText.h"
#include "game/ClientRuntimeState.h"
#include "renderer/RendererTerrainRuntimeBridge.h"
#include "renderer/RendererVulkanState.h"
#include "renderer/TextRenderPath.h"

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace dolbuto
{
    namespace
    {
        constexpr double PerformanceSampleSeconds = 0.5;
        constexpr const char* VersionText = "DOLBUTO 0.0.0.2";
    }

    RendererDiagnosticsBridge::RendererDiagnosticsBridge(
        game::ClientRuntimeState& client,
        const RendererVulkanState& vulkan,
        DebugOverlayText& debugOverlayText,
        TextRenderPath& textRenderPath,
        const RendererTerrainRuntimeBridge& terrainRuntimeBridge,
        const VkDeviceSize& localMemoryHeapSize,
        const uint32_t& localMemoryHeapIndex,
        const bool& memoryBudgetSupported) :
        client_(client),
        vulkan_(vulkan),
        debugOverlayText_(debugOverlayText),
        textRenderPath_(textRenderPath),
        terrainRuntimeBridge_(terrainRuntimeBridge),
        localMemoryHeapSize_(localMemoryHeapSize),
        localMemoryHeapIndex_(localMemoryHeapIndex),
        memoryBudgetSupported_(memoryBudgetSupported)
    {
    }

    world::ClimateSystem RendererDiagnosticsBridge::climateSystem() const
    {
        return world::ClimateSystem(terrainRuntimeBridge_.terrainBuilderConfig());
    }

    void RendererDiagnosticsBridge::updateDebugTextBatch(std::string_view fpsText)
    {
        debugOverlayText_.buildBatch(textRenderPath_, fpsText, VersionText, vulkan_.swapchainExtent);
    }

    void RendererDiagnosticsBridge::updatePerformanceText(double cpuFrameMs)
    {
        const auto now = std::chrono::steady_clock::now();
        if (client_.diagnostics.performanceSampleStart == std::chrono::steady_clock::time_point{})
        {
            client_.diagnostics.performanceSampleStart = now;
        }

        client_.diagnostics.accumulatedCpuFrameMs += cpuFrameMs;
        client_.diagnostics.accumulatedGpuFrameMs += vulkan_.lastGpuFrameMs;
        ++client_.diagnostics.performanceSampleCount;

        const std::chrono::duration<double> elapsed = now - client_.diagnostics.performanceSampleStart;
        if (elapsed.count() < PerformanceSampleSeconds)
        {
            return;
        }

        const double sampleCount = static_cast<double>(std::max<uint32_t>(client_.diagnostics.performanceSampleCount, 1));
        debugOverlayText_.setFrameTimings(
            client_.diagnostics.accumulatedCpuFrameMs / sampleCount,
            client_.diagnostics.accumulatedGpuFrameMs / sampleCount,
            vulkan_.timestampSupported);
        updateVramText();

        client_.diagnostics.accumulatedCpuFrameMs = 0.0;
        client_.diagnostics.accumulatedGpuFrameMs = 0.0;
        client_.diagnostics.performanceSampleCount = 0;
        client_.diagnostics.performanceSampleStart = now;
    }

    void RendererDiagnosticsBridge::updateTerrainDebugText()
    {
        if (client_.diagnostics.terrainDebugInitialized)
        {
            return;
        }

        client_.diagnostics.terrainDebugInitialized = true;
        debugOverlayText_.setTerrainStats(2, 0, 9);
    }

    void RendererDiagnosticsBridge::updateVramText()
    {
        if (localMemoryHeapIndex_ == UINT32_MAX)
        {
            debugOverlayText_.setVramText("VRAM: N/A");
            return;
        }

        if (memoryBudgetSupported_)
        {
            VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
            budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

            VkPhysicalDeviceMemoryProperties2 properties{};
            properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
            properties.pNext = &budget;

            const auto getMemoryProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(
                vkGetInstanceProcAddr(vulkan_.instance, "vkGetPhysicalDeviceMemoryProperties2"));
            if (getMemoryProperties2 != nullptr)
            {
                getMemoryProperties2(vulkan_.physicalDevice, &properties);
            }
            else
            {
                const auto getMemoryProperties2Khr = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2KHR>(
                    vkGetInstanceProcAddr(vulkan_.instance, "vkGetPhysicalDeviceMemoryProperties2KHR"));
                if (getMemoryProperties2Khr == nullptr)
                {
                    debugOverlayText_.setVramText("VRAM: " + std::to_string(static_cast<uint64_t>(localMemoryHeapSize_ / (1024u * 1024u))) + "MB");
                    return;
                }
                getMemoryProperties2Khr(vulkan_.physicalDevice, reinterpret_cast<VkPhysicalDeviceMemoryProperties2KHR*>(&properties));
            }

            const uint64_t usedMb = static_cast<uint64_t>(budget.heapUsage[localMemoryHeapIndex_] / (1024u * 1024u));
            const uint64_t budgetMb = static_cast<uint64_t>(budget.heapBudget[localMemoryHeapIndex_] / (1024u * 1024u));
            debugOverlayText_.setVramText("VRAM: " + std::to_string(usedMb) + " / " + std::to_string(budgetMb) + "MB");
            return;
        }

        debugOverlayText_.setVramText("VRAM: " + std::to_string(static_cast<uint64_t>(localMemoryHeapSize_ / (1024u * 1024u))) + "MB");
    }
}
