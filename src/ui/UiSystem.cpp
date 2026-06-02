#include "ui/UiSystem.h"

#include "platform/Log.h"
#include "ui/RmlInput.h"

#include <GLFW/glfw3.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Unit.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <utility>

namespace dolbuto::ui
{
    namespace
    {
        class RmlGlfwSystemInterface final : public Rml::SystemInterface
        {
        public:
            explicit RmlGlfwSystemInterface(GLFWwindow* window)
                : window_(window)
            {
            }

            double GetElapsedTime() override
            {
                return glfwGetTime();
            }

            void SetClipboardText(const Rml::String& text) override
            {
                if (window_ != nullptr)
                {
                    glfwSetClipboardString(window_, text.c_str());
                }
            }

            void GetClipboardText(Rml::String& text) override
            {
                text.clear();
                if (window_ == nullptr)
                {
                    return;
                }

                const char* clipboard = glfwGetClipboardString(window_);
                if (clipboard != nullptr)
                {
                    text = clipboard;
                }
            }

        private:
            GLFWwindow* window_ = nullptr;
        };

        int statBarHeight(int value, int maxValue)
        {
            const int clampedMax = std::max(1, maxValue);
            const int clampedValue = std::clamp(value, 0, clampedMax);
            constexpr int BarPixelHeight = 192;
            return static_cast<int>(std::round(static_cast<double>(BarPixelHeight) * static_cast<double>(clampedValue) / static_cast<double>(clampedMax)));
        }

        void setStatBarHeight(Rml::ElementDocument* document, const char* elementId, int value, int maxValue)
        {
            if (Rml::Element* fill = document->GetElementById(elementId))
            {
                fill->SetProperty(Rml::PropertyId::Height, Rml::Property(static_cast<float>(statBarHeight(value, maxValue)), Rml::Unit::PX));
            }
        }
    }

    std::string escapeRml(std::string_view text)
    {
        std::string escaped;
        escaped.reserve(text.size());
        for (const char c : text)
        {
            switch (c)
            {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            default: escaped.push_back(c); break;
            }
        }
        return escaped;
    }

    bool UiSystem::initialize(
        GLFWwindow* window,
        const std::filesystem::path& assetDirectory,
        int width,
        int height,
        Rml::RenderInterface* renderInterface)
    {
        window_ = window;
        systemInterface_ = std::make_unique<RmlGlfwSystemInterface>(window);
        Rml::SetSystemInterface(systemInterface_.get());
        Rml::SetRenderInterface(renderInterface);
        if (!Rml::Initialise())
        {
            log::warn("RmlUi initialization failed.");
            Rml::SetRenderInterface(nullptr);
            Rml::SetSystemInterface(nullptr);
            systemInterface_.reset();
            return false;
        }

        initialized_ = true;
        const std::filesystem::path fontPath = assetDirectory / "fonts" / "VCR_OSD_MONO.ttf";
        if (!Rml::LoadFontFace(fontPath.string(), true))
        {
            log::warn("RmlUi font load failed: " + fontPath.string());
        }

        context_ = Rml::CreateContext("main", Rml::Vector2i(width, height), renderInterface);
        if (context_ == nullptr)
        {
            log::warn("RmlUi context creation failed.");
            return false;
        }

        const std::filesystem::path uiDir = assetDirectory / "ui";
        lobbyDocument_ = context_->LoadDocument((uiDir / "lobby.rml").string());
        worldSelectDocument_ = context_->LoadDocument((uiDir / "world_select.rml").string());
        worldCreateDocument_ = context_->LoadDocument((uiDir / "world_create.rml").string());
        hudDocument_ = context_->LoadDocument((uiDir / "hud.rml").string());
        inventoryDocument_ = context_->LoadDocument((uiDir / "inventory.rml").string());
        pauseDocument_ = context_->LoadDocument((uiDir / "pause.rml").string());
        optionsDocument_ = context_->LoadDocument((uiDir / "options.rml").string());

        attachDocumentEvents(lobbyDocument_);
        attachDocumentEvents(worldSelectDocument_);
        attachDocumentEvents(worldCreateDocument_);
        attachDocumentEvents(hudDocument_);
        attachDocumentEvents(inventoryDocument_);
        attachDocumentEvents(pauseDocument_);
        attachDocumentEvents(optionsDocument_);

        setDocument(0);
        return true;
    }

