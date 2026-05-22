#include "renderer/DebugOverlayText.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace dolbuto
{
    void DebugOverlayText::setHardwareInfo(std::string cpu, std::string gpu, std::string vulkan, std::string driver)
    {
        setDirtyIfChanged(cpuText_, std::move(cpu));
        setDirtyIfChanged(gpuText_, std::move(gpu));
        setDirtyIfChanged(vulkanText_, std::move(vulkan));
        setDirtyIfChanged(driverText_, std::move(driver));
    }

    void DebugOverlayText::setTerrainStats(uint32_t drawCount, uint32_t faceCount, uint32_t vertexCount)
    {
        setDirtyIfChanged(terrainDrawText_, "DRAWS: " + std::to_string(drawCount));
        setDirtyIfChanged(terrainFaceText_, "FACES: " + std::to_string(faceCount));
        setDirtyIfChanged(terrainVertexText_, "QUADS: " + std::to_string(vertexCount));
    }

    void DebugOverlayText::setFrameTimings(double cpuFrameMs, double gpuFrameMs, bool gpuAvailable)
    {
        std::ostringstream cpuText;
        cpuText << "CPU: " << std::fixed << std::setprecision(3) << cpuFrameMs << "MS";
        setDirtyIfChanged(cpuFrameText_, cpuText.str());

        if (gpuAvailable)
        {
            std::ostringstream gpuText;
            gpuText << "GPU: " << std::fixed << std::setprecision(3) << gpuFrameMs << "MS";
            setDirtyIfChanged(gpuFrameText_, gpuText.str());
        }
        else
        {
            setGpuUnavailable();
        }
    }

    void DebugOverlayText::setGpuUnavailable()
    {
        setDirtyIfChanged(gpuFrameText_, "GPU: N/A");
    }

    void DebugOverlayText::setVramText(std::string text)
    {
        setDirtyIfChanged(vramText_, std::move(text));
    }

    void DebugOverlayText::markDirty()
    {
        dirty_ = true;
    }

    void DebugOverlayText::buildBatch(TextRenderPath& textRenderPath, std::string_view fpsText, std::string_view perfText, std::string_view versionText, VkExtent2D extent)
    {
        (void)perfText;
        if (cachedFpsText_ != fpsText)
        {
            cachedFpsText_ = fpsText;
            dirty_ = true;
        }

        if (lastResolutionExtent_.width != extent.width || lastResolutionExtent_.height != extent.height)
        {
            lastResolutionExtent_ = extent;
            resolutionText_ = "RESOLUTION: " + std::to_string(extent.width) + " x " + std::to_string(extent.height);
            dirty_ = true;
        }

        if (!dirty_)
        {
            return;
        }

        batch_.outline.clear();
        batch_.fill.clear();
        batch_.outline.reserve(8192);
        batch_.fill.reserve(2048);

        const float rightX = static_cast<float>(extent.width) - 12.0f;
        textRenderPath.addText(batch_, cachedFpsText_, 12.0f, 24.0f, false, extent);
        textRenderPath.addText(batch_, versionText, rightX, 24.0f, true, extent);
        textRenderPath.addText(batch_, cpuText_, rightX, 46.0f, true, extent);
        textRenderPath.addText(batch_, gpuText_, rightX, 68.0f, true, extent);
        textRenderPath.addText(batch_, vulkanText_, rightX, 90.0f, true, extent);
        textRenderPath.addText(batch_, driverText_, rightX, 112.0f, true, extent);
        textRenderPath.addText(batch_, resolutionText_, rightX, 134.0f, true, extent);
        textRenderPath.addText(batch_, cpuFrameText_, rightX, 156.0f, true, extent);
        textRenderPath.addText(batch_, gpuFrameText_, rightX, 178.0f, true, extent);
        textRenderPath.addText(batch_, vramText_, rightX, 200.0f, true, extent);
        textRenderPath.addText(batch_, terrainDrawText_, rightX, 222.0f, true, extent);
        textRenderPath.addText(batch_, terrainFaceText_, rightX, 244.0f, true, extent);
        textRenderPath.addText(batch_, terrainVertexText_, rightX, 266.0f, true, extent);

        dirty_ = false;
    }

    const TextRenderPath::TextBatch& DebugOverlayText::batch() const
    {
        return batch_;
    }

    void DebugOverlayText::setDirtyIfChanged(std::string& target, std::string value)
    {
        if (target == value)
        {
            return;
        }
        target = std::move(value);
        dirty_ = true;
    }
}
