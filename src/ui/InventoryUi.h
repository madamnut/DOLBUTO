#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dolbuto::ui
{
    struct InventoryItemView
    {
        uint16_t itemId = 0;
        uint16_t count = 0;
        uint16_t stackSize = 0;
        uint16_t durability = 0;
        uint16_t maxDurability = 0;
        uint16_t breakLevel = 0;
        std::string name;
        std::string key;
        std::string slotTexture;
        std::string slotRender;
        std::string droppedRender;
        std::string droppedTexture;
        std::string heldRender;
        std::string heldTexture;
        std::vector<std::string> useActions;
        std::vector<std::string> breakActions;
        std::vector<std::string> placeActions;
        uint16_t placeBlockId = 0;
    };

    struct TooltipLayout
    {
        int left = 0;
        int top = 0;
        int width = 0;
        int height = 0;
    };

    std::string inventoryDebugSlotRml(size_t slotIndex, bool inventorySlot);
    std::optional<size_t> inventorySlotAt(double x, double y, uint32_t screenWidth, uint32_t screenHeight, size_t slotCount);
    std::string itemStackContentRml(const InventoryItemView& item, int itemLeft, int itemTop);
    std::string itemTooltipRml(const InventoryItemView& item);
    std::string itemSlotImageRml(size_t slotIndex, bool inventorySlot, const InventoryItemView& item);
    std::optional<TooltipLayout> itemTooltipLayout(const InventoryItemView& item, double mouseX, double mouseY, uint32_t screenWidth, uint32_t screenHeight);
}
