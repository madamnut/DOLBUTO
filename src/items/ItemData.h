#pragma once

#include <cstdint>
#include <string>

namespace dolbuto
{
    struct ItemStack
    {
        uint16_t itemId = 0;
        uint16_t count = 0;
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
        uint16_t stackSize = 0;
        uint32_t droppedTextureLayer = 0;
        uint32_t heldTextureLayer = 0;
        ItemRenderType droppedRender = ItemRenderType::ExtrudedSprite;
        ItemRenderType heldRender = ItemRenderType::ExtrudedSprite;
    };
}
