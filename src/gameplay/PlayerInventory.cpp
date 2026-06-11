#include "gameplay/PlayerInventory.h"

#include <algorithm>
#include <utility>

namespace dolbuto::gameplay
{
    namespace
    {
        bool isMoltenCarrier(const ItemDefinition& definition)
        {
            return std::find(definition.useActions.begin(), definition.useActions.end(), "scoop") != definition.useActions.end() ||
                std::find(definition.useActions.begin(), definition.useActions.end(), "pour") != definition.useActions.end();
        }
    }

    size_t PlayerInventory::slotCount() const
    {
        return slots_.size();
    }

    const ItemStack& PlayerInventory::slot(size_t index) const
    {
        static const ItemStack EmptyStack{};
        return index < slots_.size() ? slots_[index] : EmptyStack;
    }

    const ItemStack& PlayerInventory::offhandSlot() const
    {
        return offhandSlot_;
    }

    const ItemStack& PlayerInventory::cursorStack() const
    {
        return cursorStack_;
    }

    void PlayerInventory::clear()
    {
        slots_.fill(ItemStack{});
        offhandSlot_ = {};
        cursorStack_ = {};
    }

    void PlayerInventory::setSlots(const std::array<ItemStack, PlayerInventory::SlotCount>& slots, const std::vector<ItemDefinition>& itemDefinitions)
    {
        slots_.fill(ItemStack{});
        for (size_t i = 0; i < slots.size() && i < slots_.size(); ++i)
        {
            const ItemStack& source = slots[i];
            if (!validStack(source, itemDefinitions))
            {
                continue;
            }

            slots_[i] = normalizedStack(source, itemDefinitions);
        }
        cursorStack_ = {};
    }

    void PlayerInventory::setOffhandSlot(ItemStack stack, const std::vector<ItemDefinition>& itemDefinitions)
    {
        offhandSlot_ = validStack(stack, itemDefinitions) ? normalizedStack(stack, itemDefinitions) : ItemStack{};
    }

    std::array<ItemStack, PlayerInventory::SlotCount> PlayerInventory::snapshot() const
    {
        return slots_;
    }

    uint16_t PlayerInventory::add(ItemStack stack, const std::vector<ItemDefinition>& itemDefinitions)
    {
        return addToRange(stack, 0, slots_.size(), itemDefinitions);
    }

    uint16_t PlayerInventory::addToRange(ItemStack& stack, size_t begin, size_t end, const std::vector<ItemDefinition>& itemDefinitions)
    {
        if (!validStack(stack, itemDefinitions))
        {
            return stack.count;
        }
        stack = normalizedStack(stack, itemDefinitions);

        end = std::min(end, slots_.size());
        if (begin >= end)
        {
            return stack.count;
        }

        const uint16_t maxStack = itemDefinitions[stack.itemId].stackSize;
        for (size_t i = begin; i < end && stack.count > 0; ++i)
        {
            ItemStack& target = slots_[i];
            if (!stackCanMerge(target, stack, itemDefinitions))
            {
                continue;
            }

            const uint16_t available = static_cast<uint16_t>(maxStack - target.count);
            const uint16_t moved = std::min(available, stack.count);
            target.count = static_cast<uint16_t>(target.count + moved);
            stack.count = static_cast<uint16_t>(stack.count - moved);
        }

        for (size_t i = begin; i < end && stack.count > 0; ++i)
        {
            ItemStack& target = slots_[i];
            if (target.itemId != 0 || target.count != 0)
            {
                continue;
            }

            const uint16_t moved = std::min(maxStack, stack.count);
            target.itemId = stack.itemId;
            target.count = moved;
            target.durability = stack.durability;
            target.burnTicksRemaining = stack.burnTicksRemaining;
            target.moltenFluidId = stack.moltenFluidId;
            target.moltenAmount = stack.moltenAmount;
            stack.count = static_cast<uint16_t>(stack.count - moved);
            if (stack.count == 0)
            {
                stack.durability = 0;
                stack.burnTicksRemaining = 0;
                stack.moltenFluidId = 0;
                stack.moltenAmount = 0;
            }
        }

        return stack.count;
    }

    bool PlayerInventory::removeFromSlot(size_t slotIndex, uint16_t count)
    {
        if (slotIndex >= slots_.size() || count == 0)
        {
            return false;
        }

        ItemStack& target = slots_[slotIndex];
        if (target.itemId == 0 || target.count < count)
        {
            return false;
        }

        target.count = static_cast<uint16_t>(target.count - count);
        if (target.count == 0)
        {
            target = {};
        }
        return true;
    }

    bool PlayerInventory::replaceSlot(size_t slotIndex, ItemStack stack, const std::vector<ItemDefinition>& itemDefinitions)
    {
        if (slotIndex >= slots_.size())
        {
            return false;
        }

        slots_[slotIndex] = validStack(stack, itemDefinitions) ? normalizedStack(stack, itemDefinitions) : ItemStack{};
        return true;
    }

