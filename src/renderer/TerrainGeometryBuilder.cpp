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
        const std::unordered_map<uint16_t, assets::PropMesh>& propMeshesByBlock,
        const std::unordered_map<uint16_t, DroppedItemRenderPath::ItemSpriteMesh>& moldMeshesByBlock) :
        blockDefinitions_(blockDefinitions),
        blockTextureLayers_(blockTextureLayers),
        propMeshesByBlock_(propMeshesByBlock),
        moldMeshesByBlock_(moldMeshesByBlock)
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

        auto blockWavingType = [&](uint16_t block) -> uint8_t
        {
            return static_cast<uint8_t>(blockDefinition(block).waving);
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

        auto appendFace = [&](TerrainBuildData& buildData, int x, int y, int z, int face, int width, int height, uint32_t textureLayer, uint8_t rotation, float mipDistanceScale, float alphaBlend, uint8_t packedLight, uint8_t wavingType)
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
                vertex.wavingType = wavingType;
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

        auto appendModelQuad = [&](
            TerrainBuildData& buildData,
            std::array<TerrainVertex, 4> quad,
            uint32_t textureLayer,
            float mipDistanceScale,
            float alphaBlend,
            uint8_t packedLight)
        {
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

        auto appendCuboid = [&](
            TerrainBuildData& buildData,
            int x,
            int y,
            int z,
            uint16_t block,
            float minX,
            float minY,
            float minZ,
            float maxX,
            float maxY,
            float maxZ)
        {
            const uint32_t textureLayer = blockFaceTextureLayer(block, 0);
            const float mipDistanceScale = blockDefinition(block).mipDistanceScale;
            const float alphaBlend = blockAlphaBlend(block);
            const float width = maxX - minX;
            const float height = maxY - minY;
            const float depth = maxZ - minZ;
            const float worldMinX = static_cast<float>(x) - 0.5f + minX;
            const float worldMaxX = static_cast<float>(x) - 0.5f + maxX;
            const float worldMinY = static_cast<float>(y) + minY;
            const float worldMaxY = static_cast<float>(y) + maxY;
            const float worldMinZ = static_cast<float>(z) - 0.5f + minZ;
            const float worldMaxZ = static_cast<float>(z) - 0.5f + maxZ;
            const auto setAo = [](TerrainVertex& vertex)
            {
                vertex.ao = 1.0f;
            };
            auto cuboidFaceLight = [&](int face) -> uint8_t
            {
                if ((face == 0 && maxY >= 1.0f) ||
                    (face == 1 && minY <= 0.0f) ||
                    (face == 2 && maxX >= 1.0f) ||
                    (face == 3 && minX <= 0.0f) ||
                    (face == 4 && maxZ >= 1.0f) ||
                    (face == 5 && minZ <= 0.0f))
                {
                    return faceLight(x, y, z, face);
                }

                return lightAt(x - worldXStart, y, z - worldZStart);
            };
            auto quad = [&](std::array<TerrainVertex, 4> vertices, int face)
            {
                for (TerrainVertex& vertex : vertices)
                {
                    setAo(vertex);
                }
                appendModelQuad(
                    buildData,
                    vertices,
                    textureLayer,
                    mipDistanceScale,
                    alphaBlend,
                    cuboidFaceLight(face));
            };
            quad({{
                TerrainVertex{worldMinX, worldMaxY, worldMinZ, 0.0f, 0.0f},
                TerrainVertex{worldMinX, worldMaxY, worldMaxZ, 0.0f, depth},
                TerrainVertex{worldMaxX, worldMaxY, worldMaxZ, width, depth},
                TerrainVertex{worldMaxX, worldMaxY, worldMinZ, width, 0.0f}
            }}, 0);
            quad({{
                TerrainVertex{worldMinX, worldMinY, worldMaxZ, 0.0f, depth},
                TerrainVertex{worldMinX, worldMinY, worldMinZ, 0.0f, 0.0f},
                TerrainVertex{worldMaxX, worldMinY, worldMinZ, width, 0.0f},
                TerrainVertex{worldMaxX, worldMinY, worldMaxZ, width, depth}
            }}, 1);
            quad({{
                TerrainVertex{worldMaxX, worldMinY, worldMinZ, 0.0f, 1.0f},
                TerrainVertex{worldMaxX, worldMaxY, worldMinZ, 0.0f, 1.0f - height},
                TerrainVertex{worldMaxX, worldMaxY, worldMaxZ, depth, 1.0f - height},
                TerrainVertex{worldMaxX, worldMinY, worldMaxZ, depth, 1.0f}
            }}, 2);
            quad({{
                TerrainVertex{worldMinX, worldMinY, worldMaxZ, 0.0f, 1.0f},
                TerrainVertex{worldMinX, worldMaxY, worldMaxZ, 0.0f, 1.0f - height},
                TerrainVertex{worldMinX, worldMaxY, worldMinZ, depth, 1.0f - height},
                TerrainVertex{worldMinX, worldMinY, worldMinZ, depth, 1.0f}
            }}, 3);
            quad({{
                TerrainVertex{worldMaxX, worldMinY, worldMaxZ, 0.0f, 1.0f},
                TerrainVertex{worldMaxX, worldMaxY, worldMaxZ, 0.0f, 1.0f - height},
                TerrainVertex{worldMinX, worldMaxY, worldMaxZ, width, 1.0f - height},
                TerrainVertex{worldMinX, worldMinY, worldMaxZ, width, 1.0f}
            }}, 4);
            quad({{
                TerrainVertex{worldMinX, worldMinY, worldMinZ, 0.0f, 1.0f},
                TerrainVertex{worldMinX, worldMaxY, worldMinZ, 0.0f, 1.0f - height},
                TerrainVertex{worldMaxX, worldMaxY, worldMinZ, width, 1.0f - height},
                TerrainVertex{worldMaxX, worldMinY, worldMinZ, width, 1.0f}
            }}, 5);
        };

        auto appendCrucibleBlock = [&](TerrainBuildData& buildData, int x, int y, int z, uint16_t block)
        {
            appendCuboid(buildData, x, y, z, block, 0.0f, 0.0f, 0.0f, 1.0f, 0.2f, 1.0f);
            appendCuboid(buildData, x, y, z, block, 0.0f, 0.2f, 0.0f, 1.0f, 1.0f, 0.2f);
            appendCuboid(buildData, x, y, z, block, 0.0f, 0.2f, 0.8f, 1.0f, 1.0f, 1.0f);
            appendCuboid(buildData, x, y, z, block, 0.0f, 0.2f, 0.2f, 0.2f, 1.0f, 0.8f);
            appendCuboid(buildData, x, y, z, block, 0.8f, 0.2f, 0.2f, 1.0f, 1.0f, 0.8f);
        };

        auto appendMoldBlock = [&](TerrainBuildData& buildData, int x, int y, int z, uint16_t block)
        {
            const auto meshIt = moldMeshesByBlock_.find(block);
            if (meshIt == moldMeshesByBlock_.end())
            {
                return;
            }

            const DroppedItemRenderPath::ItemSpriteMesh& mesh = meshIt->second;
            const float mipDistanceScale = blockDefinition(block).mipDistanceScale;
            const float alphaBlend = blockAlphaBlend(block);
            const uint8_t packedLight = lightAt(x - worldXStart, y, z - worldZStart);

            for (const DroppedItemRenderPath::ItemSpriteQuad& sourceQuad : mesh.quads)
            {
                std::array<TerrainVertex, 4> quad{};
                for (size_t vertexIndex = 0; vertexIndex < quad.size(); ++vertexIndex)
                {
                    const Vec3& position = sourceQuad.positions[vertexIndex];
                    TerrainVertex& vertex = quad[vertexIndex];
                    vertex.x = static_cast<float>(x) + position.x;
                    vertex.y = static_cast<float>(y) + position.y;
                    vertex.z = static_cast<float>(z) + position.z;
                    vertex.u = sourceQuad.uvs[vertexIndex][0];
                    vertex.v = sourceQuad.uvs[vertexIndex][1];
                    vertex.ao = sourceQuad.ao;
                    vertex.textureLayer = sourceQuad.textureLayer >= 0.0f ? sourceQuad.textureLayer : static_cast<float>(blockFaceTextureLayer(block, 0));
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
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex + 1);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 3);
                buildData.indices.push_back(baseIndex + 2);
            }
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
                a.wavingType = blockWavingType(block);
                b.wavingType = blockWavingType(block);
                c.wavingType = blockWavingType(block);
                d.wavingType = blockWavingType(block);

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
            const float mipDistanceScale = blockDefinition(block).mipDistanceScale;
            const float alphaBlend = blockAlphaBlend(block);

            struct SlabVertex
            {
                Vec3 local{};
                float u = 0.0f;
                float v = 0.0f;
            };

            auto transformLocal = [&](Vec3 local)
            {
                switch (world::block_visual::attachState(blockState))
                {
                case BlockAttachState::Top:
                    return Vec3{local.x, local.y + 0.5f, local.z};
                case BlockAttachState::North:
                    return Vec3{local.x, 1.0f - local.z, local.y};
                case BlockAttachState::South:
                    return Vec3{local.x, local.z, 1.0f - local.y};
                case BlockAttachState::West:
                    return Vec3{local.y, local.z, local.x};
                case BlockAttachState::East:
                    return Vec3{1.0f - local.y, local.z, 1.0f - local.x};
                case BlockAttachState::Bottom:
                default:
                    return local;
                }
            };

            auto normalFace = [](const std::array<SlabVertex, 4>& quad)
            {
                const Vec3 a = quad[0].local;
                const Vec3 b = quad[1].local;
                const Vec3 c = quad[2].local;
                const float ux = b.x - a.x;
                const float uy = b.y - a.y;
                const float uz = b.z - a.z;
                const float vx = c.x - a.x;
                const float vy = c.y - a.y;
                const float vz = c.z - a.z;
                const float nx = uy * vz - uz * vy;
                const float ny = uz * vx - ux * vz;
                const float nz = ux * vy - uy * vx;
                if (std::abs(ny) >= std::abs(nx) && std::abs(ny) >= std::abs(nz))
                {
                    return ny >= 0.0f ? 0 : 1;
                }
                if (std::abs(nx) >= std::abs(nz))
                {
                    return nx >= 0.0f ? 2 : 3;
                }
                return nz >= 0.0f ? 4 : 5;
            };

            auto onBoundary = [](const std::array<SlabVertex, 4>& quad, int face)
            {
                constexpr float Epsilon = 0.0001f;
                auto nearValue = [=](float value, float target)
                {
                    return std::abs(value - target) <= Epsilon;
                };

                for (const SlabVertex& vertex : quad)
                {
                    if ((face == 0 && !nearValue(vertex.local.y, 1.0f)) ||
                        (face == 1 && !nearValue(vertex.local.y, 0.0f)) ||
                        (face == 2 && !nearValue(vertex.local.x, 1.0f)) ||
                        (face == 3 && !nearValue(vertex.local.x, 0.0f)) ||
                        (face == 4 && !nearValue(vertex.local.z, 1.0f)) ||
                        (face == 5 && !nearValue(vertex.local.z, 0.0f)))
                    {
                        return false;
                    }
                }
                return true;
            };

            auto neighborForFace = [&](int face)
            {
                const int localX = x - worldXStart;
                const int localZ = z - worldZStart;
                switch (face)
                {
                case 0: return blockAt(localX, y + 1, localZ);
                case 1: return blockAt(localX, y - 1, localZ);
                case 2: return blockAt(localX + 1, y, localZ);
                case 3: return blockAt(localX - 1, y, localZ);
                case 4: return blockAt(localX, y, localZ + 1);
                case 5: return blockAt(localX, y, localZ - 1);
                default: return BlockAir;
                }
            };

            auto appendQuad = [&](std::array<SlabVertex, 4> quad, uint32_t textureLayer)
            {
                for (SlabVertex& vertex : quad)
                {
                    vertex.local = transformLocal(vertex.local);
                }

                const int face = normalFace(quad);
                if (onBoundary(quad, face) && neighborCullsFace(block, neighborForFace(face)))
                {
                    return;
                }

                std::array<TerrainVertex, 4> terrainQuad{};
                for (size_t i = 0; i < quad.size(); ++i)
                {
                    TerrainVertex& vertex = terrainQuad[i];
                    vertex.x = static_cast<float>(x) - 0.5f + quad[i].local.x;
                    vertex.y = static_cast<float>(y) + quad[i].local.y;
                    vertex.z = static_cast<float>(z) - 0.5f + quad[i].local.z;
                    vertex.u = quad[i].u;
                    vertex.v = quad[i].v;
                    vertex.ao = 1.0f;
                    vertex.textureLayer = static_cast<float>(textureLayer);
                    vertex.mipDistanceScale = mipDistanceScale;
                    vertex.alphaBlend = alphaBlend;
                    vertex.packedLight = faceLight(x, y, z, face);
                }

                const uint32_t baseIndex = static_cast<uint32_t>(buildData.vertices.size());
                buildData.vertices.push_back(terrainQuad[0]);
                buildData.vertices.push_back(terrainQuad[1]);
                buildData.vertices.push_back(terrainQuad[2]);
                buildData.vertices.push_back(terrainQuad[3]);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 1);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex);
                buildData.indices.push_back(baseIndex + 2);
                buildData.indices.push_back(baseIndex + 3);
            };

            constexpr float TopY = 0.5f;
            constexpr float SideTopV = 0.5f;
            constexpr float SideBottomV = 1.0f;
            appendQuad({{
                    SlabVertex{Vec3{0.0f, TopY, 0.0f}, 0.0f, 0.0f},
                    SlabVertex{Vec3{0.0f, TopY, 1.0f}, 1.0f, 0.0f},
                    SlabVertex{Vec3{1.0f, TopY, 1.0f}, 1.0f, 1.0f},
                    SlabVertex{Vec3{1.0f, TopY, 0.0f}, 0.0f, 1.0f}
                }},
                blockFaceTextureLayer(block, 0));
            appendQuad({{
                    SlabVertex{Vec3{0.0f, 0.0f, 1.0f}, 0.0f, 0.0f},
                    SlabVertex{Vec3{0.0f, 0.0f, 0.0f}, 1.0f, 0.0f},
                    SlabVertex{Vec3{1.0f, 0.0f, 0.0f}, 1.0f, 1.0f},
                    SlabVertex{Vec3{1.0f, 0.0f, 1.0f}, 0.0f, 1.0f}
                }},
                blockFaceTextureLayer(block, 1));
            appendQuad({{
                    SlabVertex{Vec3{1.0f, 0.0f, 0.0f}, 0.0f, SideBottomV},
                    SlabVertex{Vec3{1.0f, TopY, 0.0f}, 0.0f, SideTopV},
                    SlabVertex{Vec3{1.0f, TopY, 1.0f}, 1.0f, SideTopV},
                    SlabVertex{Vec3{1.0f, 0.0f, 1.0f}, 1.0f, SideBottomV}
                }},
                blockFaceTextureLayer(block, 2));
            appendQuad({{
                    SlabVertex{Vec3{0.0f, 0.0f, 1.0f}, 0.0f, SideBottomV},
                    SlabVertex{Vec3{0.0f, TopY, 1.0f}, 0.0f, SideTopV},
                    SlabVertex{Vec3{0.0f, TopY, 0.0f}, 1.0f, SideTopV},
                    SlabVertex{Vec3{0.0f, 0.0f, 0.0f}, 1.0f, SideBottomV}
                }},
                blockFaceTextureLayer(block, 3));
            appendQuad({{
                    SlabVertex{Vec3{1.0f, 0.0f, 1.0f}, 0.0f, SideBottomV},
                    SlabVertex{Vec3{1.0f, TopY, 1.0f}, 0.0f, SideTopV},
                    SlabVertex{Vec3{0.0f, TopY, 1.0f}, 1.0f, SideTopV},
                    SlabVertex{Vec3{0.0f, 0.0f, 1.0f}, 1.0f, SideBottomV}
                }},
                blockFaceTextureLayer(block, 4));
            appendQuad({{
                    SlabVertex{Vec3{0.0f, 0.0f, 0.0f}, 0.0f, SideBottomV},
                    SlabVertex{Vec3{0.0f, TopY, 0.0f}, 0.0f, SideTopV},
                    SlabVertex{Vec3{1.0f, TopY, 0.0f}, 1.0f, SideTopV},
                    SlabVertex{Vec3{1.0f, 0.0f, 0.0f}, 1.0f, SideBottomV}
                }},
                blockFaceTextureLayer(block, 5));
        };

        auto appendHalfSlabBlock = [&](TerrainBuildData& buildData, int x, int y, int z, uint16_t block, uint16_t blockState)
        {
            const float mipDistanceScale = blockDefinition(block).mipDistanceScale;
            const float alphaBlend = blockAlphaBlend(block);
            const world::block_visual::AttachFaceBasis basis = world::block_visual::attachFaceBasis(
                world::block_visual::attachGridFace(blockState));
            const int grid = world::block_visual::attachGridCommand(blockState);
            const bool corner = grid == 1 || grid == 3 || grid == 7 || grid == 9;

            enum class HalfSlabAxisSemantic
            {
                Height,
                Cut,
                Long
            };

            struct ModelAxisRange
            {
                world::block_visual::LocalAxis axis{};
                float min = 0.0f;
                float max = 1.0f;
                float materialMin = 0.0f;
                float materialMax = 1.0f;
                HalfSlabAxisSemantic semantic = HalfSlabAxisSemantic::Long;
            };

            auto faceForAxis = [](world::block_visual::LocalAxis axis, int semanticSign)
            {
                const int sign = axis.sign * semanticSign;
                if (axis.axis == 0)
                {
                    return sign >= 0 ? 2 : 3;
                }
                if (axis.axis == 1)
                {
                    return sign >= 0 ? 0 : 1;
                }
                return sign >= 0 ? 4 : 5;
            };

            auto axisValue = [](world::block_visual::LocalAxis axis, float value)
            {
                return axis.sign >= 0 ? value : 1.0f - value;
            };

            auto setAxisValue = [](Vec3& point, int axis, float value)
            {
                if (axis == 0)
                {
                    point.x = value;
                }
                else if (axis == 1)
                {
                    point.y = value;
                }
                else
                {
                    point.z = value;
                }
            };

            ModelAxisRange axisA{};
            ModelAxisRange axisB{};
            ModelAxisRange axisL{};
            if (corner)
            {
                const float uMin = (grid == 3 || grid == 9) ? 0.5f : 0.0f;
                const float vMin = (grid == 7 || grid == 9) ? 0.5f : 0.0f;
                axisA = ModelAxisRange{basis.u, uMin, uMin + 0.5f, uMin, uMin + 0.5f, HalfSlabAxisSemantic::Cut};
                axisB = ModelAxisRange{basis.v, vMin, vMin + 0.5f, 0.0f, 0.5f, HalfSlabAxisSemantic::Height};
                axisL = ModelAxisRange{basis.normal, 0.0f, 1.0f, 0.0f, 1.0f, HalfSlabAxisSemantic::Long};
            }
            else if (grid == 2 || grid == 8)
            {
                const float vMin = grid == 8 ? 0.5f : 0.0f;
                axisA = ModelAxisRange{basis.v, vMin, vMin + 0.5f, vMin, vMin + 0.5f, HalfSlabAxisSemantic::Cut};
                axisB = ModelAxisRange{basis.normal, 0.0f, 0.5f, 0.0f, 0.5f, HalfSlabAxisSemantic::Height};
                axisL = ModelAxisRange{basis.u, 0.0f, 1.0f, 0.0f, 1.0f, HalfSlabAxisSemantic::Long};
            }
            else
            {
                const float uMin = grid == 6 ? 0.5f : 0.0f;
                axisA = ModelAxisRange{basis.u, uMin, uMin + 0.5f, uMin, uMin + 0.5f, HalfSlabAxisSemantic::Cut};
                axisB = ModelAxisRange{basis.normal, 0.0f, 0.5f, 0.0f, 0.5f, HalfSlabAxisSemantic::Height};
                axisL = ModelAxisRange{basis.v, 0.0f, 1.0f, 0.0f, 1.0f, HalfSlabAxisSemantic::Long};
            }

            auto point = [&](float a, float b, float length)
            {
                Vec3 result{};
                setAxisValue(result, axisA.axis.axis, axisValue(axisA.axis, a));
                setAxisValue(result, axisB.axis.axis, axisValue(axisB.axis, b));
                setAxisValue(result, axisL.axis.axis, axisValue(axisL.axis, length));
                return result;
            };

            auto normalized = [](float value, float minValue, float maxValue)
            {
                const float extent = std::max(0.0001f, maxValue - minValue);
                return std::clamp((value - minValue) / extent, 0.0f, 1.0f);
            };

            auto modelVertex = [&](float a, float b, float length, float u, float v)
            {
                TerrainVertex vertex{};
                const Vec3 local = point(a, b, length);
                vertex.x = static_cast<float>(x) - 0.5f + local.x;
                vertex.y = static_cast<float>(y) + local.y;
                vertex.z = static_cast<float>(z) - 0.5f + local.z;
                vertex.u = u;
                vertex.v = v;
                vertex.ao = 1.0f;
                vertex.mipDistanceScale = mipDistanceScale;
                vertex.alphaBlend = alphaBlend;
                return vertex;
            };

            auto normalFace = [](const std::array<TerrainVertex, 4>& quad)
            {
                const Vec3 a{quad[0].x, quad[0].y, quad[0].z};
                const Vec3 b{quad[1].x, quad[1].y, quad[1].z};
                const Vec3 c{quad[2].x, quad[2].y, quad[2].z};
                const float ux = b.x - a.x;
                const float uy = b.y - a.y;
                const float uz = b.z - a.z;
                const float vx = c.x - a.x;
                const float vy = c.y - a.y;
                const float vz = c.z - a.z;
                const float nx = uy * vz - uz * vy;
                const float ny = uz * vx - ux * vz;
                const float nz = ux * vy - uy * vx;
                if (std::abs(ny) >= std::abs(nx) && std::abs(ny) >= std::abs(nz))
                {
                    return ny >= 0.0f ? 0 : 1;
                }
                if (std::abs(nx) >= std::abs(nz))
                {
                    return nx >= 0.0f ? 2 : 3;
                }
                return nz >= 0.0f ? 4 : 5;
            };

            auto onBoundary = [&](const std::array<TerrainVertex, 4>& quad, int face)
            {
                constexpr float Epsilon = 0.0001f;
                for (const TerrainVertex& vertex : quad)
                {
                    const float localX = vertex.x - static_cast<float>(x) + 0.5f;
                    const float localY = vertex.y - static_cast<float>(y);
                    const float localZ = vertex.z - static_cast<float>(z) + 0.5f;
                    if ((face == 0 && std::abs(localY - 1.0f) > Epsilon) ||
                        (face == 1 && std::abs(localY) > Epsilon) ||
                        (face == 2 && std::abs(localX - 1.0f) > Epsilon) ||
                        (face == 3 && std::abs(localX) > Epsilon) ||
                        (face == 4 && std::abs(localZ - 1.0f) > Epsilon) ||
                        (face == 5 && std::abs(localZ) > Epsilon))
                    {
                        return false;
                    }
                }
                return true;
            };

            auto neighborForFace = [&](int face)
            {
                const int localX = x - worldXStart;
                const int localZ = z - worldZStart;
                switch (face)
                {
                case 0: return blockAt(localX, y + 1, localZ);
                case 1: return blockAt(localX, y - 1, localZ);
                case 2: return blockAt(localX + 1, y, localZ);
                case 3: return blockAt(localX - 1, y, localZ);
                case 4: return blockAt(localX, y, localZ + 1);
                case 5: return blockAt(localX, y, localZ - 1);
                default: return BlockAir;
                }
            };

            auto appendQuad = [&](std::array<TerrainVertex, 4> quad, int desiredFace, uint32_t textureLayer)
            {
                if (normalFace(quad) != desiredFace)
                {
                    std::swap(quad[1], quad[3]);
                }
                if (onBoundary(quad, desiredFace) && neighborCullsFace(block, neighborForFace(desiredFace)))
                {
                    return;
                }
                for (TerrainVertex& vertex : quad)
                {
                    vertex.textureLayer = static_cast<float>(textureLayer);
                    vertex.packedLight = faceLight(x, y, z, desiredFace);
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

            const uint32_t topBottomLayer = blockFaceTextureLayer(block, 0);
            const uint32_t sideLayer = blockFaceTextureLayer(block, 4);
            const uint32_t sectionLayer = static_cast<size_t>(block) < blockTextureLayers_.size()
                ? blockTextureLayers_[block].verticalSection
                : sideLayer;
            const auto isSectionFace = [](const ModelAxisRange& range, bool minFace)
            {
                return range.semantic == HalfSlabAxisSemantic::Cut &&
                    ((minFace && range.min > 0.0f) || (!minFace && range.max < 1.0f));
            };
            const auto layerForFace = [&](const ModelAxisRange& range, bool minFace)
            {
                if (range.semantic == HalfSlabAxisSemantic::Height)
                {
                    return topBottomLayer;
                }
                return isSectionFace(range, minFace) ? sectionLayer : sideLayer;
            };

            const float a0 = axisA.min;
            const float a1 = axisA.max;
            const float b0 = axisB.min;
            const float b1 = axisB.max;
            const float l0 = axisL.min;
            const float l1 = axisL.max;
            const auto materialValue = [&](const ModelAxisRange& range, float value)
            {
                return range.materialMin + normalized(value, range.min, range.max) * (range.materialMax - range.materialMin);
            };
            const auto semanticValue = [&](HalfSlabAxisSemantic semantic, float a, float b, float length)
            {
                if (axisA.semantic == semantic)
                {
                    return materialValue(axisA, a);
                }
                if (axisB.semantic == semantic)
                {
                    return materialValue(axisB, b);
                }
                return materialValue(axisL, length);
            };
            const auto faceUv = [&](const ModelAxisRange& faceRange, bool minFace, float a, float b, float length)
            {
                const float heightValue = semanticValue(HalfSlabAxisSemantic::Height, a, b, length);
                const float cutValue = semanticValue(HalfSlabAxisSemantic::Cut, a, b, length);
                const float longValue = semanticValue(HalfSlabAxisSemantic::Long, a, b, length);
                if (faceRange.semantic == HalfSlabAxisSemantic::Height)
                {
                    return std::array<float, 2>{cutValue, longValue};
                }
                if (isSectionFace(faceRange, minFace))
                {
                    return std::array<float, 2>{longValue, 1.0f - normalized(heightValue, 0.0f, 0.5f)};
                }
                if (faceRange.semantic == HalfSlabAxisSemantic::Cut)
                {
                    return std::array<float, 2>{longValue, 1.0f - heightValue};
                }
                return std::array<float, 2>{cutValue, 1.0f - heightValue};
            };

            const std::array<float, 2> longMinUv0 = faceUv(axisL, true, a0, b0, l0);
            const std::array<float, 2> longMinUv1 = faceUv(axisL, true, a1, b0, l0);
            const std::array<float, 2> longMinUv2 = faceUv(axisL, true, a1, b1, l0);
            const std::array<float, 2> longMinUv3 = faceUv(axisL, true, a0, b1, l0);
            appendQuad({{
                    modelVertex(a0, b0, l0, longMinUv0[0], longMinUv0[1]),
                    modelVertex(a1, b0, l0, longMinUv1[0], longMinUv1[1]),
                    modelVertex(a1, b1, l0, longMinUv2[0], longMinUv2[1]),
                    modelVertex(a0, b1, l0, longMinUv3[0], longMinUv3[1])
                }},
                faceForAxis(axisL.axis, -1),
                layerForFace(axisL, true));
            const std::array<float, 2> longMaxUv0 = faceUv(axisL, false, a0, b1, l1);
            const std::array<float, 2> longMaxUv1 = faceUv(axisL, false, a1, b1, l1);
            const std::array<float, 2> longMaxUv2 = faceUv(axisL, false, a1, b0, l1);
            const std::array<float, 2> longMaxUv3 = faceUv(axisL, false, a0, b0, l1);
            appendQuad({{
                    modelVertex(a0, b1, l1, longMaxUv0[0], longMaxUv0[1]),
                    modelVertex(a1, b1, l1, longMaxUv1[0], longMaxUv1[1]),
                    modelVertex(a1, b0, l1, longMaxUv2[0], longMaxUv2[1]),
                    modelVertex(a0, b0, l1, longMaxUv3[0], longMaxUv3[1])
                }},
                faceForAxis(axisL.axis, 1),
                layerForFace(axisL, false));
            const std::array<float, 2> axisAMinUv0 = faceUv(axisA, true, a0, b0, l1);
            const std::array<float, 2> axisAMinUv1 = faceUv(axisA, true, a0, b1, l1);
            const std::array<float, 2> axisAMinUv2 = faceUv(axisA, true, a0, b1, l0);
            const std::array<float, 2> axisAMinUv3 = faceUv(axisA, true, a0, b0, l0);
            appendQuad({{
                    modelVertex(a0, b0, l1, axisAMinUv0[0], axisAMinUv0[1]),
                    modelVertex(a0, b1, l1, axisAMinUv1[0], axisAMinUv1[1]),
                    modelVertex(a0, b1, l0, axisAMinUv2[0], axisAMinUv2[1]),
                    modelVertex(a0, b0, l0, axisAMinUv3[0], axisAMinUv3[1])
                }},
                faceForAxis(axisA.axis, -1),
                layerForFace(axisA, true));
            const std::array<float, 2> axisAMaxUv0 = faceUv(axisA, false, a1, b0, l0);
            const std::array<float, 2> axisAMaxUv1 = faceUv(axisA, false, a1, b1, l0);
            const std::array<float, 2> axisAMaxUv2 = faceUv(axisA, false, a1, b1, l1);
            const std::array<float, 2> axisAMaxUv3 = faceUv(axisA, false, a1, b0, l1);
            appendQuad({{
                    modelVertex(a1, b0, l0, axisAMaxUv0[0], axisAMaxUv0[1]),
                    modelVertex(a1, b1, l0, axisAMaxUv1[0], axisAMaxUv1[1]),
                    modelVertex(a1, b1, l1, axisAMaxUv2[0], axisAMaxUv2[1]),
                    modelVertex(a1, b0, l1, axisAMaxUv3[0], axisAMaxUv3[1])
                }},
                faceForAxis(axisA.axis, 1),
                layerForFace(axisA, false));
            const std::array<float, 2> axisBMinUv0 = faceUv(axisB, true, a0, b0, l0);
            const std::array<float, 2> axisBMinUv1 = faceUv(axisB, true, a1, b0, l0);
            const std::array<float, 2> axisBMinUv2 = faceUv(axisB, true, a1, b0, l1);
            const std::array<float, 2> axisBMinUv3 = faceUv(axisB, true, a0, b0, l1);
            appendQuad({{
                    modelVertex(a0, b0, l0, axisBMinUv0[0], axisBMinUv0[1]),
                    modelVertex(a1, b0, l0, axisBMinUv1[0], axisBMinUv1[1]),
                    modelVertex(a1, b0, l1, axisBMinUv2[0], axisBMinUv2[1]),
                    modelVertex(a0, b0, l1, axisBMinUv3[0], axisBMinUv3[1])
                }},
                faceForAxis(axisB.axis, -1),
                layerForFace(axisB, true));
            const std::array<float, 2> axisBMaxUv0 = faceUv(axisB, false, a0, b1, l1);
            const std::array<float, 2> axisBMaxUv1 = faceUv(axisB, false, a1, b1, l1);
            const std::array<float, 2> axisBMaxUv2 = faceUv(axisB, false, a1, b1, l0);
            const std::array<float, 2> axisBMaxUv3 = faceUv(axisB, false, a0, b1, l0);
            appendQuad({{
                    modelVertex(a0, b1, l1, axisBMaxUv0[0], axisBMaxUv0[1]),
                    modelVertex(a1, b1, l1, axisBMaxUv1[0], axisBMaxUv1[1]),
                    modelVertex(a1, b1, l0, axisBMaxUv2[0], axisBMaxUv2[1]),
                    modelVertex(a0, b1, l0, axisBMaxUv3[0], axisBMaxUv3[1])
                }},
                faceForAxis(axisB.axis, 1),
                layerForFace(axisB, false));
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
                (static_cast<uint64_t>(faceLight(x, y, z, face)) << 49u) |
                (static_cast<uint64_t>(blockWavingType(block) & 0x3u) << 57u);
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
                appendFace(meshForBlock(block), worldX, y, worldZ, 0, width, height, blockFaceTextureLayer(block, 0), topFaceRotation(block, worldX, y, worldZ), blockDefinition(block).mipDistanceScale, blockAlphaBlend(block), faceLight(worldX, y, worldZ, 0), blockWavingType(block));
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
                appendFace(meshForBlock(block), worldX, y, worldZ, 1, width, height, blockFaceTextureLayer(block, 1), 0, blockDefinition(block).mipDistanceScale, blockAlphaBlend(block), faceLight(worldX, y, worldZ, 1), blockWavingType(block));
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
                appendFace(meshForBlock(block), worldX, y, worldZ, 2, width, height, blockFaceTextureLayer(block, 2), 0, blockDefinition(block).mipDistanceScale, blockAlphaBlend(block), faceLight(worldX, y, worldZ, 2), blockWavingType(block));
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
                appendFace(meshForBlock(block), worldX, y, worldZ, 3, width, height, blockFaceTextureLayer(block, 3), 0, blockDefinition(block).mipDistanceScale, blockAlphaBlend(block), faceLight(worldX, y, worldZ, 3), blockWavingType(block));
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
                appendFace(meshForBlock(block), worldX, y, worldZ, 4, width, height, blockFaceTextureLayer(block, 4), 0, blockDefinition(block).mipDistanceScale, blockAlphaBlend(block), faceLight(worldX, y, worldZ, 4), blockWavingType(block));
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
                appendFace(meshForBlock(block), worldX, y, worldZ, 5, width, height, blockFaceTextureLayer(block, 5), 0, blockDefinition(block).mipDistanceScale, blockAlphaBlend(block), faceLight(worldX, y, worldZ, 5), blockWavingType(block));
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
                    else if (blockDefinition(block).renderType == BlockRenderType::HalfSlab)
                    {
                        appendHalfSlabBlock(meshForBlock(block), worldXStart + localX, y, worldZStart + localZ, block, blockStateAt(localX, y, localZ));
                    }
                    else if (blockDefinition(block).renderType == BlockRenderType::Crucible)
                    {
                        appendCrucibleBlock(meshForBlock(block), worldXStart + localX, y, worldZStart + localZ, block);
                    }
                    else if (blockDefinition(block).renderType == BlockRenderType::Mold)
                    {
                        appendMoldBlock(meshForBlock(block), worldXStart + localX, y, worldZStart + localZ, block);
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
