#include "ui/ClientUiBridge.h"

#include "gameplay/ClientGameplayRuntime.h"
#include "ui/UiSystem.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace dolbuto::ui
{
    namespace
    {
        std::string displayActionName(std::string_view action)
        {
            std::string text;
            text.reserve(action.size());
            bool upperNext = true;
            for (const char c : action)
            {
                if (c == '_' || c == '-')
                {
                    text.push_back(' ');
                    upperNext = true;
                    continue;
                }
                if (upperNext && c >= 'a' && c <= 'z')
                {
                    text.push_back(static_cast<char>(c - 'a' + 'A'));
                }
                else
                {
                    text.push_back(c);
                }
                upperNext = false;
            }
            return text;
        }

        std::string candidateDisplayName(
            const ItemInteractionCandidate& candidate,
            const std::vector<ItemDefinition>& definitions)
        {
            std::string label;
            for (const ItemInteractionOutput& output : candidate.outputs)
            {
                if (output.itemId == 0 || static_cast<std::size_t>(output.itemId) >= definitions.size())
                {
                    continue;
                }

                if (!label.empty())
                {
                    label += " + ";
                }
                label += definitions[output.itemId].name;
            }
            return label;
        }

        struct CandidateIconPlacement
        {
            int left = 4;
            int top = 4;
            int size = 64;
        };

        CandidateIconPlacement candidateIconPlacement(std::size_t outputIndex, std::size_t outputCount)
        {
            if (outputCount <= 1)
            {
                return {4, 4, 64};
            }
            if (outputCount == 2)
            {
                return outputIndex == 0 ? CandidateIconPlacement{6, 19, 34} : CandidateIconPlacement{32, 19, 34};
            }
            if (outputCount == 3)
            {
                if (outputIndex == 0)
                {
                    return {21, 5, 30};
                }
                return outputIndex == 1 ? CandidateIconPlacement{8, 36, 28} : CandidateIconPlacement{36, 36, 28};
            }

            switch (outputIndex)
            {
            case 0:
                return {8, 8, 28};
            case 1:
                return {36, 8, 28};
            case 2:
                return {8, 36, 28};
            default:
                return {36, 36, 28};
            }
        }
    }

    void ClientUiBridge::setContext(
        UiSystem* uiSystem,
        gameplay::ClientGameplayRuntime* gameplayRuntime,
        const std::vector<ItemDefinition>* itemDefinitions)
    {
        uiSystem_ = uiSystem;
        gameplayRuntime_ = gameplayRuntime;
        itemDefinitions_ = itemDefinitions;
    }

    UiSystem& ClientUiBridge::uiSystem()
    {
        if (uiSystem_ == nullptr)
        {
            throw std::runtime_error("ClientUiBridge UI system is not initialized.");
        }
        return *uiSystem_;
    }

    const UiSystem& ClientUiBridge::uiSystem() const
    {
        if (uiSystem_ == nullptr)
        {
            throw std::runtime_error("ClientUiBridge UI system is not initialized.");
        }
        return *uiSystem_;
    }

    gameplay::ClientGameplayRuntime& ClientUiBridge::gameplayRuntime()
    {
        if (gameplayRuntime_ == nullptr)
        {
            throw std::runtime_error("ClientUiBridge gameplay runtime is not initialized.");
        }
        return *gameplayRuntime_;
    }

    const gameplay::ClientGameplayRuntime& ClientUiBridge::gameplayRuntime() const
    {
        if (gameplayRuntime_ == nullptr)
        {
            throw std::runtime_error("ClientUiBridge gameplay runtime is not initialized.");
        }
        return *gameplayRuntime_;
    }

    const std::vector<ItemDefinition>& ClientUiBridge::itemDefinitions() const
    {
        if (itemDefinitions_ == nullptr)
        {
            throw std::runtime_error("ClientUiBridge item definitions are not initialized.");
        }
        return *itemDefinitions_;
    }

    void ClientUiBridge::setHotbarSelectedSlot(int slot)
    {
        gameplayRuntime().setHotbarSelectedSlot(slot);
        updateHotbarScopeClass();
    }

    void ClientUiBridge::updateHotbarScopeClass()
    {
        uiSystem().setHotbarScopeClass(gameplayRuntime().hotbarSelectedSlot());
    }

    void ClientUiBridge::updateInventoryDebugSlots()
    {
        std::string hotbarRml;
        std::string inventoryRml;
        if (inventoryDebugSlotsVisible_)
        {
            for (std::size_t i = 0; i < gameplay::PlayerInventory::HotbarSlotCount; ++i)
            {
                hotbarRml += inventoryDebugSlotRml(i, false);
            }
            for (std::size_t i = 0; i < gameplay::PlayerInventory::SlotCount; ++i)
            {
                inventoryRml += inventoryDebugSlotRml(i, true);
            }
        }
        uiSystem().setInventoryDebugSlots(hotbarRml, inventoryRml, inventoryDebugSlotsVisible_);
    }

    void ClientUiBridge::updateInventoryUi()
    {
        std::string hotbarRml;
        for (std::size_t i = 0; i < gameplay::PlayerInventory::HotbarSlotCount; ++i)
        {
            hotbarRml += itemSlotImageRml(i, false, inventoryItemView(gameplayRuntime().inventorySlot(i)));
        }

        std::string inventoryRml;
        for (std::size_t i = 0; i < gameplayRuntime().inventorySlotCount(); ++i)
        {
            inventoryRml += itemSlotImageRml(i, true, inventoryItemView(gameplayRuntime().inventorySlot(i)));
        }

        uiSystem().setInventoryItems(hotbarRml, inventoryRml);
        updateInventoryCursorUi();
    }

    void ClientUiBridge::updateInventoryCursorUi()
    {
        const ItemStack& cursorStack = gameplayRuntime().inventoryCursorStack();
        if (cursorStack.itemId == 0 || cursorStack.count == 0)
        {
            uiSystem().setInventoryCursorItem("", false);
            return;
        }

        const int left = static_cast<int>(std::round(mouseX_)) - 32;
        const int top = static_cast<int>(std::round(mouseY_)) - 32;
        uiSystem().setInventoryCursorItem(itemStackContentRml(inventoryItemView(cursorStack), left, top), true);
    }

    void ClientUiBridge::updateItemTooltipUi(uint32_t screenWidth, uint32_t screenHeight)
    {
        const ItemStack& cursorStack = gameplayRuntime().inventoryCursorStack();
        if (cursorStack.itemId != 0 && cursorStack.count != 0)
        {
            uiSystem().hideItemTooltip();
            return;
        }

        const std::optional<std::size_t> hoveredSlot = inventorySlotAt(mouseX_, mouseY_, screenWidth, screenHeight);
        if (!hoveredSlot.has_value())
        {
            uiSystem().hideItemTooltip();
            return;
        }

        const InventoryItemView item = inventoryItemView(gameplayRuntime().inventorySlot(*hoveredSlot));
        const std::string rml = itemTooltipRml(item);
        if (rml.empty())
        {
            uiSystem().hideItemTooltip();
            return;
        }

        const std::optional<TooltipLayout> layout = itemTooltipLayout(item, mouseX_, mouseY_, screenWidth, screenHeight);
        if (!layout.has_value())
        {
            uiSystem().hideItemTooltip();
            return;
        }

        uiSystem().showItemTooltip(rml, layout->left, layout->top);
    }

    void ClientUiBridge::setRadialMenu(
        const std::vector<gameplay::ItemInteractionActionMenu>& actions,
        std::optional<std::size_t> selectedActionIndex,
        std::optional<std::size_t> selectedCandidateIndex)
    {
        if (actions.empty())
        {
            hideRadialMenu();
            return;
        }

        constexpr double Pi = 3.14159265358979323846;
        constexpr int Center = 210;
        const double startAngle = -Pi * 0.5;

        std::string centerLabel = "Cancel";
        if (selectedActionIndex.has_value() && *selectedActionIndex < actions.size())
        {
            centerLabel = displayActionName(actions[*selectedActionIndex].action);
            const std::vector<ItemInteractionCandidate>& candidates = actions[*selectedActionIndex].candidates;
            if (selectedCandidateIndex.has_value() && *selectedCandidateIndex < candidates.size())
            {
                const std::vector<ItemDefinition>& definitions = itemDefinitions();
                const std::string candidateLabel = candidateDisplayName(candidates[*selectedCandidateIndex], definitions);
                if (!candidateLabel.empty())
                {
                    centerLabel = candidateLabel;
                }
            }
        }

        constexpr int ActionRadius = 95;
        constexpr int ActionSize = 54;
        const double actionStep = (2.0 * Pi) / static_cast<double>(actions.size());
        std::string actionsRml;
        for (std::size_t i = 0; i < actions.size(); ++i)
        {
            const double angle = startAngle + (static_cast<double>(i) + 0.5) * actionStep;
            const int left = static_cast<int>(std::round(static_cast<double>(Center) + std::cos(angle) * ActionRadius - static_cast<double>(ActionSize) * 0.5));
            const int top = static_cast<int>(std::round(static_cast<double>(Center) + std::sin(angle) * ActionRadius - static_cast<double>(ActionSize) * 0.5));
            const bool selected = selectedActionIndex.has_value() && *selectedActionIndex == i;

            actionsRml += "<div class=\"radial-action";
            if (selected)
            {
                actionsRml += " selected";
            }
            actionsRml += "\" style=\"left: " + std::to_string(left) + "px; top: " + std::to_string(top) + "px;\">";
            actionsRml += "<img class=\"radial-action-symbol\" src=\"../textures/symbol/actions/" + escapeRml(actions[i].action) + ".png\"/>";
            actionsRml += "</div>";
        }

        std::string candidatesRml;
        if (selectedActionIndex.has_value() && *selectedActionIndex < actions.size())
        {
            constexpr int CandidateRadius = 172;
            constexpr int CandidateWidth = 72;
            constexpr int CandidateHeight = 72;
            const std::vector<ItemInteractionCandidate>& candidates = actions[*selectedActionIndex].candidates;
            const double selectedActionStart = startAngle + actionStep * static_cast<double>(*selectedActionIndex);
            const double candidateStep = candidates.empty() ? 0.0 : actionStep / static_cast<double>(candidates.size());
            const std::vector<ItemDefinition>& definitions = itemDefinitions();
            for (std::size_t i = 0; i < candidates.size(); ++i)
            {
                const ItemInteractionCandidate& candidate = candidates[i];
                if (candidate.outputs.empty())
                {
                    continue;
                }

                const double angle = selectedActionStart + (static_cast<double>(i) + 0.5) * candidateStep;
                const int left = static_cast<int>(std::round(static_cast<double>(Center) + std::cos(angle) * CandidateRadius - static_cast<double>(CandidateWidth) * 0.5));
                const int top = static_cast<int>(std::round(static_cast<double>(Center) + std::sin(angle) * CandidateRadius - static_cast<double>(CandidateHeight) * 0.5));
                const bool selected = selectedCandidateIndex.has_value() && *selectedCandidateIndex == i;

                candidatesRml += "<div class=\"radial-candidate";
                if (selected)
                {
                    candidatesRml += " selected";
                }
                candidatesRml += "\" style=\"left: " + std::to_string(left) + "px; top: " + std::to_string(top) + "px;\">";
                const std::size_t outputCount = std::min<std::size_t>(candidate.outputs.size(), 4);
                for (std::size_t outputIndex = 0; outputIndex < outputCount; ++outputIndex)
                {
                    const uint16_t itemId = candidate.outputs[outputIndex].itemId;
                    if (itemId == 0 || static_cast<std::size_t>(itemId) >= definitions.size())
                    {
                        continue;
                    }

                    const ItemDefinition& definition = definitions[itemId];
                    const CandidateIconPlacement placement = candidateIconPlacement(outputIndex, outputCount);
                    candidatesRml += "<img class=\"radial-candidate-icon\" style=\"left: " +
                        std::to_string(placement.left) + "px; top: " +
                        std::to_string(placement.top) + "px; width: " +
                        std::to_string(placement.size) + "px; height: " +
                        std::to_string(placement.size) + "px;\" src=\"../textures/item/" +
                        escapeRml(definition.slotTexture) + ".png\"/>";
                }
                candidatesRml += "</div>";
            }
        }

        uiSystem().setRadialMenu(escapeRml(centerLabel), actionsRml, candidatesRml, true);
    }

    void ClientUiBridge::hideRadialMenu()
    {
        uiSystem().setRadialMenu("", "", "", false);
    }

    void ClientUiBridge::closeInventoryInteraction(uint32_t screenWidth, uint32_t screenHeight)
    {
        gameplayRuntime().closeInventoryCursor();
        updateItemTooltipUi(screenWidth, screenHeight);
        updateInventoryCursorUi();
    }

    void ClientUiBridge::setWorldList(const std::vector<game::WorldListItem>& worlds)
    {
        std::vector<WorldListEntry> entries;
        entries.reserve(worlds.size());
        for (const game::WorldListItem& world : worlds)
        {
            entries.push_back(WorldListEntry{
                world.name,
                world.createdText,
                world.lastPlayedText
            });
        }
        uiSystem().setWorldList(entries);
    }

    void ClientUiBridge::processMouseMove(double x, double y, uint32_t screenWidth, uint32_t screenHeight)
    {
        mouseX_ = x;
        mouseY_ = y;
        uiSystem().processMouseMove(x, y);
        if (uiSystem().activeMenuOverlayMode() == 5)
        {
            updateItemTooltipUi(screenWidth, screenHeight);
            updateInventoryCursorUi();
        }
    }

    void ClientUiBridge::processMouseButton(int button, bool pressed, int modifiers, uint32_t screenWidth, uint32_t screenHeight)
    {
        uiSystem().processMouseButton(button, pressed, modifiers);

        if (!pressed || uiSystem().activeMenuOverlayMode() != 5 ||
            (button != GLFW_MOUSE_BUTTON_LEFT && button != GLFW_MOUSE_BUTTON_RIGHT))
        {
            return;
        }

        const std::optional<std::size_t> slot = inventorySlotAt(mouseX_, mouseY_, screenWidth, screenHeight);
        if (slot.has_value())
        {
            handleInventorySlotClick(*slot, button, modifiers, screenWidth, screenHeight);
        }
    }

    void ClientUiBridge::processMouseWheel(double yOffset)
    {
        uiSystem().processMouseWheel(yOffset);
    }

    void ClientUiBridge::processTextInput(unsigned int codepoint)
    {
        uiSystem().processTextInput(codepoint);
    }

    bool ClientUiBridge::processKey(int key, bool pressed, int modifiers, uint32_t screenWidth, uint32_t screenHeight)
    {
        if (pressed && uiSystem().activeMenuOverlayMode() == 5 && key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
        {
            handleInventoryHotbarSwapKey(key, screenWidth, screenHeight);
        }
        return uiSystem().processKey(key, pressed, modifiers);
    }

    std::optional<std::size_t> ClientUiBridge::inventorySlotAt(double x, double y, uint32_t screenWidth, uint32_t screenHeight) const
    {
        return ui::inventorySlotAt(x, y, screenWidth, screenHeight, gameplayRuntime().inventorySlotCount());
    }

    InventoryItemView ClientUiBridge::inventoryItemView(const ItemStack& stack) const
    {
        InventoryItemView item{};
        item.itemId = stack.itemId;
        item.count = stack.count;
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        if (stack.itemId == 0 || stack.count == 0 || static_cast<std::size_t>(stack.itemId) >= definitions.size())
        {
            return item;
        }

        const ItemDefinition& definition = definitions[stack.itemId];
        const auto renderTypeText = [](ItemRenderType type)
        {
            switch (type)
            {
            case ItemRenderType::BlockModel:
                return "block_model";
            case ItemRenderType::ExtrudedSprite:
                return "extruded_sprite";
            }
            return "unknown";
        };
        const auto slotRenderTypeText = [](ItemSlotRenderType type)
        {
            switch (type)
            {
            case ItemSlotRenderType::BlockModel:
                return "block_model";
            case ItemSlotRenderType::Sprite:
                return "sprite";
            }
            return "unknown";
        };

        item.stackSize = definition.stackSize;
        item.durability = stack.durability;
        item.maxDurability = definition.maxDurability;
        item.breakLevel = definition.breakLevel;
        item.name = definition.name;
        item.key = definition.key;
        item.slotTexture = definition.slotTexture;
        item.slotRender = slotRenderTypeText(definition.slotRender);
        item.droppedRender = renderTypeText(definition.droppedRender);
        item.droppedTexture = definition.droppedTexture;
        item.heldRender = renderTypeText(definition.heldRender);
        item.heldTexture = definition.heldTexture;
        item.useActions = definition.useActions;
        item.breakActions = definition.breakActions;
        item.placeActions = definition.placeActions;
        item.placeBlockId = definition.placeBlockId;
        return item;
    }

    bool ClientUiBridge::handleInventorySlotClick(
        std::size_t slotIndex,
        int button,
        int modifiers,
        uint32_t screenWidth,
        uint32_t screenHeight)
    {
        const bool shift = (modifiers & GLFW_MOD_SHIFT) != 0;
        gameplay::InventoryClickButton clickButton = gameplay::InventoryClickButton::Left;
        if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            clickButton = gameplay::InventoryClickButton::Right;
        }

        if (!gameplayRuntime().handleInventorySlotClick(slotIndex, clickButton, shift))
        {
            return false;
        }

        updateInventoryUi();
        updateItemTooltipUi(screenWidth, screenHeight);
        return true;
    }

    bool ClientUiBridge::handleInventoryHotbarSwapKey(int key, uint32_t screenWidth, uint32_t screenHeight)
    {
        int hotbarSlot = -1;
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9)
        {
            hotbarSlot = key - GLFW_KEY_1;
        }
        else if (key == GLFW_KEY_0)
        {
            hotbarSlot = 9;
        }
        if (hotbarSlot < 0)
        {
            return false;
        }

        const std::optional<std::size_t> hoveredSlot = inventorySlotAt(mouseX_, mouseY_, screenWidth, screenHeight);
        if (!hoveredSlot.has_value())
        {
            return false;
        }

        if (!gameplayRuntime().swapHotbarWithSlot(*hoveredSlot, static_cast<std::size_t>(hotbarSlot)))
        {
            return false;
        }

        updateInventoryUi();
        updateItemTooltipUi(screenWidth, screenHeight);
        return true;
    }
}
