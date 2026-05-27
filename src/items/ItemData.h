#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dolbuto
{
    struct ItemStack
    {
        uint16_t itemId = 0;
        uint16_t count = 0;
        uint16_t durability = 0;
    };

    enum class ItemRenderType : uint8_t
    {
        ExtrudedSprite
    };

    struct ItemDefinition
    {
        std::string key = "none";
        std::string name = "None";
        std::string slotTexture = "none";
        std::string droppedTexture = "none";
        std::string heldTexture = "none";
        std::vector<std::string> useActions;
        std::vector<std::string> breakActions;
        uint16_t stackSize = 0;
        uint16_t breakLevel = 0;
        uint16_t maxDurability = 0;
        uint32_t droppedTextureLayer = 0;
        uint32_t heldTextureLayer = 0;
        ItemRenderType droppedRender = ItemRenderType::ExtrudedSprite;
        ItemRenderType heldRender = ItemRenderType::ExtrudedSprite;
    };

    struct ItemInteractionRecipe
    {
        std::string action;
        uint16_t targetItemId = 0;
        std::vector<uint16_t> candidateItemIds;
        uint16_t resultCountMin = 1;
        uint16_t resultCountMax = 1;
    };
}
