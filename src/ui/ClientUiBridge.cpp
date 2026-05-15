#include "ui/ClientUiBridge.h"

#include "gameplay/ClientGameplayRuntime.h"
#include "ui/UiSystem.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dolbuto::ui
{
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

        uiSystem().showItemTooltip(rml, layout->left, layout->top, layout->width, layout->height);
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
            case ItemRenderType::ExtrudedSprite:
                return "extruded_sprite";
            }
            return "unknown";
        };

        item.stackSize = definition.stackSize;
        item.name = definition.name;
        item.key = definition.key;
        item.slotTexture = definition.slotTexture;
        item.droppedRender = renderTypeText(definition.droppedRender);
        item.droppedTexture = definition.droppedTexture;
        item.heldRender = renderTypeText(definition.heldRender);
        item.heldTexture = definition.heldTexture;
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
