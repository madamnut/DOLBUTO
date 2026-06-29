#include "gameplay/ClientGameplayRuntime.h"

#include "world/BlockData.h"
#include "world/BlockVisualShape.h"
#include "world/DroppedItemSystem.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace dolbuto::gameplay
{
    namespace
    {
        constexpr uint16_t BlockAir = 0;

        bool recipeTargetsBlock(const ItemInteractionRecipe& recipe, uint16_t block)
        {
            return recipe.targetAnyBlock || recipe.targetBlockId == block;
        }

        bool itemHasUseAction(const ItemDefinition& definition, const std::string& action)
        {
            return std::find(definition.useActions.begin(), definition.useActions.end(), action) != definition.useActions.end();
        }

        std::vector<std::string> stackUseActions(const ItemStack& stack, const ItemDefinition& definition)
        {
            return !stack.dynamicUseActions.empty() ? stack.dynamicUseActions : definition.useActions;
        }

        bool containsString(const std::vector<std::string>& values, const std::string& value)
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }

        bool assemblyPartMatches(const AssemblyPartDefinition& condition, const AssemblyPartDefinition& value)
        {
            if (condition.present && !value.present)
            {
                return false;
            }
            if (!condition.part.empty() && condition.part != value.part)
            {
                return false;
            }
            if (!condition.type.empty() && condition.type != value.type)
            {
                return false;
            }
            if (!condition.material.empty() && condition.material != value.material)
            {
                return false;
            }
            if (!condition.size.empty() && condition.size != value.size)
            {
                return false;
            }
            for (const std::string& allowedSize : condition.allowedSizes)
            {
                if (!containsString(value.allowedSizes, allowedSize))
                {
                    return false;
                }
            }
            return true;
        }

        bool interactionInputMatches(
            const ItemInteractionInput& input,
            const ItemStack& stack,
            const std::vector<ItemDefinition>& definitions)
        {
            if (stack.itemId == 0 ||
                stack.count < input.count ||
                static_cast<std::size_t>(stack.itemId) >= definitions.size())
            {
                return false;
            }
            const ItemDefinition& definition = definitions[stack.itemId];
            if (input.itemId != 0 && input.itemId != stack.itemId)
            {
                return false;
            }
            if (input.assemblyPart.present && !assemblyPartMatches(input.assemblyPart, definition.assemblyPart))
            {
                return false;
            }
            return true;
        }

        const ItemInteractionBoundInput* boundInputByAlias(
            const std::vector<ItemInteractionBoundInput>& inputs,
            const std::string& alias)
        {
            const auto it = std::find_if(
                inputs.begin(),
                inputs.end(),
                [&](const ItemInteractionBoundInput& input)
                {
                    return input.alias == alias;
                });
            return it == inputs.end() ? nullptr : &*it;
        }

        std::string assemblyPathValue(
            const std::vector<ItemInteractionBoundInput>& inputs,
            const std::vector<ItemDefinition>& definitions,
            const std::string& path)
        {
            const std::size_t dot = path.find('.');
            if (dot == std::string::npos)
            {
                return {};
            }
            const ItemInteractionBoundInput* input = boundInputByAlias(inputs, path.substr(0, dot));
            if (input == nullptr ||
                input->stack.itemId == 0 ||
                static_cast<std::size_t>(input->stack.itemId) >= definitions.size())
            {
                return {};
            }
            const ItemDefinition& definition = definitions[input->stack.itemId];
            const std::string suffix = path.substr(dot + 1u);
            if (suffix == "key")
            {
                return definition.key;
            }
            if (suffix == "components.assemblyPart.part")
            {
                return definition.assemblyPart.part;
            }
            if (suffix == "components.assemblyPart.type")
            {
                return definition.assemblyPart.type;
            }
            if (suffix == "components.assemblyPart.material")
            {
                return definition.assemblyPart.material;
            }
            if (suffix == "components.assemblyPart.size")
            {
                return definition.assemblyPart.size;
            }
            return {};
        }

        const std::vector<std::string>* assemblyPathArray(
            const std::vector<ItemInteractionBoundInput>& inputs,
            const std::vector<ItemDefinition>& definitions,
            const std::string& path)
        {
            const std::size_t dot = path.find('.');
            if (dot == std::string::npos)
            {
                return nullptr;
            }
            const ItemInteractionBoundInput* input = boundInputByAlias(inputs, path.substr(0, dot));
            if (input == nullptr ||
                input->stack.itemId == 0 ||
                static_cast<std::size_t>(input->stack.itemId) >= definitions.size())
            {
                return nullptr;
            }
            const ItemDefinition& definition = definitions[input->stack.itemId];
            return path.substr(dot + 1u) == "components.assemblyPart.allowedSizes"
                ? &definition.assemblyPart.allowedSizes
                : nullptr;
        }

        bool interactionConstraintsMatch(
            const std::vector<ItemInteractionConstraint>& constraints,
            const std::vector<ItemInteractionBoundInput>& inputs,
            const std::vector<ItemDefinition>& definitions)
        {
            for (const ItemInteractionConstraint& constraint : constraints)
            {
                if (constraint.op == "==")
                {
                    if (assemblyPathValue(inputs, definitions, constraint.left) !=
                        assemblyPathValue(inputs, definitions, constraint.right))
                    {
                        return false;
                    }
                    continue;
                }
                if (constraint.op == "in")
                {
                    const std::string value = assemblyPathValue(inputs, definitions, constraint.left);
                    const std::vector<std::string>* values = assemblyPathArray(inputs, definitions, constraint.right);
                    if (values == nullptr || !containsString(*values, value))
                    {
                        return false;
                    }
                    continue;
                }
                return false;
            }
            return true;
        }

        std::string toolNameForHead(std::string_view headType, std::string_view size)
        {
            if (headType == "blade" && size == "short")
            {
                return "Knife";
            }
            if (headType == "point" && size == "short")
            {
                return "Awl";
            }
            if (headType == "point" && size == "default")
            {
                return "Javelin";
            }
            if (headType == "point" && size == "long")
            {
                return "Spear";
            }
            if (headType == "chopper" && size == "default")
            {
                return "Axe";
            }
            if (headType == "pestle" && size == "short")
            {
                return "Hammer";
            }
            if (headType == "maul" && size == "default")
            {
                return "Sledge";
            }
            return {};
        }

        uint16_t effectiveMaxDurability(const ItemStack& stack, const ItemDefinition& definition)
        {
            return stack.dynamicMaxDurability != 0 ? stack.dynamicMaxDurability : definition.maxDurability;
        }

        std::string moldFormFromBlockName(const std::string& name)
        {
            static constexpr std::array<std::string_view, 9> Forms{
                "small_plate",
                "plate",
                "large_plate",
                "small_preform",
                "preform",
                "large_preform",
                "short_rod",
                "rod",
                "long_rod"
            };
            for (std::string_view form : Forms)
            {
                const std::string suffix = std::string(form) + "_mold";
                if (name == suffix)
                {
                    return std::string(form);
                }
            }
            return {};
        }

        uint16_t moldRequiredAmount(std::string_view form)
        {
            if (form == "small_plate" || form == "small_preform")
            {
                return 10;
            }
            if (form == "plate" || form == "preform")
            {
                return 20;
            }
            if (form == "large_plate" || form == "large_preform")
            {
                return 30;
            }
            if (form == "short_rod")
            {
                return 5;
            }
            if (form == "rod")
            {
                return 10;
            }
            if (form == "long_rod")
            {
                return 15;
            }
            return 0;
        }

        std::string metalKeyFromMoltenFluid(uint16_t fluidId)
        {
            switch (fluidId)
            {
            case 1000: return "tin";
            case 1001: return "zinc";
            case 1002: return "silver";
            case 1003: return "gold";
            case 1004: return "copper";
            case 1005: return "iron";
            default: return {};
            }
        }

        uint16_t itemIdByKey(const std::vector<ItemDefinition>& definitions, const std::string& key)
        {
            for (size_t i = 0; i < definitions.size(); ++i)
            {
                if (definitions[i].key == key)
                {
                    return static_cast<uint16_t>(i);
                }
            }
            return 0;
        }

        uint16_t castPartItemId(
            const std::vector<ItemDefinition>& definitions,
            uint16_t moltenFluidId,
            std::string_view form)
        {
            const std::string metal = metalKeyFromMoltenFluid(moltenFluidId);
            if (metal.empty() || form.empty())
            {
                return 0;
            }
            return itemIdByKey(definitions, metal + "_" + std::string(form));
        }

        uint16_t attachStateForPlacement(const BlockRaycastHit& hit)
        {
            const int dx = hit.previousBlockX - hit.blockX;
            const int dy = hit.previousBlockY - hit.blockY;
            const int dz = hit.previousBlockZ - hit.blockZ;
            if (dy > 0)
            {
                return static_cast<uint16_t>(BlockAttachState::Bottom);
            }
            if (dy < 0)
            {
                return static_cast<uint16_t>(BlockAttachState::Top);
            }
            if (dx > 0)
            {
                return static_cast<uint16_t>(BlockAttachState::West);
            }
            if (dx < 0)
            {
                return static_cast<uint16_t>(BlockAttachState::East);
            }
            if (dz > 0)
            {
                return static_cast<uint16_t>(BlockAttachState::North);
            }
            if (dz < 0)
            {
                return static_cast<uint16_t>(BlockAttachState::South);
            }
            return static_cast<uint16_t>(BlockAttachState::Bottom);
        }

        struct PlacementAxis
        {
            int x = 0;
            int y = 0;
            int z = 0;
        };

        struct SlabPlacementFrame
        {
            PlacementAxis uAxis{};
            PlacementAxis vAxis{};
            double u = 0.5;
            double v = 0.5;
        };

        double axisDot(Vec3 direction, PlacementAxis axis)
        {
            return static_cast<double>(direction.x) * static_cast<double>(axis.x) +
                static_cast<double>(direction.y) * static_cast<double>(axis.y) +
                static_cast<double>(direction.z) * static_cast<double>(axis.z);
        }

        PlacementAxis negated(PlacementAxis axis)
        {
            return PlacementAxis{-axis.x, -axis.y, -axis.z};
        }

        uint16_t attachStateForDirection(PlacementAxis axis)
        {
            if (axis.y > 0)
            {
                return static_cast<uint16_t>(BlockAttachState::Top);
            }
            if (axis.y < 0)
            {
                return static_cast<uint16_t>(BlockAttachState::Bottom);
            }
            if (axis.x > 0)
            {
                return static_cast<uint16_t>(BlockAttachState::East);
            }
            if (axis.x < 0)
            {
                return static_cast<uint16_t>(BlockAttachState::West);
            }
            if (axis.z > 0)
            {
                return static_cast<uint16_t>(BlockAttachState::South);
            }
            if (axis.z < 0)
            {
                return static_cast<uint16_t>(BlockAttachState::North);
            }
            return static_cast<uint16_t>(BlockAttachState::Bottom);
        }

        SlabPlacementFrame slabPlacementFrame(const BlockRaycastHit& hit)
        {
            const double localX = std::clamp(hit.hitPosition.x - (static_cast<double>(hit.blockX) - 0.5), 0.0, 1.0);
            const double localY = std::clamp(hit.hitPosition.y - static_cast<double>(hit.blockY), 0.0, 1.0);
            const double localZ = std::clamp(hit.hitPosition.z - (static_cast<double>(hit.blockZ) - 0.5), 0.0, 1.0);
            const int dx = hit.previousBlockX - hit.blockX;
            const int dy = hit.previousBlockY - hit.blockY;
            const int dz = hit.previousBlockZ - hit.blockZ;

            if (dy > 0)
            {
                return SlabPlacementFrame{PlacementAxis{1, 0, 0}, PlacementAxis{0, 0, -1}, localX, 1.0 - localZ};
            }
            if (dy < 0)
            {
                return SlabPlacementFrame{PlacementAxis{1, 0, 0}, PlacementAxis{0, 0, 1}, localX, localZ};
            }
            if (dx > 0)
            {
                return SlabPlacementFrame{PlacementAxis{0, 0, 1}, PlacementAxis{0, 1, 0}, localZ, localY};
            }
            if (dx < 0)
            {
                return SlabPlacementFrame{PlacementAxis{0, 0, -1}, PlacementAxis{0, 1, 0}, 1.0 - localZ, localY};
            }
            if (dz > 0)
            {
                return SlabPlacementFrame{PlacementAxis{-1, 0, 0}, PlacementAxis{0, 1, 0}, 1.0 - localX, localY};
            }
            if (dz < 0)
            {
                return SlabPlacementFrame{PlacementAxis{1, 0, 0}, PlacementAxis{0, 1, 0}, localX, localY};
            }
            return SlabPlacementFrame{};
        }

        int slabGridCommand(double u, double v)
        {
            const int col = std::clamp(static_cast<int>(std::floor(std::clamp(u, 0.0, 1.0) * 3.0)), 0, 2);
            const int row = std::clamp(static_cast<int>(std::floor(std::clamp(v, 0.0, 1.0) * 3.0)), 0, 2);
            return row * 3 + col + 1;
        }

        int resolveSlabCornerCommand(int command, const SlabPlacementFrame& frame, Vec3 viewDirection)
        {
            const bool useUAxis = std::abs(axisDot(viewDirection, frame.uAxis)) >= std::abs(axisDot(viewDirection, frame.vAxis));
            switch (command)
            {
            case 1: return useUAxis ? 4 : 2;
            case 3: return useUAxis ? 6 : 2;
            case 7: return useUAxis ? 4 : 8;
            case 9: return useUAxis ? 6 : 8;
            default: return command;
            }
        }

        uint16_t slabPlacementState(const BlockRaycastHit& hit, Vec3 viewDirection)
        {
            const SlabPlacementFrame frame = slabPlacementFrame(hit);
            switch (resolveSlabCornerCommand(slabGridCommand(frame.u, frame.v), frame, viewDirection))
            {
            case 2: return attachStateForDirection(negated(frame.vAxis));
            case 4: return attachStateForDirection(negated(frame.uAxis));
            case 6: return attachStateForDirection(frame.uAxis);
            case 8: return attachStateForDirection(frame.vAxis);
            case 5:
            default:
                return attachStateForPlacement(hit);
            }
        }

        BlockAttachState attachFaceForPlacement(const BlockRaycastHit& hit)
        {
            return static_cast<BlockAttachState>(attachStateForPlacement(hit));
        }

        int resolveHalfSlabGridCommand(int command, const SlabPlacementFrame& frame, Vec3 viewDirection)
        {
            if (command != 5)
            {
                return command;
            }

            const double u = axisDot(viewDirection, frame.uAxis);
            const double v = axisDot(viewDirection, frame.vAxis);
            if (std::abs(u) >= std::abs(v))
            {
                return u >= 0.0 ? 6 : 4;
            }
            return v >= 0.0 ? 8 : 2;
        }

        uint16_t halfSlabPlacementState(const BlockRaycastHit& hit, Vec3 viewDirection)
        {
            const SlabPlacementFrame frame = slabPlacementFrame(hit);
            return world::block_visual::attachGridState(
                attachFaceForPlacement(hit),
                resolveHalfSlabGridCommand(slabGridCommand(frame.u, frame.v), frame, viewDirection));
        }

        uint16_t placementStateForBlock(const BlockDefinition& definition, const BlockRaycastHit& hit, Vec3 viewDirection)
        {
            if (definition.renderType == BlockRenderType::Slab && definition.stateKind == BlockStateKind::Attach)
            {
                return slabPlacementState(hit, viewDirection);
            }
            if (definition.renderType == BlockRenderType::HalfSlab && definition.stateKind == BlockStateKind::AttachGrid)
            {
                return halfSlabPlacementState(hit, viewDirection);
            }
            return 0;
        }
    }

    ClientGameplayRuntime::ClientGameplayRuntime(
        world::WorldRuntime* worldRuntime,
        const std::vector<ItemDefinition>* itemDefinitions,
        const std::unordered_map<std::string, AssemblyMaterialDefinition>* assemblyMaterials,
        const std::unordered_map<std::string, uint32_t>* itemTextureLayerByName)
        : itemDefinitions_(itemDefinitions),
        assemblyMaterials_(assemblyMaterials),
        itemTextureLayerByName_(itemTextureLayerByName),
        worldRuntime_(worldRuntime),
        droppedItemRuntime_(worldRuntime, itemDefinitions)
    {
    }

    void ClientGameplayRuntime::setContext(
        world::WorldRuntime* worldRuntime,
        const std::vector<ItemDefinition>* itemDefinitions,
        const std::unordered_map<std::string, AssemblyMaterialDefinition>* assemblyMaterials,
        const std::unordered_map<std::string, uint32_t>* itemTextureLayerByName)
    {
        itemDefinitions_ = itemDefinitions;
        assemblyMaterials_ = assemblyMaterials;
        itemTextureLayerByName_ = itemTextureLayerByName;
        worldRuntime_ = worldRuntime;
        droppedItemRuntime_.setContext(worldRuntime, itemDefinitions);
    }

    const std::vector<ItemDefinition>& ClientGameplayRuntime::itemDefinitions() const
    {
        if (itemDefinitions_ == nullptr)
        {
            throw std::runtime_error("ClientGameplayRuntime item definitions are not initialized.");
        }
        return *itemDefinitions_;
    }

    bool ClientGameplayRuntime::playerColliderIntersectsTerrain(
        DVec3 playerPosition,
        double heightScale,
        const TerrainAabbCollisionPredicate& terrainCellIntersectsPlayer) const
    {
        return BlockInteractionSystem::playerColliderIntersectsTerrain(playerPosition, heightScale, terrainCellIntersectsPlayer);
    }

    bool ClientGameplayRuntime::playerColliderHasSupportBelow(
        DVec3 playerPosition,
        const TerrainAabbCollisionPredicate& terrainCellIntersectsPlayer) const
    {
        return BlockInteractionSystem::playerColliderHasSupportBelow(playerPosition, terrainCellIntersectsPlayer);
    }

    bool ClientGameplayRuntime::playerColliderIntersectsWater(
        DVec3 playerPosition,
        double heightScale,
        const FluidSampler& fluidAtWorld) const
    {
        return BlockInteractionSystem::playerColliderIntersectsWater(playerPosition, heightScale, fluidAtWorld);
    }

    BlockEditResult ClientGameplayRuntime::editBlockInView(
        DVec3 origin,
        Vec3 direction,
        bool placeBlock,
        uint16_t placeBlockId,
        DVec3 playerPosition,
        double playerHeightScale,
        const BlockSampler& blockAtWorld,
        const BlockDefinitionProvider& blockDefinition,
        const BlockInteractionSystem::PropMeshProvider& propMesh,
        const SetBlockFn& setBlockAtWorld,
        const MarkDirtyFn& markDirty)
    {
        BlockRaycastHit hit{};
        if (!BlockInteractionSystem::raycastBlock(
                origin,
                direction,
                blockAtWorld,
                blockDefinition,
                hit,
                propMesh,
                [this](int x, int y, int z)
                {
                    return worldRuntime_ != nullptr ? worldRuntime_->blockStateAtWorld(x, y, z) : 0;
                }))
        {
            return {};
        }

        if (!placeBlock)
        {
            return breakBlockAtHit(hit, 0, blockAtWorld, blockDefinition, setBlockAtWorld, markDirty);
        }

        const BlockDefinition& placedDefinition = blockDefinition(placeBlockId);
        const uint16_t placementState = placementStateForBlock(placedDefinition, hit, direction);
        if (placedDefinition.attachmentFace == BlockAttachmentFace::Bottom)
        {
            const uint16_t support = blockAtWorld ? blockAtWorld(hit.previousBlockX, hit.previousBlockY - 1, hit.previousBlockZ) : BlockAir;
            if (support == BlockAir || !blockDefinition(support).collision)
            {
                return {};
            }
        }
        if (BlockInteractionSystem::blockIntersectsPlayerCollider(
                hit.previousBlockX,
                hit.previousBlockY,
                hit.previousBlockZ,
                placedDefinition,
                playerPosition,
                playerHeightScale,
                placementState,
                propMesh ? propMesh(placeBlockId) : nullptr))
        {
            return {};
        }

        if (!setBlockAtWorld || !setBlockAtWorld(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ, placeBlockId))
        {
            return {};
        }
        if (placementState != 0 && worldRuntime_ != nullptr)
        {
            worldRuntime_->setBlockStateAtWorld(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ, placementState);
        }

        BlockEditResult result{};
        result.changed = true;
        result.type = BlockEditType::Place;
        result.hit = hit;
        result.block = placeBlockId;
        return result;
    }

    BlockEditResult ClientGameplayRuntime::breakBlockAtHit(
        const BlockRaycastHit& hit,
        uint16_t durabilityCost,
        const BlockSampler& blockAtWorld,
        const BlockDefinitionProvider& blockDefinition,
        const SetBlockFn& setBlockAtWorld,
        const MarkDirtyFn& markDirty)
    {
        const uint16_t destroyedBlock = blockAtWorld ? blockAtWorld(hit.blockX, hit.blockY, hit.blockZ) : BlockAir;
        if (destroyedBlock == BlockAir || blockDefinition(destroyedBlock).hardness < 0.0f)
        {
            return {};
        }

        if (!setBlockAtWorld || !setBlockAtWorld(hit.blockX, hit.blockY, hit.blockZ, BlockAir))
        {
            return {};
        }

        droppedItemRuntime_.spawnBlockDrops(
            hit.blockX,
            hit.blockY,
            hit.blockZ,
            blockDefinition(destroyedBlock),
            markDirty);

        BlockEditResult result{};
        result.changed = true;
        result.type = BlockEditType::Break;
        result.hit = hit;
        result.block = destroyedBlock;
        result.inventoryChanged = damageSelectedHotbarItem(durabilityCost);
        return result;
    }

    BlockBreakingUpdate ClientGameplayRuntime::updateBlockBreaking(
        DVec3 origin,
        Vec3 direction,
        bool breaking,
        float deltaSeconds,
        bool sandboxMode,
        const BlockSampler& blockAtWorld,
        const BlockDefinitionProvider& blockDefinition,
        const BlockInteractionSystem::PropMeshProvider& propMesh)
    {
        return BlockInteractionSystem::updateBreaking(
            blockBreaking_,
            origin,
            direction,
            breaking,
            deltaSeconds,
            sandboxMode,
            currentBlockBreakTool(),
            blockAtWorld,
            blockDefinition,
            propMesh,
            [this](int x, int y, int z)
            {
                return worldRuntime_ != nullptr ? worldRuntime_->blockStateAtWorld(x, y, z) : 0;
            });
    }

    BlockTickResult ClientGameplayRuntime::tickBlockUpdates(
        uint32_t maxCells,
        const BlockDefinitionProvider& blockDefinition,
        const std::vector<ItemProcessingRecipe>& processingRecipes,
        const SetBlockFn& setBlockAtWorld,
        const MarkDirtyFn& markDirty)
    {
        BlockTickResult result{};
        if (worldRuntime_ == nullptr || !setBlockAtWorld)
        {
            return result;
        }

        auto markBlockEntityDirty = [&](int x, int z)
        {
            const int chunkX = world::WorldRuntime::floorDiv(x, world::WorldRuntime::ChunkSizeX);
            const int chunkZ = world::WorldRuntime::floorDiv(z, world::WorldRuntime::ChunkSizeZ);
            RuntimeChunk* chunk = worldRuntime_->findChunk(chunkX, chunkZ);
            if (chunk != nullptr && markDirty)
            {
                markDirty(*chunk);
            }
        };
        auto blockSealsFire = [&](int x, int y, int z)
        {
            const uint16_t block = worldRuntime_->blockAtWorld(x, y, z);
            return block != BlockAir && blockDefinition(block).collision;
        };
        struct FireWorkVolume
        {
            std::vector<std::array<int, 3>> processingCells;
            uint32_t leakCount = 0;
        };
        auto fireWorkVolume = [&](int x, int y, int z) -> FireWorkVolume
        {
            constexpr int Directions[6][3] = {
                {1, 0, 0},
                {-1, 0, 0},
                {0, 1, 0},
                {0, -1, 0},
                {0, 0, 1},
                {0, 0, -1}
            };
            constexpr int WatchRadiusXZ = 2;
            constexpr int WatchRadiusY = 1;
            bool leakDirections[6] = {};
            auto insideWatchBounds = [&](int cellX, int cellY, int cellZ)
            {
                return std::abs(cellX - x) <= WatchRadiusXZ &&
                    std::abs(cellY - y) <= WatchRadiusY &&
                    std::abs(cellZ - z) <= WatchRadiusXZ;
            };
            auto markBoundaryLeak = [&](int cellX, int cellY, int cellZ)
            {
                bool boundary = false;
                if (cellX == x + WatchRadiusXZ)
                {
                    leakDirections[0] = true;
                    boundary = true;
                }
                if (cellX == x - WatchRadiusXZ)
                {
                    leakDirections[1] = true;
                    boundary = true;
                }
                if (cellY == y + WatchRadiusY)
                {
                    leakDirections[2] = true;
                    boundary = true;
                }
                if (cellY == y - WatchRadiusY)
                {
                    leakDirections[3] = true;
                    boundary = true;
                }
                if (cellZ == z + WatchRadiusXZ)
                {
                    leakDirections[4] = true;
                    boundary = true;
                }
                if (cellZ == z - WatchRadiusXZ)
                {
                    leakDirections[5] = true;
                    boundary = true;
                }
                return boundary;
            };
            auto insideHorizontalWorkArea = [&](int cellX, int cellY, int cellZ)
            {
                return cellY == y &&
                    std::abs(cellX - x) <= 1 &&
                    std::abs(cellZ - z) <= 1;
            };
            auto containsCell = [](const std::vector<std::array<int, 3>>& cells, int cellX, int cellY, int cellZ)
            {
                return std::find_if(cells.begin(), cells.end(), [&](const std::array<int, 3>& cell)
                {
                    return cell[0] == cellX && cell[1] == cellY && cell[2] == cellZ;
                }) != cells.end();
            };

            std::vector<std::array<int, 3>> visited;
            visited.push_back({x, y, z});
            for (std::size_t index = 0; index < visited.size(); ++index)
            {
                const std::array<int, 3> current = visited[index];
                for (const auto& direction : Directions)
                {
                    const int nextX = current[0] + direction[0];
                    const int nextY = current[1] + direction[1];
                    const int nextZ = current[2] + direction[2];
                    if (!insideWatchBounds(nextX, nextY, nextZ))
                    {
                        continue;
                    }
                    if (blockSealsFire(nextX, nextY, nextZ))
                    {
                        continue;
                    }
                    if (markBoundaryLeak(nextX, nextY, nextZ))
                    {
                        continue;
                    }
                    if (!containsCell(visited, nextX, nextY, nextZ))
                    {
                        visited.push_back({nextX, nextY, nextZ});
                    }
                }
            }

            FireWorkVolume volume{};
            for (const bool leakDirection : leakDirections)
            {
                if (leakDirection)
                {
                    ++volume.leakCount;
                }
            }
            for (const std::array<int, 3>& cell : visited)
            {
                if ((cell[0] == x && cell[1] == y && cell[2] == z) ||
                    !insideHorizontalWorkArea(cell[0], cell[1], cell[2]))
                {
                    continue;
                }
                volume.processingCells.push_back(cell);
            }
            return volume;
        };
        auto fireSmokeMultiplier = [](FireMode mode)
        {
            return mode == FireMode::Pyrolysis ? 3.0f : 1.0f;
        };
        auto fireSmokeTextureSet = [](FireMode mode)
        {
            switch (mode)
            {
            case FireMode::Pyrolysis:
                return 1u;
            case FireMode::Firing:
                return 2u;
            default:
                return 0u;
            }
        };
        auto spawnFireOutput = [&](int x, int y, int z, ItemStack stack)
        {
            if (stack.itemId == 0 || stack.count == 0)
            {
                return false;
            }

            WorldEntity output{};
            output.entityId = droppedItemRuntime_.allocateEntityId();
            output.type = WorldEntityType::DroppedItem;
            output.position = Vec3{
                static_cast<float>(x),
                static_cast<float>(y) + 0.08f,
                static_cast<float>(z)
            };
            output.previousPosition = output.position;
            output.velocity = Vec3{0.0f, 2.0f, 0.0f};
            output.droppedItem.stack = stack;
            output.renderSpinX = 5.0f;
            output.renderSpin = 5.0f;
            output.renderSpinZ = 5.0f;
            world::DroppedItemSystem::setGrounded(output, false);
            return droppedItemRuntime_.addWorldEntity(std::move(output), markDirty);
        };
        auto fireModeForOcclusionAndHeat = [](uint32_t leakCount, uint16_t heatLevel)
        {
            if (leakCount == 0)
            {
                return FireMode::Pyrolysis;
            }
            if (leakCount == 1 && heatLevel >= 3)
            {
                return FireMode::Firing;
            }
            return FireMode::Exposed;
        };
        auto fireModeForFuel = [&](int x, int y, int z, uint16_t heatLevel)
        {
            const FireWorkVolume volume = fireWorkVolume(x, y, z);
            return fireModeForOcclusionAndHeat(volume.leakCount, heatLevel);
        };
        auto setFireMode = [&](int x, int y, int z, BlockEntity& entity, FireMode nextMode, bool forceNotify)
        {
            if (entity.fireMode == nextMode && !forceNotify)
            {
                return;
            }

            const FireMode previousMode = entity.fireMode;
            entity.fireMode = nextMode;
            if (previousMode != nextMode)
            {
                markBlockEntityDirty(x, z);
            }
            result.fireSmokeRateUpdates.push_back(FireSmokeRateUpdate{
                x,
                y,
                z,
                fireSmokeMultiplier(nextMode),
                fireSmokeTextureSet(nextMode)
            });
        };
        auto breakTickBlock = [&](const world::WorldRuntime::BlockTickCell& cell, uint16_t block, const BlockDefinition& definition, bool playSound)
        {
            if (!setBlockAtWorld(cell.x, cell.y, cell.z, BlockAir))
            {
                return false;
            }

            droppedItemRuntime_.spawnBlockDrops(cell.x, cell.y, cell.z, definition, markDirty);
            result.brokenBlocks.push_back(BlockBreakEvent{
                cell.x,
                cell.y,
                cell.z,
                block,
                playSound
            });
            return true;
        };
        auto leafHasDecaySupport = [&](int x, int y, int z)
        {
            constexpr int LeafDecayDepth = 4;
            constexpr int Directions[6][3] = {
                {1, 0, 0},
                {-1, 0, 0},
                {0, 1, 0},
                {0, -1, 0},
                {0, 0, 1},
                {0, 0, -1}
            };
            struct LeafSearchNode
            {
                int x = 0;
                int y = 0;
                int z = 0;
                int depth = 0;
            };

            std::vector<LeafSearchNode> frontier;
            std::vector<LeafSearchNode> visited;
            frontier.push_back(LeafSearchNode{x, y, z, 0});
            visited.push_back(LeafSearchNode{x, y, z, 0});
            for (std::size_t index = 0; index < frontier.size(); ++index)
            {
                const LeafSearchNode current = frontier[index];
                if (current.depth >= LeafDecayDepth)
                {
                    continue;
                }

                for (const auto& direction : Directions)
                {
                    const int nextX = current.x + direction[0];
                    const int nextY = current.y + direction[1];
                    const int nextZ = current.z + direction[2];
                    const uint16_t neighbor = worldRuntime_->blockAtWorld(nextX, nextY, nextZ);
                    const BlockDefinition& neighborDefinition = blockDefinition(neighbor);
                    if (neighborDefinition.leafDecaySupport)
                    {
                        return true;
                    }
                    if (!neighborDefinition.leafDecayable)
                    {
                        continue;
                    }

                    const bool alreadyVisited = std::find_if(visited.begin(), visited.end(), [&](const LeafSearchNode& node)
                    {
                        return node.x == nextX && node.y == nextY && node.z == nextZ;
                    }) != visited.end();
                    if (alreadyVisited)
                    {
                        continue;
                    }

                    const LeafSearchNode next{nextX, nextY, nextZ, current.depth + 1};
                    frontier.push_back(next);
                    visited.push_back(next);
                }
            }
            return false;
        };

        auto updateFireMode = [&](int x, int y, int z, BlockEntity& entity, bool forceNotify)
        {
            const FireWorkVolume volume = fireWorkVolume(x, y, z);
            const FireMode nextMode = fireModeForOcclusionAndHeat(volume.leakCount, entity.fireHeatLevel);
            if (entity.fireMode == nextMode)
            {
                if (forceNotify)
                {
                    result.fireSmokeRateUpdates.push_back(FireSmokeRateUpdate{
                        x,
                        y,
                        z,
                        fireSmokeMultiplier(entity.fireMode),
                        fireSmokeTextureSet(entity.fireMode)
                    });
                }
                return;
            }

            setFireMode(x, y, z, entity, nextMode, forceNotify);
        };

        auto heatInputAt = [&](int x, int y, int z) -> uint16_t
        {
            const uint16_t below = worldRuntime_->blockAtWorld(x, y - 1, z);
            if (below == BlockAir || blockDefinition(below).renderType != BlockRenderType::Fire)
            {
                return 0;
            }
            const BlockEntity* fire = worldRuntime_->blockEntityAtWorld(x, y - 1, z);
            if (fire == nullptr || fire->type != BlockEntityType::Fire || fire->remainingBurnTicks == 0)
            {
                return 0;
            }
            return fire->fireHeatLevel;
        };

        auto processCrucible = [&](const world::WorldRuntime::BlockTickCell& cell, uint16_t block)
        {
            BlockEntity* entity = worldRuntime_->blockEntityAtWorld(cell.x, cell.y, cell.z);
            if (entity == nullptr || entity->type != BlockEntityType::Crucible)
            {
                entity = worldRuntime_->ensureCrucibleBlockEntityAtWorld(cell.x, cell.y, cell.z);
            }
            if (entity == nullptr || entity->type != BlockEntityType::Crucible)
            {
                return;
            }

            constexpr uint16_t CrucibleCapacity = 100;
            constexpr uint32_t SmeltElapsedTicks = 1;
            const uint16_t heatLevel = heatInputAt(cell.x, cell.y, cell.z);
            if (heatLevel != 0 && entity->moltenAmount < CrucibleCapacity)
            {
                const world::DroppedItemRuntime::SmeltProcessingResult smelt = droppedItemRuntime_.processCrucibleSmeltInAabb(
                    static_cast<float>(cell.x) - 0.32f,
                    static_cast<float>(cell.y) + 0.16f,
                    static_cast<float>(cell.z) - 0.32f,
                    static_cast<float>(cell.x) + 0.32f,
                    static_cast<float>(cell.y) + 0.95f,
                    static_cast<float>(cell.z) + 0.32f,
                    processingRecipes,
                    heatLevel,
                    entity->moltenFluidId,
                    entity->moltenAmount,
                    CrucibleCapacity,
                    SmeltElapsedTicks,
                    markDirty);
                if (smelt.completed &&
                    smelt.outputFluidId != 0 &&
                    smelt.outputAmount != 0 &&
                    entity->moltenAmount <= CrucibleCapacity &&
                    smelt.outputAmount <= CrucibleCapacity - entity->moltenAmount &&
                    (entity->moltenFluidId == 0 || entity->moltenFluidId == smelt.outputFluidId))
                {
                    entity->moltenFluidId = smelt.outputFluidId;
                    entity->moltenAmount = static_cast<uint16_t>(entity->moltenAmount + smelt.outputAmount);
                    markBlockEntityDirty(cell.x, cell.z);
                }
            }

            worldRuntime_->scheduleBlockTickAtWorld(
                cell.x,
                cell.y,
                cell.z,
                world::WorldRuntime::BlockTickReasonSelfBlockChanged);
            (void)block;
        };

        auto processMold = [&](const world::WorldRuntime::BlockTickCell& cell, uint16_t block)
        {
            BlockEntity* entity = worldRuntime_->blockEntityAtWorld(cell.x, cell.y, cell.z);
            if (entity == nullptr || entity->type != BlockEntityType::Mold)
            {
                entity = worldRuntime_->ensureMoldBlockEntityAtWorld(cell.x, cell.y, cell.z);
            }
            if (entity == nullptr || entity->type != BlockEntityType::Mold)
            {
                return;
            }

            const std::string form = moldFormFromBlockName(blockDefinition(block).name);
            const uint16_t requiredAmount = moldRequiredAmount(form);
            if (requiredAmount == 0 || entity->moltenFluidId == 0 || entity->moltenAmount == 0)
            {
                return;
            }

            if (entity->moltenAmount < requiredAmount)
            {
                return;
            }

            constexpr uint16_t CoolingTicks = 200;
            if (entity->coolingTicks < CoolingTicks)
            {
                ++entity->coolingTicks;
                markBlockEntityDirty(cell.x, cell.z);
                worldRuntime_->scheduleBlockTickAtWorld(
                    cell.x,
                    cell.y,
                    cell.z,
                    world::WorldRuntime::BlockTickReasonSelfBlockChanged);
                return;
            }

            const uint16_t resultItemId = castPartItemId(itemDefinitions(), entity->moltenFluidId, form);
            if (resultItemId == 0)
            {
                return;
            }

            ItemStack output{};
            output.itemId = resultItemId;
            output.count = 1;
            if (spawnFireOutput(cell.x, cell.y + 1, cell.z, output))
            {
                entity = worldRuntime_->blockEntityAtWorld(cell.x, cell.y, cell.z);
                if (entity != nullptr && entity->type == BlockEntityType::Mold)
                {
                    entity->moltenFluidId = 0;
                    entity->moltenAmount = 0;
                    entity->coolingTicks = 0;
                    markBlockEntityDirty(cell.x, cell.z);
                }
            }
        };

        auto processFireBurn = [&](const world::WorldRuntime::BlockTickCell& cell, uint16_t block)
        {
            BlockEntity* entity = worldRuntime_->blockEntityAtWorld(cell.x, cell.y, cell.z);
            if (entity == nullptr || entity->type != BlockEntityType::Fire)
            {
                entity = worldRuntime_->ensureFireBlockEntityAtWorld(cell.x, cell.y, cell.z, 0);
            }
            if (entity == nullptr || entity->type != BlockEntityType::Fire)
            {
                return;
            }

            const bool modeEvent = (cell.reasons &
                (world::WorldRuntime::BlockTickReasonSelfBlockChanged | world::WorldRuntime::BlockTickReasonBlockNeighborChanged)) != 0;
            if (modeEvent)
            {
                updateFireMode(cell.x, cell.y, cell.z, *entity, true);
            }

            if ((cell.reasons & world::WorldRuntime::BlockTickReasonFireBurn) == 0)
            {
                return;
            }

            if (entity->remainingBurnTicks > 0)
            {
                --entity->remainingBurnTicks;
                markBlockEntityDirty(cell.x, cell.z);
                if (entity->remainingBurnTicks > 0)
                {
                    constexpr uint32_t ProcessingIntervalTicks = 5;
                    if ((entity->fireMode == FireMode::Pyrolysis || entity->fireMode == FireMode::Firing) &&
                        entity->remainingBurnTicks % ProcessingIntervalTicks == 0)
                    {
                        const FireWorkVolume volume = fireWorkVolume(cell.x, cell.y, cell.z);
                        const bool validProcessingMode =
                            (entity->fireMode == FireMode::Pyrolysis && volume.leakCount == 0) ||
                            (entity->fireMode == FireMode::Firing && volume.leakCount == 1);
                        const char* processingType = entity->fireMode == FireMode::Pyrolysis
                            ? "pyrolysis"
                            : "firing";
                        if (validProcessingMode)
                        {
                            droppedItemRuntime_.processItemsInCells(
                                volume.processingCells,
                                processingRecipes,
                                processingType,
                                entity->fireHeatLevel,
                                ProcessingIntervalTicks,
                                markDirty);
                        }
                    }
                    worldRuntime_->scheduleBlockTickAtWorld(
                        cell.x,
                        cell.y,
                        cell.z,
                        world::WorldRuntime::BlockTickReasonFireBurn);
                    return;
                }

                if (entity->burnRemainderItemId != 0 && entity->burnRemainderCount != 0)
                {
                    const ItemStack remainder{
                        entity->burnRemainderItemId,
                        entity->burnRemainderCount,
                        0
                    };
                    entity->burnRemainderItemId = 0;
                    entity->burnRemainderCount = 0;
                    markBlockEntityDirty(cell.x, cell.z);
                    spawnFireOutput(cell.x, cell.y, cell.z, remainder);
                    entity = worldRuntime_->blockEntityAtWorld(cell.x, cell.y, cell.z);
                    if (entity == nullptr || entity->type != BlockEntityType::Fire)
                    {
                        return;
                    }
                }
            }

            const world::DroppedItemRuntime::BurnableConsumptionResult consumedFuel = droppedItemRuntime_.consumeHighestHeatBurnableInAabb(
                static_cast<float>(cell.x) - 0.5f,
                static_cast<float>(cell.y),
                static_cast<float>(cell.z) - 0.5f,
                static_cast<float>(cell.x) + 0.5f,
                static_cast<float>(cell.y + 1),
                static_cast<float>(cell.z) + 0.5f,
                markDirty);
            if (consumedFuel.burnTimeTicks > 0)
            {
                entity = worldRuntime_->blockEntityAtWorld(cell.x, cell.y, cell.z);
                if (entity != nullptr && entity->type == BlockEntityType::Fire)
                {
                    entity->remainingBurnTicks += consumedFuel.burnTimeTicks;
                    entity->fireHeatLevel = static_cast<uint8_t>(std::min<uint16_t>(consumedFuel.heatLevel, 255));
                    entity->burnRemainderItemId = consumedFuel.remainderItemId;
                    entity->burnRemainderCount = consumedFuel.remainderCount;
                    const FireMode consumedFuelMode = fireModeForFuel(cell.x, cell.y, cell.z, consumedFuel.heatLevel);
                    setFireMode(cell.x, cell.y, cell.z, *entity, consumedFuelMode, false);
                    markBlockEntityDirty(cell.x, cell.z);
                    worldRuntime_->scheduleBlockTickAtWorld(
                        cell.x,
                        cell.y,
                        cell.z,
                        world::WorldRuntime::BlockTickReasonFireBurn);
                }
                return;
            }

            if (!setBlockAtWorld(cell.x, cell.y, cell.z, BlockAir))
            {
                return;
            }

            result.brokenBlocks.push_back(BlockBreakEvent{
                cell.x,
                cell.y,
                cell.z,
                block
            });
        };

        const std::vector<world::WorldRuntime::BlockTickCell> cells = worldRuntime_->takeScheduledBlockTicks(maxCells);
        for (const world::WorldRuntime::BlockTickCell& cell : cells)
        {
            const uint16_t block = worldRuntime_->blockAtWorld(cell.x, cell.y, cell.z);
            if (block != BlockAir && blockDefinition(block).renderType == BlockRenderType::Fire)
            {
                processFireBurn(cell, block);
                continue;
            }
            if (block != BlockAir && blockDefinition(block).renderType == BlockRenderType::Crucible)
            {
                processCrucible(cell, block);
                continue;
            }
            if (block != BlockAir && blockDefinition(block).renderType == BlockRenderType::Mold)
            {
                processMold(cell, block);
                continue;
            }
            if (worldRuntime_->blockEntityAtWorld(cell.x, cell.y, cell.z) != nullptr)
            {
                worldRuntime_->removeBlockEntityAtWorld(cell.x, cell.y, cell.z);
            }
            if (block == BlockAir)
            {
                continue;
            }

            const BlockDefinition& definition = blockDefinition(block);
            if (definition.leafDecayable)
            {
                if (leafHasDecaySupport(cell.x, cell.y, cell.z))
                {
                    continue;
                }

                breakTickBlock(cell, block, definition, false);
                continue;
            }

            if (definition.attachmentFace == BlockAttachmentFace::None)
            {
                continue;
            }

            bool attached = true;
            if (definition.attachmentFace == BlockAttachmentFace::Bottom)
            {
                const uint16_t support = worldRuntime_->blockAtWorld(cell.x, cell.y - 1, cell.z);
                attached = support != BlockAir && blockDefinition(support).collision;
            }
            if (attached)
            {
                continue;
            }

            breakTickBlock(cell, block, definition, true);
        }

        return result;
    }

    void ClientGameplayRuntime::resetBlockBreaking()
    {
        BlockInteractionSystem::resetBreaking(blockBreaking_);
    }

    const BlockBreakingState& ClientGameplayRuntime::blockBreakingState() const
    {
        return blockBreaking_;
    }

    bool ClientGameplayRuntime::pickupDroppedItemInView(DVec3 origin, Vec3 direction, const MarkDirtyFn& markDirty)
    {
        return droppedItemRuntime_.pickupInView(origin, direction, markDirty);
    }

    bool ClientGameplayRuntime::dropSelectedHotbarItem(
        bool wholeStack,
        DVec3 sourcePosition,
        Vec3 direction,
        const MarkDirtyFn& markDirty)
    {
        const std::size_t slotIndex = static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9));
        const ItemStack& slot = playerInventory_.slot(slotIndex);
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        if (slot.itemId == 0 || slot.count == 0 || static_cast<std::size_t>(slot.itemId) >= definitions.size())
        {
            return false;
        }

        const uint16_t dropCount = wholeStack ? slot.count : 1u;
        ItemStack dropStack{};
        dropStack.itemId = slot.itemId;
        dropStack.count = dropCount;
        dropStack.durability = slot.durability;
        dropStack.burnTicksRemaining = slot.burnTicksRemaining;
        dropStack.moltenFluidId = slot.moltenFluidId;
        dropStack.moltenAmount = slot.moltenAmount;

        WorldEntity item = droppedItemRuntime_.createManualDropEntity(dropStack, sourcePosition, direction);
        if (!droppedItemRuntime_.addWorldEntity(std::move(item), markDirty))
        {
            return false;
        }

        return playerInventory_.removeFromSlot(slotIndex, dropCount);
    }

    BlockEditResult ClientGameplayRuntime::placeSelectedItemBlockInView(
        DVec3 origin,
        Vec3 direction,
        DVec3 playerPosition,
        double playerHeightScale,
        const BlockSampler& blockAtWorld,
        const BlockDefinitionProvider& blockDefinition,
        const BlockInteractionSystem::PropMeshProvider& propMesh,
        const SetBlockFn& setBlockAtWorld,
        const world::DroppedItemRuntime::TerrainCollisionFn& terrainCellBlocksItem,
        const MarkDirtyFn& markDirty)
    {
        const std::size_t slotIndex = static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9));
        const ItemStack& heldStack = playerInventory_.slot(slotIndex);
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        if (heldStack.itemId == 0 ||
            heldStack.count == 0 ||
            static_cast<std::size_t>(heldStack.itemId) >= definitions.size())
        {
            return {};
        }

        const ItemDefinition& heldDefinition = definitions[heldStack.itemId];
        if (heldDefinition.placeBlockId == BlockAir ||
            std::find(heldDefinition.placeActions.begin(), heldDefinition.placeActions.end(), "place") == heldDefinition.placeActions.end())
        {
            return {};
        }

        BlockRaycastHit hit{};
        if (!BlockInteractionSystem::raycastBlock(
                origin,
                direction,
                blockAtWorld,
                blockDefinition,
                hit,
                propMesh,
                [this](int x, int y, int z)
                {
                    return worldRuntime_ != nullptr ? worldRuntime_->blockStateAtWorld(x, y, z) : 0;
                }))
        {
            return {};
        }
        if (blockAtWorld && blockAtWorld(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ) != BlockAir)
        {
            return {};
        }
        const BlockDefinition& placedDefinition = blockDefinition(heldDefinition.placeBlockId);
        const uint16_t placementState = placementStateForBlock(placedDefinition, hit, direction);
        if (placedDefinition.attachmentFace == BlockAttachmentFace::Bottom)
        {
            const uint16_t support = blockAtWorld ? blockAtWorld(hit.previousBlockX, hit.previousBlockY - 1, hit.previousBlockZ) : BlockAir;
            if (support == BlockAir || !blockDefinition(support).collision)
            {
                return {};
            }
        }
        if (BlockInteractionSystem::blockIntersectsPlayerCollider(
                hit.previousBlockX,
                hit.previousBlockY,
                hit.previousBlockZ,
                placedDefinition,
                playerPosition,
                playerHeightScale,
                placementState,
                propMesh ? propMesh(heldDefinition.placeBlockId) : nullptr))
        {
            return {};
        }
        if (!setBlockAtWorld || !setBlockAtWorld(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ, heldDefinition.placeBlockId))
        {
            return {};
        }
        if (placementState != 0 && worldRuntime_ != nullptr)
        {
            worldRuntime_->setBlockStateAtWorld(hit.previousBlockX, hit.previousBlockY, hit.previousBlockZ, placementState);
        }

        droppedItemRuntime_.pushItemsOutOfBlock(
            hit.previousBlockX,
            hit.previousBlockY,
            hit.previousBlockZ,
            placedDefinition,
            placementState,
            terrainCellBlocksItem,
            markDirty);

        BlockEditResult result{};
        result.changed = true;
        result.type = BlockEditType::Place;
        result.hit = hit;
        result.block = heldDefinition.placeBlockId;
        result.inventoryChanged = playerInventory_.removeFromSlot(slotIndex, 1);
        return result;
    }

    ItemInteractionMenu ClientGameplayRuntime::beginItemInteractionInView(
        DVec3 origin,
        Vec3 direction,
        bool preferHeldItemBlockActions,
        const std::vector<ItemInteractionRecipe>& recipes,
        const BlockSampler& blockAtWorld,
        const BlockDefinitionProvider& blockDefinition,
        const BlockInteractionSystem::PropMeshProvider& propMesh)
    {
        pendingItemInteraction_ = {};

        const std::size_t slotIndex = static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9));
        const ItemStack& heldStack = playerInventory_.slot(slotIndex);
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        std::vector<std::string> heldUseActions;
        if (heldStack.itemId != 0 &&
            heldStack.count != 0 &&
            static_cast<std::size_t>(heldStack.itemId) < definitions.size())
        {
            heldUseActions = stackUseActions(heldStack, definitions[heldStack.itemId]);
        }

        BlockRaycastHit interactionBlockHit{};
        uint16_t interactionBlock = BlockAir;
        bool hasBlockTarget = false;
        bool hasInteractionBlock = false;
        if (blockAtWorld && blockDefinition &&
            BlockInteractionSystem::raycastBlock(
                origin,
                direction,
                blockAtWorld,
                blockDefinition,
                interactionBlockHit,
                propMesh,
                [this](int x, int y, int z)
                {
                    return worldRuntime_ != nullptr ? worldRuntime_->blockStateAtWorld(x, y, z) : 0;
                }))
        {
            interactionBlock = blockAtWorld(interactionBlockHit.blockX, interactionBlockHit.blockY, interactionBlockHit.blockZ);
            hasBlockTarget = interactionBlock != BlockAir;
            hasInteractionBlock = hasBlockTarget &&
                !blockDefinition(interactionBlock).interactActions.empty();
        }
        bool heldHasBlockTargetAction = false;
        if (hasBlockTarget)
        {
            for (const std::string& heldAction : heldUseActions)
            {
                for (const ItemInteractionRecipe& recipe : recipes)
                {
                    if (recipe.action == heldAction &&
                        !recipe.candidates.empty() &&
                        (recipe.heldItemId == 0 || recipe.heldItemId == heldStack.itemId) &&
                        recipeTargetsBlock(recipe, interactionBlock))
                    {
                        heldHasBlockTargetAction = true;
                        break;
                    }
                }
                if (heldHasBlockTargetAction)
                {
                    break;
                }
                const BlockDefinition& targetDefinition = blockDefinition(interactionBlock);
                if (heldAction == "fill" &&
                    targetDefinition.renderType == BlockRenderType::Crucible &&
                    heldStack.moltenAmount < 10)
                {
                    heldHasBlockTargetAction = true;
                    break;
                }
                if (heldAction == "pour" &&
                    targetDefinition.renderType == BlockRenderType::Mold &&
                    heldStack.moltenFluidId != 0 &&
                    heldStack.moltenAmount != 0)
                {
                    heldHasBlockTargetAction = true;
                    break;
                }
            }
        }

        world::DroppedItemRuntime::Target target{};
        if ((!hasInteractionBlock || preferHeldItemBlockActions) &&
            !heldHasBlockTargetAction &&
            !(preferHeldItemBlockActions && hasBlockTarget) &&
            droppedItemRuntime_.targetInView(origin, direction, target) &&
            target.stack.itemId != 0 &&
            static_cast<std::size_t>(target.stack.itemId) < definitions.size())
        {
            ItemInteractionMenu menu{};
            menu.targetItemId = target.stack.itemId;
            for (const ItemInteractionRecipe& recipe : recipes)
            {
                if (recipe.targetItemId == target.stack.itemId)
                {
                    menu.hasUseTarget = true;
                    break;
                }
            }
            if (menu.hasUseTarget)
            {
                struct AvailableAction
                {
                    std::string action;
                    bool consumesHeldDurability = false;
                };
                std::vector<AvailableAction> availableActions{{"handcraft", false}};
                for (const std::string& action : heldUseActions)
                {
                    const auto existing = std::find_if(
                        availableActions.begin(),
                        availableActions.end(),
                        [&](const AvailableAction& availableAction)
                        {
                            return availableAction.action == action;
                        });
                    if (existing == availableActions.end())
                    {
                        availableActions.push_back(AvailableAction{action, true});
                    }
                }

                for (const AvailableAction& availableAction : availableActions)
                {
                    for (const ItemInteractionRecipe& recipe : recipes)
                    {
                        if (recipe.action != availableAction.action ||
                            recipe.targetItemId != target.stack.itemId ||
                            !recipe.ingredients.empty() ||
                            recipe.candidates.empty())
                        {
                            continue;
                        }

                        std::vector<ItemInteractionCandidate> candidates = recipe.candidates;
                        const bool hasEnoughTargetItems = target.stack.count >= recipe.targetCount;
                        for (ItemInteractionCandidate& candidate : candidates)
                        {
                            candidate.enabled = hasEnoughTargetItems;
                        }

                        ItemInteractionActionMenu actionMenu{};
                        actionMenu.action = availableAction.action;
                        actionMenu.targetCount = recipe.targetCount;
                        actionMenu.candidates = std::move(candidates);
                        actionMenu.actions = {availableAction.action};
                        actionMenu.consumesHeldDurability = availableAction.consumesHeldDurability;
                        menu.actions.push_back(std::move(actionMenu));
                        break;
                    }
                }

                if (menu.actions.empty())
                {
                    return menu;
                }

                menu.available = true;
                pendingItemInteraction_.active = true;
                pendingItemInteraction_.heldSlotIndex = slotIndex;
                pendingItemInteraction_.targetHandle = target.handle;
                pendingItemInteraction_.targetEntityId = target.entityId;
                pendingItemInteraction_.actions = menu.actions;
                return menu;
            }
        }

        if (!hasBlockTarget)
        {
            return {};
        }
        const BlockRaycastHit hit = interactionBlockHit;
        const BlockDefinition& definition = blockDefinition(interactionBlock);

        const float areaMinX = static_cast<float>(hit.blockX) - 0.5f;
        const float areaMinY = static_cast<float>(hit.blockY + 1);
        const float areaMinZ = static_cast<float>(hit.blockZ) - 0.5f;
        const float areaMaxX = static_cast<float>(hit.blockX) + 0.5f;
        const float areaMaxY = static_cast<float>(hit.blockY + 2);
        const float areaMaxZ = static_cast<float>(hit.blockZ) + 0.5f;
        const Vec3 resultPosition{
            static_cast<float>(hit.blockX),
            static_cast<float>(hit.blockY + 1) + 0.05f,
            static_cast<float>(hit.blockZ)
        };

        std::unordered_map<uint16_t, uint32_t> areaCounts;
        const std::vector<world::DroppedItemRuntime::Target> areaTargets =
            droppedItemRuntime_.targetsInAabb(areaMinX, areaMinY, areaMinZ, areaMaxX, areaMaxY, areaMaxZ);
        for (const world::DroppedItemRuntime::Target& areaTarget : areaTargets)
        {
            areaCounts[areaTarget.stack.itemId] += areaTarget.stack.count;
        }

        auto recipeCandidatesForAction = [&](const std::string& action)
        {
            std::vector<ItemInteractionCandidate> candidates;
            for (const ItemInteractionRecipe& recipe : recipes)
            {
                if (recipe.action != action || recipe.candidates.empty())
                {
                    continue;
                }

                if (!recipe.inputs.empty())
                {
                    std::vector<std::vector<ItemStack>> matchingInputs;
                    matchingInputs.reserve(recipe.inputs.size());
                    bool allInputsMatched = true;
                    for (const ItemInteractionInput& input : recipe.inputs)
                    {
                        std::vector<ItemStack> matches;
                        for (const world::DroppedItemRuntime::Target& areaTarget : areaTargets)
                        {
                            if (interactionInputMatches(input, areaTarget.stack, definitions))
                            {
                                matches.push_back(areaTarget.stack);
                            }
                        }
                        if (matches.empty())
                        {
                            allInputsMatched = false;
                            break;
                        }
                        matchingInputs.push_back(std::move(matches));
                    }
                    if (!allInputsMatched)
                    {
                        continue;
                    }

                    std::vector<std::size_t> selection(recipe.inputs.size(), 0);
                    std::function<void(std::size_t)> appendCandidatesForSelection;
                    appendCandidatesForSelection = [&](std::size_t inputIndex)
                    {
                        if (inputIndex < recipe.inputs.size())
                        {
                            for (std::size_t matchIndex = 0; matchIndex < matchingInputs[inputIndex].size(); ++matchIndex)
                            {
                                selection[inputIndex] = matchIndex;
                                appendCandidatesForSelection(inputIndex + 1u);
                            }
                            return;
                        }

                        std::vector<ItemInteractionBoundInput> boundInputs;
                        std::vector<ItemInteractionIngredient> ingredients;
                        boundInputs.reserve(recipe.inputs.size());
                        ingredients.reserve(recipe.inputs.size());
                        for (std::size_t i = 0; i < recipe.inputs.size(); ++i)
                        {
                            const ItemInteractionInput& input = recipe.inputs[i];
                            const ItemStack& stack = matchingInputs[i][selection[i]];
                            boundInputs.push_back(ItemInteractionBoundInput{input.alias, stack});
                            ingredients.push_back(ItemInteractionIngredient{stack.itemId, input.count});
                        }
                        if (!interactionConstraintsMatch(recipe.constraints, boundInputs, definitions))
                        {
                            return;
                        }

                        for (ItemInteractionCandidate candidate : recipe.candidates)
                        {
                            candidate.ingredients = ingredients;
                            candidate.boundInputs = boundInputs;
                            candidate.enabled = true;
                            for (ItemInteractionOutput& output : candidate.outputs)
                            {
                                if (output.deriveType != "head_binding_handle")
                                {
                                    continue;
                                }

                                const ItemInteractionBoundInput* headInput = boundInputByAlias(boundInputs, output.deriveHead);
                                const ItemInteractionBoundInput* bindingInput = boundInputByAlias(boundInputs, output.deriveBinding);
                                const ItemInteractionBoundInput* handleInput = boundInputByAlias(boundInputs, output.deriveHandle);
                                if (headInput == nullptr || bindingInput == nullptr || handleInput == nullptr ||
                                    static_cast<std::size_t>(headInput->stack.itemId) >= definitions.size() ||
                                    static_cast<std::size_t>(bindingInput->stack.itemId) >= definitions.size() ||
                                    static_cast<std::size_t>(handleInput->stack.itemId) >= definitions.size())
                                {
                                    candidate.enabled = false;
                                    continue;
                                }

                                const ItemDefinition& headDefinition = definitions[headInput->stack.itemId];
                                const ItemDefinition& bindingDefinition = definitions[bindingInput->stack.itemId];
                                const ItemDefinition& handleDefinition = definitions[handleInput->stack.itemId];
                                const std::string& size = bindingDefinition.assemblyPart.size;
                                const std::string toolName = toolNameForHead(headDefinition.assemblyPart.type, size);
                                if (toolName.empty())
                                {
                                    candidate.enabled = false;
                                    continue;
                                }

                                auto materialDefinition = [&](const std::string& part, const std::string& material) -> AssemblyMaterialDefinition
                                {
                                    if (assemblyMaterials_ != nullptr)
                                    {
                                        const auto it = assemblyMaterials_->find(part + ":" + material);
                                        if (it != assemblyMaterials_->end())
                                        {
                                            return it->second;
                                        }
                                    }
                                    AssemblyMaterialDefinition fallback{};
                                    fallback.key = material;
                                    fallback.displayName = material.empty() ? "Unknown" : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(material.front())))) + material.substr(1);
                                    fallback.durabilityMultiplier = 1.0f;
                                    return fallback;
                                };

                                const AssemblyMaterialDefinition headMaterial = materialDefinition("head", headDefinition.assemblyPart.material);
                                const AssemblyMaterialDefinition bindingMaterial = materialDefinition("binding", bindingDefinition.assemblyPart.material);
                                const AssemblyMaterialDefinition handleMaterial = materialDefinition("handle", handleDefinition.assemblyPart.material);
                                const uint16_t headMaxDurability = effectiveMaxDurability(headInput->stack, headDefinition);
                                const float headRatio = headMaxDurability == 0
                                    ? 1.0f
                                    : static_cast<float>(headInput->stack.durability == 0 ? headMaxDurability : std::min(headInput->stack.durability, headMaxDurability)) /
                                        static_cast<float>(headMaxDurability);
                                const uint16_t resultMaxDurability = static_cast<uint16_t>(std::max(
                                    1.0f,
                                    std::ceil(static_cast<float>(headMaxDurability) *
                                        bindingMaterial.durabilityMultiplier *
                                        handleMaterial.durabilityMultiplier)));

                                const std::string textureName =
                                    "generated/composites/" +
                                    headDefinition.key + "__" +
                                    bindingDefinition.key + "__" +
                                    handleDefinition.key + "__" +
                                    size;
                                const uint32_t textureLayer = itemTextureLayerByName_ != nullptr && itemTextureLayerByName_->find(textureName) != itemTextureLayerByName_->end()
                                    ? itemTextureLayerByName_->at(textureName)
                                    : definitions[output.itemId].droppedTextureLayer;

                                output.hasStackOverride = true;
                                output.stackOverride = ItemStack{};
                                output.stackOverride.itemId = output.itemId;
                                output.stackOverride.count = 1;
                                output.stackOverride.dynamicMaxDurability = resultMaxDurability;
                                output.stackOverride.durability = static_cast<uint16_t>(std::max(1.0f, std::ceil(static_cast<float>(resultMaxDurability) * headRatio)));
                                output.stackOverride.dynamicBreakLevel = headDefinition.breakLevel;
                                output.stackOverride.dynamicName = headMaterial.displayName + " " + toolName;
                                output.stackOverride.dynamicSlotTexture = textureName;
                                output.stackOverride.dynamicDroppedTexture = textureName;
                                output.stackOverride.dynamicHeldTexture = textureName;
                                output.stackOverride.dynamicDroppedTextureLayer = textureLayer;
                                output.stackOverride.dynamicHeldTextureLayer = textureLayer;
                                output.stackOverride.dynamicUseActions = headDefinition.useActions;
                                output.stackOverride.dynamicBreakActions = headDefinition.breakActions;
                                candidate.displayName = output.stackOverride.dynamicName;
                                candidate.iconTexture = textureName;
                            }
                            candidates.push_back(std::move(candidate));
                        }
                    };
                    appendCandidatesForSelection(0);
                    continue;
                }

                if (recipe.targetItemId == 0)
                {
                    continue;
                }

                std::vector<ItemInteractionIngredient> ingredients;
                ingredients.push_back(ItemInteractionIngredient{recipe.targetItemId, recipe.targetCount});
                ingredients.insert(ingredients.end(), recipe.ingredients.begin(), recipe.ingredients.end());

                if (areaCounts[recipe.targetItemId] == 0)
                {
                    continue;
                }

                bool hasEnoughIngredients = true;
                for (const ItemInteractionIngredient& ingredient : ingredients)
                {
                    if (ingredient.itemId == 0 || ingredient.count == 0 || areaCounts[ingredient.itemId] < ingredient.count)
                    {
                        hasEnoughIngredients = false;
                        break;
                    }
                }

                for (ItemInteractionCandidate candidate : recipe.candidates)
                {
                    candidate.ingredients = ingredients;
                    candidate.enabled = hasEnoughIngredients;
                    candidates.push_back(std::move(candidate));
                }
            }
            return candidates;
        };

        auto blockCandidatesForAction = [&](const std::string& action)
        {
            std::vector<ItemInteractionCandidate> candidates;
            const int placeX = hit.blockX;
            const int placeY = hit.blockY + 1;
            const int placeZ = hit.blockZ;
            const bool hasSolidSupport = blockDefinition(interactionBlock).collision;
            const bool canPlaceAbove = blockAtWorld &&
                blockAtWorld(placeX, placeY, placeZ) == BlockAir &&
                hasSolidSupport;
            for (const ItemInteractionRecipe& recipe : recipes)
            {
                if (recipe.action != action ||
                    recipe.candidates.empty() ||
                    (recipe.heldItemId != 0 && recipe.heldItemId != heldStack.itemId) ||
                    !recipeTargetsBlock(recipe, interactionBlock))
                {
                    continue;
                }

                for (ItemInteractionCandidate candidate : recipe.candidates)
                {
                    if (candidate.placeBlockId == 0 && !candidate.resultTargetsHeldItem)
                    {
                        continue;
                    }
                    candidate.enabled = candidate.resultTargetsHeldItem ||
                        (canPlaceAbove &&
                            (candidate.placeBlockPlacement.empty() ||
                                candidate.placeBlockPlacement == "above_target"));
                    candidates.push_back(std::move(candidate));
                }
            }
            return candidates;
        };

        auto specialBlockCandidatesForAction = [&](const std::string& action)
        {
            std::vector<ItemInteractionCandidate> candidates;
            if (heldStack.itemId == 0 ||
                heldStack.count == 0 ||
                static_cast<std::size_t>(heldStack.itemId) >= definitions.size())
            {
                return candidates;
            }

            const ItemDefinition& heldDefinition = definitions[heldStack.itemId];
            if (!itemHasUseAction(heldDefinition, action))
            {
                return candidates;
            }

            if (action == "fill" && definition.renderType == BlockRenderType::Crucible)
            {
                const BlockEntity* entity = worldRuntime_ != nullptr
                    ? worldRuntime_->blockEntityAtWorld(hit.blockX, hit.blockY, hit.blockZ)
                    : nullptr;
                const bool canFill = entity != nullptr &&
                    entity->type == BlockEntityType::Crucible &&
                    entity->moltenFluidId != 0 &&
                    entity->moltenAmount != 0 &&
                    heldStack.moltenAmount < 10 &&
                    (heldStack.moltenFluidId == 0 || heldStack.moltenFluidId == entity->moltenFluidId);

                ItemInteractionCandidate candidate{};
                candidate.enabled = canFill;
                candidate.outputs.push_back(ItemInteractionOutput{heldStack.itemId, 1});
                candidate.displayName = "Fill";
                candidate.specialAction = "fill_crucible";
                candidates.push_back(std::move(candidate));
                return candidates;
            }

            if (action == "pour" && definition.renderType == BlockRenderType::Mold)
            {
                const std::string form = moldFormFromBlockName(definition.name);
                const uint16_t requiredAmount = moldRequiredAmount(form);
                const uint16_t resultItemId = castPartItemId(definitions, heldStack.moltenFluidId, form);
                if (requiredAmount == 0 || resultItemId == 0)
                {
                    return candidates;
                }

                const BlockEntity* entity = worldRuntime_ != nullptr
                    ? worldRuntime_->blockEntityAtWorld(hit.blockX, hit.blockY, hit.blockZ)
                    : nullptr;
                const bool moldAcceptsFluid = entity == nullptr ||
                    entity->type != BlockEntityType::Mold ||
                    entity->moltenFluidId == 0 ||
                    entity->moltenFluidId == heldStack.moltenFluidId;
                const uint16_t currentAmount = entity != nullptr && entity->type == BlockEntityType::Mold
                    ? entity->moltenAmount
                    : 0;
                const bool canPour = heldStack.moltenFluidId != 0 &&
                    heldStack.moltenAmount != 0 &&
                    moldAcceptsFluid &&
                    currentAmount < requiredAmount;

                ItemInteractionCandidate candidate{};
                candidate.enabled = canPour;
                candidate.outputs.push_back(ItemInteractionOutput{resultItemId, 1});
                candidate.displayName = definitions[resultItemId].name;
                candidate.specialAction = "pour_mold:" + form;
                candidates.push_back(std::move(candidate));
            }
            return candidates;
        };

        ItemInteractionMenu menu{};
        menu.hasUseTarget = true;
        if (hasInteractionBlock && !preferHeldItemBlockActions)
        {
            for (const std::string& blockAction : definition.interactActions)
            {
                std::vector<ItemInteractionCandidate> candidates;
                if (blockAction == "craft")
                {
                    std::vector<ItemInteractionCandidate> handcraftCandidates = recipeCandidatesForAction("handcraft");
                    candidates.insert(
                        candidates.end(),
                        std::make_move_iterator(handcraftCandidates.begin()),
                        std::make_move_iterator(handcraftCandidates.end()));
                    std::vector<ItemInteractionCandidate> craftCandidates = recipeCandidatesForAction("craft");
                    candidates.insert(
                        candidates.end(),
                        std::make_move_iterator(craftCandidates.begin()),
                        std::make_move_iterator(craftCandidates.end()));
                }
                else
                {
                    candidates = recipeCandidatesForAction(blockAction);
                }

                ItemInteractionActionMenu actionMenu{};
                actionMenu.action = blockAction;
                actionMenu.targetCount = 1;
                actionMenu.candidates = std::move(candidates);
                actionMenu.actions = {blockAction};
                actionMenu.areaInteraction = true;
                menu.actions.push_back(std::move(actionMenu));

                for (const std::string& heldAction : heldUseActions)
                {
                    std::vector<ItemInteractionCandidate> toolCandidates = recipeCandidatesForAction(heldAction);
                    if (toolCandidates.empty())
                    {
                        continue;
                    }

                    ItemInteractionActionMenu toolActionMenu{};
                    toolActionMenu.action = blockAction;
                    toolActionMenu.targetCount = 1;
                    toolActionMenu.candidates = std::move(toolCandidates);
                    toolActionMenu.actions = {blockAction, heldAction};
                    toolActionMenu.consumesHeldDurability = true;
                    toolActionMenu.areaInteraction = true;
                    menu.actions.push_back(std::move(toolActionMenu));
                }
            }
        }

        if (!hasInteractionBlock || preferHeldItemBlockActions)
        {
            for (const std::string& heldAction : heldUseActions)
            {
                std::vector<ItemInteractionCandidate> candidates = blockCandidatesForAction(heldAction);
                std::vector<ItemInteractionCandidate> specialCandidates = specialBlockCandidatesForAction(heldAction);
                candidates.insert(
                    candidates.end(),
                    std::make_move_iterator(specialCandidates.begin()),
                    std::make_move_iterator(specialCandidates.end()));
                if (candidates.empty())
                {
                    continue;
                }

                ItemInteractionActionMenu actionMenu{};
                actionMenu.action = heldAction;
                actionMenu.targetCount = 1;
                actionMenu.candidates = std::move(candidates);
                actionMenu.actions = {heldAction};
                actionMenu.consumesHeldDurability = true;
                actionMenu.areaInteraction = false;
                menu.actions.push_back(std::move(actionMenu));
            }
        }

        if (menu.actions.empty())
        {
            if (hasInteractionBlock && !preferHeldItemBlockActions)
            {
                return menu;
            }
            return {};
        }
        menu.available = true;
        pendingItemInteraction_.active = true;
        pendingItemInteraction_.heldSlotIndex = slotIndex;
        pendingItemInteraction_.blockInteraction = true;
        pendingItemInteraction_.blockX = hit.blockX;
        pendingItemInteraction_.blockY = hit.blockY;
        pendingItemInteraction_.blockZ = hit.blockZ;
        pendingItemInteraction_.blockId = interactionBlock;
        pendingItemInteraction_.areaInteraction = true;
        pendingItemInteraction_.areaMinX = areaMinX;
        pendingItemInteraction_.areaMinY = areaMinY;
        pendingItemInteraction_.areaMinZ = areaMinZ;
        pendingItemInteraction_.areaMaxX = areaMaxX;
        pendingItemInteraction_.areaMaxY = areaMaxY;
        pendingItemInteraction_.areaMaxZ = areaMaxZ;
        pendingItemInteraction_.areaResultPosition = resultPosition;
        pendingItemInteraction_.actions = menu.actions;
        return menu;
    }

    ItemInteractionExecuteResult ClientGameplayRuntime::executePendingItemInteraction(
        std::size_t actionIndex,
        std::size_t candidateIndex,
        bool repeat,
        const SetBlockFn& setBlockAtWorld,
        const MarkDirtyFn& markDirty)
    {
        ItemInteractionExecuteResult result{};
        if (!pendingItemInteraction_.active ||
            actionIndex >= pendingItemInteraction_.actions.size() ||
            candidateIndex >= pendingItemInteraction_.actions[actionIndex].candidates.size())
        {
            pendingItemInteraction_ = {};
            return result;
        }

        const ItemInteractionCandidate candidate = pendingItemInteraction_.actions[actionIndex].candidates[candidateIndex];
        const ItemInteractionActionMenu actionMenu = pendingItemInteraction_.actions[actionIndex];
        if ((candidate.outputs.empty() && candidate.placeBlockId == 0 && candidate.specialAction.empty()) || !candidate.enabled)
        {
            pendingItemInteraction_ = {};
            return result;
        }
        const std::size_t heldSlotIndex = pendingItemInteraction_.heldSlotIndex;
        const WorldEntityHandle targetHandle = pendingItemInteraction_.targetHandle;
        const uint64_t targetEntityId = pendingItemInteraction_.targetEntityId;
        const bool areaInteraction = actionMenu.areaInteraction;
        const bool blockInteraction = pendingItemInteraction_.blockInteraction;
        const int blockX = pendingItemInteraction_.blockX;
        const int blockY = pendingItemInteraction_.blockY;
        const int blockZ = pendingItemInteraction_.blockZ;
        const float areaMinX = pendingItemInteraction_.areaMinX;
        const float areaMinY = pendingItemInteraction_.areaMinY;
        const float areaMinZ = pendingItemInteraction_.areaMinZ;
        const float areaMaxX = pendingItemInteraction_.areaMaxX;
        const float areaMaxY = pendingItemInteraction_.areaMaxY;
        const float areaMaxZ = pendingItemInteraction_.areaMaxZ;
        const Vec3 areaResultPosition = pendingItemInteraction_.areaResultPosition;
        const ItemStack heldStack = playerInventory_.slot(heldSlotIndex);
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        uint16_t maxApplications = 1;
        bool consumesDurability = actionMenu.consumesHeldDurability;
        if (repeat)
        {
            maxApplications = UINT16_MAX;
            if (consumesDurability &&
                heldStack.itemId != 0 &&
                static_cast<std::size_t>(heldStack.itemId) < definitions.size())
            {
                const uint16_t maxDurability = effectiveMaxDurability(heldStack, definitions[heldStack.itemId]);
                if (maxDurability > 0)
                {
                    maxApplications = heldStack.durability == 0
                        ? maxDurability
                        : std::min(heldStack.durability, maxDurability);
                }
                else
                {
                    consumesDurability = false;
                }
            }
        }
        else if (consumesDurability &&
            (heldStack.itemId == 0 ||
                static_cast<std::size_t>(heldStack.itemId) >= definitions.size() ||
                effectiveMaxDurability(heldStack, definitions[heldStack.itemId]) == 0))
        {
            consumesDurability = false;
        }
        pendingItemInteraction_ = {};
        auto markBlockEntityDirtyAt = [&](int x, int z)
        {
            if (worldRuntime_ == nullptr || !markDirty)
            {
                return;
            }
            const int chunkX = world::WorldRuntime::floorDiv(x, world::WorldRuntime::ChunkSizeX);
            const int chunkZ = world::WorldRuntime::floorDiv(z, world::WorldRuntime::ChunkSizeZ);
            RuntimeChunk* chunk = worldRuntime_->findChunk(chunkX, chunkZ);
            if (chunk != nullptr)
            {
                markDirty(*chunk);
            }
        };

        if (!candidate.specialAction.empty())
        {
            if (!blockInteraction || worldRuntime_ == nullptr)
            {
                return result;
            }

            if (candidate.specialAction == "fill_crucible")
            {
                BlockEntity* entity = worldRuntime_->blockEntityAtWorld(blockX, blockY, blockZ);
                if (entity == nullptr ||
                    entity->type != BlockEntityType::Crucible ||
                    entity->moltenFluidId == 0 ||
                    entity->moltenAmount == 0 ||
                    heldStack.itemId == 0 ||
                    heldStack.count == 0 ||
                    static_cast<std::size_t>(heldStack.itemId) >= definitions.size() ||
                    !itemHasUseAction(definitions[heldStack.itemId], "fill") ||
                    heldStack.moltenAmount >= 10 ||
                    (heldStack.moltenFluidId != 0 && heldStack.moltenFluidId != entity->moltenFluidId))
                {
                    return result;
                }

                const uint16_t moved = std::min<uint16_t>(
                    static_cast<uint16_t>(10u - heldStack.moltenAmount),
                    entity->moltenAmount);
                if (moved == 0)
                {
                    return result;
                }

                ItemStack replacement = heldStack;
                replacement.moltenFluidId = entity->moltenFluidId;
                replacement.moltenAmount = static_cast<uint16_t>(replacement.moltenAmount + moved);
                entity->moltenAmount = static_cast<uint16_t>(entity->moltenAmount - moved);
                if (entity->moltenAmount == 0)
                {
                    entity->moltenFluidId = 0;
                }
                if (!playerInventory_.replaceSlot(heldSlotIndex, replacement, definitions))
                {
                    return result;
                }

                markBlockEntityDirtyAt(blockX, blockZ);
                result.executed = true;
                result.inventoryChanged = true;
                return result;
            }

            constexpr std::string_view PourMoldPrefix = "pour_mold:";
            if (candidate.specialAction.rfind("pour_mold:", 0) == 0)
            {
                if (heldStack.itemId == 0 ||
                    heldStack.count == 0 ||
                    heldStack.moltenFluidId == 0 ||
                    heldStack.moltenAmount == 0 ||
                    static_cast<std::size_t>(heldStack.itemId) >= definitions.size() ||
                    !itemHasUseAction(definitions[heldStack.itemId], "pour"))
                {
                    return result;
                }

                const std::string form = candidate.specialAction.substr(PourMoldPrefix.size());
                const uint16_t requiredAmount = moldRequiredAmount(form);
                if (requiredAmount == 0)
                {
                    return result;
                }

                BlockEntity* entity = worldRuntime_->ensureMoldBlockEntityAtWorld(blockX, blockY, blockZ);
                if (entity == nullptr ||
                    entity->type != BlockEntityType::Mold ||
                    (entity->moltenFluidId != 0 && entity->moltenFluidId != heldStack.moltenFluidId) ||
                    entity->moltenAmount >= requiredAmount)
                {
                    return result;
                }

                const uint16_t moved = std::min<uint16_t>(
                    heldStack.moltenAmount,
                    static_cast<uint16_t>(requiredAmount - entity->moltenAmount));
                if (moved == 0)
                {
                    return result;
                }

                ItemStack replacement = heldStack;
                entity->moltenFluidId = heldStack.moltenFluidId;
                entity->moltenAmount = static_cast<uint16_t>(entity->moltenAmount + moved);
                replacement.moltenAmount = static_cast<uint16_t>(replacement.moltenAmount - moved);
                if (replacement.moltenAmount == 0)
                {
                    replacement.moltenFluidId = 0;
                }
                if (!playerInventory_.replaceSlot(heldSlotIndex, replacement, definitions))
                {
                    return result;
                }

                if (entity->moltenAmount >= requiredAmount)
                {
                    entity->coolingTicks = 0;
                    worldRuntime_->scheduleBlockTickAtWorld(
                        blockX,
                        blockY,
                        blockZ,
                        world::WorldRuntime::BlockTickReasonSelfBlockChanged);
                }
                markBlockEntityDirtyAt(blockX, blockZ);
                result.executed = true;
                result.inventoryChanged = true;
                return result;
            }

            return result;
        }
        if (candidate.resultTargetsHeldItem)
        {
            if (!blockInteraction || candidate.outputs.empty())
            {
                return result;
            }

            const ItemInteractionOutput& output = candidate.outputs.front();
            if (output.itemId == 0 ||
                static_cast<std::size_t>(output.itemId) >= definitions.size() ||
                definitions[output.itemId].stackSize == 0)
            {
                return result;
            }

            ItemStack replacement{};
            replacement.itemId = output.itemId;
            replacement.count = std::min<uint16_t>(output.count, definitions[output.itemId].stackSize);
            if (replacement.count == 0)
            {
                replacement.count = 1;
            }
            if (definitions[output.itemId].maxBurnTicks > 0 && heldStack.burnTicksRemaining > 0)
            {
                replacement.burnTicksRemaining = std::min(heldStack.burnTicksRemaining, definitions[output.itemId].maxBurnTicks);
            }

            if (!playerInventory_.replaceSlot(heldSlotIndex, replacement, definitions))
            {
                return result;
            }
            result.executed = true;
            result.inventoryChanged = true;
            return result;
        }

        if (candidate.placeBlockId != 0)
        {
            if (!blockInteraction ||
                !(candidate.placeBlockPlacement.empty() || candidate.placeBlockPlacement == "above_target") ||
                !setBlockAtWorld)
            {
                return result;
            }

            const int placeX = blockX;
            const int placeY = blockY + 1;
            const int placeZ = blockZ;
            if (worldRuntime_ == nullptr || worldRuntime_->blockAtWorld(placeX, placeY, placeZ) != BlockAir)
            {
                return result;
            }
            if (!setBlockAtWorld(placeX, placeY, placeZ, candidate.placeBlockId))
            {
                return result;
            }

            BlockEditResult edit{};
            edit.changed = true;
            edit.type = BlockEditType::Place;
            edit.hit.blockX = blockX;
            edit.hit.blockY = blockY;
            edit.hit.blockZ = blockZ;
            edit.hit.previousBlockX = placeX;
            edit.hit.previousBlockY = placeY;
            edit.hit.previousBlockZ = placeZ;
            edit.block = candidate.placeBlockId;
            result.blockEdits.push_back(edit);
            result.executed = true;
            if (consumesDurability)
            {
                result.inventoryChanged = playerInventory_.damageSlot(heldSlotIndex, 1, itemDefinitions());
            }
            return result;
        }

        const uint16_t applicationCount = areaInteraction
            ? droppedItemRuntime_.replaceAreaItems(
                areaMinX,
                areaMinY,
                areaMinZ,
                areaMaxX,
                areaMaxY,
                areaMaxZ,
                candidate.ingredients,
                candidate.outputs,
                maxApplications,
                areaResultPosition,
                markDirty)
            : droppedItemRuntime_.replaceTargetItems(
                targetHandle,
                targetEntityId,
                candidate.outputs,
                actionMenu.targetCount,
                maxApplications,
                markDirty);
        if (applicationCount == 0)
        {
            return result;
        }

        result.executed = true;
        if (consumesDurability)
        {
            result.inventoryChanged = playerInventory_.damageSlot(heldSlotIndex, applicationCount, itemDefinitions());
        }
        return result;
    }

    void ClientGameplayRuntime::cancelPendingItemInteraction()
    {
        pendingItemInteraction_ = {};
    }

    bool ClientGameplayRuntime::updateDroppedItems(
        Vec3 playerPosition,
        double now,
        const world::DroppedItemRuntime::TerrainCollisionFn& terrainCellBlocksPlayer,
        const PickupSoundFn& playPickupSound,
        const MarkDirtyFn& markDirty)
    {
        bool inventoryChanged = false;
        droppedItemRuntime_.update(
            playerPosition,
            now,
            terrainCellBlocksPlayer,
            [this, &inventoryChanged](ItemStack stack)
            {
                const uint16_t originalCount = stack.count;
                const uint16_t remaining = addItemToPlayerInventory(stack);
                inventoryChanged = inventoryChanged || remaining != originalCount;
                return remaining;
            },
            playPickupSound,
            markDirty);
        return inventoryChanged;
    }

    void ClientGameplayRuntime::reserveDroppedItemTracking(std::size_t capacity)
    {
        droppedItemRuntime_.reserveTracking(capacity);
    }

    void ClientGameplayRuntime::refreshDroppedItemChunkTracking(uint64_t key)
    {
        droppedItemRuntime_.refreshChunkTracking(key);
    }

    void ClientGameplayRuntime::removeDroppedItemChunkTracking(uint64_t key)
    {
        droppedItemRuntime_.removeChunkTracking(key);
    }

    void ClientGameplayRuntime::resetDroppedItemTracking()
    {
        droppedItemRuntime_.resetTracking();
    }

    void ClientGameplayRuntime::resetForScene(double timestamp)
    {
        resetBlockBreaking();
        droppedItemRuntime_.resetForScene(timestamp);
        clearInventory();
    }

    void ClientGameplayRuntime::resetForUnload()
    {
        resetBlockBreaking();
        droppedItemRuntime_.resetForUnload();
        clearInventory();
    }

    void ClientGameplayRuntime::normalizeLoadedEntity(WorldEntity& entity)
    {
        droppedItemRuntime_.normalizeLoadedEntity(entity);
    }

    std::size_t ClientGameplayRuntime::loadedDroppedItemCount() const
    {
        return droppedItemRuntime_.loadedItemCount();
    }

    float ClientGameplayRuntime::droppedItemRenderAlpha() const
    {
        return droppedItemRuntime_.renderAlpha();
    }

    const std::unordered_map<uint64_t, std::size_t>& ClientGameplayRuntime::droppedItemTrackedChunkCounts() const
    {
        return droppedItemRuntime_.trackedChunkCounts();
    }

    void ClientGameplayRuntime::setHotbarSelectedSlot(int slot)
    {
        hotbarSelectedSlot_ = std::clamp(slot, 0, 9);
    }

    int ClientGameplayRuntime::hotbarSelectedSlot() const
    {
        return hotbarSelectedSlot_;
    }

    std::size_t ClientGameplayRuntime::inventorySlotCount() const
    {
        return playerInventory_.slotCount();
    }

    const ItemStack& ClientGameplayRuntime::inventorySlot(std::size_t index) const
    {
        return playerInventory_.slot(index);
    }

    const ItemStack& ClientGameplayRuntime::offhandSlot() const
    {
        return playerInventory_.offhandSlot();
    }

    const ItemStack& ClientGameplayRuntime::inventoryCursorStack() const
    {
        return playerInventory_.cursorStack();
    }

    uint16_t ClientGameplayRuntime::heldPortableLightEmission() const
    {
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        auto emissionFor = [&](const ItemStack& stack) -> uint16_t
        {
            if (stack.itemId == 0 || stack.count == 0 || static_cast<std::size_t>(stack.itemId) >= definitions.size())
            {
                return 0;
            }
            const ItemDefinition& definition = definitions[stack.itemId];
            if (definition.portableLightEmission == 0)
            {
                return 0;
            }
            if (definition.maxBurnTicks > 0 && stack.burnTicksRemaining == 0)
            {
                return 0;
            }
            return definition.portableLightEmission;
        };

        uint16_t emission = 0;
        if (hotbarSelectedSlot_ >= 0 && hotbarSelectedSlot_ < static_cast<int>(PlayerInventory::HotbarSlotCount))
        {
            emission = std::max(emission, emissionFor(playerInventory_.slot(static_cast<std::size_t>(hotbarSelectedSlot_))));
        }
        emission = std::max(emission, emissionFor(playerInventory_.offhandSlot()));
        return emission;
    }

    void ClientGameplayRuntime::clearInventory()
    {
        playerInventory_.clear();
    }

    std::array<ItemStack, PlayerInventory::SlotCount> ClientGameplayRuntime::inventorySnapshot() const
    {
        return playerInventory_.snapshot();
    }

    void ClientGameplayRuntime::setInventorySnapshot(const std::array<ItemStack, PlayerInventory::SlotCount>& slots)
    {
        playerInventory_.setSlots(slots, itemDefinitions());
    }

    void ClientGameplayRuntime::setOffhandSlot(ItemStack stack)
    {
        playerInventory_.setOffhandSlot(stack, itemDefinitions());
    }

    uint16_t ClientGameplayRuntime::addItemToPlayerInventory(ItemStack stack)
    {
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        if (stack.itemId == 0 || stack.count == 0 || static_cast<std::size_t>(stack.itemId) >= definitions.size())
        {
            return stack.count;
        }

        const uint16_t maxStack = definitions[stack.itemId].stackSize;
        if (maxStack == 0)
        {
            return stack.count;
        }

        return playerInventory_.add(stack, definitions);
    }

    bool ClientGameplayRuntime::handleInventorySlotClick(std::size_t slotIndex, InventoryClickButton button, bool shift)
    {
        return playerInventory_.handleSlotClick(slotIndex, button, shift, itemDefinitions());
    }

    bool ClientGameplayRuntime::swapHotbarWithSlot(std::size_t slotIndex, std::size_t hotbarSlot)
    {
        return playerInventory_.swapHotbarWithSlot(slotIndex, hotbarSlot);
    }

    bool ClientGameplayRuntime::swapSelectedHotbarWithOffhand()
    {
        const std::size_t slotIndex = static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9));
        return playerInventory_.swapOffhandWithHotbar(slotIndex);
    }

    bool ClientGameplayRuntime::closeInventoryCursor()
    {
        return playerInventory_.closeCursor(itemDefinitions());
    }

    bool ClientGameplayRuntime::tickHeldBurningItems(bool extinguishHeldBurnableLights)
    {
        return playerInventory_.tickHeldBurningItems(
            static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9)),
            extinguishHeldBurnableLights,
            itemDefinitions());
    }

    BlockBreakTool ClientGameplayRuntime::currentBlockBreakTool() const
    {
        BlockBreakTool tool{};
        const std::size_t slotIndex = static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9));
        const ItemStack& heldStack = playerInventory_.slot(slotIndex);
        const std::vector<ItemDefinition>& definitions = itemDefinitions();
        if (heldStack.itemId == 0 ||
            heldStack.count == 0 ||
            static_cast<std::size_t>(heldStack.itemId) >= definitions.size())
        {
            return tool;
        }

        const ItemDefinition& definition = definitions[heldStack.itemId];
        const std::vector<std::string>& breakActions = !heldStack.dynamicBreakActions.empty()
            ? heldStack.dynamicBreakActions
            : definition.breakActions;
        const uint16_t breakLevel = heldStack.dynamicBreakLevel != 0
            ? heldStack.dynamicBreakLevel
            : definition.breakLevel;
        if (breakActions.empty() && breakLevel == 0)
        {
            return tool;
        }

        tool.level = breakLevel;
        tool.actions = breakActions;
        tool.durable = effectiveMaxDurability(heldStack, definition) > 0;
        return tool;
    }

    bool ClientGameplayRuntime::damageSelectedHotbarItem(uint16_t damage)
    {
        if (damage == 0)
        {
            return false;
        }
        const std::size_t slotIndex = static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9));
        return playerInventory_.damageSlot(slotIndex, damage, itemDefinitions());
    }
}
