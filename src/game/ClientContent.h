#pragma once

#include "items/ItemData.h"
#include "world/BlockData.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace dolbuto::game
{
    struct PropModelBinding
    {
        uint16_t blockId = 0;
        std::string modelName;
    };

    class ClientContent
    {
    public:
        static constexpr std::size_t BlockBreakingStageCount = 10;

        static ClientContent load(const std::filesystem::path& assetDirectory);

        const std::vector<BlockDefinition>& blockDefinitions() const;
        const std::vector<FluidDefinition>& fluidDefinitions() const;
        LightAttenuationTablesPtr lightAttenuationTables() const;
        const std::vector<BlockTextureLayers>& blockTextureLayers() const;
        const std::array<uint32_t, BlockBreakingStageCount>& blockBreakingTextureLayers() const;
        const std::vector<ItemDefinition>& itemDefinitions() const;
        const std::unordered_map<std::string, uint16_t>& itemIdByKey() const;
        const std::vector<ItemInteractionRecipe>& itemInteractionRecipes() const;
        const std::vector<std::string>& blockTextureNames() const;
        const std::vector<std::string>& itemTextureNames() const;
        const std::vector<PropModelBinding>& propModelBindings() const;

    private:
        std::vector<BlockDefinition> blockDefinitions_;
        std::vector<FluidDefinition> fluidDefinitions_;
        LightAttenuationTablesPtr lightAttenuationTables_;
        std::vector<BlockTextureLayers> blockTextureLayers_;
        std::array<uint32_t, BlockBreakingStageCount> blockBreakingTextureLayers_{};
        std::vector<ItemDefinition> itemDefinitions_;
        std::unordered_map<std::string, uint16_t> itemIdByKey_;
        std::vector<ItemInteractionRecipe> itemInteractionRecipes_;
        std::vector<std::string> blockTextureNames_;
        std::vector<std::string> itemTextureNames_;
        std::vector<PropModelBinding> propModelBindings_;
    };
}
