#include "renderer/ScreenPresentation.h"

#include "renderer/ClimateOverlayTextureBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace dolbuto
{
    namespace
    {
        constexpr uint64_t SkyTicksPerDay = 28800;
        constexpr double TwoPi = 6.283185307179586;
        constexpr double HalfPi = 1.5707963267948966;
        constexpr float MenuButtonWidthPixels = 240.0f;
        constexpr float MenuButtonHeightPixels = 56.0f;
        constexpr float LobbyBackgroundTilePixels = 96.0f;
    }

    void ScreenPresentation::drawSkySprites(
        VkCommandBuffer commandBuffer,
        const Camera& camera,
        float fovRadians,
        VkExtent2D extent,
        uint64_t worldTicks,
        const RendererAssetStore& assets,
        const SpriteRenderPath& sprites,
        VkPipelineLayout pipelineLayout,
        VkBuffer spriteVertexBuffer) const
    {
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const double dayPhase = static_cast<double>(worldTicks % SkyTicksPerDay) / static_cast<double>(SkyTicksPerDay);
        const double skyAngle = HalfPi - dayPhase * TwoPi;
        const std::array<float, 3> sunDirection{
            static_cast<float>(std::cos(skyAngle)),
            static_cast<float>(std::sin(skyAngle)),
            0.0f
        };
        const std::array<float, 3> moonDirection{-sunDirection[0], -sunDirection[1], -sunDirection[2]};

        SpriteRenderPath::Rect rect;
        if (projectSkyDirection(camera, aspect, fovRadians, sunDirection, rect))
        {
            sprites.draw(commandBuffer, pipelineLayout, spriteVertexBuffer, assets.sun, rect);
        }
        if (projectSkyDirection(camera, aspect, fovRadians, moonDirection, rect))
        {
            sprites.draw(commandBuffer, pipelineLayout, spriteVertexBuffer, assets.moon, rect);
        }
    }

    void ScreenPresentation::drawSceneComposite(
        VkCommandBuffer commandBuffer,
        const Texture& sceneTexture,
        VkExtent2D extent,
        const RendererAssetStore& assets,
        const SpriteRenderPath& sprites,
        VkPipeline spritePipeline,
        VkPipelineLayout spritePipelineLayout,
        VkBuffer spriteVertexBuffer,
        WaterOverlay waterOverlay,
        const Texture& waterBlurTexture,
        int climateOverlayMode) const
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, spritePipeline);
        SpriteRenderPath::Rect sceneRect{};
        sceneRect.halfWidth = 1.0f;
        sceneRect.halfHeight = 1.0f;
        sprites.draw(commandBuffer, spritePipelineLayout, spriteVertexBuffer, sceneTexture, sceneRect, {0.0f, 1.0f, 1.0f, -1.0f});

        const float line = std::clamp(waterOverlay.waterLineY, -1.0f, 1.0f);
        if (waterOverlay.active && line < 1.0f && waterBlurTexture.descriptorSet != VK_NULL_HANDLE)
        {
            SpriteRenderPath::Rect waterArea{};
            waterArea.centerY = (line + 1.0f) * 0.5f;
            waterArea.halfWidth = 1.0f;
            waterArea.halfHeight = (1.0f - line) * 0.5f;

            const float uvTop = (line + 1.0f) * 0.5f;
            const float uvBottom = 1.0f;
            sprites.draw(
                commandBuffer,
                spritePipelineLayout,
                spriteVertexBuffer,
                waterBlurTexture,
                waterArea,
                {0.0f, uvBottom, 1.0f, uvTop - uvBottom},
                {1.0f, 1.0f, 1.0f, std::clamp(waterOverlay.blurIntensity, 0.0f, 1.0f)});
            sprites.draw(commandBuffer, spritePipelineLayout, spriteVertexBuffer, assets.white, waterArea, {}, {0.18f, 0.55f, 0.70f, std::clamp(waterOverlay.tint, 0.0f, 1.0f)});
        }

        drawClimateOverlay(commandBuffer, climateOverlayMode, assets, sprites, extent, spritePipelineLayout, spriteVertexBuffer);
    }

    void ScreenPresentation::drawCrosshair(
        VkCommandBuffer commandBuffer,
        VkExtent2D extent,
        const RendererAssetStore& assets,
        const SpriteRenderPath& sprites,
        VkPipelineLayout pipelineLayout,
        VkBuffer spriteVertexBuffer) const
    {
        constexpr float CrosshairPixels = 32.0f;
        SpriteRenderPath::Rect crosshairRect{};
        crosshairRect.halfWidth = CrosshairPixels / static_cast<float>(extent.width);
        crosshairRect.halfHeight = CrosshairPixels / static_cast<float>(extent.height);
        sprites.draw(commandBuffer, pipelineLayout, spriteVertexBuffer, assets.crosshair, crosshairRect);
    }

    void ScreenPresentation::drawDebugText(
        VkCommandBuffer commandBuffer,
        const TextRenderPath::TextBatch& debugTextBatch,
        const RendererAssetStore& assets,
        TextRenderPath& text,
        VkExtent2D extent,
        VkPipelineLayout pipelineLayout) const
    {
        text.drawBatch(commandBuffer, debugTextBatch, assets.font, extent, pipelineLayout);
    }

    void ScreenPresentation::drawMenuOverlay(
        VkCommandBuffer commandBuffer,
        int menuOverlayMode,
        const RendererAssetStore& assets,
        const SpriteRenderPath& sprites,
        TextRenderPath& text,
        VkExtent2D extent,
        VkPipelineLayout pipelineLayout,
        VkBuffer spriteVertexBuffer)
    {
        if (menuOverlayMode == 0)
        {
            return;
        }

        const float width = static_cast<float>(extent.width);
        const float height = static_cast<float>(extent.height);
        const SpriteRenderPath::Rect fullScreenRect = rectFromPixels(extent, width * 0.5f, height * 0.5f, width, height);
        if (menuOverlayMode == 1 || menuOverlayMode == 3)
        {
            const SpriteRenderPath::UvRect tiledUv{0.0f, height / LobbyBackgroundTilePixels, width / LobbyBackgroundTilePixels, -height / LobbyBackgroundTilePixels};
            sprites.draw(commandBuffer, pipelineLayout, spriteVertexBuffer, assets.lobbyBackground, fullScreenRect, tiledUv, {1.0f, 1.0f, 1.0f, 1.0f});
        }

        const SpriteRenderPath::Rect dimRect = rectFromPixels(extent, width * 0.5f, height * 0.5f, width, height);
        sprites.draw(commandBuffer, pipelineLayout, spriteVertexBuffer, assets.white, dimRect, {}, {0.02f, 0.03f, 0.04f, 0.62f});

        if ((menuOverlayMode == 1 || menuOverlayMode == 3) && assets.lobbyTitle.width > 0 && assets.lobbyTitle.height > 0)
        {
            const float titleWidth = std::min(width * 0.58f, static_cast<float>(assets.lobbyTitle.width) * 2.0f);
            const float titleHeight = titleWidth * static_cast<float>(assets.lobbyTitle.height) / static_cast<float>(assets.lobbyTitle.width);
            const SpriteRenderPath::Rect titleRect = rectFromPixels(extent, width * 0.5f, height * 0.22f, titleWidth, titleHeight);
            sprites.draw(commandBuffer, pipelineLayout, spriteVertexBuffer, assets.lobbyTitle, titleRect, {0.0f, 1.0f, 1.0f, -1.0f});
        }

        if (menuOverlayMode == 3)
        {
            const std::array<float, 4> buttonYs = {height * 0.34f, height * 0.45f, height * 0.56f, height * 0.72f};
            for (size_t i = 0; i < buttonYs.size(); ++i)
            {
                const SpriteRenderPath::Rect button = rectFromPixels(extent, width * 0.5f, buttonYs[i], MenuButtonWidthPixels, MenuButtonHeightPixels);
                const SpriteRenderPath::Color color = i == buttonYs.size() - 1u ? SpriteRenderPath::Color{0.08f, 0.11f, 0.14f, 0.92f} : SpriteRenderPath::Color{0.12f, 0.18f, 0.22f, 0.92f};
                sprites.draw(commandBuffer, pipelineLayout, spriteVertexBuffer, assets.white, button, {}, color);
            }
        }
        else
        {
            const float firstButtonY = menuOverlayMode == 1 ? height * 0.45f : height * 0.46f;
            const float secondButtonY = menuOverlayMode == 1 ? height * 0.56f : height * 0.57f;
            const SpriteRenderPath::Rect firstButton = rectFromPixels(extent, width * 0.5f, firstButtonY, MenuButtonWidthPixels, MenuButtonHeightPixels);
            const SpriteRenderPath::Rect secondButton = rectFromPixels(extent, width * 0.5f, secondButtonY, MenuButtonWidthPixels, MenuButtonHeightPixels);
            sprites.draw(commandBuffer, pipelineLayout, spriteVertexBuffer, assets.white, firstButton, {}, {0.12f, 0.18f, 0.22f, 0.92f});
            sprites.draw(commandBuffer, pipelineLayout, spriteVertexBuffer, assets.white, secondButton, {}, {0.08f, 0.11f, 0.14f, 0.92f});
        }

        buildMenuTextBatch(menuOverlayMode, text, extent);
        text.drawBatch(commandBuffer, menuTextBatch_, assets.font, extent, pipelineLayout);
    }

    SpriteRenderPath::Rect ScreenPresentation::rectFromPixels(VkExtent2D extent, float centerX, float centerY, float width, float height)
    {
        SpriteRenderPath::Rect rect{};
        rect.centerX = centerX / static_cast<float>(extent.width) * 2.0f - 1.0f;
        rect.centerY = centerY / static_cast<float>(extent.height) * 2.0f - 1.0f;
        rect.halfWidth = width / static_cast<float>(extent.width);
        rect.halfHeight = height / static_cast<float>(extent.height);
        return rect;
    }

    bool ScreenPresentation::projectSkyDirection(const Camera& camera, float aspect, float fovRadians, const std::array<float, 3>& direction, SpriteRenderPath::Rect& rect)
    {
        Vec3 dir = normalize({direction[0], direction[1], direction[2]});
        const float x = -dot(dir, camera.right());
        const float y = dot(dir, camera.up());
        const float z = dot(dir, camera.forward());

        if (z <= 0.01f)
        {
            return false;
        }

        const float tanHalfFov = std::tan(fovRadians * 0.5f);
        rect.centerX = (x / z) / (tanHalfFov * aspect);
        rect.centerY = (y / z) / tanHalfFov;
        rect.halfWidth = 0.04f;
        rect.halfHeight = 0.04f * aspect;
        return rect.centerX > -1.2f && rect.centerX < 1.2f && rect.centerY > -1.2f && rect.centerY < 1.2f;
    }

    void ScreenPresentation::drawClimateOverlay(
        VkCommandBuffer commandBuffer,
        int mode,
        const RendererAssetStore& assets,
        const SpriteRenderPath& sprites,
        VkExtent2D extent,
        VkPipelineLayout pipelineLayout,
        VkBuffer spriteVertexBuffer) const
    {
        if (mode == ClimateOverlayTextureBuilder::Off)
        {
            return;
        }

        const Texture* texture = nullptr;
        if (mode == ClimateOverlayTextureBuilder::Temperature && assets.climateTemperatureOverlay.descriptorSet != VK_NULL_HANDLE)
        {
            texture = &assets.climateTemperatureOverlay;
        }
        else if (mode == ClimateOverlayTextureBuilder::Precipitation && assets.climatePrecipitationOverlay.descriptorSet != VK_NULL_HANDLE)
        {
            texture = &assets.climatePrecipitationOverlay;
        }
        else if (mode == ClimateOverlayTextureBuilder::Groundness && assets.terrainGroundnessOverlay.descriptorSet != VK_NULL_HANDLE)
        {
            texture = &assets.terrainGroundnessOverlay;
        }
        else if (mode == ClimateOverlayTextureBuilder::Smoothness && assets.terrainSmoothnessOverlay.descriptorSet != VK_NULL_HANDLE)
        {
            texture = &assets.terrainSmoothnessOverlay;
        }
        else if (mode == ClimateOverlayTextureBuilder::Weirdness && assets.terrainWeirdnessOverlay.descriptorSet != VK_NULL_HANDLE)
        {
            texture = &assets.terrainWeirdnessOverlay;
        }
        else if (mode == ClimateOverlayTextureBuilder::Pv && assets.terrainPvOverlay.descriptorSet != VK_NULL_HANDLE)
        {
            texture = &assets.terrainPvOverlay;
        }
        if (texture == nullptr)
        {
            return;
        }

        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        SpriteRenderPath::Rect rect{};
        rect.halfHeight = 0.82f;
        rect.halfWidth = std::min(0.92f, rect.halfHeight / std::max(aspect, 0.001f));
        sprites.draw(commandBuffer, pipelineLayout, spriteVertexBuffer, *texture, rect, {}, {1.0f, 1.0f, 1.0f, 1.0f});
    }

    void ScreenPresentation::buildMenuTextBatch(int menuOverlayMode, TextRenderPath& text, VkExtent2D extent)
    {
        if (cachedMenuOverlayMode_ == menuOverlayMode &&
            cachedMenuExtent_.width == extent.width &&
            cachedMenuExtent_.height == extent.height)
        {
            return;
        }

        cachedMenuOverlayMode_ = menuOverlayMode;
        cachedMenuExtent_ = extent;
        menuTextBatch_.outline.clear();
        menuTextBatch_.fill.clear();
        menuTextBatch_.outline.reserve(1024);
        menuTextBatch_.fill.reserve(512);

        const float width = static_cast<float>(extent.width);
        const float height = static_cast<float>(extent.height);
        const float centerX = width * 0.5f;

        if (menuOverlayMode == 1)
        {
            text.addText(menuTextBatch_, "START", centerX - text.measureText("START") * 0.5f, height * 0.45f + 6.0f, false, extent);
            text.addText(menuTextBatch_, "EXIT", centerX - text.measureText("EXIT") * 0.5f, height * 0.56f + 6.0f, false, extent);
        }
        else if (menuOverlayMode == 3)
        {
            text.addText(menuTextBatch_, "SELECT WORLD", centerX - text.measureText("SELECT WORLD") * 0.5f, height * 0.27f, false, extent);
            text.addText(menuTextBatch_, "WORLD 1", centerX - text.measureText("WORLD 1") * 0.5f, height * 0.34f + 6.0f, false, extent);
            text.addText(menuTextBatch_, "WORLD 2", centerX - text.measureText("WORLD 2") * 0.5f, height * 0.45f + 6.0f, false, extent);
            text.addText(menuTextBatch_, "WORLD 3", centerX - text.measureText("WORLD 3") * 0.5f, height * 0.56f + 6.0f, false, extent);
            text.addText(menuTextBatch_, "BACK", centerX - text.measureText("BACK") * 0.5f, height * 0.72f + 6.0f, false, extent);
        }
        else if (menuOverlayMode == 2)
        {
            text.addText(menuTextBatch_, "RESUME", centerX - text.measureText("RESUME") * 0.5f, height * 0.46f + 6.0f, false, extent);
            text.addText(menuTextBatch_, "EXIT", centerX - text.measureText("EXIT") * 0.5f, height * 0.57f + 6.0f, false, extent);
        }
    }
}
