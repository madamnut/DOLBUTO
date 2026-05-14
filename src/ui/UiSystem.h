#pragma once

#include <RmlUi/Core.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace dolbuto::ui
{
    struct WorldListEntry
    {
        std::string name;
        std::string createdText;
        std::string lastPlayedText;
    };

    std::string escapeRml(std::string_view text);

    class UiSystem final : public Rml::EventListener
    {
    public:
        bool initialize(
            GLFWwindow* window,
            const std::filesystem::path& assetDirectory,
            int width,
            int height,
            Rml::RenderInterface* renderInterface);
        void shutdown();

        bool available() const;
        bool render(int menuOverlayMode, int width, int height);
        int activeMenuOverlayMode() const;

        Rml::Context* context() const;
        Rml::ElementDocument* lobbyDocument() const;
        Rml::ElementDocument* worldSelectDocument() const;
        Rml::ElementDocument* worldCreateDocument() const;
        Rml::ElementDocument* hudDocument() const;
        Rml::ElementDocument* inventoryDocument() const;
        Rml::ElementDocument* pauseDocument() const;

        void attachActionEvent(Rml::Element* element, const Rml::String& eventType);
        void setClickCallback(std::function<void()> callback);
        std::optional<std::string> consumeAction();
        std::string inputValue(std::string_view id) const;
        void setHotbarScopeClass(int selectedSlot);
        void setInventoryDebugSlots(std::string_view hotbarRml, std::string_view inventoryRml, bool visible);
        void setInventoryItems(std::string_view hotbarRml, std::string_view inventoryRml);
        void setInventoryCursorItem(std::string_view rml, bool visible);
        void hideItemTooltip();
        void showItemTooltip(std::string_view rml, int left, int top, int width, int height);
        void setWorldList(const std::vector<WorldListEntry>& worlds);
        void processMouseMove(double x, double y);
        void processMouseButton(int button, bool pressed, int modifiers);
        void processMouseWheel(double yOffset);
        void processTextInput(unsigned int codepoint);
        bool processKey(int key, bool pressed, int modifiers);

        void ProcessEvent(Rml::Event& event) override;

    private:
        void attachDocumentEvents(Rml::ElementDocument* document);
        void setDocument(int menuOverlayMode);
        void closeDocument(Rml::ElementDocument*& document);

        std::unique_ptr<Rml::SystemInterface> systemInterface_;
        std::function<void()> clickCallback_;
        bool initialized_ = false;
        Rml::Context* context_ = nullptr;
        Rml::ElementDocument* lobbyDocument_ = nullptr;
        Rml::ElementDocument* worldSelectDocument_ = nullptr;
        Rml::ElementDocument* worldCreateDocument_ = nullptr;
        Rml::ElementDocument* hudDocument_ = nullptr;
        Rml::ElementDocument* inventoryDocument_ = nullptr;
        Rml::ElementDocument* pauseDocument_ = nullptr;
        int activeMenuOverlayMode_ = -1;
        std::optional<std::string> pendingAction_;
        GLFWwindow* window_ = nullptr;
    };
}
