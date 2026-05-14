#include "ui/InventoryUi.h"

#include "ui/UiSystem.h"

#include <algorithm>
#include <cmath>

namespace dolbuto::ui
{
    namespace
    {
        constexpr int Scale = 4;
        constexpr int SlotSize = 64;
        constexpr int InventoryPanelWidth = 708;
        constexpr int InventoryPanelHeight = 388;
        constexpr int InventoryPanelHalfWidth = InventoryPanelWidth / 2;
        constexpr int InventoryPadding = 4;
        constexpr int InventoryStep = 17;
        constexpr int InventoryHotbarY = 77;
        constexpr int HotbarStartX = 3;
        constexpr int HotbarStartY = 3;
        constexpr int HotbarOffset = 4;
        constexpr int CountBoxWidth = 48;
        constexpr int CountRightInset = 2;
        constexpr int CountTopOffset = 40;

        struct SlotPixelPosition
        {
            int left = 0;
            int top = 0;
        };

        SlotPixelPosition slotPixelPosition(size_t slotIndex, bool inventorySlot)
        {
            int sourceX = 0;
            int sourceY = 0;
            if (inventorySlot)
            {
                const int col = static_cast<int>(slotIndex % 10u);
                sourceX = InventoryPadding + col * InventoryStep;
                if (slotIndex < 10u)
                {
                    sourceY = InventoryHotbarY;
                }
                else
                {
                    const int group = static_cast<int>(slotIndex / 10u);
                    sourceY = InventoryPadding + (4 - group) * InventoryStep;
                }
            }
            else
            {
                sourceX = HotbarStartX + static_cast<int>(slotIndex) * InventoryStep;
                sourceY = HotbarStartY;
            }

            const int offsetX = inventorySlot ? 0 : HotbarOffset;
            const int offsetY = inventorySlot ? 0 : HotbarOffset;
            return SlotPixelPosition{
                sourceX * Scale + offsetX,
                sourceY * Scale + offsetY
            };
        }

        bool hasVisibleItem(const InventoryItemView& item)
        {
            return item.itemId != 0 &&
                item.count != 0 &&
                !item.slotTexture.empty() &&
                item.slotTexture != "none";
        }
    }

    std::string inventoryDebugSlotRml(size_t slotIndex, bool inventorySlot)
    {
        const SlotPixelPosition position = slotPixelPosition(slotIndex, inventorySlot);

        std::string rml;
        rml += "<div class=\"slot-debug\" style=\"left: " + std::to_string(position.left) + "px; top: " + std::to_string(position.top) + "px;\">";
        rml += std::to_string(slotIndex);
        rml += "</div>";
        return rml;
    }

    std::optional<size_t> inventorySlotAt(double x, double y, uint32_t screenWidth, uint32_t screenHeight, size_t slotCount)
    {
        if (screenWidth == 0 || screenHeight == 0)
        {
            return std::nullopt;
        }

        const int panelLeft = static_cast<int>(screenWidth) / 2 - InventoryPanelHalfWidth;
        const int panelTop = static_cast<int>(screenHeight) - InventoryPanelHeight;
        const int localX = static_cast<int>(std::floor(x)) - panelLeft;
        const int localY = static_cast<int>(std::floor(y)) - panelTop;
        if (localX < 0 || localY < 0 || localX >= InventoryPanelWidth || localY >= InventoryPanelHeight)
        {
            return std::nullopt;
        }

        for (size_t slotIndex = 0; slotIndex < slotCount; ++slotIndex)
        {
            const SlotPixelPosition position = slotPixelPosition(slotIndex, true);
            if (localX >= position.left && localX < position.left + SlotSize &&
                localY >= position.top && localY < position.top + SlotSize)
            {
                return slotIndex;
            }
        }

        return std::nullopt;
    }

    std::string itemStackContentRml(const InventoryItemView& item, int itemLeft, int itemTop)
    {
        if (!hasVisibleItem(item))
        {
            return {};
        }

        const int countLeft = itemLeft + SlotSize - CountBoxWidth - CountRightInset;
        const int countTop = itemTop + CountTopOffset;
        const std::string src = "../textures/item/" + item.slotTexture + ".png";

        std::string rml;
        rml += "<img class=\"slot-item\" src=\"" + escapeRml(src) + "\" style=\"left: " + std::to_string(itemLeft) + "px; top: " + std::to_string(itemTop) + "px;\"/>";
        if (item.count > 1)
        {
            rml += "<div class=\"slot-count\" style=\"left: " + std::to_string(countLeft) + "px; top: " + std::to_string(countTop) + "px;\">";
            rml += std::to_string(item.count);
            rml += "</div>";
        }
        return rml;
    }

