#include "game/ClientContent.h"

#include "assets/BlockItemIconBuilder.h"
#include "assets/PropModelLoader.h"
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
            if (value == "block_model")
            {
                return ItemRenderType::BlockModel;
            }
            if (value == "extruded_sprite" || value == "sprite")
            {
                return ItemRenderType::ExtrudedSprite;
            }
            throw std::runtime_error("Unknown item render type: " + value);
        }

        ItemSlotRenderType parseItemSlotRenderType(const std::string& value)
        {
            if (value == "block_model")
            {
                return ItemSlotRenderType::BlockModel;
            }
            if (value == "sprite" || value == "texture")
            {
                return ItemSlotRenderType::Sprite;
            }
            throw std::runtime_error("Unknown item slot render type: " + value);
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

        BlockAttachmentFace parseAttachmentFace(const std::string& value)
        {
            if (value == "bottom")
            {
                return BlockAttachmentFace::Bottom;
            }
            return BlockAttachmentFace::None;
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
            const std::string slotTexture = definition.slotRenderTexture != "none"
                ? definition.slotRenderTexture
                : (definition.slotTexture != "none" ? definition.slotTexture : droppedTexture);

            ItemDefinition itemDefinition{};
            itemDefinition.key = definition.key;
            itemDefinition.name = definition.name;
            itemDefinition.slotTexture = slotTexture;
            itemDefinition.droppedTexture = droppedTexture;
            itemDefinition.heldTexture = heldTexture;
            itemDefinition.useActions = definition.useActions;
            itemDefinition.breakActions = definition.breakActions;
            itemDefinition.placeActions = definition.placeActions;
            itemDefinition.stackSize = definition.stackSize;
            itemDefinition.breakLevel = definition.breakLevel;
            itemDefinition.maxDurability = definition.maxDurability;
            itemDefinition.slotRender = parseItemSlotRenderType(definition.slotRender);
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
                for (const data::ParsedInteractionCandidate& parsedCandidate : definition.candidates)
                {
                    ItemInteractionCandidate candidate{};
                    for (const data::ParsedInteractionOutput& parsedOutput : parsedCandidate.outputs)
                    {
                        const auto outputIt = content.itemIdByKey_.find(parsedOutput.item);
                        if (outputIt == content.itemIdByKey_.end())
                        {
                            log::warn("Interaction references unknown candidate item key: " + definition.action + " -> " + parsedOutput.item);
                            continue;
                        }

                        ItemInteractionOutput output{};
                        output.itemId = outputIt->second;
                        output.min = parsedOutput.min;
                        output.max = parsedOutput.max;
                        candidate.outputs.push_back(output);
                    }
                    if (!candidate.outputs.empty())
                    {
                        recipe.candidates.push_back(std::move(candidate));
                    }
                }
                if (!recipe.action.empty() && recipe.targetItemId != 0 && !recipe.candidates.empty())
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
        std::unordered_map<std::string, uint16_t> blockIdByName;
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
            blockDefinition.breakLevel = definition.breakLevel;
            blockDefinition.breakAction = definition.breakAction;
            blockDefinition.lightAttenuation = definition.lightAttenuation;
            blockDefinition.lightEmission = definition.lightEmission;
            blockDefinition.randomOffset = definition.randomOffset;
            blockDefinition.attachmentFace = parseAttachmentFace(definition.attachmentFace);
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
            blockIdByName[definition.name] = definition.id;

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

        const std::filesystem::path propModelDirectory = assetDirectory / "textures" / "block" / "model";
        std::unordered_set<std::string> checkedPropModels;
        for (const PropModelBinding& binding : content.propModelBindings_)
        {
            if (checkedPropModels.insert(binding.modelName).second)
            {
                assets::ensurePropModelBinary(propModelDirectory, binding.modelName);
            }
        }
        for (const PropModelBinding& binding : content.propModelBindings_)
        {
            const std::filesystem::path dpmPath = propModelDirectory / (binding.modelName + ".dpm");
            assets::PropMesh mesh = assets::loadDpmRenderMesh(dpmPath);
            if (mesh.quads.empty())
            {
                log::warn("Prop model dpm could not be loaded: " + dpmPath.string());
                continue;
            }
            content.propMeshesByBlock_[binding.blockId] = std::move(mesh);
        }

        for (const data::ParsedItemDefinition& definition : parsedItems)
        {
            if (definition.placeBlock.empty())
            {
                continue;
            }

            const auto blockIt = blockIdByName.find(definition.placeBlock);
            if (blockIt == blockIdByName.end())
            {
                log::warn("Item placeBlock references unknown block name: " + definition.key + " -> " + definition.placeBlock);
                continue;
            }
            content.itemDefinitions_[definition.id].placeBlockId = blockIt->second;
        }

        const std::filesystem::path blockTextureDir = assetDirectory / "textures" / "block";
        for (const data::ParsedItemDefinition& definition : parsedItems)
        {
            if (definition.id >= content.itemDefinitions_.size())
            {
                continue;
            }

            ItemDefinition& item = content.itemDefinitions_[definition.id];
            if (item.slotRender != ItemSlotRenderType::BlockModel)
            {
                continue;
            }
            if (item.placeBlockId == 0 ||
                static_cast<size_t>(item.placeBlockId) >= content.blockTextureLayers_.size())
            {
                log::warn("Block model slot item has no valid placeBlock: " + item.key);
                continue;
            }

            const std::string generatedTexture = "generated/" + item.key + "_slot";
            const std::filesystem::path outputPath = assetDirectory / "textures" / "item" / (generatedTexture + ".png");
            if (assets::writeBlockItemIcon(
                blockTextureDir,
                content.blockTextureNames_,
                content.blockTextureLayers_[item.placeBlockId],
                outputPath))
            {
                item.slotTexture = generatedTexture;
            }
            else
            {
                log::warn("Block model slot icon generation failed: " + item.key);
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

    const std::unordered_map<uint16_t, assets::PropMesh>& ClientContent::propMeshesByBlock() const
    {
        return propMeshesByBlock_;
    }

    const assets::PropMesh* ClientContent::propMeshForBlock(uint16_t block) const
    {
        const auto it = propMeshesByBlock_.find(block);
        return it != propMeshesByBlock_.end() ? &it->second : nullptr;
    }
}
