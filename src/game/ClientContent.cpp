#include "game/ClientContent.h"

#include "data/DataLoaders.h"
#include "platform/Log.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace dolbuto::game
{
    namespace
    {
        std::vector<char> readContentFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Failed to open file: " + path.string());
            }

            const std::streamsize size = file.tellg();
            std::vector<char> buffer(static_cast<size_t>(size));
            file.seekg(0);
            file.read(buffer.data(), size);
            return buffer;
        }

        ItemRenderType parseItemRenderType(const std::string& value)
        {
            if (value == "extruded_sprite" || value == "sprite")
            {
                return ItemRenderType::ExtrudedSprite;
            }
            throw std::runtime_error("Unknown item render type: " + value);
        }

        BlockRenderType parseRenderType(const std::string& value)
        {
            if (value == "cube")
            {
                return BlockRenderType::Cube;
            }
            if (value == "cross")
            {
                return BlockRenderType::Cross;
            }
            if (value == "prop")
            {
                return BlockRenderType::Prop;
            }
            return BlockRenderType::None;
        }

        BlockFaceOcclusion parseFaceOcclusion(const std::string& value)
        {
            if (value == "opaque")
            {
                return BlockFaceOcclusion::Opaque;
            }
            if (value == "cutout")
            {
                return BlockFaceOcclusion::Cutout;
            }
            return BlockFaceOcclusion::None;
        }

        BlockAlphaMode parseAlphaMode(const std::string& value)
        {
            if (value == "cutout")
            {
                return BlockAlphaMode::Cutout;
            }
            if (value == "blend")
            {
                return BlockAlphaMode::Blend;
            }
            return BlockAlphaMode::Opaque;
        }
    }

    ClientContent ClientContent::load(const std::filesystem::path& assetDirectory)
    {
        ClientContent content{};

        const std::vector<char> itemDefinitionData = readContentFile(assetDirectory / "data" / "items.json");
        const std::string itemDefinitionText(itemDefinitionData.begin(), itemDefinitionData.end());
        const std::vector<data::ParsedItemDefinition> parsedItems = data::parseItemDefinitions(itemDefinitionText);

        std::unordered_map<std::string, uint32_t> itemTextureLayerByName;
        auto layerForItemTexture = [&](const std::string& textureName) -> uint32_t
        {
            auto it = itemTextureLayerByName.find(textureName);
            if (it != itemTextureLayerByName.end())
            {
                return it->second;
            }

            const uint32_t layer = static_cast<uint32_t>(content.itemTextureNames_.size());
            itemTextureLayerByName.emplace(textureName, layer);
            content.itemTextureNames_.push_back(textureName);
            return layer;
        };

        content.itemDefinitions_.assign(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u, {});
        for (const data::ParsedItemDefinition& definition : parsedItems)
        {
            const std::string droppedTexture = definition.droppedTexture != "none" ? definition.droppedTexture : definition.texture;
            const std::string heldTexture = definition.heldTexture != "none" ? definition.heldTexture : droppedTexture;
            const std::string slotTexture = definition.slotTexture != "none" ? definition.slotTexture : droppedTexture;

            ItemDefinition itemDefinition{};
            itemDefinition.key = definition.key;
            itemDefinition.name = definition.name;
            itemDefinition.slotTexture = slotTexture;
            itemDefinition.droppedTexture = droppedTexture;
            itemDefinition.heldTexture = heldTexture;
            itemDefinition.actions = definition.actions;
            itemDefinition.stackSize = definition.stackSize;
            itemDefinition.droppedRender = parseItemRenderType(definition.droppedRender);
            itemDefinition.heldRender = parseItemRenderType(definition.heldRender);
            if (droppedTexture != "none")
            {
                itemDefinition.droppedTextureLayer = layerForItemTexture(droppedTexture);
            }
            if (heldTexture != "none")
            {
                itemDefinition.heldTextureLayer = layerForItemTexture(heldTexture);
            }
            content.itemDefinitions_[definition.id] = itemDefinition;
            if (!definition.key.empty())
            {
                content.itemIdByKey_[definition.key] = definition.id;
            }
        }

        const std::filesystem::path interactionPath = assetDirectory / "data" / "interactions.json";
        if (std::filesystem::exists(interactionPath))
        {
            const std::vector<char> interactionData = readContentFile(interactionPath);
            const std::string interactionText(interactionData.begin(), interactionData.end());
            const std::vector<data::ParsedInteractionDefinition> parsedInteractions = data::parseInteractionDefinitions(interactionText);
            for (const data::ParsedInteractionDefinition& definition : parsedInteractions)
            {
                const auto targetIt = content.itemIdByKey_.find(definition.target);
                if (targetIt == content.itemIdByKey_.end())
                {
                    log::warn("Interaction references unknown target item key: " + definition.action + " -> " + definition.target);
                    continue;
                }

                ItemInteractionRecipe recipe{};
                recipe.action = definition.action;
                recipe.targetItemId = targetIt->second;
                for (const std::string& candidate : definition.candidates)
                {
                    const auto candidateIt = content.itemIdByKey_.find(candidate);
                    if (candidateIt == content.itemIdByKey_.end())
                    {
                        log::warn("Interaction references unknown candidate item key: " + definition.action + " -> " + candidate);
                        continue;
                    }
                    recipe.candidateItemIds.push_back(candidateIt->second);
                }
                if (!recipe.action.empty() && recipe.targetItemId != 0 && !recipe.candidateItemIds.empty())
                {
                    content.itemInteractionRecipes_.push_back(std::move(recipe));
                }
            }
        }

        const std::vector<char> blockDefinitionData = readContentFile(assetDirectory / "data" / "blocks.json");
        const std::string blockDefinitionText(blockDefinitionData.begin(), blockDefinitionData.end());
        const std::vector<data::ParsedBlockDefinition> parsedBlocks = data::parseBlockDefinitions(blockDefinitionText);

        std::unordered_map<std::string, uint32_t> textureLayerByName;
        auto layerForTexture = [&](const std::string& textureName) -> uint32_t
        {
            auto it = textureLayerByName.find(textureName);
            if (it != textureLayerByName.end())
            {
                return it->second;
            }

            const uint32_t layer = static_cast<uint32_t>(content.blockTextureNames_.size());
            textureLayerByName.emplace(textureName, layer);
            content.blockTextureNames_.push_back(textureName);
            return layer;
        };

        content.blockDefinitions_.assign(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u, {});
        content.blockTextureLayers_.assign(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u, {});
        for (const data::ParsedBlockDefinition& definition : parsedBlocks)
        {
            BlockDefinition blockDefinition{};
            blockDefinition.name = definition.name;
            blockDefinition.renderType = parseRenderType(definition.renderType);
            blockDefinition.directional = definition.directional;
            blockDefinition.collision = definition.collision;
            blockDefinition.ao = definition.ao;
            blockDefinition.faceOcclusion = parseFaceOcclusion(definition.faceOcclusion);
            blockDefinition.sameBlockFaceCulling = definition.sameBlockFaceCulling;
            blockDefinition.alphaMode = parseAlphaMode(definition.alphaMode);
            blockDefinition.alphaCutoff = definition.alphaCutoff;
            blockDefinition.alphaBlend = definition.alphaBlend;
            blockDefinition.mipDistanceScale = definition.mipDistanceScale;
            blockDefinition.hardness = definition.hardness;
            blockDefinition.lightAttenuation = definition.lightAttenuation;
            blockDefinition.lightEmission = definition.lightEmission;
            blockDefinition.randomOffset = definition.randomOffset;
            for (size_t dropIndex = 0; dropIndex < definition.dropItemKeys.size(); ++dropIndex)
            {
                const auto itemIt = content.itemIdByKey_.find(definition.dropItemKeys[dropIndex]);
                if (itemIt == content.itemIdByKey_.end())
                {
                    log::warn("Block drop references unknown item key: " + definition.name + " -> " + definition.dropItemKeys[dropIndex]);
                    continue;
                }

                BlockDrop drop{};
                drop.itemId = itemIt->second;
                drop.min = dropIndex < definition.dropMins.size() ? definition.dropMins[dropIndex] : 1;
                drop.max = dropIndex < definition.dropMaxes.size() ? definition.dropMaxes[dropIndex] : drop.min;
                if (drop.max < drop.min)
                {
                    std::swap(drop.min, drop.max);
                }
                drop.chance = dropIndex < definition.dropChances.size() ? definition.dropChances[dropIndex] : 1.0f;
                if (drop.itemId != 0 && drop.max > 0)
                {
                    blockDefinition.drops.push_back(drop);
                }
            }
            content.blockDefinitions_[definition.id] = blockDefinition;

            BlockTextureLayers layers{};
            if (const auto it = definition.textures.find("all"); it != definition.textures.end())
            {
                layers.faces.fill(layerForTexture(it->second));
            }
            if (const auto it = definition.textures.find("topBottom"); it != definition.textures.end())
            {
                const uint32_t layer = layerForTexture(it->second);
                layers.faces[0] = layer;
                layers.faces[1] = layer;
            }
            if (const auto it = definition.textures.find("side"); it != definition.textures.end())
            {
                const uint32_t layer = layerForTexture(it->second);
                layers.faces[2] = layer;
                layers.faces[3] = layer;
                layers.faces[4] = layer;
                layers.faces[5] = layer;
            }
            if (const auto it = definition.textures.find("top"); it != definition.textures.end())
            {
                layers.faces[0] = layerForTexture(it->second);
            }
            if (const auto it = definition.textures.find("bottom"); it != definition.textures.end())
            {
                layers.faces[1] = layerForTexture(it->second);
            }
            if (!definition.propTexture.empty())
            {
                layers.faces.fill(layerForTexture(definition.propTexture));
            }
            content.blockTextureLayers_[definition.id] = layers;

            if (definition.renderType == "prop" && !definition.propModel.empty())
            {
                content.propModelBindings_.push_back(PropModelBinding{definition.id, definition.propModel});
            }
        }

        const std::vector<char> fluidDefinitionData = readContentFile(assetDirectory / "data" / "fluids.json");
        const std::string fluidDefinitionText(fluidDefinitionData.begin(), fluidDefinitionData.end());
        const std::vector<data::ParsedFluidDefinition> parsedFluids = data::parseFluidDefinitions(fluidDefinitionText);
        content.fluidDefinitions_.assign(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u, {});
        for (const data::ParsedFluidDefinition& definition : parsedFluids)
        {
            FluidDefinition fluidDefinition{};
            fluidDefinition.name = definition.name;
            fluidDefinition.lightAttenuation = definition.lightAttenuation;
            content.fluidDefinitions_[definition.id] = fluidDefinition;
        }

        auto lightAttenuationTables = std::make_shared<LightAttenuationTables>();
        lightAttenuationTables->block.assign(content.blockDefinitions_.size(), 15);
        lightAttenuationTables->blockEmission.assign(content.blockDefinitions_.size(), 0);
        for (size_t i = 0; i < content.blockDefinitions_.size(); ++i)
        {
            lightAttenuationTables->block[i] = content.blockDefinitions_[i].lightAttenuation;
            lightAttenuationTables->blockEmission[i] = content.blockDefinitions_[i].lightEmission;
        }
        lightAttenuationTables->fluid.assign(content.fluidDefinitions_.size(), 0);
        for (size_t i = 0; i < content.fluidDefinitions_.size(); ++i)
        {
            lightAttenuationTables->fluid[i] = content.fluidDefinitions_[i].lightAttenuation;
        }
        content.lightAttenuationTables_ = std::move(lightAttenuationTables);

        for (size_t i = 0; i < content.blockBreakingTextureLayers_.size(); ++i)
        {
            content.blockBreakingTextureLayers_[i] = layerForTexture("breaking/destroy_stage_" + std::to_string(i));
        }

        if (content.blockTextureNames_.empty())
        {
            throw std::runtime_error("No block textures were found in blocks.json.");
        }

        return content;
    }

    const std::vector<BlockDefinition>& ClientContent::blockDefinitions() const
    {
        return blockDefinitions_;
    }

    const std::vector<FluidDefinition>& ClientContent::fluidDefinitions() const
    {
        return fluidDefinitions_;
    }

    LightAttenuationTablesPtr ClientContent::lightAttenuationTables() const
    {
        return lightAttenuationTables_;
    }

    const std::vector<BlockTextureLayers>& ClientContent::blockTextureLayers() const
    {
        return blockTextureLayers_;
    }

    const std::array<uint32_t, ClientContent::BlockBreakingStageCount>& ClientContent::blockBreakingTextureLayers() const
    {
        return blockBreakingTextureLayers_;
    }

    const std::vector<ItemDefinition>& ClientContent::itemDefinitions() const
    {
        return itemDefinitions_;
    }

    const std::unordered_map<std::string, uint16_t>& ClientContent::itemIdByKey() const
    {
        return itemIdByKey_;
    }

    const std::vector<ItemInteractionRecipe>& ClientContent::itemInteractionRecipes() const
    {
        return itemInteractionRecipes_;
    }

    const std::vector<std::string>& ClientContent::blockTextureNames() const
    {
        return blockTextureNames_;
    }

    const std::vector<std::string>& ClientContent::itemTextureNames() const
    {
        return itemTextureNames_;
    }

    const std::vector<PropModelBinding>& ClientContent::propModelBindings() const
    {
        return propModelBindings_;
    }
}
