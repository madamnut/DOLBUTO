#include "renderer/Renderer.h"

#include "renderer/ClimateOverlayTextureBuilder.h"
#include "renderer/RendererDiagnosticsBridge.h"

#include <vector>

namespace dolbuto
{
    void Renderer::ensureClimateOverlayTexture(int mode)
    {
        if (mode == ClimateOverlayTextureBuilder::Temperature && !client_.climateTemperatureOverlayReady)
        {
            const std::vector<unsigned char> pixels = ClimateOverlayTextureBuilder::buildPixels(mode, diagnosticsBridge_->climateSystem());
            rendererAssets_.climateTemperatureOverlay = gpuResources_.createTextureFromRgba(pixels.data(), ClimateOverlayTextureBuilder::OverlaySize, ClimateOverlayTextureBuilder::OverlaySize, VK_FORMAT_R8G8B8A8_SRGB);
            client_.climateTemperatureOverlayReady = true;
        }
        else if (mode == ClimateOverlayTextureBuilder::Precipitation && !client_.climatePrecipitationOverlayReady)
        {
            const std::vector<unsigned char> pixels = ClimateOverlayTextureBuilder::buildPixels(mode, diagnosticsBridge_->climateSystem());
            rendererAssets_.climatePrecipitationOverlay = gpuResources_.createTextureFromRgba(pixels.data(), ClimateOverlayTextureBuilder::OverlaySize, ClimateOverlayTextureBuilder::OverlaySize, VK_FORMAT_R8G8B8A8_SRGB);
            client_.climatePrecipitationOverlayReady = true;
        }
        else if (mode == ClimateOverlayTextureBuilder::Groundness && !client_.terrainGroundnessOverlayReady)
        {
            const std::vector<unsigned char> pixels = ClimateOverlayTextureBuilder::buildPixels(mode, diagnosticsBridge_->climateSystem());
            rendererAssets_.terrainGroundnessOverlay = gpuResources_.createTextureFromRgba(pixels.data(), ClimateOverlayTextureBuilder::OverlaySize, ClimateOverlayTextureBuilder::OverlaySize, VK_FORMAT_R8G8B8A8_SRGB);
            client_.terrainGroundnessOverlayReady = true;
        }
        else if (mode == ClimateOverlayTextureBuilder::Smoothness && !client_.terrainSmoothnessOverlayReady)
        {
            const std::vector<unsigned char> pixels = ClimateOverlayTextureBuilder::buildPixels(mode, diagnosticsBridge_->climateSystem());
            rendererAssets_.terrainSmoothnessOverlay = gpuResources_.createTextureFromRgba(pixels.data(), ClimateOverlayTextureBuilder::OverlaySize, ClimateOverlayTextureBuilder::OverlaySize, VK_FORMAT_R8G8B8A8_SRGB);
            client_.terrainSmoothnessOverlayReady = true;
        }
        else if (mode == ClimateOverlayTextureBuilder::Weirdness && !client_.terrainWeirdnessOverlayReady)
        {
            const std::vector<unsigned char> pixels = ClimateOverlayTextureBuilder::buildPixels(mode, diagnosticsBridge_->climateSystem());
            rendererAssets_.terrainWeirdnessOverlay = gpuResources_.createTextureFromRgba(pixels.data(), ClimateOverlayTextureBuilder::OverlaySize, ClimateOverlayTextureBuilder::OverlaySize, VK_FORMAT_R8G8B8A8_SRGB);
            client_.terrainWeirdnessOverlayReady = true;
        }
        else if (mode == ClimateOverlayTextureBuilder::Pv && !client_.terrainPvOverlayReady)
        {
            const std::vector<unsigned char> pixels = ClimateOverlayTextureBuilder::buildPixels(mode, diagnosticsBridge_->climateSystem());
            rendererAssets_.terrainPvOverlay = gpuResources_.createTextureFromRgba(pixels.data(), ClimateOverlayTextureBuilder::OverlaySize, ClimateOverlayTextureBuilder::OverlaySize, VK_FORMAT_R8G8B8A8_SRGB);
            client_.terrainPvOverlayReady = true;
        }
    }
}
