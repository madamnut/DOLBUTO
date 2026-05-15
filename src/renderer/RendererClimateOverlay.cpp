#include "renderer/Renderer.h"

#include "renderer/ClimateOverlayTextureBuilder.h"
#include "renderer/RendererDiagnosticsBridge.h"

#include <vector>

namespace dolbuto
{
    void Renderer::ensureClimateOverlayTexture(int mode)
    {
        if (mode == 1 && !client_.climateTemperatureOverlayReady)
        {
            const std::vector<unsigned char> pixels = ClimateOverlayTextureBuilder::buildPixels(mode, diagnosticsBridge_->climateSystem());
            rendererAssets_.climateTemperatureOverlay = gpuResources_.createTextureFromRgba(pixels.data(), ClimateOverlayTextureBuilder::OverlaySize, ClimateOverlayTextureBuilder::OverlaySize, VK_FORMAT_R8G8B8A8_SRGB);
            client_.climateTemperatureOverlayReady = true;
        }
        else if (mode == 2 && !client_.climatePrecipitationOverlayReady)
        {
            const std::vector<unsigned char> pixels = ClimateOverlayTextureBuilder::buildPixels(mode, diagnosticsBridge_->climateSystem());
            rendererAssets_.climatePrecipitationOverlay = gpuResources_.createTextureFromRgba(pixels.data(), ClimateOverlayTextureBuilder::OverlaySize, ClimateOverlayTextureBuilder::OverlaySize, VK_FORMAT_R8G8B8A8_SRGB);
            client_.climatePrecipitationOverlayReady = true;
        }
    }
}
