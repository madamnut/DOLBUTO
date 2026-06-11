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
        uint16_t burnTicksRemaining = 0;
        uint16_t moltenFluidId = 0;
        uint16_t moltenAmount = 0;
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

    enum class ItemSlotGaugeSource : uint8_t
    {
        None,
        Durability,
        BurnTicks
    };

    struct ItemDefinition
    {
        std::string key = "none";
        std::string name = "None";
        std::string slotTexture = "none";
        std::string droppedTexture = "none";
        std::string heldTexture = "none";
        std::string droppedBottomTexture = "none";
        std::string droppedTopTexture = "none";
        std::string heldBottomTexture = "none";
        std::string heldTopTexture = "none";
        std::vector<std::string> useActions;
        std::vector<std::string> breakActions;
        std::vector<std::string> placeActions;
        uint16_t stackSize = 0;
        uint16_t breakLevel = 0;
        uint16_t maxDurability = 0;
        uint16_t placeBlockId = 0;
        uint16_t modelBlockId = 0;
        uint32_t modelTextureLayer = 0;
        uint32_t burnTimeTicks = 0;
        uint16_t heatLevel = 0;
        uint16_t burnRemainderItemId = 0;
        uint16_t burnRemainderCount = 0;
        uint16_t portableLightEmission = 0;
        uint16_t maxBurnTicks = 0;
        ItemSlotGaugeSource slotGaugeSource = ItemSlotGaugeSource::None;
        uint16_t extinguishedItemId = 0;
        uint16_t burnoutItemId = 0;
        uint16_t burnoutCount = 0;
        bool burnTicksOnlyWhileHeld = false;
        uint32_t droppedTextureLayer = 0;
        uint32_t heldTextureLayer = 0;
        uint32_t droppedBottomTextureLayer = 0;
        uint32_t droppedTopTextureLayer = 0;
        uint32_t heldBottomTextureLayer = 0;
        uint32_t heldTopTextureLayer = 0;
        float blockModelWidth = 0.0f;
        float blockModelHeight = 0.0f;
        float blockModelDepth = 0.0f;
        bool useBlockModelVerticalSection = false;
        bool useBlockModelCrucibleShape = false;
        bool hasModelTexture = false;
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
        bool resultTargetsHeldItem = false;
        std::string placeBlockPlacement;
        std::string displayName;
        std::string iconTexture;
        std::string specialAction;
    };

    struct ItemInteractionRecipe
    {
        std::string action;
        uint16_t targetItemId = 0;
        uint16_t targetBlockId = 0;
        uint16_t heldItemId = 0;
        bool targetAnyBlock = false;
        uint16_t targetCount = 1;
        std::vector<ItemInteractionIngredient> ingredients;
        std::vector<ItemInteractionCandidate> candidates;
    };

    struct ItemProcessingRecipe
    {
        std::string type;
        uint16_t inputItemId = 0;
        uint16_t outputItemId = 0;
        uint16_t outputCount = 1;
        uint16_t outputFluidId = 0;
        uint16_t outputAmount = 0;
        uint16_t requiredHeatLevel = 0;
        uint32_t requiredTicks = 0;
    };
}
