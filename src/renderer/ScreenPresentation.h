#pragma once

#include "camera/Camera.h"
#include "renderer/RendererAssetStore.h"
#include "renderer/SpriteRenderPath.h"
#include "renderer/TextRenderPath.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace dolbuto
{
    class ScreenPresentation
    {
    public:
        struct WaterOverlay
        {
            bool active = false;
            float waterLineY = -1.0f;
            float blurSpread = 1.0f;
            float blurIntensity = 0.75f;
            float tint = 0.025f;
        };

        struct BloomOverlay
        {
            bool active = false;
            float intensity = 0.35f;
        };

        struct OxygenOverlay
        {
            float effect = 0.0f;
        };

        void drawSkySprites(
            VkCommandBuffer commandBuffer,
            const Camera& camera,
            float fovRadians,
            VkExtent2D extent,
            uint64_t worldTicks,
            const RendererAssetStore& assets,
            const SpriteRenderPath& sprites,
            VkPipelineLayout pipelineLayout,
            VkBuffer spriteVertexBuffer) const;

        void drawSceneComposite(
            VkCommandBuffer commandBuffer,
            const Texture& sceneTexture,
            VkExtent2D extent,
            const RendererAssetStore& assets,
            const SpriteRenderPath& sprites,
            VkPipeline spritePipeline,
            VkPipeline additiveSpritePipeline,
            VkPipelineLayout spritePipelineLayout,
            VkBuffer spriteVertexBuffer,
            BloomOverlay bloomOverlay,
            const Texture& bloomTexture,
            WaterOverlay waterOverlay,
            const Texture& waterBlurTexture,
            OxygenOverlay oxygenOverlay,
            int climateOverlayMode) const;

        void drawCrosshair(
            VkCommandBuffer commandBuffer,
            VkExtent2D extent,
            const RendererAssetStore& assets,
            const SpriteRenderPath& sprites,
            VkPipelineLayout pipelineLayout,
            VkBuffer spriteVertexBuffer) const;

        void drawDebugText(
            VkCommandBuffer commandBuffer,
            const TextRenderPath::TextBatch& debugTextBatch,
            const RendererAssetStore& assets,
            TextRenderPath& text,
            VkExtent2D extent,
            VkPipelineLayout pipelineLayout) const;

        void drawMenuOverlay(
            VkCommandBuffer commandBuffer,
            int menuOverlayMode,
            const RendererAssetStore& assets,
            const SpriteRenderPath& sprites,
            TextRenderPath& text,
            VkExtent2D extent,
            VkPipelineLayout pipelineLayout,
            VkBuffer spriteVertexBuffer);

    private:
        static SpriteRenderPath::Rect rectFromPixels(VkExtent2D extent, float centerX, float centerY, float width, float height);
        static bool projectSkyDirection(const Camera& camera, float aspect, float fovRadians, const std::array<float, 3>& direction, SpriteRenderPath::Rect& rect);
        void drawClimateOverlay(
            VkCommandBuffer commandBuffer,
            int mode,
            const RendererAssetStore& assets,
            const SpriteRenderPath& sprites,
            VkExtent2D extent,
            VkPipelineLayout pipelineLayout,
            VkBuffer spriteVertexBuffer) const;
        void buildMenuTextBatch(int menuOverlayMode, TextRenderPath& text, VkExtent2D extent);

        TextRenderPath::TextBatch menuTextBatch_;
        int cachedMenuOverlayMode_ = -1;
        VkExtent2D cachedMenuExtent_{};
    };
}