    std::string itemTooltipRml(const InventoryItemView& item)
    {
        if (item.itemId == 0 || item.count == 0 || item.key.empty() || item.key == "none")
        {
            return {};
        }

        std::string rml;
        rml += "<div class=\"item-tooltip-title\">" + escapeRml(item.name) + "</div>";
        const auto line = [&](std::string_view key, const std::string& value)
        {
            rml += "<div class=\"item-tooltip-line\">" + std::string(key) + ": " + escapeRml(value) + "</div>";
        };
        line("ID", std::to_string(item.itemId));
        line("KEY", item.key);
        line("COUNT", std::to_string(item.count) + " / " + std::to_string(item.stackSize));
        line("STACK_SIZE", std::to_string(item.stackSize));
        line("SLOT_TEXTURE", item.slotTexture);
        line("DROPPED_RENDER", item.droppedRender);
        line("DROPPED_TEXTURE", item.droppedTexture);
        line("HELD_RENDER", item.heldRender);
        line("HELD_TEXTURE", item.heldTexture);
        return rml;
    }

    std::string itemSlotImageRml(size_t slotIndex, bool inventorySlot, const InventoryItemView& item)
    {
        const SlotPixelPosition position = slotPixelPosition(slotIndex, inventorySlot);

        std::string rml;
        if (!inventorySlot)
        {
            rml += "<div class=\"hotbar-slot-background\" style=\"left: " + std::to_string(position.left) + "px; top: " + std::to_string(position.top) + "px;\"></div>";
        }
        else
        {
            rml += "<div class=\"inventory-slot-cell\" style=\"left: " + std::to_string(position.left) + "px; top: " + std::to_string(position.top) + "px;\">";
        }

        const int itemLeft = inventorySlot ? 0 : position.left;
        const int itemTop = inventorySlot ? 0 : position.top;
        rml += itemStackContentRml(item, itemLeft, itemTop);
        if (inventorySlot)
        {
            rml += "</div>";
        }
        return rml;
    }

    std::optional<TooltipLayout> itemTooltipLayout(const InventoryItemView& item, double mouseX, double mouseY, uint32_t screenWidth, uint32_t screenHeight)
    {
        if (item.itemId == 0 || item.count == 0 || item.key.empty() || item.key == "none")
        {
            return std::nullopt;
        }

        constexpr int Padding = 10;
        constexpr int TitleHeight = 22;
        constexpr int TitleMargin = 4;
        constexpr int LineHeight = 18;
        constexpr int LineCount = 9;
        constexpr int MinWidth = 180;
        constexpr int MaxWidth = 520;
        constexpr int TitleCharWidth = 12;
        constexpr int LineCharWidth = 8;
        constexpr int Height = Padding * 2 + TitleHeight + TitleMargin + LineHeight * LineCount;
        constexpr int MouseOffset = 16;

        const auto lineText = [](std::string_view key, const std::string& value)
        {
            return std::string(key) + ": " + value;
        };

        int contentWidth = static_cast<int>(item.name.size()) * TitleCharWidth;
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("ID", std::to_string(item.itemId)).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("KEY", item.key).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("COUNT", std::to_string(item.count) + " / " + std::to_string(item.stackSize)).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("STACK_SIZE", std::to_string(item.stackSize)).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("SLOT_TEXTURE", item.slotTexture).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("DROPPED_RENDER", item.droppedRender).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("DROPPED_TEXTURE", item.droppedTexture).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("HELD_RENDER", item.heldRender).size()) * LineCharWidth);
        contentWidth = std::max(contentWidth, static_cast<int>(lineText("HELD_TEXTURE", item.heldTexture).size()) * LineCharWidth);
        const int width = std::clamp(contentWidth + Padding * 2, MinWidth, MaxWidth);

        int left = static_cast<int>(std::round(mouseX)) + MouseOffset;
        int top = static_cast<int>(std::round(mouseY)) + MouseOffset;
        const int screenW = static_cast<int>(screenWidth);
        const int screenH = static_cast<int>(screenHeight);
        if (left + width > screenW)
        {
            left = static_cast<int>(std::round(mouseX)) - width - MouseOffset;
        }
        if (top + Height > screenH)
        {
            top = static_cast<int>(std::round(mouseY)) - Height - MouseOffset;
        }
        left = std::clamp(left, 0, std::max(0, screenW - width));
        top = std::clamp(top, 0, std::max(0, screenH - Height));

        return TooltipLayout{left, top, width, Height};
    }
}