    void UiSystem::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        closeDocument(lobbyDocument_);
        closeDocument(worldSelectDocument_);
        closeDocument(worldCreateDocument_);
        closeDocument(hudDocument_);
        closeDocument(inventoryDocument_);
        closeDocument(pauseDocument_);
        closeDocument(optionsDocument_);

        if (context_ != nullptr)
        {
            Rml::RemoveContext("main");
            context_ = nullptr;
        }

        Rml::Shutdown();
        Rml::SetRenderInterface(nullptr);
        Rml::SetSystemInterface(nullptr);
        systemInterface_.reset();
        window_ = nullptr;
        initialized_ = false;
        activeMenuOverlayMode_ = -1;
        pendingAction_.reset();
    }

    bool UiSystem::available() const
    {
        return initialized_ && context_ != nullptr;
    }

    bool UiSystem::render(int menuOverlayMode, int width, int height)
    {
        if (!available())
        {
            return false;
        }

        setDocument(menuOverlayMode);
        context_->SetDimensions(Rml::Vector2i(width, height));
        context_->Update();
        context_->Render();
        return true;
    }

    int UiSystem::activeMenuOverlayMode() const
    {
        return activeMenuOverlayMode_;
    }

    Rml::Context* UiSystem::context() const
    {
        return context_;
    }

    Rml::ElementDocument* UiSystem::lobbyDocument() const
    {
        return lobbyDocument_;
    }

    Rml::ElementDocument* UiSystem::worldSelectDocument() const
    {
        return worldSelectDocument_;
    }

    Rml::ElementDocument* UiSystem::worldCreateDocument() const
    {
        return worldCreateDocument_;
    }

    Rml::ElementDocument* UiSystem::hudDocument() const
    {
        return hudDocument_;
    }

    Rml::ElementDocument* UiSystem::inventoryDocument() const
    {
        return inventoryDocument_;
    }

    Rml::ElementDocument* UiSystem::pauseDocument() const
    {
        return pauseDocument_;
    }

    Rml::ElementDocument* UiSystem::optionsDocument() const
    {
        return optionsDocument_;
    }

    void UiSystem::attachActionEvent(Rml::Element* element, const Rml::String& eventType)
    {
        if (element != nullptr)
        {
            element->AddEventListener(eventType, this);
        }
    }

    void UiSystem::setClickCallback(std::function<void()> callback)
    {
        clickCallback_ = std::move(callback);
    }

    std::optional<std::string> UiSystem::consumeAction()
    {
        std::optional<std::string> action = std::move(pendingAction_);
        pendingAction_.reset();
        return action;
    }

    std::string UiSystem::inputValue(std::string_view id) const
    {
        if (context_ == nullptr)
        {
            return {};
        }

        for (Rml::ElementDocument* document : {lobbyDocument_, worldSelectDocument_, worldCreateDocument_, hudDocument_, inventoryDocument_, pauseDocument_, optionsDocument_})
        {
            if (document == nullptr)
            {
                continue;
            }
            Rml::Element* element = document->GetElementById(std::string(id));
            if (element == nullptr)
            {
                continue;
            }
            if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(element))
            {
                return input->GetValue();
            }
            return element->GetAttribute<Rml::String>("value", "");
        }

        return {};
    }

    std::string UiSystem::chatInputValue() const
    {
        return inputValue("chat-input");
    }

    void UiSystem::setChatVisible(bool inputVisible, bool hasMessages)
    {
        if (hudDocument_ == nullptr)
        {
            return;
        }

        Rml::Element* panel = hudDocument_->GetElementById("chat-panel");
        if (panel != nullptr)
        {
            panel->SetAttribute("class", (inputVisible || hasMessages) ? "chat-panel" : "chat-panel ui-hidden");
        }

        if (Rml::Element* log = hudDocument_->GetElementById("chat-log"))
        {
            log->SetAttribute("class", inputVisible ? "chat-log" : "chat-log-passive");
        }

        if (Rml::Element* input = hudDocument_->GetElementById("chat-input"))
        {
            input->SetAttribute("class", inputVisible ? "chat-input" : "chat-input ui-hidden");
            if (!inputVisible)
            {
                input->Blur();
            }
        }
    }

    void UiSystem::setChatMessages(std::string_view rml)
    {
        if (hudDocument_ == nullptr)
        {
            return;
        }

        if (Rml::Element* log = hudDocument_->GetElementById("chat-log"))
        {
            log->SetInnerRML(std::string(rml));
        }
    }

    void UiSystem::clearChatInput()
    {
        if (hudDocument_ == nullptr)
        {
            return;
        }

        if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(hudDocument_->GetElementById("chat-input")))
        {
            input->SetValue("");
        }
    }

    void UiSystem::focusChatInput()
    {
        if (hudDocument_ == nullptr)
        {
            return;
        }

        if (Rml::Element* input = hudDocument_->GetElementById("chat-input"))
        {
            input->Focus();
        }
    }

    void UiSystem::setOptionsVolumes(int bgmPercent, int sfxPercent)
    {
        if (optionsDocument_ == nullptr)
        {
            return;
        }

        suppressOptionChangeEvents_ = true;
        if (Rml::Element* value = optionsDocument_->GetElementById("bgm-volume-value"))
        {
            value->SetInnerRML(std::to_string(bgmPercent) + "%");
        }
        if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(optionsDocument_->GetElementById("bgm-volume-slider")))
        {
            input->SetValue(std::to_string(bgmPercent));
        }
        if (Rml::Element* value = optionsDocument_->GetElementById("sfx-volume-value"))
        {
            value->SetInnerRML(std::to_string(sfxPercent) + "%");
        }
        if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(optionsDocument_->GetElementById("sfx-volume-slider")))
        {
            input->SetValue(std::to_string(sfxPercent));
        }
        suppressOptionChangeEvents_ = false;
    }

    void UiSystem::setOptionsFov(int fovDegrees)
    {
        if (optionsDocument_ == nullptr)
        {
            return;
        }

        suppressOptionChangeEvents_ = true;
        if (Rml::Element* value = optionsDocument_->GetElementById("fov-value"))
        {
            value->SetInnerRML(std::to_string(fovDegrees));
        }
        if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(optionsDocument_->GetElementById("fov-slider")))
        {
            input->SetValue(std::to_string(fovDegrees));
        }
        suppressOptionChangeEvents_ = false;
    }

    void UiSystem::setOptionsViewBobbing(bool enabled)
    {
        if (optionsDocument_ == nullptr)
        {
            return;
        }

        if (Rml::Element* value = optionsDocument_->GetElementById("toggle-view-bobbing-value"))
        {
            value->SetInnerRML(enabled ? "ON" : "OFF");
        }
    }

    void UiSystem::setOptionsControls(bool toggleSprint, bool toggleSneak, bool toggleProne)
    {
        if (optionsDocument_ == nullptr)
        {
            return;
        }

        if (Rml::Element* value = optionsDocument_->GetElementById("toggle-sprint-value"))
        {
            value->SetInnerRML(toggleSprint ? "TOGGLE" : "HOLD");
        }
        if (Rml::Element* value = optionsDocument_->GetElementById("toggle-sneak-value"))
        {
            value->SetInnerRML(toggleSneak ? "TOGGLE" : "HOLD");
        }
        if (Rml::Element* value = optionsDocument_->GetElementById("toggle-prone-value"))
        {
            value->SetInnerRML(toggleProne ? "TOGGLE" : "HOLD");
        }
    }

    void UiSystem::setOptionsLobbyBackground(bool lobbyBackground)
    {
        if (optionsDocument_ == nullptr)
        {
            return;
        }

        if (Rml::Element* screen = optionsDocument_->GetElementById("options-screen"))
        {
            screen->SetAttribute("class", lobbyBackground ? "screen" : "screen options-screen");
        }
    }

    void UiSystem::setWorldCreateGameMode(bool sandbox)
    {
        if (worldCreateDocument_ == nullptr)
        {
            return;
        }

        if (Rml::Element* modeButton = worldCreateDocument_->GetElementById("create-mode-toggle"))
        {
            modeButton->SetAttribute("class", "mode-button selected");
            modeButton->SetInnerRML(sandbox ? "SANDBOX" : "SURVIVAL");
        }
    }

    void UiSystem::setPlayerStats(int hp, int maxHp, int hunger, int maxHunger, int thirst, int maxThirst)
    {
        if (hudDocument_ == nullptr)
        {
            return;
        }

        setStatBarHeight(hudDocument_, "hp-bar-fill", hp, maxHp);
        setStatBarHeight(hudDocument_, "hunger-bar-fill", hunger, maxHunger);
        setStatBarHeight(hudDocument_, "thirst-bar-fill", thirst, maxThirst);
    }

    void UiSystem::setHotbarScopeClass(int selectedSlot)
    {
        if (hudDocument_ == nullptr)
        {
            return;
        }

        Rml::Element* scope = hudDocument_->GetElementById("hotbar-scope");
        if (scope == nullptr)
        {
            return;
        }

        scope->SetAttribute("class", Rml::String("hotbar-scope hotbar-slot-") + std::to_string(selectedSlot));
    }

    void UiSystem::setInventoryDebugSlots(std::string_view hotbarRml, std::string_view inventoryRml, bool visible)
    {
        if (hudDocument_ != nullptr)
        {
            if (Rml::Element* hotbarItems = hudDocument_->GetElementById("hotbar-debug-slots"))
            {
                hotbarItems->SetAttribute("class", visible ? "hotbar-items" : "hotbar-items ui-hidden");
                hotbarItems->SetInnerRML(std::string(hotbarRml));
            }
        }

        if (inventoryDocument_ != nullptr)
        {
            if (Rml::Element* inventoryItems = inventoryDocument_->GetElementById("inventory-debug-slots"))
            {
                inventoryItems->SetAttribute("class", visible ? "inventory-items" : "inventory-items ui-hidden");
                inventoryItems->SetInnerRML(std::string(inventoryRml));
            }
        }
    }

    void UiSystem::setInventoryItems(std::string_view hotbarRml, std::string_view inventoryRml, std::string_view offhandRml)
    {
        if (hudDocument_ != nullptr)
        {
            if (Rml::Element* hotbarItems = hudDocument_->GetElementById("hotbar-items"))
            {
                hotbarItems->SetInnerRML(std::string(hotbarRml));
            }
            if (Rml::Element* offhandItems = hudDocument_->GetElementById("offhand-items"))
            {
                offhandItems->SetInnerRML(std::string(offhandRml));
            }
        }

        if (inventoryDocument_ != nullptr)
        {
            if (Rml::Element* inventoryItems = inventoryDocument_->GetElementById("inventory-items"))
            {
                inventoryItems->SetInnerRML(std::string(inventoryRml));
            }
        }
    }

    void UiSystem::setInventoryCursorItem(std::string_view rml, bool visible)
    {
        if (inventoryDocument_ == nullptr)
        {
            return;
        }

        Rml::Element* cursor = inventoryDocument_->GetElementById("inventory-cursor-item");
        if (cursor == nullptr)
        {
            return;
        }

        cursor->SetAttribute("class", visible ? "cursor-item-layer" : "cursor-item-layer ui-hidden");
        cursor->SetInnerRML(std::string(rml));
    }

    void UiSystem::setRadialMenu(std::string_view centerRml, std::string_view actionsRml, std::string_view candidatesRml, bool visible)
    {
        if (hudDocument_ == nullptr)
        {
            return;
        }

        if (Rml::Element* radial = hudDocument_->GetElementById("radial-menu"))
        {
            radial->SetAttribute("class", visible ? "radial-menu" : "radial-menu ui-hidden");
        }
        if (Rml::Element* radialCenter = hudDocument_->GetElementById("radial-center-label"))
        {
            radialCenter->SetInnerRML(std::string(centerRml));
        }
        if (Rml::Element* radialActions = hudDocument_->GetElementById("radial-actions"))
        {
            radialActions->SetInnerRML(std::string(actionsRml));
        }
        if (Rml::Element* radialCandidates = hudDocument_->GetElementById("radial-candidates"))
        {
            radialCandidates->SetInnerRML(std::string(candidatesRml));
        }
    }

    void UiSystem::hideItemTooltip()
    {
        if (inventoryDocument_ == nullptr)
        {
            return;
        }

        Rml::Element* tooltip = inventoryDocument_->GetElementById("item-tooltip");
        if (tooltip == nullptr)
        {
            return;
        }

        tooltip->SetAttribute("class", "item-tooltip ui-hidden");
        tooltip->SetInnerRML("");
    }

    void UiSystem::showItemTooltip(std::string_view rml, int left, int top)
    {
        if (inventoryDocument_ == nullptr)
        {
            return;
        }

        Rml::Element* tooltip = inventoryDocument_->GetElementById("item-tooltip");
        if (tooltip == nullptr)
        {
            return;
        }

        tooltip->SetAttribute("class", "item-tooltip");
        tooltip->SetAttribute(
            "style",
            "left: " + std::to_string(left) +
            "px; top: " + std::to_string(top) +
            "px;");
        tooltip->SetInnerRML(std::string(rml));
    }

    void UiSystem::setWorldList(const std::vector<WorldListEntry>& worlds)
    {
        if (worldSelectDocument_ == nullptr)
        {
            return;
        }

        Rml::Element* list = worldSelectDocument_->GetElementById("world-list");
        if (list == nullptr)
        {
            return;
        }

        std::string rml;
        if (worlds.empty())
        {
            rml =
                "<div class=\"world-row large\">"
                "<div class=\"world-name\">No worlds yet</div>"
                "<div class=\"world-meta\">Create a new world to begin</div>"
                "</div>";
        }
        else
        {
            for (size_t i = 0; i < worlds.size(); ++i)
            {
                rml += "<div id=\"world-open-" + std::to_string(i) + "\" class=\"world-row large\">";
                rml += "<div class=\"world-name\">" + escapeRml(worlds[i].name) + "</div>";
                rml += "<div class=\"world-meta\">CREATED " + escapeRml(worlds[i].createdText) + " / LAST " + escapeRml(worlds[i].lastPlayedText) + "</div>";
                rml += "</div>";
            }
        }

        list->SetInnerRML(rml);
        for (size_t i = 0; i < worlds.size(); ++i)
        {
            attachActionEvent(worldSelectDocument_->GetElementById("world-open-" + std::to_string(i)), "dblclick");
        }
    }

    void UiSystem::processMouseMove(double x, double y)
    {
        if (context_ != nullptr)
        {
            context_->ProcessMouseMove(static_cast<int>(std::round(x)), static_cast<int>(std::round(y)), currentRmlKeyModifiers(window_));
        }
    }

    void UiSystem::processMouseButton(int button, bool pressed, int modifiers)
    {
        if (context_ == nullptr)
        {
            return;
        }

        int rmlButton = 0;
        if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            rmlButton = 1;
        }
        else if (button == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            rmlButton = 2;
        }

        if (pressed)
        {
            context_->ProcessMouseButtonDown(rmlButton, rmlKeyModifiersFromGlfw(modifiers));
        }
        else
        {
            context_->ProcessMouseButtonUp(rmlButton, rmlKeyModifiersFromGlfw(modifiers));
        }
    }

    void UiSystem::processMouseWheel(double yOffset)
    {
        if (context_ != nullptr)
        {
            context_->ProcessMouseWheel(static_cast<float>(-yOffset), currentRmlKeyModifiers(window_));
        }
    }

    void UiSystem::processTextInput(unsigned int codepoint)
    {
        if (context_ != nullptr)
        {
            context_->ProcessTextInput(static_cast<Rml::Character>(codepoint));
        }
    }

    bool UiSystem::processKey(int key, bool pressed, int modifiers)
    {
        if (context_ == nullptr)
        {
            return false;
        }

        const Rml::Input::KeyIdentifier identifier = rmlKeyFromGlfw(key);
        if (identifier == Rml::Input::KI_UNKNOWN)
        {
            return false;
        }

        if (pressed)
        {
            context_->ProcessKeyDown(identifier, rmlKeyModifiersFromGlfw(modifiers));
        }
        else
        {
            context_->ProcessKeyUp(identifier, rmlKeyModifiersFromGlfw(modifiers));
        }
        return true;
    }

    void UiSystem::ProcessEvent(Rml::Event& event)
    {
        Rml::Element* target = event.GetCurrentElement();
        if (target == nullptr)
        {
            target = event.GetTargetElement();
        }
        if (target == nullptr)
        {
            return;
        }

        if (suppressOptionChangeEvents_ && event.GetType() == "change" &&
            (target->GetId() == "bgm-volume-slider" || target->GetId() == "sfx-volume-slider" || target->GetId() == "fov-slider"))
        {
            return;
        }

        if (event.GetType() == "click" && clickCallback_)
        {
            clickCallback_();
        }
        pendingAction_ = target->GetId();
    }

    void UiSystem::attachDocumentEvents(Rml::ElementDocument* document)
    {
        if (document == nullptr)
        {
            return;
        }

        constexpr std::array<const char*, 15> ButtonIds = {
            "start",
            "exit",
            "new-world",
            "create-world",
            "create-mode-toggle",
            "back-to-lobby",
            "back-to-world-select",
            "resume",
            "exit-to-lobby",
            "options",
            "options-back",
            "toggle-view-bobbing",
            "toggle-sprint",
            "toggle-sneak",
            "toggle-prone"
        };
        for (const char* id : ButtonIds)
        {
            attachActionEvent(document->GetElementById(id), "click");
        }

        constexpr std::array<const char*, 3> SliderIds = {
            "bgm-volume-slider",
            "sfx-volume-slider",
            "fov-slider"
        };
        for (const char* id : SliderIds)
        {
            attachActionEvent(document->GetElementById(id), "change");
        }
    }

    void UiSystem::setDocument(int menuOverlayMode)
    {
        if (activeMenuOverlayMode_ == menuOverlayMode)
        {
            return;
        }

        activeMenuOverlayMode_ = menuOverlayMode;
        if (lobbyDocument_ != nullptr)
        {
            menuOverlayMode == 1 ? lobbyDocument_->Show() : lobbyDocument_->Hide();
        }
        if (worldSelectDocument_ != nullptr)
        {
            menuOverlayMode == 3 ? worldSelectDocument_->Show() : worldSelectDocument_->Hide();
        }
        if (worldCreateDocument_ != nullptr)
        {
            menuOverlayMode == 4 ? worldCreateDocument_->Show() : worldCreateDocument_->Hide();
        }
        if (hudDocument_ != nullptr)
        {
            (menuOverlayMode == 0 || menuOverlayMode == 5) ? hudDocument_->Show() : hudDocument_->Hide();
        }
        if (inventoryDocument_ != nullptr)
        {
            menuOverlayMode == 5 ? inventoryDocument_->Show() : inventoryDocument_->Hide();
        }
        if (pauseDocument_ != nullptr)
        {
            menuOverlayMode == 2 ? pauseDocument_->Show() : pauseDocument_->Hide();
        }
        if (optionsDocument_ != nullptr)
        {
            menuOverlayMode == 6 ? optionsDocument_->Show() : optionsDocument_->Hide();
        }
    }

    void UiSystem::closeDocument(Rml::ElementDocument*& document)
    {
        if (document != nullptr)
        {
            document->Close();
            document = nullptr;
        }
    }
}
