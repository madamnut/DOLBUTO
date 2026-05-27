#pragma once

#include "items/ItemData.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dolbuto::gameplay
{
    enum class InventoryClickButton
    {
        Left,
        Right
    };

    class PlayerInventory
    {
    public:
        static constexpr size_t HotbarSlotCount = 10;
        static constexpr size_t SlotCount = 50;

        size_t slotCount() const;
        const ItemStack& slot(size_t index) const;
        const ItemStack& cursorStack() const;

        void clear();
        void setSlots(const std::array<ItemStack, SlotCount>& slots, const std::vector<ItemDefinition>& itemDefinitions);
        std::array<ItemStack, SlotCount> snapshot() const;

        uint16_t add(ItemStack stack, const std::vector<ItemDefinition>& itemDefinitions);
        uint16_t addToRange(ItemStack& stack, size_t begin, size_t end, const std::vector<ItemDefinition>& itemDefinitions);
        bool removeFromSlot(size_t slotIndex, uint16_t count);
        bool damageSlot(size_t slotIndex, uint16_t damage, const std::vector<ItemDefinition>& itemDefinitions);
        bool stackCanMerge(const ItemStack& slot, const ItemStack& stack, const std::vector<ItemDefinition>& itemDefinitions) const;

        bool handleSlotClick(size_t slotIndex, InventoryClickButton button, bool shift, const std::vector<ItemDefinition>& itemDefinitions);
        bool swapHotbarWithSlot(size_t slotIndex, size_t hotbarSlot);
        bool closeCursor(const std::vector<ItemDefinition>& itemDefinitions);

    private:
        static bool validStack(const ItemStack& stack, const std::vector<ItemDefinition>& itemDefinitions);
        static ItemStack normalizedStack(ItemStack stack, const std::vector<ItemDefinition>& itemDefinitions);

        std::array<ItemStack, SlotCount> slots_{};
        ItemStack cursorStack_{};
    };
}
