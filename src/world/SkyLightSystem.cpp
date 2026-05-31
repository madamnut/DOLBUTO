#include "world/SkyLightSystem.h"

#include "world/BlockLightRules.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace dolbuto::world
{
    namespace
    {
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeY = 512;
        constexpr int ChunkSizeZ = 16;
        constexpr int ChunkStrideZ = ChunkSizeX;
        constexpr int ChunkStrideY = ChunkSizeX * ChunkSizeZ;
        constexpr uint16_t BlockAir = 0;
        constexpr uint16_t FluidNone = 0;
        constexpr int FluidAmountBits = 7;
        constexpr uint16_t FluidAmountMask = (1u << FluidAmountBits) - 1u;

        struct LightNode
        {
            uint32_t index = 0;
            uint8_t light = 0;
        };

        template <typename Node>
        class LightQueue
        {
        public:
            explicit LightQueue(std::size_t reserveCount)
            {
                nodes_.reserve(reserveCount);
            }

            bool empty() const
            {
                return head_ >= nodes_.size();
            }

            void push(Node node)
            {
                nodes_.push_back(node);
            }

            Node pop()
            {
                return nodes_[head_++];
            }

        private:
            std::vector<Node> nodes_;
            std::size_t head_ = 0;
        };

        constexpr std::size_t blockIndex(int x, int y, int z)
        {
            return static_cast<std::size_t>((y * ChunkSizeZ + z) * ChunkSizeX + x);
        }

        constexpr uint32_t queueIndex(std::size_t index)
        {
            return static_cast<uint32_t>(index);
        }

        constexpr int blockX(uint32_t index)
        {
            return static_cast<int>(index & 0x0Fu);
        }

        constexpr int blockZ(uint32_t index)
        {
            return static_cast<int>((index >> 4u) & 0x0Fu);
        }

        constexpr int blockY(uint32_t index)
        {
            return static_cast<int>(index >> 8u);
        }

        constexpr uint16_t fluidId(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid >> FluidAmountBits);
        }

        constexpr uint16_t fluidAmount(uint16_t fluid)
        {
            return static_cast<uint16_t>(fluid & FluidAmountMask);
        }

        bool hasFluid(const ChunkData& chunk, std::size_t index)
        {
            return index < chunk.fluids.size() &&
                fluidId(chunk.fluids[index]) != FluidNone &&
                fluidAmount(chunk.fluids[index]) != 0;
        }

        uint8_t fallbackBlockLightAttenuation(uint16_t block)
        {
            return block == BlockAir ? 1 : MaxSkyLight;
        }

        uint8_t blockLightAttenuation(const LightAttenuationTables* lightAttenuation, uint16_t block)
        {
            if (lightAttenuation != nullptr && static_cast<std::size_t>(block) < lightAttenuation->block.size())
            {
                return lightAttenuation->block[block];
            }
            return fallbackBlockLightAttenuation(block);
        }

        uint8_t fluidLightAttenuation(const LightAttenuationTables* lightAttenuation, uint16_t fluid)
        {
            if (lightAttenuation != nullptr && static_cast<std::size_t>(fluid) < lightAttenuation->fluid.size())
            {
                return lightAttenuation->fluid[fluid];
            }
            return fluid == FluidNone ? 0 : 2;
        }

        uint8_t blockLightEmission(const LightAttenuationTables* lightAttenuation, uint16_t block)
        {
            if (lightAttenuation != nullptr && static_cast<std::size_t>(block) < lightAttenuation->blockEmission.size())
            {
                return lightAttenuation->blockEmission[block];
            }
            return 0;
        }

        uint16_t blockStateAt(const ChunkData& chunk, std::size_t index)
        {
            return index < chunk.blockStates.size() ? chunk.blockStates[index] : 0;
        }

        uint8_t directionalAttenuationForCell(
            const ChunkData& chunk,
            std::size_t index,
            block_light::Direction direction,
            const LightAttenuationTables* lightAttenuation)
        {
            if (index >= chunk.blocks.size())
            {
                return MaxSkyLight;
            }

            const uint16_t block = chunk.blocks[index];
            uint8_t attenuation = block_light::directionalAttenuation(
                lightAttenuation,
                block,
                blockStateAt(chunk, index),
                direction,
                blockLightAttenuation(lightAttenuation, block));
            if (hasFluid(chunk, index))
            {
                attenuation = std::max<uint8_t>(attenuation, fluidLightAttenuation(lightAttenuation, fluidId(chunk.fluids[index])));
            }
            return attenuation;
        }

        uint8_t transitionAttenuation(
            const ChunkData& chunk,
            std::size_t currentIndex,
            std::size_t nextIndex,
            block_light::Direction direction,
            const LightAttenuationTables* lightAttenuation)
        {
            return std::max<uint8_t>(
                directionalAttenuationForCell(chunk, currentIndex, direction, lightAttenuation),
                directionalAttenuationForCell(chunk, nextIndex, block_light::opposite(direction), lightAttenuation));
        }

        void setBufferedSkyLight(std::vector<uint8_t>& light, std::size_t index, uint8_t skyLight)
        {
            const uint8_t blockLight = index < light.size() ? blockLightFromPacked(light[index]) : 0;
            light[index] = packLight(skyLight, blockLight);
        }

        void setBufferedBlockLight(std::vector<uint8_t>& light, std::size_t index, uint8_t blockLight)
        {
            const uint8_t skyLight = index < light.size() ? skyLightFromPacked(light[index]) : 0;
            light[index] = packLight(skyLight, blockLight);
        }

        void enqueueSkyLight(
            std::vector<uint8_t>& light,
            LightQueue<LightNode>& queue,
            std::size_t index,
            uint8_t skyLight)
        {
            if (skyLight <= skyLightFromPacked(light[index]))
            {
                return;
            }

            setBufferedSkyLight(light, index, skyLight);
            queue.push(LightNode{queueIndex(index), skyLight});
        }

        void enqueueBlockLight(
            std::vector<uint8_t>& light,
            LightQueue<LightNode>& queue,
            std::size_t index,
            uint8_t blockLight)
        {
            if (blockLight <= blockLightFromPacked(light[index]))
            {
                return;
            }

            setBufferedBlockLight(light, index, blockLight);
            queue.push(LightNode{queueIndex(index), blockLight});
        }

        void tryPropagateSkyLight(
            std::vector<uint8_t>& light,
            const ChunkData& chunk,
            LightQueue<LightNode>& queue,
            std::size_t currentIndex,
            std::size_t nextIndex,
            uint8_t sourceSkyLight,
            block_light::Direction direction,
            const LightAttenuationTables* lightAttenuation)
        {
            const uint8_t nextAttenuation = transitionAttenuation(chunk, currentIndex, nextIndex, direction, lightAttenuation);
            if (nextAttenuation >= sourceSkyLight)
            {
                return;
            }

            enqueueSkyLight(light, queue, nextIndex, static_cast<uint8_t>(sourceSkyLight - nextAttenuation));
        }

        void tryPropagateBlockLight(
            std::vector<uint8_t>& light,
            const ChunkData& chunk,
            LightQueue<LightNode>& queue,
            std::size_t currentIndex,
            std::size_t nextIndex,
            uint8_t sourceBlockLight,
            block_light::Direction direction,
            const LightAttenuationTables* lightAttenuation)
        {
            const uint8_t nextAttenuation = transitionAttenuation(chunk, currentIndex, nextIndex, direction, lightAttenuation);
            if (nextAttenuation >= sourceBlockLight)
            {
                return;
            }

            enqueueBlockLight(light, queue, nextIndex, static_cast<uint8_t>(sourceBlockLight - nextAttenuation));
        }

        void propagateSkyLight(
            std::vector<uint8_t>& light,
            const ChunkData& chunk,
            const LightAttenuationTables* lightAttenuation,
            LightQueue<LightNode>& queue)
        {
            while (!queue.empty())
            {
                const LightNode node = queue.pop();

                const std::size_t currentIndex = static_cast<std::size_t>(node.index);
                if (skyLightFromPacked(light[currentIndex]) != node.light)
                {
                    continue;
                }

                const int x = blockX(node.index);
                const int y = blockY(node.index);
                const int z = blockZ(node.index);

                if (x < ChunkSizeX - 1)
                {
                    tryPropagateSkyLight(light, chunk, queue, currentIndex, currentIndex + 1, node.light, block_light::Direction::PosX, lightAttenuation);
                }
                if (x > 0)
                {
                    tryPropagateSkyLight(light, chunk, queue, currentIndex, currentIndex - 1, node.light, block_light::Direction::NegX, lightAttenuation);
                }
                if (z < ChunkSizeZ - 1)
                {
                    tryPropagateSkyLight(light, chunk, queue, currentIndex, currentIndex + ChunkStrideZ, node.light, block_light::Direction::PosZ, lightAttenuation);
                }
                if (z > 0)
                {
                    tryPropagateSkyLight(light, chunk, queue, currentIndex, currentIndex - ChunkStrideZ, node.light, block_light::Direction::NegZ, lightAttenuation);
                }
                if (y < ChunkSizeY - 1)
                {
                    tryPropagateSkyLight(light, chunk, queue, currentIndex, currentIndex + ChunkStrideY, node.light, block_light::Direction::PosY, lightAttenuation);
                }
                if (y > 0)
                {
                    tryPropagateSkyLight(light, chunk, queue, currentIndex, currentIndex - ChunkStrideY, node.light, block_light::Direction::NegY, lightAttenuation);
                }
            }
        }

        void propagateBlockLight(
            std::vector<uint8_t>& light,
            const ChunkData& chunk,
            const LightAttenuationTables* lightAttenuation,
            LightQueue<LightNode>& queue)
        {
            while (!queue.empty())
            {
                const LightNode node = queue.pop();

                const std::size_t currentIndex = static_cast<std::size_t>(node.index);
                if (blockLightFromPacked(light[currentIndex]) != node.light)
                {
                    continue;
                }

                const int x = blockX(node.index);
                const int y = blockY(node.index);
                const int z = blockZ(node.index);

                if (x < ChunkSizeX - 1)
                {
                    tryPropagateBlockLight(light, chunk, queue, currentIndex, currentIndex + 1, node.light, block_light::Direction::PosX, lightAttenuation);
                }
                if (x > 0)
                {
                    tryPropagateBlockLight(light, chunk, queue, currentIndex, currentIndex - 1, node.light, block_light::Direction::NegX, lightAttenuation);
                }
                if (z < ChunkSizeZ - 1)
                {
                    tryPropagateBlockLight(light, chunk, queue, currentIndex, currentIndex + ChunkStrideZ, node.light, block_light::Direction::PosZ, lightAttenuation);
                }
                if (z > 0)
                {
                    tryPropagateBlockLight(light, chunk, queue, currentIndex, currentIndex - ChunkStrideZ, node.light, block_light::Direction::NegZ, lightAttenuation);
                }
                if (y < ChunkSizeY - 1)
                {
                    tryPropagateBlockLight(light, chunk, queue, currentIndex, currentIndex + ChunkStrideY, node.light, block_light::Direction::PosY, lightAttenuation);
                }
                if (y > 0)
                {
                    tryPropagateBlockLight(light, chunk, queue, currentIndex, currentIndex - ChunkStrideY, node.light, block_light::Direction::NegY, lightAttenuation);
                }
            }
        }

        std::vector<uint8_t> recomputeLocalSkyLight(const ChunkData& source, const LightAttenuationTables* lightAttenuation)
        {
            std::vector<uint8_t> light(ChunkBlockCount, 0);
            LightQueue<LightNode> skyQueue(ChunkBlockCount);
            for (int z = 0; z < ChunkSizeZ; ++z)
            {
                for (int x = 0; x < ChunkSizeX; ++x)
                {
                    bool hasPreviousCell = false;
                    std::size_t previousIndex = 0;
                    for (int y = ChunkSizeY - 1; y >= 0; --y)
                    {
                        const std::size_t index = blockIndex(x, y, z);
                        const uint8_t verticalAttenuation = hasPreviousCell
                            ? transitionAttenuation(source, previousIndex, index, block_light::Direction::NegY, lightAttenuation)
                            : directionalAttenuationForCell(source, index, block_light::Direction::PosY, lightAttenuation);
                        if (verticalAttenuation >= MaxSkyLight)
                        {
                            break;
                        }

                        enqueueSkyLight(light, skyQueue, index, MaxSkyLight);
                        previousIndex = index;
                        hasPreviousCell = true;
                    }
                }
            }

            LightQueue<LightNode> blockQueue(ChunkBlockCount);
            for (std::size_t index = 0; index < ChunkBlockCount; ++index)
            {
                const uint8_t emission = blockLightEmission(lightAttenuation, source.blocks[index]);
                if (emission > 0)
                {
                    enqueueBlockLight(light, blockQueue, index, emission);
                }
            }

            propagateSkyLight(light, source, lightAttenuation, skyQueue);
            propagateBlockLight(light, source, lightAttenuation, blockQueue);
            return light;
        }

        void injectBoundaryLight(
            std::vector<uint8_t>& resolved,
            LightQueue<LightNode>& skyQueue,
            LightQueue<LightNode>& blockQueue,
            int centerX,
            int centerZ,
            const ChunkData& center,
            const ChunkData& neighbor,
            int neighborX,
            int neighborZ,
            block_light::Direction directionFromNeighbor,
            const LightAttenuationTables* lightAttenuation)
        {
            if (neighbor.localLight.size() != ChunkBlockCount)
            {
                return;
            }

            for (int y = 0; y < ChunkSizeY; ++y)
            {
                const std::size_t centerIndex = blockIndex(centerX, y, centerZ);
                const std::size_t neighborIndex = blockIndex(neighborX, y, neighborZ);
                const uint8_t neighborPackedLight = neighbor.localLight[neighborIndex];
                const uint8_t neighborSkyLight = skyLightFromPacked(neighborPackedLight);
                const uint8_t neighborBlockLight = blockLightFromPacked(neighborPackedLight);
                const uint8_t centerAttenuation = std::max<uint8_t>(
                    directionalAttenuationForCell(neighbor, neighborIndex, directionFromNeighbor, lightAttenuation),
                    directionalAttenuationForCell(center, centerIndex, block_light::opposite(directionFromNeighbor), lightAttenuation));
                if (centerAttenuation < neighborSkyLight)
                {
                    enqueueSkyLight(resolved, skyQueue, centerIndex, static_cast<uint8_t>(neighborSkyLight - centerAttenuation));
                }
                if (centerAttenuation < neighborBlockLight)
                {
                    enqueueBlockLight(resolved, blockQueue, centerIndex, static_cast<uint8_t>(neighborBlockLight - centerAttenuation));
                }
            }
        }
    }

    uint8_t packLight(uint8_t skyLight, uint8_t blockLight)
    {
        return static_cast<uint8_t>((std::min<uint8_t>(skyLight, MaxSkyLight) << 4u) |
            std::min<uint8_t>(blockLight, MaxBlockLight));
    }

    uint8_t skyLightFromPacked(uint8_t packedLight)
    {
        return static_cast<uint8_t>((packedLight >> 4u) & 0x0Fu);
    }

    uint8_t blockLightFromPacked(uint8_t packedLight)
    {
        return static_cast<uint8_t>(packedLight & 0x0Fu);
    }

    void recomputeChunkSkyLight(ChunkData& chunk, const LightAttenuationTables* lightAttenuation)
    {
        if (chunk.blocks.size() != ChunkBlockCount)
        {
            chunk.light.clear();
            return;
        }
        if (chunk.fluids.size() != ChunkBlockCount)
        {
            chunk.fluids.assign(ChunkBlockCount, 0);
        }

        chunk.localLight = computeLocalSkyLight(chunk, lightAttenuation);
        chunk.light = chunk.localLight;
    }

    std::vector<uint8_t> computeLocalSkyLight(const ChunkData& chunk, const LightAttenuationTables* lightAttenuation)
    {
        if (chunk.blocks.size() != ChunkBlockCount)
        {
            return {};
        }

        return recomputeLocalSkyLight(chunk, lightAttenuation);
    }

    std::vector<uint8_t> resolveCenterSkyLight(
        const std::array<std::shared_ptr<ChunkData>, 9>& chunks,
        const LightAttenuationTables* lightAttenuation)
    {
        const std::shared_ptr<ChunkData>& north = chunks[1];
        const std::shared_ptr<ChunkData>& west = chunks[3];
        const std::shared_ptr<ChunkData>& center = chunks[4];
        const std::shared_ptr<ChunkData>& east = chunks[5];
        const std::shared_ptr<ChunkData>& south = chunks[7];
        if (!center || center->blocks.size() != ChunkBlockCount)
        {
            return {};
        }
        if (!north || !west || !east || !south ||
            north->localLight.size() != ChunkBlockCount ||
            west->localLight.size() != ChunkBlockCount ||
            east->localLight.size() != ChunkBlockCount ||
            south->localLight.size() != ChunkBlockCount)
        {
            return {};
        }

        std::vector<uint8_t> resolved = center->localLight.size() == ChunkBlockCount
            ? center->localLight
            : computeLocalSkyLight(*center, lightAttenuation);
        if (resolved.size() != ChunkBlockCount)
        {
            return {};
        }

        LightQueue<LightNode> skyQueue(ChunkBlockCount);
        LightQueue<LightNode> blockQueue(ChunkBlockCount);
        for (int z = 0; z < ChunkSizeZ; ++z)
        {
            injectBoundaryLight(resolved, skyQueue, blockQueue, 0, z, *center, *west, ChunkSizeX - 1, z, block_light::Direction::PosX, lightAttenuation);
            injectBoundaryLight(resolved, skyQueue, blockQueue, ChunkSizeX - 1, z, *center, *east, 0, z, block_light::Direction::NegX, lightAttenuation);
        }
        for (int x = 0; x < ChunkSizeX; ++x)
        {
            injectBoundaryLight(resolved, skyQueue, blockQueue, x, 0, *center, *north, x, ChunkSizeZ - 1, block_light::Direction::PosZ, lightAttenuation);
            injectBoundaryLight(resolved, skyQueue, blockQueue, x, ChunkSizeZ - 1, *center, *south, x, 0, block_light::Direction::NegZ, lightAttenuation);
        }

        propagateSkyLight(resolved, *center, lightAttenuation, skyQueue);
        propagateBlockLight(resolved, *center, lightAttenuation, blockQueue);
        return resolved;
    }
}
