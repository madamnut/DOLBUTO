#include "renderer/RendererUiRuntimeBridge.h"

#include "renderer/RendererAudioBridge.h"
#include "game/ClientRuntimeState.h"
#include "renderer/RendererRmlUiBackend.h"
#include "renderer/RendererVulkanState.h"

#include <string>
#include <utility>
#include <vector>

namespace dolbuto
{
    RendererUiRuntimeBridge::RendererUiRuntimeBridge(
        GLFWwindow* window,
        game::ClientRuntimeState& client,
        const RendererVulkanState& vulkan,
        RendererRmlUiBackend& rmlUiBackend,
        RendererAudioBridge& audioBridge,
        std::filesystem::path assetDirectory) :
        window_(window),
        client_(client),
        vulkan_(vulkan),
        rmlUiBackend_(rmlUiBackend),
        audioBridge_(audioBridge),
        assetDirectory_(std::move(assetDirectory))
    {
    }

    void RendererUiRuntimeBridge::initialize()
    {
        client_.ui.initialize(
            window_,
            assetDirectory_,
            static_cast<int>(vulkan_.swapchainExtent.width),
            static_cast<int>(vulkan_.swapchainExtent.height),
            rmlUiBackend_.renderInterface());
        client_.ui.setClickCallback([this]()
        {
            audioBridge_.playButtonClick();
        });
        client_.uiBridge.updateHotbarScopeClass();
        updateInventoryUi();
        client_.uiBridge.updateInventoryDebugSlots();
    }

    void RendererUiRuntimeBridge::shutdown()
    {
        client_.ui.shutdown();
    }

    bool RendererUiRuntimeBridge::render(VkCommandBuffer commandBuffer, int menuOverlayMode, bool hudVisible)
    {
        if (!client_.ui.available())
        {
            return false;
        }

        const int effectiveMenuOverlayMode = hudVisible ? menuOverlayMode : (menuOverlayMode == 0 ? -1 : menuOverlayMode);
        if (client_.ui.activeMenuOverlayMode() == 5 && effectiveMenuOverlayMode != 5)
        {
            closeInventoryInteraction();
        }
        rmlUiBackend_.beginFrame(commandBuffer);
        const bool rendered = client_.ui.render(
            effectiveMenuOverlayMode,
            static_cast<int>(vulkan_.swapchainExtent.width),
            static_cast<int>(vulkan_.swapchainExtent.height));
        rmlUiBackend_.endFrame();
        return rendered;
    }

    void RendererUiRuntimeBridge::mouseMove(double x, double y)
    {
        client_.uiBridge.processMouseMove(x, y, vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height);
    }

    void RendererUiRuntimeBridge::mouseButton(int button, bool pressed, int modifiers)
    {
        client_.uiBridge.processMouseButton(button, pressed, modifiers, vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height);
    }

    void RendererUiRuntimeBridge::mouseWheel(double yOffset)
    {
        client_.uiBridge.processMouseWheel(yOffset);
    }

    void RendererUiRuntimeBridge::textInput(unsigned int codepoint)
    {
        client_.uiBridge.processTextInput(codepoint);
    }

    void RendererUiRuntimeBridge::key(int key, bool pressed, int modifiers)
    {
        client_.uiBridge.processKey(key, pressed, modifiers, vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height);
    }

    void RendererUiRuntimeBridge::closeInventoryInteraction()
    {
        client_.uiBridge.closeInventoryInteraction(vulkan_.swapchainExtent.width, vulkan_.swapchainExtent.height);
    }

    void RendererUiRuntimeBridge::updateInventoryUi()
    {
        client_.uiBridge.updateInventoryUi();
    }

}
