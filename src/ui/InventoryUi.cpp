#include "ui/InventoryUi.h"

#include "ui/UiSystem.h"

#include <algorithm>
#include <cmath>
#include <optional>

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
        constexpr int DurabilityBarLeftInset = 6;
        constexpr int DurabilityBarTopOffset = 56;
        constexpr int DurabilityBarWidth = 52;
        constexpr int DurabilityBarHeight = 4;

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

        std::string hexColor(uint8_t r, uint8_t g, uint8_t b)
        {
            constexpr char Digits[] = "0123456789abcdef";
            std::string value = "#000000";
            value[1] = Digits[(r >> 4u) & 0x0Fu];
            value[2] = Digits[r & 0x0Fu];
            value[3] = Digits[(g >> 4u) & 0x0Fu];
            value[4] = Digits[g & 0x0Fu];
            value[5] = Digits[(b >> 4u) & 0x0Fu];
            value[6] = Digits[b & 0x0Fu];
            return value;
        }

        std::string slotGaugeColor(float ratio)
        {
            ratio = std::clamp(ratio, 0.0f, 1.0f);
            const auto lerpByte = [](float from, float to, float t)
            {
                return static_cast<uint8_t>(std::round(from + (to - from) * t));
            };

            if (ratio < 0.5f)
            {
                const float t = ratio / 0.5f;
                return hexColor(lerpByte(220.0f, 235.0f, t), lerpByte(45.0f, 220.0f, t), lerpByte(40.0f, 45.0f, t));
            }

            const float t = (ratio - 0.5f) / 0.5f;
            return hexColor(lerpByte(235.0f, 95.0f, t), lerpByte(220.0f, 220.0f, t), lerpByte(45.0f, 65.0f, t));
        }

        struct SlotGaugeValue
        {
            uint16_t current = 0;
            uint16_t max = 0;
        };

        std::optional<SlotGaugeValue> slotGaugeValue(const InventoryItemView& item)
        {
            if (item.slotGaugeSource == "durability")
            {
                return SlotGaugeValue{item.durability, item.maxDurability};
            }
            if (item.slotGaugeSource == "burnTicks")
            {
                return SlotGaugeValue{item.burnTicksRemaining, item.maxBurnTicks};
            }
            return std::nullopt;
        }

        bool showSlotGauge(const InventoryItemView& item)
        {
            const std::optional<SlotGaugeValue> value = slotGaugeValue(item);
            return value.has_value() &&
                value->max > 0 &&
                value->current > 0 &&
                value->current < value->max;
        }

        std::string joinedValues(const std::vector<std::string>& values)
        {
            if (values.empty())
            {
                return "none";
            }

            std::string text;
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0)
                {
                    text += ", ";
                }
                text += values[i];
            }
            return text;
        }

        struct TooltipLine
        {
            std::string key;
            std::string value;
        };

        std::vector<TooltipLine> itemTooltipLines(const InventoryItemView& item)
        {
            std::vector<TooltipLine> lines;
            lines.push_back({"ID", std::to_string(item.itemId)});
            lines.push_back({"KEY", item.key});
            lines.push_back({"COUNT", std::to_string(item.count) + " / " + std::to_string(item.stackSize)});
            lines.push_back({"USE_ACTIONS", joinedValues(item.useActions)});
            lines.push_back({"BREAK_ACTIONS", joinedValues(item.breakActions)});
            lines.push_back({"PLACE_ACTIONS", joinedValues(item.placeActions)});
            lines.push_back({"PLACE_BLOCK", item.placeBlockId == 0 ? "none" : std::to_string(item.placeBlockId)});
            lines.push_back({"BREAK_LEVEL", std::to_string(item.breakLevel)});
            lines.push_back({"DURABILITY", item.maxDurability > 0
                ? std::to_string(item.durability) + " / " + std::to_string(item.maxDurability)
                : "none"});
            lines.push_back({"BURN_TIME", item.burnTimeTicks > 0
                ? std::to_string(item.burnTimeTicks) + " ticks"
                : "none"});
            lines.push_back({"BURN_REMAINING", item.maxBurnTicks > 0
                ? std::to_string(item.burnTicksRemaining) + " / " + std::to_string(item.maxBurnTicks)
                : "none"});
            lines.push_back({"SLOT_GAUGE", item.slotGaugeSource.empty() ? "none" : item.slotGaugeSource});
            lines.push_back({"PORTABLE_LIGHT", item.portableLightEmission > 0
                ? std::to_string(item.portableLightEmission)
                : "none"});
            lines.push_back({"HEAT_LEVEL", item.heatLevel > 0
                ? std::to_string(item.heatLevel)
                : "none"});
            lines.push_back({"STACK_SIZE", std::to_string(item.stackSize)});
            lines.push_back({"SLOT_RENDER", item.slotRender});
            lines.push_back({"SLOT_TEXTURE", item.slotTexture});
            lines.push_back({"DROPPED_RENDER", item.droppedRender});
            lines.push_back({"DROPPED_TEXTURE", item.droppedTexture});
            lines.push_back({"HELD_RENDER", item.heldRender});
            lines.push_back({"HELD_TEXTURE", item.heldTexture});
            return lines;
        }

        std::string tooltipLineText(const TooltipLine& line)
        {
            return line.key + ": " + line.value;
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
        if (showSlotGauge(item))
        {
            const SlotGaugeValue gauge = *slotGaugeValue(item);
            const float ratio = gauge.max <= 1
                ? 0.0f
                : static_cast<float>(gauge.current - 1u) / static_cast<float>(gauge.max - 1u);
            const int barLeft = itemLeft + DurabilityBarLeftInset;
            const int barTop = itemTop + DurabilityBarTopOffset;
            const int fillWidth = static_cast<int>(std::round(static_cast<float>(DurabilityBarWidth) * ratio));
            rml += "<div class=\"slot-gauge-bg\" style=\"left: " + std::to_string(barLeft) + "px; top: " + std::to_string(barTop) + "px;\">";
            rml += "<div class=\"slot-gauge-fill\" style=\"width: " + std::to_string(fillWidth) + "px; height: " + std::to_string(DurabilityBarHeight) + "px; background-color: " + slotGaugeColor(ratio) + ";\"></div>";
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
        for (const TooltipLine& line : itemTooltipLines(item))
        {
            rml += "<div class=\"item-tooltip-line\">" + escapeRml(tooltipLineText(line)) + "</div>";
        }
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

    std::string offhandSlotRml(const InventoryItemView& item)
    {
        std::string rml;
        rml += "<img class=\"offhand-slot-image\" src=\"../textures/ui/player/slot.png\"/>";
        rml += itemStackContentRml(item, 16, 16);
        return rml;
    }

    std::optional<TooltipLayout> itemTooltipLayout(const InventoryItemView& item, double mouseX, double mouseY, uint32_t screenWidth, uint32_t screenHeight)
    {
        if (item.itemId == 0 || item.count == 0 || item.key.empty() || item.key == "none")
        {
            return std::nullopt;
        }

        constexpr int Padding = 10;
        constexpr int TitleHeight = 28;
        constexpr int TitleMargin = 4;
        constexpr int LineHeight = 24;
        constexpr int MinWidth = 180;
        constexpr int MaxWidth = 1000;
        constexpr int TitleCharWidth = 15;
        constexpr int LineCharWidth = 16;
        constexpr int MouseOffset = 16;

        const std::vector<TooltipLine> lines = itemTooltipLines(item);
        const int height = Padding * 2 + TitleHeight + TitleMargin + LineHeight * static_cast<int>(lines.size());

        int contentWidth = static_cast<int>(item.name.size()) * TitleCharWidth;
        for (const TooltipLine& line : lines)
        {
            contentWidth = std::max(contentWidth, static_cast<int>(tooltipLineText(line).size()) * LineCharWidth);
        }
        const int width = std::clamp(contentWidth + Padding * 2, MinWidth, MaxWidth);

        int left = static_cast<int>(std::round(mouseX)) + MouseOffset;
        int top = static_cast<int>(std::round(mouseY)) + MouseOffset;
        const int screenW = static_cast<int>(screenWidth);
        const int screenH = static_cast<int>(screenHeight);
        if (left + width > screenW)
        {
            left = static_cast<int>(std::round(mouseX)) - width - MouseOffset;
        }
        if (top + height > screenH)
        {
            top = static_cast<int>(std::round(mouseY)) - height - MouseOffset;
        }
        left = std::clamp(left, 0, std::max(0, screenW - width));
        top = std::clamp(top, 0, std::max(0, screenH - height));

        return TooltipLayout{left, top, width, height};
    }
}
