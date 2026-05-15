#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <filesystem>

namespace dolbuto
{
    class RendererAudioBridge;
    class RendererRmlUiBackend;
    namespace game
    {
        struct ClientRuntimeState;
    }
    struct RendererVulkanState;

    class RendererUiRuntimeBridge
    {
    public:
        RendererUiRuntimeBridge(
            GLFWwindow* window,
            game::ClientRuntimeState& client,
            const RendererVulkanState& vulkan,
            RendererRmlUiBackend& rmlUiBackend,
            RendererAudioBridge& audioBridge,
            std::filesystem::path assetDirectory);

        void initialize();
        void shutdown();
        bool render(VkCommandBuffer commandBuffer, int menuOverlayMode, bool hudVisible);
        void mouseMove(double x, double y);
        void mouseButton(int button, bool pressed, int modifiers);
        void mouseWheel(double yOffset);
        void textInput(unsigned int codepoint);
        void key(int key, bool pressed, int modifiers);
        void closeInventoryInteraction();
        void updateInventoryUi();

    private:
        GLFWwindow* window_ = nullptr;
        game::ClientRuntimeState& client_;
        const RendererVulkanState& vulkan_;
        RendererRmlUiBackend& rmlUiBackend_;
        RendererAudioBridge& audioBridge_;
        std::filesystem::path assetDirectory_;
    };
}
