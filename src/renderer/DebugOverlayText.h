#pragma once

#include "renderer/TextRenderPath.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace dolbuto
{
    class DebugOverlayText
    {
    public:
        void setHardwareInfo(std::string cpu, std::string gpu, std::string vulkan, std::string driver);
        void setTerrainStats(uint32_t drawCount, uint32_t faceCount, uint32_t vertexCount);
        void setFrameTimings(double cpuFrameMs, double gpuFrameMs, bool gpuAvailable);
        void setGpuUnavailable();
        void setVramText(std::string text);
        void markDirty();

        void buildBatch(TextRenderPath& textRenderPath, std::string_view fpsText, std::string_view versionText, VkExtent2D extent);
        const TextRenderPath::TextBatch& batch() const;

    private:
        void setDirtyIfChanged(std::string& target, std::string value);

        std::string cpuText_;
        std::string gpuText_;
        std::string vulkanText_;
        std::string driverText_;
        std::string resolutionText_;
        std::string terrainDrawText_ = "DRAWS: 0";
        std::string terrainFaceText_ = "FACES: 0";
        std::string terrainVertexText_ = "QUADS: 0";
        std::string cpuFrameText_ = "CPU: ---.---MS";
        std::string gpuFrameText_ = "GPU: ---.---MS";
        std::string vramText_ = "VRAM: 0MB";
        std::string cachedFpsText_;
        VkExtent2D lastResolutionExtent_{};
        TextRenderPath::TextBatch batch_;
        bool dirty_ = true;
    };
}
