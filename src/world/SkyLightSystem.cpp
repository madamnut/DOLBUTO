#include "world/SkyLightSystem.h"

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

        uint8_t attenuationForCell(const ChunkData& chunk, std::size_t index, const LightAttenuationTables* lightAttenuation)
        {
            if (index >= chunk.blocks.size())
            {
                return MaxSkyLight;
            }
            const uint16_t block = chunk.blocks[index];
            uint8_t attenuation = blockLightAttenuation(lightAttenuation, block);
            if (hasFluid(chunk, index))
            {
                attenuation = std::max<uint8_t>(attenuation, fluidLightAttenuation(lightAttenuation, fluidId(chunk.fluids[index])));
            }
            return attenuation;
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

        std::vector<uint8_t> buildAttenuationCache(const ChunkData& chunk, const LightAttenuationTables* lightAttenuation)
        {
            std::vector<uint8_t> attenuation(ChunkBlockCount, MaxSkyLight);
            for (std::size_t index = 0; index < ChunkBlockCount; ++index)
            {
                attenuation[index] = attenuationForCell(chunk, index, lightAttenuation);
            }
            return attenuation;
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
            const std::vector<uint8_t>& attenuation,
            LightQueue<LightNode>& queue,
            std::size_t nextIndex,
            uint8_t sourceSkyLight)
        {
            const uint8_t nextAttenuation = attenuation[nextIndex];
            if (nextAttenuation >= sourceSkyLight)
            {
                return;
            }

            enqueueSkyLight(light, queue, nextIndex, static_cast<uint8_t>(sourceSkyLight - nextAttenuation));
        }

        void tryPropagateBlockLight(
            std::vector<uint8_t>& light,
            const std::vector<uint8_t>& attenuation,
            LightQueue<LightNode>& queue,
            std::size_t nextIndex,
            uint8_t sourceBlockLight)
        {
            const uint8_t nextAttenuation = attenuation[nextIndex];
            if (nextAttenuation >= sourceBlockLight)
            {
                return;
            }

            enqueueBlockLight(light, queue, nextIndex, static_cast<uint8_t>(sourceBlockLight - nextAttenuation));
        }

        void propagateSkyLight(
            std::vector<uint8_t>& light,
            const std::vector<uint8_t>& attenuation,
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
                    tryPropagateSkyLight(light, attenuation, queue, currentIndex + 1, node.light);
                }
                if (x > 0)
                {
                    tryPropagateSkyLight(light, attenuation, queue, currentIndex - 1, node.light);
                }
                if (z < ChunkSizeZ - 1)
                {
                    tryPropagateSkyLight(light, attenuation, queue, currentIndex + ChunkStrideZ, node.light);
                }
                if (z > 0)
                {
                    tryPropagateSkyLight(light, attenuation, queue, currentIndex - ChunkStrideZ, node.light);
                }
                if (y < ChunkSizeY - 1)
                {
                    tryPropagateSkyLight(light, attenuation, queue, currentIndex + ChunkStrideY, node.light);
                }
                if (y > 0)
                {
                    tryPropagateSkyLight(light, attenuation, queue, currentIndex - ChunkStrideY, node.light);
                }
            }
        }

        void propagateBlockLight(
            std::vector<uint8_t>& light,
            const std::vector<uint8_t>& attenuation,
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
                    tryPropagateBlockLight(light, attenuation, queue, currentIndex + 1, node.light);
                }
                if (x > 0)
                {
                    tryPropagateBlockLight(light, attenuation, queue, currentIndex - 1, node.light);
                }
                if (z < ChunkSizeZ - 1)
                {
                    tryPropagateBlockLight(light, attenuation, queue, currentIndex + ChunkStrideZ, node.light);
                }
                if (z > 0)
                {
                    tryPropagateBlockLight(light, attenuation, queue, currentIndex - ChunkStrideZ, node.light);
                }
                if (y < ChunkSizeY - 1)
                {
                    tryPropagateBlockLight(light, attenuation, queue, currentIndex + ChunkStrideY, node.light);
                }
                if (y > 0)
                {
                    tryPropagateBlockLight(light, attenuation, queue, currentIndex - ChunkStrideY, node.light);
                }
            }
        }

        std::vector<uint8_t> recomputeLocalSkyLight(const ChunkData& source, const LightAttenuationTables* lightAttenuation)
        {
            std::vector<uint8_t> light(ChunkBlockCount, 0);
            const std::vector<uint8_t> attenuation = buildAttenuationCache(source, lightAttenuation);
            LightQueue<LightNode> skyQueue(ChunkBlockCount);
            for (int z = 0; z < ChunkSizeZ; ++z)
            {
                for (int x = 0; x < ChunkSizeX; ++x)
                {
                    for (int y = ChunkSizeY - 1; y >= 0; --y)
                    {
                        const std::size_t index = blockIndex(x, y, z);
                        if (attenuation[index] >= MaxSkyLight)
                        {
                            break;
                        }

                        enqueueSkyLight(light, skyQueue, index, MaxSkyLight);
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

            propagateSkyLight(light, attenuation, skyQueue);
            propagateBlockLight(light, attenuation, blockQueue);
            return light;
        }

        void injectBoundaryLight(
            std::vector<uint8_t>& resolved,
            const std::vector<uint8_t>& attenuation,
            LightQueue<LightNode>& skyQueue,
            LightQueue<LightNode>& blockQueue,
            int centerX,
            int centerZ,
            const ChunkData& neighbor,
            int neighborX,
            int neighborZ)
        {
            if (neighbor.localLight.size() != ChunkBlockCount)
            {
                return;
            }

            for (int y = 0; y < ChunkSizeY; ++y)
            {
                const std::size_t centerIndex = blockIndex(centerX, y, centerZ);
                const uint8_t neighborPackedLight = neighbor.localLight[blockIndex(neighborX, y, neighborZ)];
                const uint8_t neighborSkyLight = skyLightFromPacked(neighborPackedLight);
                const uint8_t neighborBlockLight = blockLightFromPacked(neighborPackedLight);
                const uint8_t centerAttenuation = attenuation[centerIndex];
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

        const std::vector<uint8_t> attenuation = buildAttenuationCache(*center, lightAttenuation);
        LightQueue<LightNode> skyQueue(ChunkBlockCount);
        LightQueue<LightNode> blockQueue(ChunkBlockCount);
        for (int z = 0; z < ChunkSizeZ; ++z)
        {
            injectBoundaryLight(resolved, attenuation, skyQueue, blockQueue, 0, z, *west, ChunkSizeX - 1, z);
            injectBoundaryLight(resolved, attenuation, skyQueue, blockQueue, ChunkSizeX - 1, z, *east, 0, z);
        }
        for (int x = 0; x < ChunkSizeX; ++x)
        {
            injectBoundaryLight(resolved, attenuation, skyQueue, blockQueue, x, 0, *north, x, ChunkSizeZ - 1);
            injectBoundaryLight(resolved, attenuation, skyQueue, blockQueue, x, ChunkSizeZ - 1, *south, x, 0);
        }

        propagateSkyLight(resolved, attenuation, skyQueue);
        propagateBlockLight(resolved, attenuation, blockQueue);
        return resolved;
    }
}
