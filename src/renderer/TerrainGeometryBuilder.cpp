#include "renderer/TerrainGeometryBuilder.h"

#include "world/BlockVisualShape.h"
#include "world/SkyLightSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dolbuto
{
    namespace
    {
        constexpr int ChunkSizeX = 16;
        constexpr int ChunkSizeY = 512;
        constexpr int ChunkSizeZ = 16;
        constexpr int SubchunkSize = 16;
        constexpr int SubchunksPerChunk = ChunkSizeY / SubchunkSize;
        constexpr uint16_t BlockAir = 0;
        constexpr int TerrainTilePeriod = 65536;
        constexpr uint32_t TopFaceRotationSalt = 0x51A7E001u;
        constexpr uint32_t PlantPlacementSalt = 0x9A7D3E21u;
        constexpr float RandomBlockOffsetHalfRange = 0.2f;

        int wrapBlockCoordinate(int coordinate)
        {
            int wrapped = coordinate % TerrainTilePeriod;
            if (wrapped < 0)
            {
                wrapped += TerrainTilePeriod;
            }
            return wrapped;
        }

        uint32_t worldRandomHash(int x, int y, int z, uint32_t salt)
        {
            uint32_t hash = static_cast<uint32_t>(x) * 0x8da6b343u;
            hash ^= static_cast<uint32_t>(y) * 0xd8163841u;
            hash ^= static_cast<uint32_t>(z) * 0xcb1ab31fu;
            hash ^= salt;
            hash ^= hash >> 16u;
            hash *= 0x7feb352du;
            hash ^= hash >> 15u;
            hash *= 0x846ca68bu;
            hash ^= hash >> 16u;
            return hash;
        }

        uint8_t worldRandom8(int x, int y, int z, uint32_t salt)
        {
            return static_cast<uint8_t>(worldRandomHash(wrapBlockCoordinate(x), y, wrapBlockCoordinate(z), salt) & 255u);
        }
    }

    TerrainGeometryBuilder::TerrainGeometryBuilder(
        const std::vector<BlockDefinition>& blockDefinitions,
        const std::vector<BlockTextureLayers>& blockTextureLayers,
        const std::unordered_map<uint16_t, assets::PropMesh>& propMeshesByBlock) :
        blockDefinitions_(blockDefinitions),
        blockTextureLayers_(blockTextureLayers),
        propMeshesByBlock_(propMeshesByBlock)
    {
    }

    TerrainSubchunkBuildData TerrainGeometryBuilder::buildSubchunkMesh(
        const std::shared_ptr<ChunkData>& chunk,
        int subchunkY,
        const world::TerrainMesher::BlockSampler& blockAt,
        const world::TerrainMesher::BlockStateSampler& blockStateAt,
        const world::TerrainMesher::LightSampler& lightAt) const
    {
        TerrainSubchunkBuildData result{};

        if (subchunkY < 0 || subchunkY >= SubchunksPerChunk || chunk->emptySubchunks[static_cast<size_t>(subchunkY)])
        {
            return result;
        }

        auto vertexAoIndex = [&](int worldX, int worldY, int worldZ, int nx, int ny, int nz, int ax, int ay, int az, int bx, int by, int bz) -> int
        {
            const int localX = worldX - chunk->chunkX * ChunkSizeX;
            const int localZ = worldZ - chunk->chunkZ * ChunkSizeZ;
            const bool sideA = blockContributesAo(blockAt(localX + nx + ax, worldY + ny + ay, localZ + nz + az));
            const bool sideB = blockContributesAo(blockAt(localX + nx + bx, worldY + ny + by, localZ + nz + bz));
            const bool corner = blockContributesAo(blockAt(localX + nx + ax + bx, worldY + ny + ay + by, localZ + nz + az + bz));
            return std::clamp(sideA && sideB ? 0 : 3 - static_cast<int>(sideA) - static_cast<int>(sideB) - static_cast<int>(corner), 0, 3);
        };

        auto vertexAoStrength = [&](int worldX, int worldY, int worldZ, int nx, int ny, int nz, int ax, int ay, int az, int bx, int by, int bz) -> float
        {
            constexpr std::array<float, 4> AoStrength = {0.55f, 0.68f, 0.82f, 1.0f};
            return AoStrength[static_cast<size_t>(vertexAoIndex(worldX, worldY, worldZ, nx, ny, nz, ax, ay, az, bx, by, bz))];
        };

        auto packAo = [](int a0, int a1, int a2, int a3) -> uint32_t
        {
            return 1u |
                (static_cast<uint32_t>(a0) << 1u) |
                (static_cast<uint32_t>(a1) << 3u) |
                (static_cast<uint32_t>(a2) << 5u) |
                (static_cast<uint32_t>(a3) << 7u);
        };

        auto faceAoMergeSignature = [&](int a0, int a1, int a2, int a3, int x, int y, int z, int face) -> uint32_t
        {
            const uint32_t signature = packAo(a0, a1, a2, a3);
            if (a0 == a1 && a0 == a2 && a0 == a3)
            {
                return signature;
            }

            return signature |
                (1u << 9u) |
                ((static_cast<uint32_t>(x) & 0x0fu) << 10u) |
                ((static_cast<uint32_t>(y) & 0x0fu) << 14u) |
                ((static_cast<uint32_t>(z) & 0x0fu) << 18u) |
                (static_cast<uint32_t>(face) << 22u);
        };

        auto faceAoSignature = [&](int x, int y, int z, int face) -> uint32_t
        {
            if (face == 0)
            {
                return faceAoMergeSignature(
                    vertexAoIndex(x, y, z, 0, 1, 0, -1, 0, 0, 0, 0, -1),
                    vertexAoIndex(x, y, z, 0, 1, 0, -1, 0, 0, 0, 0, 1),
                    vertexAoIndex(x, y, z, 0, 1, 0, 1, 0, 0, 0, 0, 1),
                    vertexAoIndex(x, y, z, 0, 1, 0, 1, 0, 0, 0, 0, -1),
                    x, y, z, face);
            }
            if (face == 1)
            {
                return faceAoMergeSignature(
                    vertexAoIndex(x, y, z, 0, -1, 0, -1, 0, 0, 0, 0, 1),
                    vertexAoIndex(x, y, z, 0, -1, 0, -1, 0, 0, 0, 0, -1),
                    vertexAoIndex(x, y, z, 0, -1, 0, 1, 0, 0, 0, 0, -1),
                    vertexAoIndex(x, y, z, 0, -1, 0, 1, 0, 0, 0, 0, 1),
                    x, y, z, face);
            }
            if (face == 2)
            {
                return faceAoMergeSignature(
                    vertexAoIndex(x, y, z, 1, 0, 0, 0, -1, 0, 0, 0, -1),
                    vertexAoIndex(x, y, z, 1, 0, 0, 0, 1, 0, 0, 0, -1),
                    vertexAoIndex(x, y, z, 1, 0, 0, 0, 1, 0, 0, 0, 1),
                    vertexAoIndex(x, y, z, 1, 0, 0, 0, -1, 0, 0, 0, 1),
                    x, y, z, face);
            }
            if (face == 3)
            {
                return faceAoMergeSignature(
                    vertexAoIndex(x, y, z, -1, 0, 0, 0, -1, 0, 0, 0, 1),
                    vertexAoIndex(x, y, z, -1, 0, 0, 0, 1, 0, 0, 0, 1),
                    vertexAoIndex(x, y, z, -1, 0, 0, 0, 1, 0, 0, 0, -1),
                    vertexAoIndex(x, y, z, -1, 0, 0, 0, -1, 0, 0, 0, -1),
                    x, y, z, face);
            }
            if (face == 4)
            {
                return faceAoMergeSignature(
                    vertexAoIndex(x, y, z, 0, 0, 1, 1, 0, 0, 0, -1, 0),
                    vertexAoIndex(x, y, z, 0, 0, 1, 1, 0, 0, 0, 1, 0),
                    vertexAoIndex(x, y, z, 0, 0, 1, -1, 0, 0, 0, 1, 0),
                    vertexAoIndex(x, y, z, 0, 0, 1, -1, 0, 0, 0, -1, 0),
                    x, y, z, face);
            }

            return faceAoMergeSignature(
                vertexAoIndex(x, y, z, 0, 0, -1, -1, 0, 0, 0, -1, 0),
                vertexAoIndex(x, y, z, 0, 0, -1, -1, 0, 0, 0, 1, 0),
                vertexAoIndex(x, y, z, 0, 0, -1, 1, 0, 0, 0, 1, 0),
                vertexAoIndex(x, y, z, 0, 0, -1, 1, 0, 0, 0, -1, 0),
                x, y, z, face);
        };

        auto topFaceRotation = [&](uint16_t block, int x, int y, int z) -> uint8_t
        {
            if (block == BlockAir || blockDefinition(block).directional)
            {
                return 0;
            }
            return static_cast<uint8_t>(worldRandom8(x, y, z, TopFaceRotationSalt) & 3u);
        };

        auto blockAlphaBlend = [&](uint16_t block) -> float
        {
            return std::clamp(blockDefinition(block).alphaBlend, 0.0f, 1.0f);
        };

        auto meshForBlock = [&](uint16_t block) -> TerrainBuildData&
        {
            return blockDefinition(block).alphaMode == BlockAlphaMode::Blend ? result.blend : result.solid;
        };

        auto randomBlockOffset = [&](uint16_t block, int x, int y, int z) -> std::array<float, 2>
        {
            if (!blockDefinition(block).randomOffset)
            {
                return {0.0f, 0.0f};
            }

            const auto offsetFromByte = [](uint8_t value)
            {
                return (static_cast<float>(value) / 255.0f) * (RandomBlockOffsetHalfRange * 2.0f) - RandomBlockOffsetHalfRange;
            };
            return {
                offsetFromByte(worldRandom8(x, y, z, PlantPlacementSalt)),
                offsetFromByte(worldRandom8(z, y, x, PlantPlacementSalt))
            };
        };

        auto rotateLocalXz = [](float localX, float localZ, uint8_t rotation) -> std::array<float, 2>
        {
            switch (rotation & 3u)
            {
            case 1: return {1.0f - localZ, localX};
            case 2: return {1.0f - localX, 1.0f - localZ};
            case 3: return {localZ, 1.0f - localX};
            default: return {localX, localZ};
            }
        };

        const int worldXStart = chunk->chunkX * ChunkSizeX;
        const int worldYStart = subchunkY * SubchunkSize;
        const int worldZStart = chunk->chunkZ * ChunkSizeZ;

        auto faceLight = [&](int worldX, int y, int worldZ, int face) -> uint8_t
        {
            const int localX = worldX - worldXStart;
            const int localZ = worldZ - worldZStart;
            if (face == 0)
            {
                return lightAt(localX, y + 1, localZ);
            }
            if (face == 1)
            {
                return lightAt(localX, y - 1, localZ);
            }
            if (face == 2)
            {
                return lightAt(localX + 1, y, localZ);
            }
            if (face == 3)
            {
                return lightAt(localX - 1, y, localZ);
            }
            if (face == 4)
            {
                return lightAt(localX, y, localZ + 1);
            }
            return lightAt(localX, y, localZ - 1);
        };

        auto appendFace = [&](TerrainBuildData& buildData, int x, int y, int z, int face, int width, int height, uint32_t textureLayer, uint8_t rotation, float mipDistanceScale, float alphaBlend, uint8_t packedLight)
        {
            const float x0 = static_cast<float>(x) - 0.5f;
            const float x1 = static_cast<float>(x + width) - 0.5f;
            const float y0 = static_cast<float>(y);
            const float y1 = static_cast<float>(y + height);
            const float z0 = static_cast<float>(z) - 0.5f;
            const float z1 = static_cast<float>(z + width) - 0.5f;
            const float uMax = static_cast<float>(width);
            const float vMax = static_cast<float>(height);

            std::array<TerrainVertex, 4> quad{};
            if (face == 0)
            {
                const float topX1 = static_cast<float>(x + width) - 0.5f;
                const float topZ1 = static_cast<float>(z + height) - 0.5f;
                quad = {{{x0, static_cast<float>(y + 1), z0, 0.0f, 0.0f}, {x0, static_cast<float>(y + 1), topZ1, vMax, 0.0f}, {topX1, static_cast<float>(y + 1), topZ1, vMax, uMax}, {topX1, static_cast<float>(y + 1), z0, 0.0f, uMax}}};
            }
            else if (face == 1)
            {
                const float bottomX1 = static_cast<float>(x + width) - 0.5f;
                const float bottomZ1 = static_cast<float>(z + height) - 0.5f;
                quad = {{{x0, y0, bottomZ1, 0.0f, 0.0f}, {x0, y0, z0, vMax, 0.0f}, {bottomX1, y0, z0, vMax, uMax}, {bottomX1, y0, bottomZ1, 0.0f, uMax}}};
            }
            else if (face == 2)
            {
                const float faceX = static_cast<float>(x) + 0.5f;
                quad = {{{faceX, y0, z0, 0.0f, 0.0f}, {faceX, y1, z0, vMax, 0.0f}, {faceX, y1, z1, vMax, uMax}, {faceX, y0, z1, 0.0f, uMax}}};
            }
            else if (face == 3)
            {
                const float faceX = static_cast<float>(x) - 0.5f;
                quad = {{{faceX, y0, z1, 0.0f, 0.0f}, {faceX, y1, z1, vMax, 0.0f}, {faceX, y1, z0, vMax, uMax}, {faceX, y0, z0, 0.0f, uMax}}};
            }
            else if (face == 4)
            {
                const float faceZ = static_cast<float>(z) + 0.5f;
                quad = {{{x1, y0, faceZ, 0.0f, 0.0f}, {x1, y1, faceZ, vMax, 0.0f}, {x0, y1, faceZ, vMax, uMax}, {x0, y0, faceZ, 0.0f, uMax}}};
            }
            else
            {
                const float faceZ = static_cast<float>(z) - 0.5f;
                quad = {{{x0, y0, faceZ, 0.0f, 0.0f}, {x0, y1, faceZ, vMax, 0.0f}, {x1, y1, faceZ, vMax, uMax}, {x1, y0, faceZ, 0.0f, uMax}}};
            }

            if (face >= 2)
            {
                for (TerrainVertex& vertex : quad)
                {
                    const float u = vertex.u;
                    const float v = vertex.v;
                    vertex.u = v;
                    vertex.v = vMax - u;
                }
            }
            else if (face == 0 && rotation != 0)
            {
                for (TerrainVertex& vertex : quad)
                {
                    const float u = vertex.u;
                    const float v = vertex.v;
                    if (rotation == 1)
                    {
                        vertex.u = v;
                        vertex.v = uMax - u;
                    }
                    else if (rotation == 2)
                    {
                        vertex.u = uMax - u;
                        vertex.v = vMax - v;
                    }
                    else
                    {
                        vertex.u = vMax - v;
                        vertex.v = u;
                    }
                }
            }

            if (face == 0)
            {
                quad[0].ao = vertexAoStrength(x, y, z, 0, 1, 0, -1, 0, 0, 0, 0, -1);
                quad[1].ao = vertexAoStrength(x, y, z + height - 1, 0, 1, 0, -1, 0, 0, 0, 0, 1);
                quad[2].ao = vertexAoStrength(x + width - 1, y, z + height - 1, 0, 1, 0, 1, 0, 0, 0, 0, 1);
                quad[3].ao = vertexAoStrength(x + width - 1, y, z, 0, 1, 0, 1, 0, 0, 0, 0, -1);
            }
            else if (face == 1)
            {
                quad[0].ao = vertexAoStrength(x, y, z + height - 1, 0, -1, 0, -1, 0, 0, 0, 0, 1);
                quad[1].ao = vertexAoStrength(x, y, z, 0, -1, 0, -1, 0, 0, 0, 0, -1);
                quad[2].ao = vertexAoStrength(x + width - 1, y, z, 0, -1, 0, 1, 0, 0, 0, 0, -1);
                quad[3].ao = vertexAoStrength(x + width - 1, y, z + height - 1, 0, -1, 0, 1, 0, 0, 0, 0, 1);
            }
            else if (face == 2)
            {
                quad[0].ao = vertexAoStrength(x, y, z, 1, 0, 0, 0, -1, 0, 0, 0, -1);
                quad[1].ao = vertexAoStrength(x, y + height - 1, z, 1, 0, 0, 0, 1, 0, 0, 0, -1);
                quad[2].ao = vertexAoStrength(x, y + height - 1, z + width - 1, 1, 0, 0, 0, 1, 0, 0, 0, 1);
                quad[3].ao = vertexAoStrength(x, y, z + width - 1, 1, 0, 0, 0, -1, 0, 0, 0, 1);
            }
            else if (face == 3)
            {
                quad[0].ao = vertexAoStrength(x, y, z + width - 1, -1, 0, 0, 0, -1, 0, 0, 0, 1);
                quad[1].ao = vertexAoStrength(x, y + height - 1, z + width - 1, -1, 0, 0, 0, 1, 0, 0, 0, 1);
                quad[2].ao = vertexAoStrength(x, y + height - 1, z, -1, 0, 0, 0, 1, 0, 0, 0, -1);
                quad[3].ao = vertexAoStrength(x, y, z, -1, 0, 0, 0, -1, 0, 0, 0, -1);
            }
            else if (face == 4)
            {
                quad[0].ao = vertexAoStrength(x + width - 1, y, z, 0, 0, 1, 1, 0, 0, 0, -1, 0);
                quad[1].ao = vertexAoStrength(x + width - 1, y + height - 1, z, 0, 0, 1, 1, 0, 0, 0, 1, 0);
                quad[2].ao = vertexAoStrength(x, y + height - 1, z, 0, 0, 1, -1, 0, 0, 0, 1, 0);
                quad[3].ao = vertexAoStrength(x, y, z, 0, 0, 1, -1, 0, 0, 0, -1, 0);
            }
            else
            {
                quad[0].ao = vertexAoStrength(x, y, z, 0, 0, -1, -1, 0, 0, 0, -1, 0);
                quad[1].ao = vertexAoStrength(x, y + height - 1, z, 0, 0, -1, -1, 0, 0, 0, 1, 0);
                quad[2].ao = vertexAoStrength(x + width - 1, y + height - 1, z, 0, 0, -1, 1, 0, 0, 0, 1, 0);
                quad[3].ao = vertexAoStrength(x + width - 1, y, z, 0, 0, -1, 1, 0, 0, 0, -1, 0);
            }

            for (TerrainVertex& vertex : quad)
            {
                vertex.textureLayer = static_cast<float>(textureLayer);
                vertex.mipDistanceScale = mipDistanceScale;
                vertex.alphaBlend = alphaBlend;
                vertex.packedLight = packedLight;
            }

            const uint32_t baseIndex = static_cast<uint32_t>(buildData.vertices.size());
            buildData.vertices.push_back(quad[0]);
            buildData.vertices.push_back(quad[1]);
            buildData.vertices.push_back(quad[2]);
            buildData.vertices.push_back(quad[3]);
            buildData.indices.push_back(baseIndex);
            buildData.indices.push_back(baseIndex + 1);
            buildData.indices.push_back(baseIndex + 2);
            buildData.indices.push_back(baseIndex);
            buildData.indices.push_back(baseIndex + 2);
            buildData.indices.push_back(baseIndex + 3);
        };

        auto appendCrossBlock = [&](TerrainBuildData& buildData, int x, int y, int z, uint16_t block, uint32_t textureLayer, float mipDistanceScale, float alphaBlend)
        {
            const std::array<float, 2> offset = randomBlockOffset(block, x, y, z);
            const float y0 = static_cast<float>(y);
            const float y1 = static_cast<float>(y + 1);
            const float originX = static_cast<float>(x) - 0.5f + offset[0];
            const float originZ = static_cast<float>(z) - 0.5f + offset[1];
            const uint8_t rotation = topFaceRotation(block, x, y, z);
            const uint8_t packedLight = lightAt(x - worldXStart, y, z - worldZStart);
            auto crossVertex = [&](float localX, float localY, float localZ, float u, float v)
            {
                const std::array<float, 2> rotated = rotateLocalXz(localX, localZ, rotation);
                TerrainVertex vertex{originX + rotated[0], localY, originZ + rotated[1], u, v, 1.0f};
                vertex.packedLight = packedLight;
                return vertex;
            };

            auto appendDoubleSidedQuad = [&](TerrainVertex a, TerrainVertex b, TerrainVertex c, TerrainVertex d)
            {
                a.textureLayer = static_cast<float>(textureLayer);
                b.textureLayer = static_cast<float>(textureLayer);
                c.textureLayer = static_cast<float>(textureLayer);
                d.textureLayer = static_cast<float>(textureLayer);
                a.mipDistanceScale = mipDistanceScale;
                b.mipDistanceScale = mipDistanceScale;
                c.mipDistanceScale = mipDistanceScale;
                d.mipDistanceScale = mipDistanceScale;
                a.alphaBlend = alphaBlend;
                b.alphaBlend = alphaBlend;
                c.alphaBlend = alphaBlend;
                d.alphaBlend = alphaBlend;

                const uint32_t baseIndex = static_cast<uint32_t>(buildData.vertices.size());
                buildData.vertices.push_back(a);
                buildData.vertices.push_back(b);
                buildData.vertices.push_back(c);
                buildData.vertices.push_back(d);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 1);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex + 3);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex + 1);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 3);
                buildData.indices.push_back(baseIndex + 2);
            };

            appendDoubleSidedQuad(
                crossVertex(0.0f, y0, 0.0f, 0.0f, 1.0f),
                crossVertex(0.0f, y1, 0.0f, 0.0f, 0.0f),
                crossVertex(1.0f, y1, 1.0f, 1.0f, 0.0f),
                crossVertex(1.0f, y0, 1.0f, 1.0f, 1.0f));
            appendDoubleSidedQuad(
                crossVertex(1.0f, y0, 0.0f, 0.0f, 1.0f),
                crossVertex(1.0f, y1, 0.0f, 0.0f, 0.0f),
                crossVertex(0.0f, y1, 1.0f, 1.0f, 0.0f),
                crossVertex(0.0f, y0, 1.0f, 1.0f, 1.0f));
        };

        auto appendFireBlock = [&](TerrainBuildData& buildData, int x, int y, int z, uint16_t block)
        {
            constexpr float Height = 1.0f;
            constexpr float HalfSize = 0.5f;
            constexpr float VerticalInset = 0.1f;
            constexpr float VerticalOffset = HalfSize - VerticalInset;
            constexpr float EdgeLean = Height * 0.57735026919f;

            const uint32_t textureLayer = blockFaceTextureLayer(block, 0);
            const float mipDistanceScale = blockDefinition(block).mipDistanceScale;
            const float alphaBlend = blockAlphaBlend(block);
            const float baseY = static_cast<float>(y);
            const float centerX = static_cast<float>(x);
            const float centerZ = static_cast<float>(z);
            const uint8_t sampledLight = lightAt(x - worldXStart, y, z - worldZStart);
            const uint8_t packedLight = world::packLight(world::skyLightFromPacked(sampledLight), world::MaxSkyLight);

            auto makeVertex = [&](float px, float py, float pz, float u, float v)
            {
                TerrainVertex vertex{px, py, pz, u, v, 1.0f};
                vertex.textureLayer = static_cast<float>(textureLayer);
                vertex.mipDistanceScale = mipDistanceScale;
                vertex.alphaBlend = alphaBlend;
                vertex.packedLight = packedLight;
                return vertex;
            };

            auto appendDoubleSidedQuad = [&](TerrainVertex a, TerrainVertex b, TerrainVertex c, TerrainVertex d)
            {
                const uint32_t baseIndex = static_cast<uint32_t>(buildData.vertices.size());
                buildData.vertices.push_back(a);
                buildData.vertices.push_back(b);
                buildData.vertices.push_back(c);
                buildData.vertices.push_back(d);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 1);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex + 3);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex + 1);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 3);
                buildData.indices.push_back(baseIndex + 2);
            };

            auto appendVerticalPlane = [&](float bottomCenterX, float bottomCenterZ, float topCenterX, float topCenterZ, float dirX, float dirZ, float width, float height)
            {
                const float halfWidth = width * 0.5f;
                appendDoubleSidedQuad(
                    makeVertex(bottomCenterX - dirX * halfWidth, baseY, bottomCenterZ - dirZ * halfWidth, 0.0f, 1.0f),
                    makeVertex(topCenterX - dirX * halfWidth, baseY + height, topCenterZ - dirZ * halfWidth, 0.0f, 0.0f),
                    makeVertex(topCenterX + dirX * halfWidth, baseY + height, topCenterZ + dirZ * halfWidth, 1.0f, 0.0f),
                    makeVertex(bottomCenterX + dirX * halfWidth, baseY, bottomCenterZ + dirZ * halfWidth, 1.0f, 1.0f));
            };

            constexpr float Diagonal = 0.70710678118f;
            appendVerticalPlane(centerX, centerZ, centerX, centerZ, Diagonal, Diagonal, 1.41421356237f, Height);
            appendVerticalPlane(centerX, centerZ, centerX, centerZ, Diagonal, -Diagonal, 1.41421356237f, Height);

            appendVerticalPlane(centerX, centerZ + HalfSize, centerX, centerZ + HalfSize - EdgeLean, 1.0f, 0.0f, 1.0f, Height);
            appendVerticalPlane(centerX, centerZ - HalfSize, centerX, centerZ - HalfSize + EdgeLean, 1.0f, 0.0f, 1.0f, Height);
            appendVerticalPlane(centerX + HalfSize, centerZ, centerX + HalfSize - EdgeLean, centerZ, 0.0f, 1.0f, 1.0f, Height);
            appendVerticalPlane(centerX - HalfSize, centerZ, centerX - HalfSize + EdgeLean, centerZ, 0.0f, 1.0f, 1.0f, Height);

            appendVerticalPlane(centerX, centerZ + VerticalOffset, centerX, centerZ + VerticalOffset, 1.0f, 0.0f, 1.0f, Height);
            appendVerticalPlane(centerX, centerZ - VerticalOffset, centerX, centerZ - VerticalOffset, 1.0f, 0.0f, 1.0f, Height);
            appendVerticalPlane(centerX + VerticalOffset, centerZ, centerX + VerticalOffset, centerZ, 0.0f, 1.0f, 1.0f, Height);
            appendVerticalPlane(centerX - VerticalOffset, centerZ, centerX - VerticalOffset, centerZ, 0.0f, 1.0f, 1.0f, Height);
        };

        auto appendSlabBlock = [&](TerrainBuildData& buildData, int x, int y, int z, uint16_t block, uint16_t blockState)
        {
            const world::block_visual::LocalAabb aabb = world::block_visual::slabWorldAabb(x, y, z, blockState);
            const world::block_visual::LocalAabb localAabb = world::block_visual::slabLocalAabb(blockState);
            const uint32_t topTextureLayer = blockFaceTextureLayer(block, 0);
            const uint32_t bottomTextureLayer = blockFaceTextureLayer(block, 1);
            const float mipDistanceScale = blockDefinition(block).mipDistanceScale;
            const float alphaBlend = blockAlphaBlend(block);

            auto appendQuad = [&](std::array<TerrainVertex, 4> quad, uint32_t textureLayer, uint8_t packedLight)
            {
                for (TerrainVertex& vertex : quad)
                {
                    vertex.ao = 1.0f;
                    vertex.textureLayer = static_cast<float>(textureLayer);
                    vertex.mipDistanceScale = mipDistanceScale;
                    vertex.alphaBlend = alphaBlend;
                    vertex.packedLight = packedLight;
                }

                const uint32_t baseIndex = static_cast<uint32_t>(buildData.vertices.size());
                buildData.vertices.push_back(quad[0]);
                buildData.vertices.push_back(quad[1]);
                buildData.vertices.push_back(quad[2]);
                buildData.vertices.push_back(quad[3]);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 1);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex + 3);
            };

            const float x0 = aabb.min.x;
            const float x1 = aabb.max.x;
            const float y0 = aabb.min.y;
            const float y1 = aabb.max.y;
            const float z0 = aabb.min.z;
            const float z1 = aabb.max.z;
            const float lx0 = localAabb.min.x;
            const float lx1 = localAabb.max.x;
            const float ly0 = localAabb.min.y;
            const float ly1 = localAabb.max.y;
            const float lz0 = localAabb.min.z;
            const float lz1 = localAabb.max.z;

            if (!(localAabb.max.y >= 1.0f && neighborCullsFace(block, blockAt(x - worldXStart, y + 1, z - worldZStart))))
            {
                appendQuad({{{x0, y1, z0, lz0, lx0}, {x0, y1, z1, lz1, lx0}, {x1, y1, z1, lz1, lx1}, {x1, y1, z0, lz0, lx1}}},
                    topTextureLayer,
                    faceLight(x, y, z, 0));
            }
            if (!(localAabb.min.y <= 0.0f && neighborCullsFace(block, blockAt(x - worldXStart, y - 1, z - worldZStart))))
            {
                appendQuad({{{x0, y0, z1, 1.0f - lz1, lx0}, {x0, y0, z0, 1.0f - lz0, lx0}, {x1, y0, z0, 1.0f - lz0, lx1}, {x1, y0, z1, 1.0f - lz1, lx1}}},
                    bottomTextureLayer,
                    faceLight(x, y, z, 1));
            }
            if (!(localAabb.max.x >= 1.0f && neighborCullsFace(block, blockAt(x - worldXStart + 1, y, z - worldZStart))))
            {
                appendQuad({{{x1, y0, z0, lz0, 1.0f - ly0}, {x1, y1, z0, lz0, 1.0f - ly1}, {x1, y1, z1, lz1, 1.0f - ly1}, {x1, y0, z1, lz1, 1.0f - ly0}}},
                    blockFaceTextureLayer(block, 2),
                    faceLight(x, y, z, 2));
            }
            if (!(localAabb.min.x <= 0.0f && neighborCullsFace(block, blockAt(x - worldXStart - 1, y, z - worldZStart))))
            {
                appendQuad({{{x0, y0, z1, 1.0f - lz1, 1.0f - ly0}, {x0, y1, z1, 1.0f - lz1, 1.0f - ly1}, {x0, y1, z0, 1.0f - lz0, 1.0f - ly1}, {x0, y0, z0, 1.0f - lz0, 1.0f - ly0}}},
                    blockFaceTextureLayer(block, 3),
                    faceLight(x, y, z, 3));
            }
            if (!(localAabb.max.z >= 1.0f && neighborCullsFace(block, blockAt(x - worldXStart, y, z - worldZStart + 1))))
            {
                appendQuad({{{x1, y0, z1, 1.0f - lx1, 1.0f - ly0}, {x1, y1, z1, 1.0f - lx1, 1.0f - ly1}, {x0, y1, z1, 1.0f - lx0, 1.0f - ly1}, {x0, y0, z1, 1.0f - lx0, 1.0f - ly0}}},
                    blockFaceTextureLayer(block, 4),
                    faceLight(x, y, z, 4));
            }
            if (!(localAabb.min.z <= 0.0f && neighborCullsFace(block, blockAt(x - worldXStart, y, z - worldZStart - 1))))
            {
                appendQuad({{{x0, y0, z0, lx0, 1.0f - ly0}, {x0, y1, z0, lx0, 1.0f - ly1}, {x1, y1, z0, lx1, 1.0f - ly1}, {x1, y0, z0, lx1, 1.0f - ly0}}},
                    blockFaceTextureLayer(block, 5),
                    faceLight(x, y, z, 5));
            }
        };

        auto appendPropBlock = [&](TerrainBuildData& buildData, int x, int y, int z, uint16_t block)
        {
            const auto meshIt = propMeshesByBlock_.find(block);
            if (meshIt == propMeshesByBlock_.end())
            {
                return;
            }

            const assets::PropMesh& mesh = meshIt->second;
            const uint32_t textureLayer = blockFaceTextureLayer(block, 0);
            const float mipDistanceScale = blockDefinition(block).mipDistanceScale;
            const float alphaBlend = blockAlphaBlend(block);
            const std::array<float, 2> offset = randomBlockOffset(block, x, y, z);
            const float originX = static_cast<float>(x) - 0.5f + offset[0];
            const float originY = static_cast<float>(y);
            const float originZ = static_cast<float>(z) - 0.5f + offset[1];
            const uint8_t rotation = topFaceRotation(block, x, y, z);
            const uint8_t packedLight = lightAt(x - worldXStart, y, z - worldZStart);

            for (size_t offset = 0; offset + assets::PropQuadRenderFloatCount <= mesh.quads.size(); offset += assets::PropQuadRenderFloatCount)
            {
                std::array<TerrainVertex, 4> quad{};
                size_t cursor = offset;
                for (TerrainVertex& vertex : quad)
                {
                    const float localX = mesh.quads[cursor++];
                    vertex.y = originY + mesh.quads[cursor++];
                    const float localZ = mesh.quads[cursor++];
                    const std::array<float, 2> rotated = rotateLocalXz(localX, localZ, rotation);
                    vertex.x = originX + rotated[0];
                    vertex.z = originZ + rotated[1];
                    vertex.ao = 1.0f;
                    vertex.textureLayer = static_cast<float>(textureLayer);
                    vertex.mipDistanceScale = mipDistanceScale;
                    vertex.alphaBlend = alphaBlend;
                    vertex.packedLight = packedLight;
                }
                for (TerrainVertex& vertex : quad)
                {
                    vertex.u = mesh.quads[cursor++];
                    vertex.v = mesh.quads[cursor++];
                }

                const uint32_t baseIndex = static_cast<uint32_t>(buildData.vertices.size());
                buildData.vertices.push_back(quad[0]);
                buildData.vertices.push_back(quad[1]);
                buildData.vertices.push_back(quad[2]);
                buildData.vertices.push_back(quad[3]);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 1);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex + 3);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex + 1);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 3);
                buildData.indices.push_back(baseIndex + 2);
            }
        };

        auto faceSignature = [&](uint16_t block, int x, int y, int z, int face) -> uint64_t
        {
            const uint32_t mipSignature = static_cast<uint32_t>(std::clamp(
                static_cast<int>(std::lround(blockDefinition(block).mipDistanceScale * 16.0f)),
                0,
                127));
            const uint32_t alphaSignature = static_cast<uint32_t>(std::clamp(
                static_cast<int>(std::lround(blockAlphaBlend(block) * 63.0f)),
                0,
                63));
            uint64_t signature = static_cast<uint64_t>(faceAoSignature(x, y, z, face)) |
                (static_cast<uint64_t>(mipSignature) << 25u) |
                ((static_cast<uint64_t>(blockFaceTextureLayer(block, face)) & 0xFFu) << 32u) |
                (static_cast<uint64_t>(alphaSignature) << 40u) |
                (static_cast<uint64_t>(blockDefinition(block).alphaMode == BlockAlphaMode::Blend ? 1u : 0u) << 46u) |
                (static_cast<uint64_t>(faceLight(x, y, z, face)) << 49u);
            if (face == 0)
            {
                signature |= static_cast<uint64_t>(topFaceRotation(block, x, y, z)) << 47u;
            }
            return signature;
        };

        auto emitGreedy = [](std::vector<uint64_t>& mask, int maskWidth, int maskHeight, auto emit)
        {
            for (int y = 0; y < maskHeight; ++y)
            {
                for (int x = 0; x < maskWidth;)
                {
                    if (mask[y * maskWidth + x] == 0)
                    {
                        ++x;
                        continue;
                    }

                    int width = 1;
                    const uint64_t signature = mask[y * maskWidth + x];
                    while (x + width < maskWidth && mask[y * maskWidth + x + width] == signature)
                    {
                        ++width;
                    }

                    int height = 1;
                    bool canGrow = true;
                    while (y + height < maskHeight && canGrow)
                    {
                        for (int offset = 0; offset < width; ++offset)
                        {
                            if (mask[(y + height) * maskWidth + x + offset] != signature)
                            {
                                canGrow = false;
                                break;
                            }
                        }
                        if (canGrow)
                        {
                            ++height;
                        }
                    }

                    for (int clearY = 0; clearY < height; ++clearY)
                    {
                        for (int clearX = 0; clearX < width; ++clearX)
                        {
                            mask[(y + clearY) * maskWidth + x + clearX] = 0;
                        }
                    }

                    emit(x, y, width, height);
                    x += width;
                }
            }
        };

        result.solid.vertices.reserve(256);
        result.solid.indices.reserve(384);
        result.blend.vertices.reserve(64);
        result.blend.indices.reserve(96);

        std::vector<uint64_t> mask(SubchunkSize * SubchunkSize);

        for (int localY = 0; localY < SubchunkSize; ++localY)
        {
            const int y = worldYStart + localY;
            std::fill(mask.begin(), mask.end(), 0);
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    mask[localZ * ChunkSizeX + localX] = blockUsesCubeMesh(block) && !neighborCullsFace(block, blockAt(localX, y + 1, localZ))
                        ? faceSignature(block, worldXStart + localX, y, worldZStart + localZ, 0)
                        : 0;
                }
            }
            emitGreedy(mask, ChunkSizeX, ChunkSizeZ, [&](int localX, int localZ, int width, int height)
            {
                const uint16_t block = blockAt(localX, y, localZ);
                const int worldX = worldXStart + localX;
                const int worldZ = worldZStart + localZ;
                appendFace(meshForBlock(block), worldX, y, worldZ, 0, width, height, blockFaceTextureLayer(block, 0), topFaceRotation(block, worldX, y, worldZ), blockDefinition(block).mipDistanceScale, blockAlphaBlend(block), faceLight(worldX, y, worldZ, 0));
            });

            std::fill(mask.begin(), mask.end(), 0);
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    mask[localZ * ChunkSizeX + localX] = blockUsesCubeMesh(block) && !neighborCullsFace(block, blockAt(localX, y - 1, localZ))
                        ? faceSignature(block, worldXStart + localX, y, worldZStart + localZ, 1)
                        : 0;
                }
            }
            emitGreedy(mask, ChunkSizeX, ChunkSizeZ, [&](int localX, int localZ, int width, int height)
            {
                const uint16_t block = blockAt(localX, y, localZ);
                const int worldX = worldXStart + localX;
                const int worldZ = worldZStart + localZ;
                appendFace(meshForBlock(block), worldX, y, worldZ, 1, width, height, blockFaceTextureLayer(block, 1), 0, blockDefinition(block).mipDistanceScale, blockAlphaBlend(block), faceLight(worldX, y, worldZ, 1));
            });
        }

        for (int localX = 0; localX < ChunkSizeX; ++localX)
        {
            const int worldX = worldXStart + localX;
            std::fill(mask.begin(), mask.end(), 0);
            for (int localY = 0; localY < SubchunkSize; ++localY)
            {
                const int y = worldYStart + localY;
                for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    mask[localY * ChunkSizeZ + localZ] = blockUsesCubeMesh(block) && !neighborCullsFace(block, blockAt(localX + 1, y, localZ))
                        ? faceSignature(block, worldX, y, worldZStart + localZ, 2)
                        : 0;
                }
            }
            emitGreedy(mask, ChunkSizeZ, SubchunkSize, [&](int localZ, int localY, int width, int height)
            {
                const uint16_t block = blockAt(localX, worldYStart + localY, localZ);
                const int y = worldYStart + localY;
                const int worldZ = worldZStart + localZ;
                appendFace(meshForBlock(block), worldX, y, worldZ, 2, width, height, blockFaceTextureLayer(block, 2), 0, blockDefinition(block).mipDistanceScale, blockAlphaBlend(block), faceLight(worldX, y, worldZ, 2));
            });

            std::fill(mask.begin(), mask.end(), 0);
            for (int localY = 0; localY < SubchunkSize; ++localY)
            {
                const int y = worldYStart + localY;
                for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    mask[localY * ChunkSizeZ + localZ] = blockUsesCubeMesh(block) && !neighborCullsFace(block, blockAt(localX - 1, y, localZ))
                        ? faceSignature(block, worldX, y, worldZStart + localZ, 3)
                        : 0;
                }
            }
            emitGreedy(mask, ChunkSizeZ, SubchunkSize, [&](int localZ, int localY, int width, int height)
            {
                const uint16_t block = blockAt(localX, worldYStart + localY, localZ);
                const int y = worldYStart + localY;
                const int worldZ = worldZStart + localZ;
                appendFace(meshForBlock(block), worldX, y, worldZ, 3, width, height, blockFaceTextureLayer(block, 3), 0, blockDefinition(block).mipDistanceScale, blockAlphaBlend(block), faceLight(worldX, y, worldZ, 3));
            });
        }

        for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
        {
            const int worldZ = worldZStart + localZ;
            std::fill(mask.begin(), mask.end(), 0);
            for (int localY = 0; localY < SubchunkSize; ++localY)
            {
                const int y = worldYStart + localY;
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    mask[localY * ChunkSizeX + localX] = blockUsesCubeMesh(block) && !neighborCullsFace(block, blockAt(localX, y, localZ + 1))
                        ? faceSignature(block, worldXStart + localX, y, worldZ, 4)
                        : 0;
                }
            }
            emitGreedy(mask, ChunkSizeX, SubchunkSize, [&](int localX, int localY, int width, int height)
            {
                const uint16_t block = blockAt(localX, worldYStart + localY, localZ);
                const int worldX = worldXStart + localX;
                const int y = worldYStart + localY;
                appendFace(meshForBlock(block), worldX, y, worldZ, 4, width, height, blockFaceTextureLayer(block, 4), 0, blockDefinition(block).mipDistanceScale, blockAlphaBlend(block), faceLight(worldX, y, worldZ, 4));
            });

            std::fill(mask.begin(), mask.end(), 0);
            for (int localY = 0; localY < SubchunkSize; ++localY)
            {
                const int y = worldYStart + localY;
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    mask[localY * ChunkSizeX + localX] = blockUsesCubeMesh(block) && !neighborCullsFace(block, blockAt(localX, y, localZ - 1))
                        ? faceSignature(block, worldXStart + localX, y, worldZ, 5)
                        : 0;
                }
            }
            emitGreedy(mask, ChunkSizeX, SubchunkSize, [&](int localX, int localY, int width, int height)
            {
                const uint16_t block = blockAt(localX, worldYStart + localY, localZ);
                const int worldX = worldXStart + localX;
                const int y = worldYStart + localY;
                appendFace(meshForBlock(block), worldX, y, worldZ, 5, width, height, blockFaceTextureLayer(block, 5), 0, blockDefinition(block).mipDistanceScale, blockAlphaBlend(block), faceLight(worldX, y, worldZ, 5));
            });
        }

        for (int localY = 0; localY < SubchunkSize; ++localY)
        {
            const int y = worldYStart + localY;
            for (int localZ = 0; localZ < ChunkSizeZ; ++localZ)
            {
                for (int localX = 0; localX < ChunkSizeX; ++localX)
                {
                    const uint16_t block = blockAt(localX, y, localZ);
                    if (blockDefinition(block).renderType == BlockRenderType::Cross)
                    {
                        appendCrossBlock(meshForBlock(block), worldXStart + localX, y, worldZStart + localZ, block, blockFaceTextureLayer(block, 0), blockDefinition(block).mipDistanceScale, blockAlphaBlend(block));
                    }
                    else if (blockDefinition(block).renderType == BlockRenderType::Prop)
                    {
                        appendPropBlock(meshForBlock(block), worldXStart + localX, y, worldZStart + localZ, block);
                    }
                    else if (blockDefinition(block).renderType == BlockRenderType::Fire)
                    {
                        appendFireBlock(meshForBlock(block), worldXStart + localX, y, worldZStart + localZ, block);
                    }
                    else if (blockDefinition(block).renderType == BlockRenderType::Slab)
                    {
                        appendSlabBlock(meshForBlock(block), worldXStart + localX, y, worldZStart + localZ, block, blockStateAt(localX, y, localZ));
                    }
                }
            }
        }

        return result;
    }


    const BlockDefinition& TerrainGeometryBuilder::blockDefinition(uint16_t block) const
    {
        static const BlockDefinition fallback{};
        if (static_cast<size_t>(block) >= blockDefinitions_.size())
        {
            return fallback;
        }
        return blockDefinitions_[block];
    }

    uint32_t TerrainGeometryBuilder::blockFaceTextureLayer(uint16_t block, int face) const
    {
        if (face < 0 || face >= 6 || static_cast<size_t>(block) >= blockTextureLayers_.size())
        {
            return 0;
        }
        return blockTextureLayers_[block].faces[static_cast<size_t>(face)];
    }

    bool TerrainGeometryBuilder::blockUsesCubeMesh(uint16_t block) const
    {
        const BlockDefinition& definition = blockDefinition(block);
        return definition.renderType == BlockRenderType::Cube;
    }

    bool TerrainGeometryBuilder::blockContributesAo(uint16_t block) const
    {
        return blockDefinition(block).ao;
    }

    bool TerrainGeometryBuilder::neighborCullsFace(uint16_t block, uint16_t neighbor) const
    {
        if (neighbor == BlockAir)
        {
            return false;
        }

        const BlockDefinition& neighborDefinition = blockDefinition(neighbor);
        if (block == neighbor && neighborDefinition.sameBlockFaceCulling)
        {
            return true;
        }

        return neighborDefinition.faceOcclusion == BlockFaceOcclusion::Opaque;
    }
}