    bool PlayerInventory::tickHeldBurningItems(size_t selectedHotbarSlot, bool extinguishHeldBurnableLights, const std::vector<ItemDefinition>& itemDefinitions)
    {
        bool changed = false;
        auto tickStack = [&](ItemStack& stack)
        {
            if (!validStack(stack, itemDefinitions))
            {
                return;
            }

            const ItemDefinition& definition = itemDefinitions[stack.itemId];
            if (definition.maxBurnTicks == 0 || !definition.burnTicksOnlyWhileHeld)
            {
                return;
            }

            if (extinguishHeldBurnableLights && definition.extinguishedItemId != 0 &&
                static_cast<std::size_t>(definition.extinguishedItemId) < itemDefinitions.size() &&
                itemDefinitions[definition.extinguishedItemId].stackSize != 0)
            {
                const uint16_t remainingTicks = stack.burnTicksRemaining == 0
                    ? definition.maxBurnTicks
                    : std::min(stack.burnTicksRemaining, definition.maxBurnTicks);
                stack.itemId = definition.extinguishedItemId;
                stack.count = std::min<uint16_t>(stack.count, itemDefinitions[definition.extinguishedItemId].stackSize);
                stack.durability = 0;
                stack.burnTicksRemaining = remainingTicks;
                changed = true;
                return;
            }

            if (stack.burnTicksRemaining == 0)
            {
                stack.burnTicksRemaining = definition.maxBurnTicks;
            }
            --stack.burnTicksRemaining;
            changed = true;
            if (stack.burnTicksRemaining > 0)
            {
                return;
            }

            if (definition.burnoutItemId != 0 &&
                static_cast<std::size_t>(definition.burnoutItemId) < itemDefinitions.size() &&
                definition.burnoutCount != 0)
            {
                stack = ItemStack{
                    definition.burnoutItemId,
                    std::min(definition.burnoutCount, itemDefinitions[definition.burnoutItemId].stackSize),
                    0,
                    0
                };
            }
            else
            {
                stack = {};
            }
            changed = true;
        };

        if (selectedHotbarSlot < HotbarSlotCount)
        {
            tickStack(slots_[selectedHotbarSlot]);
        }
        tickStack(offhandSlot_);
        return changed;
    }

    bool PlayerInventory::damageSlot(size_t slotIndex, uint16_t damage, const std::vector<ItemDefinition>& itemDefinitions)
    {
        if (slotIndex >= slots_.size() || damage == 0)
        {
            return false;
        }

        ItemStack& target = slots_[slotIndex];
        if (!validStack(target, itemDefinitions))
        {
            return false;
        }

        const uint16_t maxDurability = itemDefinitions[target.itemId].maxDurability;
        if (maxDurability == 0)
        {
            return false;
        }

        if (target.durability == 0 || target.durability > maxDurability)
        {
            target.durability = maxDurability;
        }
        if (damage >= target.durability)
        {
            target = {};
            return true;
        }

        target.durability = static_cast<uint16_t>(target.durability - damage);
        return true;
    }

    bool PlayerInventory::stackCanMerge(const ItemStack& slot, const ItemStack& stack, const std::vector<ItemDefinition>& itemDefinitions) const
    {
        if (slot.itemId == 0 || stack.itemId == 0 || slot.itemId != stack.itemId || static_cast<size_t>(slot.itemId) >= itemDefinitions.size())
        {
            return false;
        }
        if (slot.durability != stack.durability)
        {
            return false;
        }
        if (slot.burnTicksRemaining != stack.burnTicksRemaining)
        {
            return false;
        }
        if (slot.moltenFluidId != stack.moltenFluidId || slot.moltenAmount != stack.moltenAmount)
        {
            return false;
        }
        return slot.count < itemDefinitions[slot.itemId].stackSize;
    }

