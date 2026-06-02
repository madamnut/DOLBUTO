#include "game/ClientContent.h"

#include "assets/BlockItemIconBuilder.h"
#include "assets/PropModelLoader.h"
#include "data/DataLoaders.h"
#include "platform/Log.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dolbuto::game
{
    namespace
    {
        constexpr int FireAnimationFrameCount = 14;

        struct RgbaImage
        {
            int width = 0;
            int height = 0;
            std::vector<unsigned char> pixels;
        };

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

        struct ItemBlockModelSize
        {
            float width = 0.0f;
            float height = 0.0f;
            float depth = 0.0f;
            bool useVerticalSection = false;
        };

        ItemBlockModelSize parseItemBlockModelSize(const std::string& value)
        {
            if (value.empty() || value == "source")
            {
                return {};
            }
            if (value == "cube")
            {
                return {1.0f, 1.0f, 1.0f};
            }
            if (value == "slab")
            {
                return {1.0f, 0.5f, 1.0f};
            }
            if (value == "quarter_log" || value == "quarter")
            {
                return {0.5f, 0.5f, 1.0f, true};
            }
            throw std::runtime_error("Unknown item block model shape: " + value);
        }

        int blockTextureFaceIndex(const std::string& value)
        {
            if (value == "up" || value == "top")
            {
                return 0;
            }
            if (value == "down" || value == "bottom")
            {
                return 1;
            }
            if (value == "right" || value == "east")
            {
                return 2;
            }
            if (value == "left" || value == "west")
            {
                return 3;
            }
            if (value == "front" || value == "south")
            {
                return 4;
            }
            if (value == "back" || value == "north")
            {
                return 5;
            }
            return -1;
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
            if (value == "fire")
            {
                return BlockRenderType::Fire;
            }
            if (value == "slab")
            {
                return BlockRenderType::Slab;
            }
            if (value == "half_slab")
            {
                return BlockRenderType::HalfSlab;
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

        BlockStateKind parseStateKind(const std::string& value)
        {
            if (value == "attach")
            {
                return BlockStateKind::Attach;
            }
            if (value == "attach_grid")
            {
                return BlockStateKind::AttachGrid;
            }
            return BlockStateKind::None;
        }

        std::string displayNameFromKey(const std::string& key)
        {
            std::string text;
            text.reserve(key.size());
            bool upperNext = true;
            for (const char c : key)
            {
                if (c == '_' || c == '-')
                {
                    text.push_back(' ');
                    upperNext = true;
                    continue;
                }
                if (upperNext && c >= 'a' && c <= 'z')
                {
                    text.push_back(static_cast<char>(c - 'a' + 'A'));
                }
                else
                {
                    text.push_back(c);
                }
                upperNext = false;
            }
            return text;
        }

        bool loadRgbaImage(const std::filesystem::path& path, RgbaImage& image)
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_uc* loadedPixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (loadedPixels == nullptr || width <= 0 || height <= 0)
            {
                if (loadedPixels != nullptr)
                {
                    stbi_image_free(loadedPixels);
                }
                return false;
            }

            image.width = width;
            image.height = height;
            const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
            image.pixels.assign(loadedPixels, loadedPixels + byteCount);
            stbi_image_free(loadedPixels);
            return true;
        }

        std::string sanitizedTextureName(const std::string& value)
        {
            std::string result;
            result.reserve(value.size());
            for (const char c : value)
            {
                if ((c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') ||
                    c == '_' ||
                    c == '-')
                {
                    result.push_back(c);
                }
                else
                {
                    result.push_back('_');
                }
            }
            return result;
        }

        std::string generatedBlockTextureName(const data::ParsedBlockTextureDefinition& texture)
        {
            return "generated/" + sanitizedTextureName(texture.base + "__" + texture.mask);
        }

        bool writeMaskedBlockTexture(
            const std::filesystem::path& blockTextureDirectory,
            const data::ParsedBlockTextureDefinition& texture,
            const std::string& generatedTexture)
        {
            if (texture.base.empty() || texture.mask.empty())
            {
                return false;
            }

            RgbaImage base{};
            RgbaImage mask{};
            if (!loadRgbaImage(blockTextureDirectory / (texture.base + ".png"), base) ||
                !loadRgbaImage(blockTextureDirectory / (texture.mask + ".png"), mask) ||
                base.width != mask.width ||
                base.height != mask.height)
            {
                return false;
            }

            RgbaImage output{};
            output.width = base.width;
            output.height = base.height;
            output.pixels = base.pixels;
            const std::size_t pixelCount = static_cast<std::size_t>(base.width) * static_cast<std::size_t>(base.height);
            for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
            {
                const std::size_t index = pixel * 4u;
                const float alpha = static_cast<float>(mask.pixels[index + 3u]) / 255.0f;
                if (alpha <= 0.0f)
                {
                    continue;
                }

                for (std::size_t channel = 0; channel < 3u; ++channel)
                {
                    const float baseColor = static_cast<float>(base.pixels[index + channel]);
                    const float maskColor = static_cast<float>(mask.pixels[index + channel]);
                    output.pixels[index + channel] = static_cast<unsigned char>(
                        std::clamp(baseColor * (1.0f - alpha) + maskColor * alpha, 0.0f, 255.0f));
                }
                output.pixels[index + 3u] = base.pixels[index + 3u];
            }

            const std::filesystem::path outputPath = blockTextureDirectory / (generatedTexture + ".png");
            std::error_code error;
            std::filesystem::create_directories(outputPath.parent_path(), error);
            if (error)
            {
                return false;
            }

            return stbi_write_png(
                outputPath.string().c_str(),
                output.width,
                output.height,
                4,
                output.pixels.data(),
                output.width * 4) != 0;
        }

        std::string resolveBlockTextureName(
            const std::filesystem::path& blockTextureDirectory,
            const data::ParsedBlockTextureDefinition& texture,
            const std::string& context)
        {
            if (!texture.texture.empty())
            {
                return texture.texture;
            }
            if (texture.base.empty())
            {
                return "none";
            }
            if (texture.mask.empty())
            {
                return texture.base;
            }

            const std::string generatedTexture = generatedBlockTextureName(texture);
            if (writeMaskedBlockTexture(blockTextureDirectory, texture, generatedTexture))
            {
                return generatedTexture;
            }

            log::warn("Masked block texture generation failed: " + context + " -> " + texture.base + " + " + texture.mask);
            return texture.base;
        }

        std::string primaryBlockTexture(
            const data::ParsedBlockDefinition& definition,
            const std::filesystem::path& blockTextureDirectory)
        {
            if (const auto it = definition.textures.find("all"); it != definition.textures.end())
            {
                return resolveBlockTextureName(blockTextureDirectory, it->second, definition.name + ".all");
            }
            if (const auto it = definition.textures.find("top"); it != definition.textures.end())
            {
                return resolveBlockTextureName(blockTextureDirectory, it->second, definition.name + ".top");
            }
            if (const auto it = definition.textures.find("side"); it != definition.textures.end())
            {
                return resolveBlockTextureName(blockTextureDirectory, it->second, definition.name + ".side");
            }
            if (const auto it = definition.textures.find("topBottom"); it != definition.textures.end())
            {
                return resolveBlockTextureName(blockTextureDirectory, it->second, definition.name + ".topBottom");
            }
            if (!definition.propTexture.empty())
            {
                return definition.propTexture;
            }
            return "none";
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
            itemDefinition.burnTimeTicks = definition.burnTimeTicks;
            itemDefinition.heatLevel = definition.heatLevel != 0 || definition.burnTimeTicks == 0
                ? definition.heatLevel
                : 1;
            const ItemBlockModelSize blockModelSize = parseItemBlockModelSize(definition.modelShape);
            itemDefinition.blockModelWidth = blockModelSize.width;
            itemDefinition.blockModelHeight = blockModelSize.height;
            itemDefinition.blockModelDepth = blockModelSize.depth;
            itemDefinition.useBlockModelVerticalSection = blockModelSize.useVerticalSection;
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

        const std::vector<char> blockDefinitionData = readContentFile(assetDirectory / "data" / "blocks.json");
        const std::string blockDefinitionText(blockDefinitionData.begin(), blockDefinitionData.end());
        const std::vector<data::ParsedBlockDefinition> parsedBlocks = data::parseBlockDefinitions(blockDefinitionText);

        const std::filesystem::path blockTextureDir = assetDirectory / "textures" / "block";
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
        auto layerForBlockTextureDefinition = [&](
            const data::ParsedBlockTextureDefinition& texture,
            const BlockTextureLayers& layers,
            const std::string& context) -> uint32_t
        {
            if (!texture.texture.empty())
            {
                const int faceIndex = blockTextureFaceIndex(texture.texture);
                if (faceIndex >= 0)
                {
                    return layers.faces[static_cast<std::size_t>(faceIndex)];
                }
            }
            return layerForTexture(resolveBlockTextureName(blockTextureDir, texture, context));
        };

        content.blockDefinitions_.assign(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u, {});
        content.blockTextureLayers_.assign(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u, {});
        std::unordered_map<std::string, std::string> primaryBlockTextureByName;
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
            blockDefinition.breakEffectParticles = definition.breakEffectParticles;
            blockDefinition.stateKind = parseStateKind(definition.stateKind);
            blockDefinition.attachmentFace = parseAttachmentFace(definition.attachmentFace);
            blockDefinition.interactActions = definition.interactActions;
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
            content.blockIdByName_[definition.name] = definition.id;
            primaryBlockTextureByName[definition.name] = primaryBlockTexture(definition, blockTextureDir);

            BlockTextureLayers layers{};
            if (const auto it = definition.textures.find("all"); it != definition.textures.end())
            {
                layers.faces.fill(layerForTexture(resolveBlockTextureName(blockTextureDir, it->second, definition.name + ".all")));
            }
            if (const auto it = definition.textures.find("topBottom"); it != definition.textures.end())
            {
                const uint32_t layer = layerForTexture(resolveBlockTextureName(blockTextureDir, it->second, definition.name + ".topBottom"));
                layers.faces[0] = layer;
                layers.faces[1] = layer;
            }
            if (const auto it = definition.textures.find("side"); it != definition.textures.end())
            {
                const uint32_t layer = layerForTexture(resolveBlockTextureName(blockTextureDir, it->second, definition.name + ".side"));
                layers.faces[2] = layer;
                layers.faces[3] = layer;
                layers.faces[4] = layer;
                layers.faces[5] = layer;
            }
            if (const auto it = definition.textures.find("top"); it != definition.textures.end())
            {
                layers.faces[0] = layerForTexture(resolveBlockTextureName(blockTextureDir, it->second, definition.name + ".top"));
            }
            if (const auto it = definition.textures.find("bottom"); it != definition.textures.end())
            {
                layers.faces[1] = layerForTexture(resolveBlockTextureName(blockTextureDir, it->second, definition.name + ".bottom"));
            }
            if (!definition.propTexture.empty())
            {
                layers.faces.fill(layerForTexture(definition.propTexture));
            }
            layers.horizontalSection = layers.faces[0];
            layers.verticalSection = layers.faces[3];
            if (const auto it = definition.textures.find("horizontalSection"); it != definition.textures.end())
            {
                layers.horizontalSection = layerForBlockTextureDefinition(it->second, layers, definition.name + ".horizontalSection");
            }
            if (const auto it = definition.textures.find("verticalSection"); it != definition.textures.end())
            {
                layers.verticalSection = layerForBlockTextureDefinition(it->second, layers, definition.name + ".verticalSection");
            }
            content.blockTextureLayers_[definition.id] = layers;

            if (definition.renderType == "fire")
            {
                for (int frame = 1; frame < FireAnimationFrameCount; ++frame)
                {
                    layerForTexture("fire/fire_" + std::string(frame < 10 ? "0" : "") + std::to_string(frame));
                }
            }

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

            const auto blockIt = content.blockIdByName_.find(definition.placeBlock);
            if (blockIt == content.blockIdByName_.end())
            {
                log::warn("Item placeBlock references unknown block name: " + definition.key + " -> " + definition.placeBlock);
                continue;
            }
            content.itemDefinitions_[definition.id].placeBlockId = blockIt->second;
        }

        for (const data::ParsedItemDefinition& definition : parsedItems)
        {
            if (definition.id >= content.itemDefinitions_.size())
            {
                continue;
            }

            ItemDefinition& item = content.itemDefinitions_[definition.id];
            if (!definition.modelBlock.empty())
            {
                const auto blockIt = content.blockIdByName_.find(definition.modelBlock);
                if (blockIt == content.blockIdByName_.end())
                {
                    log::warn("Item modelBlock references unknown block name: " + definition.key + " -> " + definition.modelBlock);
                    continue;
                }
                item.modelBlockId = blockIt->second;
            }
            else if (item.placeBlockId != 0)
            {
                item.modelBlockId = item.placeBlockId;
            }
        }

        const std::filesystem::path interactionPath = assetDirectory / "data" / "recipes" / "interactions.json";
        if (std::filesystem::exists(interactionPath))
        {
            const std::vector<char> interactionData = readContentFile(interactionPath);
            const std::string interactionText(interactionData.begin(), interactionData.end());
            const std::vector<data::ParsedInteractionDefinition> parsedInteractions = data::parseInteractionDefinitions(interactionText);
            for (const data::ParsedInteractionDefinition& definition : parsedInteractions)
            {
                ItemInteractionRecipe recipe{};
                recipe.action = definition.action;
                recipe.targetCount = definition.targetCount;

                if (!definition.target.empty())
                {
                    const auto targetIt = content.itemIdByKey_.find(definition.target);
                    if (targetIt == content.itemIdByKey_.end())
                    {
                        log::warn("Interaction references unknown target item key: " + definition.action + " -> " + definition.target);
                        continue;
                    }
                    recipe.targetItemId = targetIt->second;
                }

                if (!definition.targetBlock.empty())
                {
                    if (definition.targetBlock == "*")
                    {
                        recipe.targetAnyBlock = true;
                    }
                    else
                    {
                        const auto blockIt = content.blockIdByName_.find(definition.targetBlock);
                        if (blockIt == content.blockIdByName_.end())
                        {
                            log::warn("Interaction references unknown target block key: " + definition.action + " -> " + definition.targetBlock);
                            continue;
                        }
                        recipe.targetBlockId = blockIt->second;
                    }
                }

                for (const data::ParsedInteractionIngredient& parsedIngredient : definition.ingredients)
                {
                    const auto ingredientIt = content.itemIdByKey_.find(parsedIngredient.item);
                    if (ingredientIt == content.itemIdByKey_.end())
                    {
                        log::warn("Interaction references unknown ingredient item key: " + definition.action + " -> " + parsedIngredient.item);
                        continue;
                    }

                    recipe.ingredients.push_back(ItemInteractionIngredient{
                        ingredientIt->second,
                        parsedIngredient.count
                    });
                }
                for (const data::ParsedInteractionCandidate& parsedCandidate : definition.candidates)
                {
                    ItemInteractionCandidate candidate{};
                    for (const data::ParsedInteractionOutput& parsedOutput : parsedCandidate.outputs)
                    {
                        if (!parsedOutput.item.empty())
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

                        if (!parsedOutput.block.empty())
                        {
                            const auto blockIt = content.blockIdByName_.find(parsedOutput.block);
                            if (blockIt == content.blockIdByName_.end())
                            {
                                log::warn("Interaction references unknown candidate block key: " + definition.action + " -> " + parsedOutput.block);
                                continue;
                            }

                            candidate.placeBlockId = blockIt->second;
                            candidate.placeBlockPlacement = parsedOutput.placement;
                            candidate.displayName = displayNameFromKey(parsedOutput.block);
                            if (const auto textureIt = primaryBlockTextureByName.find(parsedOutput.block); textureIt != primaryBlockTextureByName.end())
                            {
                                candidate.iconTexture = textureIt->second;
                            }
                        }
                    }
                    if (!candidate.outputs.empty() || candidate.placeBlockId != 0)
                    {
                        recipe.candidates.push_back(std::move(candidate));
                    }
                }
                if (!recipe.action.empty() &&
                    (recipe.targetItemId != 0 || recipe.targetBlockId != 0 || recipe.targetAnyBlock) &&
                    !recipe.candidates.empty())
                {
                    content.itemInteractionRecipes_.push_back(std::move(recipe));
                }
            }
        }

        const std::filesystem::path processingPath = assetDirectory / "data" / "recipes" / "processings.json";
        if (std::filesystem::exists(processingPath))
        {
            const std::vector<char> processingData = readContentFile(processingPath);
            const std::string processingText(processingData.begin(), processingData.end());
            const std::vector<data::ParsedProcessingDefinition> parsedProcessings = data::parseProcessingDefinitions(processingText);
            for (const data::ParsedProcessingDefinition& definition : parsedProcessings)
            {
                const auto inputIt = content.itemIdByKey_.find(definition.input);
                if (inputIt == content.itemIdByKey_.end())
                {
                    log::warn("Processing recipe references unknown input item key: " + definition.type + " -> " + definition.input);
                    continue;
                }

                const auto outputIt = content.itemIdByKey_.find(definition.output);
                if (outputIt == content.itemIdByKey_.end())
                {
                    log::warn("Processing recipe references unknown output item key: " + definition.type + " -> " + definition.output);
                    continue;
                }

                ItemProcessingRecipe recipe{};
                recipe.type = definition.type;
                recipe.inputItemId = inputIt->second;
                recipe.outputItemId = outputIt->second;
                recipe.requiredTicks = definition.requiredTicks;
                content.itemProcessingRecipes_.push_back(std::move(recipe));
            }
        }

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
            if (item.modelBlockId == 0 ||
                static_cast<size_t>(item.modelBlockId) >= content.blockTextureLayers_.size())
            {
                log::warn("Block model slot item has no valid modelBlock/placeBlock: " + item.key);
                continue;
            }

            const std::string generatedTexture = "generated/" + item.key + "_slot";
            const std::filesystem::path outputPath = assetDirectory / "textures" / "item" / (generatedTexture + ".png");
            if (assets::writeBlockItemIcon(
                blockTextureDir,
                content.blockTextureNames_,
                content.blockTextureLayers_[item.modelBlockId],
                content.blockDefinitions_[item.modelBlockId].renderType,
                item.blockModelWidth,
                item.blockModelHeight,
                item.blockModelDepth,
                item.useBlockModelVerticalSection,
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
        lightAttenuationTables->blockRenderTypes.assign(content.blockDefinitions_.size(), BlockRenderType::None);
        lightAttenuationTables->blockStateKinds.assign(content.blockDefinitions_.size(), BlockStateKind::None);
        for (size_t i = 0; i < content.blockDefinitions_.size(); ++i)
        {
            lightAttenuationTables->block[i] = content.blockDefinitions_[i].lightAttenuation;
            lightAttenuationTables->blockEmission[i] = content.blockDefinitions_[i].lightEmission;
            lightAttenuationTables->blockRenderTypes[i] = content.blockDefinitions_[i].renderType;
            lightAttenuationTables->blockStateKinds[i] = content.blockDefinitions_[i].stateKind;
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

    const std::unordered_map<std::string, uint16_t>& ClientContent::blockIdByName() const
    {
        return blockIdByName_;
    }

    const std::vector<ItemInteractionRecipe>& ClientContent::itemInteractionRecipes() const
    {
        return itemInteractionRecipes_;
    }

    const std::vector<ItemProcessingRecipe>& ClientContent::itemProcessingRecipes() const
    {
        return itemProcessingRecipes_;
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
