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
        ExtrudedSprite,
        BlockModel
    };

    enum class ItemSlotRenderType : uint8_t
    {
        Sprite,
        BlockModel
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
        std::vector<std::string> placeActions;
        uint16_t stackSize = 0;
        uint16_t breakLevel = 0;
        uint16_t maxDurability = 0;
        uint16_t placeBlockId = 0;
        uint32_t droppedTextureLayer = 0;
        uint32_t heldTextureLayer = 0;
        ItemSlotRenderType slotRender = ItemSlotRenderType::Sprite;
        ItemRenderType droppedRender = ItemRenderType::ExtrudedSprite;
        ItemRenderType heldRender = ItemRenderType::ExtrudedSprite;
    };

    struct ItemInteractionOutput
    {
        uint16_t itemId = 0;
        uint16_t min = 1;
        uint16_t max = 1;
    };

    struct ItemInteractionIngredient
    {
        uint16_t itemId = 0;
        uint16_t count = 1;
    };

    struct ItemInteractionCandidate
    {
        bool enabled = true;
        std::vector<ItemInteractionIngredient> ingredients;
        std::vector<ItemInteractionOutput> outputs;
        uint16_t placeBlockId = 0;
        std::string placeBlockPlacement;
        std::string displayName;
        std::string iconTexture;
    };

    struct ItemInteractionRecipe
    {
        std::string action;
        uint16_t targetItemId = 0;
        uint16_t targetBlockId = 0;
        bool targetAnyBlock = false;
        uint16_t targetCount = 1;
        std::vector<ItemInteractionIngredient> ingredients;
        std::vector<ItemInteractionCandidate> candidates;
    };
}
