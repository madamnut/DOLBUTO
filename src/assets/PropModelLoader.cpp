#include "assets/PropModelLoader.h"

#include "platform/Log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace dolbuto::assets
{
    namespace
    {
        constexpr size_t DpmHeaderSize = sizeof(uint32_t);
        constexpr size_t DpmQuadFloatCount = 4u * 3u + 4u * 2u + 3u;
        constexpr size_t DpmQuadSize = sizeof(float) * DpmQuadFloatCount;
        constexpr uint32_t GlbJsonChunkType = 0x4E4F534Au;
        constexpr uint32_t GlbBinChunkType = 0x004E4942u;

        struct PropVertex
        {
            std::array<float, 3> position{};
            std::array<float, 2> uv{};
            std::array<float, 3> normal{};
        };

        struct PropMeshData
        {
            std::vector<PropVertex> vertices;
            std::vector<uint32_t> indices;
        };

        struct PropQuad
        {
            std::array<PropVertex, 4> vertices{};
            std::array<float, 3> normal{};
        };

        struct GlbAccessor
        {
            int bufferView = -1;
            int byteOffset = 0;
            int componentType = 0;
            int count = 0;
            std::string type;
        };

        struct GlbBufferView
        {
            int byteOffset = 0;
            int byteLength = 0;
            int byteStride = 0;
        };

        struct GlbNode
        {
            int mesh = -1;
            std::vector<int> children;
            std::array<float, 3> translation{0.0f, 0.0f, 0.0f};
            std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
        };

        struct GlbPrimitive
        {
            int position = -1;
            int normal = -1;
            int uv = -1;
            int indices = -1;
        };

        struct GlbMesh
        {
            std::vector<GlbPrimitive> primitives;
        };

        std::vector<char> readFile(const std::string& path)
        {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open())
            {
                throw std::runtime_error("Failed to open file: " + path);
            }

            const auto size = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(size);
            file.seekg(0);
            file.read(buffer.data(), static_cast<std::streamsize>(size));
            return buffer;
        }

        void writeU32(std::vector<uint8_t>& bytes, uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
            {
                bytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
            }
        }

        uint32_t readU32At(const std::vector<uint8_t>& bytes, size_t offset)
        {
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                value |= static_cast<uint32_t>(bytes[offset + static_cast<size_t>(i)]) << (i * 8);
            }
            return value;
        }

        void writeF32(std::vector<uint8_t>& bytes, float value)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            writeU32(bytes, bits);
        }

        float readF32At(const std::vector<uint8_t>& bytes, size_t offset)
        {
            float value = 0.0f;
            if (offset + sizeof(value) <= bytes.size())
            {
                std::memcpy(&value, bytes.data() + offset, sizeof(value));
            }
            return value;
        }

        std::optional<std::string> jsonStringField(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t colonPos = object.find(':', keyPos + token.size());
            if (colonPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t quoteStart = object.find('"', colonPos + 1);
            if (quoteStart == std::string::npos)
            {
                return std::nullopt;
            }

            std::string value;
            bool escaped = false;
            for (size_t i = quoteStart + 1; i < object.size(); ++i)
            {
                const char c = object[i];
                if (escaped)
                {
                    value.push_back(c);
                    escaped = false;
                    continue;
                }
                if (c == '\\')
                {
                    escaped = true;
                    continue;
                }
                if (c == '"')
                {
                    return value;
                }
                value.push_back(c);
            }

            return std::nullopt;
        }

        std::optional<int> jsonIntField(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t colonPos = object.find(':', keyPos + token.size());
            if (colonPos == std::string::npos)
            {
                return std::nullopt;
            }

            size_t valueStart = object.find_first_not_of(" \t\r\n", colonPos + 1);
            if (valueStart == std::string::npos)
            {
                return std::nullopt;
            }

            size_t valueEnd = valueStart;
            if (object[valueEnd] == '-')
            {
                ++valueEnd;
            }
            while (valueEnd < object.size() && std::isdigit(static_cast<unsigned char>(object[valueEnd])) != 0)
            {
                ++valueEnd;
            }

            if (valueEnd == valueStart || (valueEnd == valueStart + 1 && object[valueStart] == '-'))
            {
                return std::nullopt;
            }

            try
            {
                return std::stoi(object.substr(valueStart, valueEnd - valueStart));
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        std::optional<std::array<float, 3>> jsonFloat3Field(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t openPos = object.find('[', keyPos + token.size());
            const size_t closePos = object.find(']', openPos == std::string::npos ? keyPos + token.size() : openPos + 1);
            if (openPos == std::string::npos || closePos == std::string::npos)
            {
                return std::nullopt;
            }

            std::array<float, 3> values{};
            size_t cursor = openPos + 1;
            for (size_t i = 0; i < values.size(); ++i)
            {
                cursor = object.find_first_not_of(" \t\r\n,", cursor);
                if (cursor == std::string::npos || cursor >= closePos)
                {
                    return std::nullopt;
                }

                size_t valueEnd = cursor;
                while (valueEnd < closePos)
                {
                    const char c = object[valueEnd];
                    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
                    {
                        ++valueEnd;
                        continue;
                    }
                    break;
                }
                if (valueEnd == cursor)
                {
                    return std::nullopt;
                }

                try
                {
                    values[i] = std::stof(object.substr(cursor, valueEnd - cursor));
                }
                catch (...)
                {
                    return std::nullopt;
                }
                cursor = valueEnd;
            }

            return values;
        }

        std::optional<std::string> jsonObjectField(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t openPos = object.find('{', keyPos + token.size());
            if (openPos == std::string::npos)
            {
                return std::nullopt;
            }

            int depth = 0;
            bool inString = false;
            bool escaped = false;
            for (size_t i = openPos; i < object.size(); ++i)
            {
                const char c = object[i];
                if (inString)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (c == '\\')
                    {
                        escaped = true;
                    }
                    else if (c == '"')
                    {
                        inString = false;
                    }
                    continue;
                }

                if (c == '"')
                {
                    inString = true;
                }
                else if (c == '{')
                {
                    ++depth;
                }
                else if (c == '}')
                {
                    --depth;
                    if (depth == 0)
                    {
                        return object.substr(openPos, i - openPos + 1);
                    }
                }
            }

            return std::nullopt;
        }

        std::optional<std::string> jsonArrayField(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t openPos = object.find('[', keyPos + token.size());
            if (openPos == std::string::npos)
            {
                return std::nullopt;
            }

            int depth = 0;
            bool inString = false;
            bool escaped = false;
            for (size_t i = openPos; i < object.size(); ++i)
            {
                const char c = object[i];
                if (inString)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (c == '\\')
                    {
                        escaped = true;
                    }
                    else if (c == '"')
                    {
                        inString = false;
                    }
                    continue;
                }

                if (c == '"')
                {
                    inString = true;
                }
                else if (c == '[')
                {
                    ++depth;
                }
                else if (c == ']')
                {
                    --depth;
                    if (depth == 0)
                    {
                        return object.substr(openPos, i - openPos + 1);
                    }
                }
            }

            return std::nullopt;
        }

        std::vector<std::string> jsonTopLevelObjects(const std::string& text)
        {
            std::vector<std::string> objects;
            int depth = 0;
            size_t objectStart = std::string::npos;
            bool inString = false;
            bool escaped = false;

            for (size_t i = 0; i < text.size(); ++i)
            {
                const char c = text[i];
                if (inString)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (c == '\\')
                    {
                        escaped = true;
                    }
                    else if (c == '"')
                    {
                        inString = false;
                    }
                    continue;
                }

                if (c == '"')
                {
                    inString = true;
                }
                else if (c == '{')
                {
                    if (depth == 0)
                    {
                        objectStart = i;
                    }
                    ++depth;
                }
                else if (c == '}')
                {
                    --depth;
                    if (depth == 0 && objectStart != std::string::npos)
                    {
                        objects.push_back(text.substr(objectStart, i - objectStart + 1));
                        objectStart = std::string::npos;
                    }
                }
            }

            return objects;
        }

        std::optional<std::string> jsonTopLevelArrayField(const std::string& object, const std::string& key)
        {
            const size_t open = object.find('{');
            const size_t close = object.rfind('}');
            if (open == std::string::npos || close == std::string::npos || close <= open)
            {
                return std::nullopt;
            }

            const std::string token = "\"" + key + "\"";
            size_t cursor = open + 1u;
            int depth = 0;
            bool inString = false;
            bool escaped = false;
            while (cursor < close)
            {
                const char c = object[cursor];
                if (inString)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (c == '\\')
                    {
                        escaped = true;
                    }
                    else if (c == '"')
                    {
                        inString = false;
                    }
                    ++cursor;
                    continue;
                }

                if (c == '"')
                {
                    if (depth == 0 && object.compare(cursor, token.size(), token) == 0)
                    {
                        const size_t colon = object.find(':', cursor + token.size());
                        if (colon == std::string::npos)
                        {
                            return std::nullopt;
                        }
                        const size_t arrayOpen = object.find_first_not_of(" \t\r\n", colon + 1u);
                        if (arrayOpen == std::string::npos || arrayOpen >= object.size() || object[arrayOpen] != '[')
                        {
                            return std::nullopt;
                        }

                        int arrayDepth = 0;
                        bool arrayInString = false;
                        bool arrayEscaped = false;
                        for (size_t i = arrayOpen; i < object.size(); ++i)
                        {
                            const char arrayChar = object[i];
                            if (arrayInString)
                            {
                                if (arrayEscaped)
                                {
                                    arrayEscaped = false;
                                }
                                else if (arrayChar == '\\')
                                {
                                    arrayEscaped = true;
                                }
                                else if (arrayChar == '"')
                                {
                                    arrayInString = false;
                                }
                                continue;
                            }
                            if (arrayChar == '"')
                            {
                                arrayInString = true;
                            }
                            else if (arrayChar == '[')
                            {
                                ++arrayDepth;
                            }
                            else if (arrayChar == ']')
                            {
                                --arrayDepth;
                                if (arrayDepth == 0)
                                {
                                    return object.substr(arrayOpen, i - arrayOpen + 1u);
                                }
                            }
                        }
                        return std::nullopt;
                    }
                    inString = true;
                }
                else if (c == '{' || c == '[')
                {
                    ++depth;
                }
                else if (c == '}' || c == ']')
                {
                    --depth;
                }
                ++cursor;
            }
            return std::nullopt;
        }

        std::vector<float> jsonFloatArrayValues(const std::string& array)
        {
            std::vector<float> values;
            const size_t openPos = array.find('[');
            const size_t closePos = array.rfind(']');
            if (openPos == std::string::npos || closePos == std::string::npos || closePos <= openPos)
            {
                return values;
            }

            size_t cursor = openPos + 1;
            while (cursor < closePos)
            {
                cursor = array.find_first_not_of(" \t\r\n,", cursor);
                if (cursor == std::string::npos || cursor >= closePos)
                {
                    break;
                }

                size_t valueEnd = cursor;
                while (valueEnd < closePos)
                {
                    const char c = array[valueEnd];
                    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
                    {
                        ++valueEnd;
                        continue;
                    }
                    break;
                }
                if (valueEnd == cursor)
                {
                    break;
                }

                try
                {
                    values.push_back(std::stof(array.substr(cursor, valueEnd - cursor)));
                }
                catch (...)
                {
                    values.clear();
                    return values;
                }
                cursor = valueEnd;
            }
            return values;
        }

        std::array<float, 3> glbPositionToBlockLocal(std::array<float, 3> value)
        {
            return {
                (value[0] + 8.0f) / 16.0f,
                value[1] / 16.0f,
                (value[2] + 8.0f) / 16.0f};
        }

        std::array<float, 3> rotateByQuaternion(std::array<float, 3> value, std::array<float, 4> q)
        {
            const std::array<float, 3> u{q[0], q[1], q[2]};
            const float s = q[3];
            const std::array<float, 3> cross1{
                u[1] * value[2] - u[2] * value[1],
                u[2] * value[0] - u[0] * value[2],
                u[0] * value[1] - u[1] * value[0]};
            const std::array<float, 3> cross2{
                u[1] * cross1[2] - u[2] * cross1[1],
                u[2] * cross1[0] - u[0] * cross1[2],
                u[0] * cross1[1] - u[1] * cross1[0]};
            return {
                value[0] + 2.0f * (s * cross1[0] + cross2[0]),
                value[1] + 2.0f * (s * cross1[1] + cross2[1]),
                value[2] + 2.0f * (s * cross1[2] + cross2[2])};
        }

        std::array<float, 3> transformPoint(std::array<float, 3> value, const std::vector<int>& chain, const std::vector<GlbNode>& nodes)
        {
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                const GlbNode& node = nodes[static_cast<size_t>(*it)];
                value = rotateByQuaternion(value, node.rotation);
                value[0] += node.translation[0];
                value[1] += node.translation[1];
                value[2] += node.translation[2];
            }
            return value;
        }

        std::vector<uint8_t> glbChunk(const std::vector<uint8_t>& bytes, uint32_t expectedType)
        {
            if (bytes.size() < 12 || bytes[0] != 'g' || bytes[1] != 'l' || bytes[2] != 'T' || bytes[3] != 'F')
            {
                return {};
            }
            size_t offset = 12;
            while (offset + 8 <= bytes.size())
            {
                const uint32_t chunkLength = readU32At(bytes, offset);
                const uint32_t chunkType = readU32At(bytes, offset + 4u);
                offset += 8u;
                if (offset + chunkLength > bytes.size())
                {
                    return {};
                }
                if (chunkType == expectedType)
                {
                    return std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunkLength));
                }
                offset += chunkLength;
            }
            return {};
        }

        GlbAccessor parseGlbAccessor(const std::string& object)
        {
            GlbAccessor accessor{};
            accessor.bufferView = jsonIntField(object, "bufferView").value_or(-1);
            accessor.byteOffset = jsonIntField(object, "byteOffset").value_or(0);
            accessor.componentType = jsonIntField(object, "componentType").value_or(0);
            accessor.count = jsonIntField(object, "count").value_or(0);
            accessor.type = jsonStringField(object, "type").value_or("");
            return accessor;
        }

        GlbBufferView parseGlbBufferView(const std::string& object)
        {
            GlbBufferView view{};
            view.byteOffset = jsonIntField(object, "byteOffset").value_or(0);
            view.byteLength = jsonIntField(object, "byteLength").value_or(0);
            view.byteStride = jsonIntField(object, "byteStride").value_or(0);
            return view;
        }

        GlbNode parseGlbNode(const std::string& object)
        {
            GlbNode node{};
            node.mesh = jsonIntField(object, "mesh").value_or(-1);
            node.translation = jsonFloat3Field(object, "translation").value_or(node.translation);
            if (const std::optional<std::string> rotation = jsonArrayField(object, "rotation"); rotation.has_value())
            {
                const std::vector<float> values = jsonFloatArrayValues(*rotation);
                if (values.size() >= 4)
                {
                    node.rotation = {values[0], values[1], values[2], values[3]};
                }
            }
            if (const std::optional<std::string> children = jsonArrayField(object, "children"); children.has_value())
            {
                const std::vector<float> values = jsonFloatArrayValues(*children);
                for (float value : values)
                {
                    node.children.push_back(static_cast<int>(std::lround(value)));
                }
            }
            return node;
        }

        GlbMesh parseGlbMesh(const std::string& object)
        {
            GlbMesh mesh{};
            const std::optional<std::string> primitives = jsonArrayField(object, "primitives");
            if (!primitives)
            {
                return mesh;
            }
            for (const std::string& primitiveObject : jsonTopLevelObjects(*primitives))
            {
                GlbPrimitive primitive{};
                if (jsonIntField(primitiveObject, "mode").value_or(4) != 4)
                {
                    continue;
                }
                primitive.indices = jsonIntField(primitiveObject, "indices").value_or(-1);
                if (const std::optional<std::string> attributes = jsonObjectField(primitiveObject, "attributes"); attributes.has_value())
                {
                    primitive.position = jsonIntField(*attributes, "POSITION").value_or(-1);
                    primitive.normal = jsonIntField(*attributes, "NORMAL").value_or(-1);
                    primitive.uv = jsonIntField(*attributes, "TEXCOORD_0").value_or(-1);
                }
                if (primitive.position >= 0 && primitive.uv >= 0 && primitive.indices >= 0)
                {
                    mesh.primitives.push_back(primitive);
                }
            }
            return mesh;
        }

        std::array<float, 3> readGlbVec3(const std::vector<uint8_t>& bin, const std::vector<GlbAccessor>& accessors, const std::vector<GlbBufferView>& views, int accessorIndex, int elementIndex)
        {
            const GlbAccessor& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const GlbBufferView& view = views[static_cast<size_t>(accessor.bufferView)];
            const int stride = view.byteStride > 0 ? view.byteStride : 12;
            const size_t offset = static_cast<size_t>(view.byteOffset + accessor.byteOffset + elementIndex * stride);
            return {readF32At(bin, offset), readF32At(bin, offset + 4u), readF32At(bin, offset + 8u)};
        }

        std::array<float, 2> readGlbVec2(const std::vector<uint8_t>& bin, const std::vector<GlbAccessor>& accessors, const std::vector<GlbBufferView>& views, int accessorIndex, int elementIndex)
        {
            const GlbAccessor& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const GlbBufferView& view = views[static_cast<size_t>(accessor.bufferView)];
            const int stride = view.byteStride > 0 ? view.byteStride : 8;
            const size_t offset = static_cast<size_t>(view.byteOffset + accessor.byteOffset + elementIndex * stride);
            return {readF32At(bin, offset), readF32At(bin, offset + 4u)};
        }

        uint32_t readGlbIndex(const std::vector<uint8_t>& bin, const std::vector<GlbAccessor>& accessors, const std::vector<GlbBufferView>& views, int accessorIndex, int elementIndex)
        {
            const GlbAccessor& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const GlbBufferView& view = views[static_cast<size_t>(accessor.bufferView)];
            const size_t offset = static_cast<size_t>(view.byteOffset + accessor.byteOffset);
            if (accessor.componentType == 5125)
            {
                return readU32At(bin, offset + static_cast<size_t>(elementIndex) * 4u);
            }
            if (accessor.componentType == 5123)
            {
                size_t readOffset = offset + static_cast<size_t>(elementIndex) * 2u;
                return readOffset + 1u < bin.size() ? static_cast<uint32_t>(bin[readOffset]) | (static_cast<uint32_t>(bin[readOffset + 1u]) << 8u) : 0u;
            }
            return offset + static_cast<size_t>(elementIndex) < bin.size() ? bin[offset + static_cast<size_t>(elementIndex)] : 0u;
        }

        std::array<float, 3> cross3(std::array<float, 3> a, std::array<float, 3> b)
        {
            return {
                a[1] * b[2] - a[2] * b[1],
                a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0]};
        }

        std::array<float, 3> subtract3(std::array<float, 3> a, std::array<float, 3> b)
        {
            return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
        }

        float lengthSquared3(std::array<float, 3> value)
        {
            return value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
        }

        std::array<float, 3> normalize3(std::array<float, 3> value)
        {
            const float length = std::sqrt(lengthSquared3(value));
            if (length <= 0.000001f)
            {
                return {0.0f, 1.0f, 0.0f};
            }
            return {value[0] / length, value[1] / length, value[2] / length};
        }

        float quadOrderError(const std::array<PropVertex, 4>& vertices, const std::array<int, 4>& order)
        {
            const std::array<float, 3>& a = vertices[static_cast<size_t>(order[0])].position;
            const std::array<float, 3>& b = vertices[static_cast<size_t>(order[1])].position;
            const std::array<float, 3>& c = vertices[static_cast<size_t>(order[2])].position;
            const std::array<float, 3>& d = vertices[static_cast<size_t>(order[3])].position;
            const std::array<float, 3> predicted{b[0] + d[0] - a[0], b[1] + d[1] - a[1], b[2] + d[2] - a[2]};
            return lengthSquared3(subtract3(predicted, c));
        }

        std::optional<PropQuad> mergeTrianglePairToQuad(const PropMeshData& mesh, size_t indexOffset)
        {
            if (indexOffset + 5u >= mesh.indices.size())
            {
                return std::nullopt;
            }

            std::array<PropVertex, 6> triangleVertices{};
            for (size_t i = 0; i < triangleVertices.size(); ++i)
            {
                const uint32_t index = mesh.indices[indexOffset + i];
                if (static_cast<size_t>(index) >= mesh.vertices.size())
                {
                    return std::nullopt;
                }
                triangleVertices[i] = mesh.vertices[static_cast<size_t>(index)];
            }

            std::array<PropVertex, 4> uniqueVertices{};
            size_t uniqueCount = 0;
            auto samePositionAndUv = [](const PropVertex& a, const PropVertex& b)
            {
                return lengthSquared3(subtract3(a.position, b.position)) <= 0.0000001f &&
                    std::abs(a.uv[0] - b.uv[0]) <= 0.0001f &&
                    std::abs(a.uv[1] - b.uv[1]) <= 0.0001f;
            };
            for (const PropVertex& vertex : triangleVertices)
            {
                bool exists = false;
                for (size_t i = 0; i < uniqueCount; ++i)
                {
                    if (samePositionAndUv(uniqueVertices[i], vertex))
                    {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                {
                    if (uniqueCount >= uniqueVertices.size())
                    {
                        return std::nullopt;
                    }
                    uniqueVertices[uniqueCount++] = vertex;
                }
            }
            if (uniqueCount != 4u)
            {
                return std::nullopt;
            }

            std::array<int, 4> bestOrder{0, 1, 2, 3};
            float bestError = std::numeric_limits<float>::max();
            std::array<int, 4> order{0, 1, 2, 3};
            do
            {
                const std::array<float, 3> edgeU = subtract3(uniqueVertices[static_cast<size_t>(order[1])].position, uniqueVertices[static_cast<size_t>(order[0])].position);
                const std::array<float, 3> edgeV = subtract3(uniqueVertices[static_cast<size_t>(order[3])].position, uniqueVertices[static_cast<size_t>(order[0])].position);
                if (lengthSquared3(edgeU) <= 0.0000001f || lengthSquared3(edgeV) <= 0.0000001f || lengthSquared3(cross3(edgeU, edgeV)) <= 0.0000001f)
                {
                    continue;
                }
                const float error = quadOrderError(uniqueVertices, order);
                if (error < bestError)
                {
                    bestError = error;
                    bestOrder = order;
                }
            } while (std::next_permutation(order.begin(), order.end()));

            if (bestError > 0.000001f)
            {
                return std::nullopt;
            }

            PropQuad quad{};
            for (size_t i = 0; i < quad.vertices.size(); ++i)
            {
                quad.vertices[i] = uniqueVertices[static_cast<size_t>(bestOrder[i])];
            }
            quad.normal = normalize3(cross3(
                subtract3(quad.vertices[1].position, quad.vertices[0].position),
                subtract3(quad.vertices[3].position, quad.vertices[0].position)));
            return quad;
        }

        std::vector<PropQuad> convertTrianglesToQuads(const PropMeshData& mesh)
        {
            std::vector<PropQuad> quads;
            quads.reserve(mesh.indices.size() / 6u);
            for (size_t offset = 0; offset + 5u < mesh.indices.size(); offset += 6u)
            {
                if (std::optional<PropQuad> quad = mergeTrianglePairToQuad(mesh, offset); quad.has_value())
                {
                    quads.push_back(*quad);
                }
            }
            return quads;
        }

        bool writeDpm(const std::filesystem::path& path, const std::vector<PropQuad>& quads)
        {
            if (quads.empty())
            {
                return false;
            }
            std::vector<uint8_t> bytes;
            bytes.reserve(DpmHeaderSize + quads.size() * DpmQuadSize);
            writeU32(bytes, static_cast<uint32_t>(quads.size()));
            for (const PropQuad& quad : quads)
            {
                for (const PropVertex& vertex : quad.vertices)
                {
                    for (float value : vertex.position) { writeF32(bytes, value); }
                }
                for (const PropVertex& vertex : quad.vertices)
                {
                    for (float value : vertex.uv) { writeF32(bytes, value); }
                }
                for (float value : quad.normal) { writeF32(bytes, value); }
            }
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                return false;
            }
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return static_cast<bool>(file);
        }

        bool dpmLooksValid(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                return false;
            }
            const auto fileSize = static_cast<size_t>(file.tellg());
            if (fileSize < DpmHeaderSize)
            {
                return false;
            }
            std::vector<uint8_t> header(DpmHeaderSize);
            file.seekg(0);
            file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
            if (!file)
            {
                return false;
            }
            const uint32_t quadCount = readU32At(header, 0);
            return fileSize == DpmHeaderSize + static_cast<size_t>(quadCount) * DpmQuadSize;
        }

        PropMeshData convertGlbToDpmMesh(const std::filesystem::path& glbPath)
        {
            const std::vector<char> file = readFile(glbPath.string());
            const std::vector<uint8_t> bytes(file.begin(), file.end());
            const std::vector<uint8_t> jsonBytes = glbChunk(bytes, GlbJsonChunkType);
            const std::vector<uint8_t> bin = glbChunk(bytes, GlbBinChunkType);
            if (jsonBytes.empty() || bin.empty())
            {
                return {};
            }
            std::string json(jsonBytes.begin(), jsonBytes.end());
            while (!json.empty() && (json.back() == '\0' || json.back() == ' '))
            {
                json.pop_back();
            }

            std::vector<GlbAccessor> accessors;
            std::vector<GlbBufferView> views;
            std::vector<GlbNode> nodes;
            std::vector<GlbMesh> meshes;
            for (const std::string& object : jsonTopLevelObjects(jsonTopLevelArrayField(json, "accessors").value_or("[]")))
            {
                accessors.push_back(parseGlbAccessor(object));
            }
            for (const std::string& object : jsonTopLevelObjects(jsonTopLevelArrayField(json, "bufferViews").value_or("[]")))
            {
                views.push_back(parseGlbBufferView(object));
            }
            for (const std::string& object : jsonTopLevelObjects(jsonTopLevelArrayField(json, "nodes").value_or("[]")))
            {
                nodes.push_back(parseGlbNode(object));
            }
            for (const std::string& object : jsonTopLevelObjects(jsonTopLevelArrayField(json, "meshes").value_or("[]")))
            {
                meshes.push_back(parseGlbMesh(object));
            }

            PropMeshData mesh;
            std::vector<bool> hasParent(nodes.size(), false);
            for (const GlbNode& node : nodes)
            {
                for (int child : node.children)
                {
                    if (child >= 0 && static_cast<size_t>(child) < hasParent.size())
                    {
                        hasParent[static_cast<size_t>(child)] = true;
                    }
                }
            }

            auto visitNode = [&](auto&& self, int nodeIndex, std::vector<int>& chain) -> void
            {
                if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= nodes.size())
                {
                    return;
                }
                chain.push_back(nodeIndex);
                const GlbNode& node = nodes[static_cast<size_t>(nodeIndex)];
                if (node.mesh >= 0 && static_cast<size_t>(node.mesh) < meshes.size())
                {
                    for (const GlbPrimitive& primitive : meshes[static_cast<size_t>(node.mesh)].primitives)
                    {
                        const GlbAccessor& positionAccessor = accessors[static_cast<size_t>(primitive.position)];
                        const uint32_t vertexBase = static_cast<uint32_t>(mesh.vertices.size());
                        for (int i = 0; i < positionAccessor.count; ++i)
                        {
                            PropVertex vertex{};
                            vertex.position = glbPositionToBlockLocal(transformPoint(readGlbVec3(bin, accessors, views, primitive.position, i), chain, nodes));
                            vertex.uv = readGlbVec2(bin, accessors, views, primitive.uv, i);
                            vertex.normal = primitive.normal >= 0 ? rotateByQuaternion(readGlbVec3(bin, accessors, views, primitive.normal, i), node.rotation) : std::array<float, 3>{0.0f, 1.0f, 0.0f};
                            mesh.vertices.push_back(vertex);
                        }
                        const GlbAccessor& indexAccessor = accessors[static_cast<size_t>(primitive.indices)];
                        for (int i = 0; i < indexAccessor.count; ++i)
                        {
                            mesh.indices.push_back(vertexBase + readGlbIndex(bin, accessors, views, primitive.indices, i));
                        }
                    }
                }
                for (int child : node.children)
                {
                    self(self, child, chain);
                }
                chain.pop_back();
            };

            std::vector<int> chain;
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                if (!hasParent[i])
                {
                    visitNode(visitNode, static_cast<int>(i), chain);
                }
            }
            return mesh;
        }

        bool convertGlbToDpm(const std::filesystem::path& glbPath, const std::filesystem::path& dpmPath)
        {
            try
            {
                const PropMeshData mesh = convertGlbToDpmMesh(glbPath);
                const std::vector<PropQuad> quads = convertTrianglesToQuads(mesh);
                if (!writeDpm(dpmPath, quads))
                {
                    return false;
                }
                return dpmLooksValid(dpmPath);
            }
            catch (...)
            {
                return false;
            }
        }
    }

    void ensurePropModelBinary(const std::filesystem::path& modelDirectory, const std::string& modelName)
    {
        if (modelName.empty())
        {
            return;
        }
        const std::filesystem::path dpmPath = modelDirectory / (modelName + ".dpm");
        const std::filesystem::path glbPath = modelDirectory / (modelName + ".glb");
        bool needsConvert = !std::filesystem::exists(dpmPath) || !dpmLooksValid(dpmPath);
        if (!needsConvert && std::filesystem::exists(glbPath))
        {
            try
            {
                needsConvert = std::filesystem::last_write_time(glbPath) > std::filesystem::last_write_time(dpmPath);
            }
            catch (...)
            {
                needsConvert = false;
            }
        }
        if (!needsConvert)
        {
            return;
        }
        if (!std::filesystem::exists(glbPath))
        {
            log::warn("Prop model dpm is missing and glb was not found: " + modelName);
            return;
        }
        log::info("Converting prop model: " + glbPath.string() + " -> " + dpmPath.string());
        if (!convertGlbToDpm(glbPath, dpmPath))
        {
            log::warn("Failed to convert prop model: " + glbPath.string());
        }
    }

    PropMesh loadDpmRenderMesh(const std::filesystem::path& dpmPath)
    {
        std::ifstream file(dpmPath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            return {};
        }
        const auto fileSize = static_cast<size_t>(file.tellg());
        std::vector<uint8_t> bytes(fileSize);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file || bytes.size() < DpmHeaderSize)
        {
            return {};
        }
        const uint32_t quadCount = readU32At(bytes, 0);
        if (bytes.size() != DpmHeaderSize + static_cast<size_t>(quadCount) * DpmQuadSize)
        {
            return {};
        }

        PropMesh mesh{};
        mesh.quads.reserve(static_cast<size_t>(quadCount) * PropQuadRenderFloatCount);
        mesh.boundsMin = {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()
        };
        mesh.boundsMax = {
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()
        };
        size_t offset = DpmHeaderSize;
        for (uint32_t quad = 0; quad < quadCount; ++quad)
        {
            std::array<PropVertex, 4> vertices{};
            for (PropVertex& vertex : vertices)
            {
                for (float& value : vertex.position) { value = readF32At(bytes, offset); offset += sizeof(float); }
            }
            for (PropVertex& vertex : vertices)
            {
                for (float& value : vertex.uv) { value = readF32At(bytes, offset); offset += sizeof(float); }
            }
            offset += sizeof(float) * 3u;

            for (const PropVertex& vertex : vertices)
            {
                mesh.boundsMin.x = std::min(mesh.boundsMin.x, vertex.position[0]);
                mesh.boundsMin.y = std::min(mesh.boundsMin.y, vertex.position[1]);
                mesh.boundsMin.z = std::min(mesh.boundsMin.z, vertex.position[2]);
                mesh.boundsMax.x = std::max(mesh.boundsMax.x, vertex.position[0]);
                mesh.boundsMax.y = std::max(mesh.boundsMax.y, vertex.position[1]);
                mesh.boundsMax.z = std::max(mesh.boundsMax.z, vertex.position[2]);
                mesh.quads.push_back(vertex.position[0]);
                mesh.quads.push_back(vertex.position[1]);
                mesh.quads.push_back(vertex.position[2]);
            }
            for (const PropVertex& vertex : vertices)
            {
                mesh.quads.push_back(vertex.uv[0]);
                mesh.quads.push_back(vertex.uv[1]);
            }
        }
        mesh.hasBounds = !mesh.quads.empty();
        return mesh;
    }
}
