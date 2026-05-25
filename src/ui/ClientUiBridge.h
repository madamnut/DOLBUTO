#pragma once

#include "game/ClientUiTypes.h"
#include "gameplay/ClientGameplayRuntime.h"
#include "items/ItemData.h"
#include "ui/InventoryUi.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dolbuto::gameplay
{
    class ClientGameplayRuntime;
}

namespace dolbuto::ui
{
    class UiSystem;

    class ClientUiBridge
    {
    public:
        ClientUiBridge() = default;

        void setContext(
            UiSystem* uiSystem,
            gameplay::ClientGameplayRuntime* gameplayRuntime,
            const std::vector<ItemDefinition>* itemDefinitions);

        void setHotbarSelectedSlot(int slot);
        void updateHotbarScopeClass();
        void updateInventoryDebugSlots();
        void updateInventoryUi();
        void updateInventoryCursorUi();
        void updateItemTooltipUi(uint32_t screenWidth, uint32_t screenHeight);
        void setRadialMenu(
            const std::vector<gameplay::ItemInteractionActionMenu>& actions,
            std::optional<std::size_t> selectedActionIndex,
            std::optional<std::size_t> selectedCandidateIndex);
        void hideRadialMenu();
        void closeInventoryInteraction(uint32_t screenWidth, uint32_t screenHeight);
        void setWorldList(const std::vector<game::WorldListItem>& worlds);

        void processMouseMove(double x, double y, uint32_t screenWidth, uint32_t screenHeight);
        void processMouseButton(int button, bool pressed, int modifiers, uint32_t screenWidth, uint32_t screenHeight);
        void processMouseWheel(double yOffset);
        void processTextInput(unsigned int codepoint);
        bool processKey(int key, bool pressed, int modifiers, uint32_t screenWidth, uint32_t screenHeight);

    private:
        UiSystem& uiSystem();
        const UiSystem& uiSystem() const;
        gameplay::ClientGameplayRuntime& gameplayRuntime();
        const gameplay::ClientGameplayRuntime& gameplayRuntime() const;
        const std::vector<ItemDefinition>& itemDefinitions() const;

        std::optional<std::size_t> inventorySlotAt(double x, double y, uint32_t screenWidth, uint32_t screenHeight) const;
        InventoryItemView inventoryItemView(const ItemStack& stack) const;
        bool handleInventorySlotClick(std::size_t slotIndex, int button, int modifiers, uint32_t screenWidth, uint32_t screenHeight);
        bool handleInventoryHotbarSwapKey(int key, uint32_t screenWidth, uint32_t screenHeight);

        UiSystem* uiSystem_ = nullptr;
        gameplay::ClientGameplayRuntime* gameplayRuntime_ = nullptr;
        const std::vector<ItemDefinition>* itemDefinitions_ = nullptr;
        double mouseX_ = 0.0;
        double mouseY_ = 0.0;
        bool inventoryDebugSlotsVisible_ = false;
    };
}