    bool PlayerInventory::handleSlotClick(size_t slotIndex, InventoryClickButton button, bool shift, const std::vector<ItemDefinition>& itemDefinitions)
    {
        if (slotIndex >= slots_.size())
        {
            return false;
        }

        ItemStack& target = slots_[slotIndex];
        if (shift && cursorStack_.itemId == 0 && target.itemId != 0 && target.count != 0)
        {
            ItemStack moving = target;
            target = {};
            if (slotIndex < HotbarSlotCount)
            {
                addToRange(moving, HotbarSlotCount, slots_.size(), itemDefinitions);
            }
            else
            {
                addToRange(moving, 0, HotbarSlotCount, itemDefinitions);
            }
            target = moving.count == 0 ? ItemStack{} : moving;
            return true;
        }

        if (button == InventoryClickButton::Left)
        {
            if (cursorStack_.itemId == 0 || cursorStack_.count == 0)
            {
                cursorStack_ = target;
                target = {};
                return true;
            }
            if (target.itemId == 0 || target.count == 0)
            {
                target = cursorStack_;
                cursorStack_ = {};
                return true;
            }
            if (stackCanMerge(target, cursorStack_, itemDefinitions))
            {
                const uint16_t maxStack = itemDefinitions[target.itemId].stackSize;
                const uint16_t moved = std::min(static_cast<uint16_t>(maxStack - target.count), cursorStack_.count);
                target.count = static_cast<uint16_t>(target.count + moved);
                cursorStack_.count = static_cast<uint16_t>(cursorStack_.count - moved);
                if (cursorStack_.count == 0)
                {
                    cursorStack_ = {};
                }
                return moved > 0;
            }

            std::swap(target, cursorStack_);
            return true;
        }

        if (cursorStack_.itemId == 0 || cursorStack_.count == 0)
        {
            if (target.itemId == 0 || target.count == 0)
            {
                return false;
            }

            const uint16_t taken = static_cast<uint16_t>((target.count + 1u) / 2u);
            cursorStack_.itemId = target.itemId;
            cursorStack_.count = taken;
            cursorStack_.durability = target.durability;
            cursorStack_.burnTicksRemaining = target.burnTicksRemaining;
            cursorStack_.moltenFluidId = target.moltenFluidId;
            cursorStack_.moltenAmount = target.moltenAmount;
            target.count = static_cast<uint16_t>(target.count - taken);
            if (target.count == 0)
            {
                target = {};
            }
            return true;
        }

        if (target.itemId == 0 || target.count == 0)
        {
            target.itemId = cursorStack_.itemId;
            target.count = 1;
            target.durability = cursorStack_.durability;
            target.burnTicksRemaining = cursorStack_.burnTicksRemaining;
            target.moltenFluidId = cursorStack_.moltenFluidId;
            target.moltenAmount = cursorStack_.moltenAmount;
            cursorStack_.count = static_cast<uint16_t>(cursorStack_.count - 1u);
            if (cursorStack_.count == 0)
            {
                cursorStack_ = {};
            }
            return true;
        }

        if (stackCanMerge(target, cursorStack_, itemDefinitions))
        {
            target.count = static_cast<uint16_t>(target.count + 1u);
            cursorStack_.count = static_cast<uint16_t>(cursorStack_.count - 1u);
            if (cursorStack_.count == 0)
            {
                cursorStack_ = {};
            }
            return true;
        }

        return false;
    }

    bool PlayerInventory::swapHotbarWithSlot(size_t slotIndex, size_t hotbarSlot)
    {
        if (slotIndex >= slots_.size() || hotbarSlot >= HotbarSlotCount || slotIndex == hotbarSlot ||
            cursorStack_.itemId != 0 || cursorStack_.count != 0)
        {
            return false;
        }

        std::swap(slots_[slotIndex], slots_[hotbarSlot]);
        return true;
    }

    bool PlayerInventory::swapOffhandWithHotbar(size_t hotbarSlot)
    {
        if (hotbarSlot >= HotbarSlotCount || cursorStack_.itemId != 0 || cursorStack_.count != 0)
        {
            return false;
        }

        std::swap(offhandSlot_, slots_[hotbarSlot]);
        return true;
    }

    bool PlayerInventory::closeCursor(const std::vector<ItemDefinition>& itemDefinitions)
    {
        if (cursorStack_.itemId == 0 || cursorStack_.count == 0)
        {
            return false;
        }

        const uint16_t remaining = add(cursorStack_, itemDefinitions);
        if (remaining == 0)
        {
            cursorStack_ = {};
        }
        else
        {
            cursorStack_.count = remaining;
        }
        return true;
    }

    bool PlayerInventory::validStack(const ItemStack& stack, const std::vector<ItemDefinition>& itemDefinitions)
    {
        if (stack.itemId == 0 || stack.count == 0 || static_cast<size_t>(stack.itemId) >= itemDefinitions.size())
        {
            return false;
        }
        return itemDefinitions[stack.itemId].stackSize != 0;
    }

    ItemStack PlayerInventory::normalizedStack(ItemStack stack, const std::vector<ItemDefinition>& itemDefinitions)
    {
        if (!validStack(stack, itemDefinitions))
        {
            return {};
        }

        const ItemDefinition& definition = itemDefinitions[stack.itemId];
        stack.count = std::min(stack.count, definition.stackSize);
        if (definition.maxDurability > 0)
        {
            stack.count = std::min<uint16_t>(stack.count, 1u);
            stack.durability = stack.durability == 0
                ? definition.maxDurability
                : std::min(stack.durability, definition.maxDurability);
        }
        else
        {
            stack.durability = 0;
        }
        if (definition.maxBurnTicks > 0)
        {
            stack.count = std::min<uint16_t>(stack.count, 1u);
            stack.burnTicksRemaining = stack.burnTicksRemaining == 0
                ? definition.maxBurnTicks
                : std::min(stack.burnTicksRemaining, definition.maxBurnTicks);
        }
        else
        {
            stack.burnTicksRemaining = 0;
        }
        if (isMoltenCarrier(definition))
        {
            stack.count = std::min<uint16_t>(stack.count, 1u);
            if (stack.moltenAmount == 0)
            {
                stack.moltenFluidId = 0;
            }
            stack.moltenAmount = std::min<uint16_t>(stack.moltenAmount, 10u);
        }
        else
        {
            stack.moltenFluidId = 0;
            stack.moltenAmount = 0;
        }
        return stack;
    }
}
