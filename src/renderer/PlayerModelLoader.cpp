#include "renderer/PlayerModelLoader.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace dolbuto
{
    namespace
    {
        constexpr uint32_t GlbJsonChunkType = 0x4E4F534Au;
        constexpr uint32_t GlbBinChunkType = 0x004E4942u;

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
            std::string name;
            int mesh = -1;
            std::vector<int> children;
            std::array<float, 3> translation{0.0f, 0.0f, 0.0f};
            std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
            std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
        };

        struct GlbPrimitive
        {
            int position = -1;
            int uv = -1;
            int indices = -1;
        };

        struct GlbMesh
        {
            std::vector<GlbPrimitive> primitives;
        };

        std::vector<char> readFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open())
            {
                throw std::runtime_error("Failed to open player model: " + path.string());
            }

            const auto size = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(size);
            file.seekg(0);
            file.read(buffer.data(), static_cast<std::streamsize>(size));
            return buffer;
        }

        uint32_t readU32At(const std::vector<uint8_t>& bytes, size_t offset)
        {
            if (offset + 4u > bytes.size())
            {
                return 0;
            }

            uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                value |= static_cast<uint32_t>(bytes[offset + static_cast<size_t>(i)]) << (i * 8);
            }
            return value;
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

        std::vector<float> jsonFloatArrayValues(const std::string& array)
        {
            std::vector<float> values;
            size_t cursor = 0;
            while (cursor < array.size())
            {
                cursor = array.find_first_of("-+0123456789", cursor);
                if (cursor == std::string::npos)
                {
                    break;
                }

                size_t valueEnd = cursor;
                while (valueEnd < array.size())
                {
                    const char c = array[valueEnd];
                    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
                    {
                        ++valueEnd;
                        continue;
                    }
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

        std::optional<std::array<float, 3>> jsonFloat3Field(const std::string& object, const std::string& key)
        {
            const std::optional<std::string> array = jsonArrayField(object, key);
            if (!array)
            {
                return std::nullopt;
            }

            const std::vector<float> values = jsonFloatArrayValues(*array);
            if (values.size() < 3u)
            {
                return std::nullopt;
            }
            return std::array<float, 3>{values[0], values[1], values[2]};
        }

        std::optional<std::string> jsonTopLevelArrayField(const std::string& object, const std::string& key)
        {
            int depth = 0;
            bool inString = false;
            bool escaped = false;
            for (size_t i = 0; i < object.size(); ++i)
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
                    if (depth != 1)
                    {
                        inString = true;
                        continue;
                    }

                    std::string fieldName;
                    bool fieldEscaped = false;
                    size_t cursor = i + 1u;
                    for (; cursor < object.size(); ++cursor)
                    {
                        const char fieldChar = object[cursor];
                        if (fieldEscaped)
                        {
                            fieldName.push_back(fieldChar);
                            fieldEscaped = false;
                            continue;
                        }
                        if (fieldChar == '\\')
                        {
                            fieldEscaped = true;
                            continue;
                        }
                        if (fieldChar == '"')
                        {
                            break;
                        }
                        fieldName.push_back(fieldChar);
                    }

                    i = cursor;
                    if (fieldName != key)
                    {
                        continue;
                    }

                    const size_t colonPos = object.find(':', i + 1u);
                    if (colonPos == std::string::npos)
                    {
                        return std::nullopt;
                    }
                    const size_t openPos = object.find_first_not_of(" \t\r\n", colonPos + 1u);
                    if (openPos == std::string::npos || object[openPos] != '[')
                    {
                        return std::nullopt;
                    }

                    int arrayDepth = 0;
                    bool arrayString = false;
                    bool arrayEscaped = false;
                    for (size_t j = openPos; j < object.size(); ++j)
                    {
                        const char arrayChar = object[j];
                        if (arrayString)
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
                                arrayString = false;
                            }
                            continue;
                        }

                        if (arrayChar == '"')
                        {
                            arrayString = true;
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
                                return object.substr(openPos, j - openPos + 1u);
                            }
                        }
                    }
                    return std::nullopt;
                }

                if (c == '{')
                {
                    ++depth;
                }
                else if (c == '}')
                {
                    --depth;
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

        std::vector<uint8_t> glbChunk(const std::vector<uint8_t>& bytes, uint32_t expectedType)
        {
            if (bytes.size() < 12 || bytes[0] != 'g' || bytes[1] != 'l' || bytes[2] != 'T' || bytes[3] != 'F')
            {
                return {};
            }
            if (readU32At(bytes, 4) != 2u)
            {
                return {};
            }

            size_t offset = 12;
            while (offset + 8u <= bytes.size())
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
                    return std::vector<uint8_t>(
                        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                        bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunkLength));
                }
                offset += chunkLength;
            }
            return {};
        }

        GlbAccessor parseAccessor(const std::string& object)
        {
            GlbAccessor accessor{};
            accessor.bufferView = jsonIntField(object, "bufferView").value_or(-1);
            accessor.byteOffset = jsonIntField(object, "byteOffset").value_or(0);
            accessor.componentType = jsonIntField(object, "componentType").value_or(0);
            accessor.count = jsonIntField(object, "count").value_or(0);
            accessor.type = jsonStringField(object, "type").value_or("");
            return accessor;
        }

        GlbBufferView parseBufferView(const std::string& object)
        {
            GlbBufferView view{};
            view.byteOffset = jsonIntField(object, "byteOffset").value_or(0);
            view.byteLength = jsonIntField(object, "byteLength").value_or(0);
            view.byteStride = jsonIntField(object, "byteStride").value_or(0);
            return view;
        }

        GlbNode parseNode(const std::string& object)
        {
            GlbNode node{};
            node.name = jsonStringField(object, "name").value_or("");
            node.mesh = jsonIntField(object, "mesh").value_or(-1);
            node.translation = jsonFloat3Field(object, "translation").value_or(node.translation);
            node.scale = jsonFloat3Field(object, "scale").value_or(node.scale);
            if (const std::optional<std::string> rotation = jsonArrayField(object, "rotation"); rotation.has_value())
            {
                const std::vector<float> values = jsonFloatArrayValues(*rotation);
                if (values.size() >= 4u)
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

        GlbMesh parseMesh(const std::string& object)
        {
            GlbMesh mesh{};
            const std::optional<std::string> primitives = jsonArrayField(object, "primitives");
            if (!primitives)
            {
                return mesh;
            }

            for (const std::string& primitiveObject : jsonTopLevelObjects(*primitives))
            {
                if (jsonIntField(primitiveObject, "mode").value_or(4) != 4)
                {
                    continue;
                }

                GlbPrimitive primitive{};
                primitive.indices = jsonIntField(primitiveObject, "indices").value_or(-1);
                if (const std::optional<std::string> attributes = jsonObjectField(primitiveObject, "attributes"); attributes.has_value())
                {
                    primitive.position = jsonIntField(*attributes, "POSITION").value_or(-1);
                    primitive.uv = jsonIntField(*attributes, "TEXCOORD_0").value_or(-1);
                }
                if (primitive.position >= 0 && primitive.uv >= 0 && primitive.indices >= 0)
                {
                    mesh.primitives.push_back(primitive);
                }
            }
            return mesh;
        }

        std::array<float, 16> matrixFromTrs(
            std::array<float, 3> translation,
            std::array<float, 4> rotation,
            std::array<float, 3> scale)
        {
            const float x = rotation[0];
            const float y = rotation[1];
            const float z = rotation[2];
            const float w = rotation[3];
            const float x2 = x + x;
            const float y2 = y + y;
            const float z2 = z + z;
            const float xx = x * x2;
            const float xy = x * y2;
            const float xz = x * z2;
            const float yy = y * y2;
            const float yz = y * z2;
            const float zz = z * z2;
            const float wx = w * x2;
            const float wy = w * y2;
            const float wz = w * z2;

            return {
                (1.0f - (yy + zz)) * scale[0], (xy + wz) * scale[0], (xz - wy) * scale[0], 0.0f,
                (xy - wz) * scale[1], (1.0f - (xx + zz)) * scale[1], (yz + wx) * scale[1], 0.0f,
                (xz + wy) * scale[2], (yz - wx) * scale[2], (1.0f - (xx + yy)) * scale[2], 0.0f,
                translation[0], translation[1], translation[2], 1.0f};
        }

        std::array<float, 3> readVec3(
            const std::vector<uint8_t>& bin,
            const std::vector<GlbAccessor>& accessors,
            const std::vector<GlbBufferView>& views,
            int accessorIndex,
            int elementIndex)
        {
            const GlbAccessor& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const GlbBufferView& view = views[static_cast<size_t>(accessor.bufferView)];
            const int stride = view.byteStride > 0 ? view.byteStride : 12;
            const size_t offset = static_cast<size_t>(view.byteOffset + accessor.byteOffset + elementIndex * stride);
            return {readF32At(bin, offset), readF32At(bin, offset + 4u), readF32At(bin, offset + 8u)};
        }

        std::array<float, 2> readVec2(
            const std::vector<uint8_t>& bin,
            const std::vector<GlbAccessor>& accessors,
            const std::vector<GlbBufferView>& views,
            int accessorIndex,
            int elementIndex)
        {
            const GlbAccessor& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const GlbBufferView& view = views[static_cast<size_t>(accessor.bufferView)];
            const int stride = view.byteStride > 0 ? view.byteStride : 8;
            const size_t offset = static_cast<size_t>(view.byteOffset + accessor.byteOffset + elementIndex * stride);
            return {readF32At(bin, offset), readF32At(bin, offset + 4u)};
        }

        uint32_t readIndex(
            const std::vector<uint8_t>& bin,
            const std::vector<GlbAccessor>& accessors,
            const std::vector<GlbBufferView>& views,
            int accessorIndex,
            int elementIndex)
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
                const size_t readOffset = offset + static_cast<size_t>(elementIndex) * 2u;
                return readOffset + 1u < bin.size()
                    ? static_cast<uint32_t>(bin[readOffset]) | (static_cast<uint32_t>(bin[readOffset + 1u]) << 8u)
                    : 0u;
            }
            return offset + static_cast<size_t>(elementIndex) < bin.size()
                ? bin[offset + static_cast<size_t>(elementIndex)]
                : 0u;
        }
    }

    PlayerModelData loadPlayerModelFromGlb(const std::filesystem::path& path)
    {
        const std::vector<char> file = readFile(path);
        const std::vector<uint8_t> bytes(file.begin(), file.end());
        const std::vector<uint8_t> jsonBytes = glbChunk(bytes, GlbJsonChunkType);
        const std::vector<uint8_t> bin = glbChunk(bytes, GlbBinChunkType);
        if (jsonBytes.empty() || bin.empty())
        {
            throw std::runtime_error("Invalid player GLB file: " + path.string());
        }

        std::string json(jsonBytes.begin(), jsonBytes.end());
        while (!json.empty() && (json.back() == '\0' || json.back() == ' '))
        {
            json.pop_back();
        }

        std::vector<GlbAccessor> accessors;
        std::vector<GlbBufferView> views;
        std::vector<GlbNode> glbNodes;
        std::vector<GlbMesh> meshes;
        for (const std::string& object : jsonTopLevelObjects(jsonTopLevelArrayField(json, "accessors").value_or("[]")))
        {
            accessors.push_back(parseAccessor(object));
        }
        for (const std::string& object : jsonTopLevelObjects(jsonTopLevelArrayField(json, "bufferViews").value_or("[]")))
        {
            views.push_back(parseBufferView(object));
        }
        for (const std::string& object : jsonTopLevelObjects(jsonTopLevelArrayField(json, "nodes").value_or("[]")))
        {
            glbNodes.push_back(parseNode(object));
        }
        for (const std::string& object : jsonTopLevelObjects(jsonTopLevelArrayField(json, "meshes").value_or("[]")))
        {
            meshes.push_back(parseMesh(object));
        }

        PlayerModelData model;
        model.nodes.resize(glbNodes.size());
        for (size_t i = 0; i < glbNodes.size(); ++i)
        {
            PlayerModelNode& node = model.nodes[i];
            const GlbNode& glbNode = glbNodes[i];
            node.name = glbNode.name;
            node.children = glbNode.children;
            node.localTransform = matrixFromTrs(glbNode.translation, glbNode.rotation, glbNode.scale);
        }

        for (size_t i = 0; i < glbNodes.size(); ++i)
        {
            for (int child : glbNodes[i].children)
            {
                if (child >= 0 && static_cast<size_t>(child) < model.nodes.size())
                {
                    model.nodes[static_cast<size_t>(child)].parent = static_cast<int>(i);
                }
            }
        }

        for (size_t nodeIndex = 0; nodeIndex < glbNodes.size(); ++nodeIndex)
        {
            const GlbNode& node = glbNodes[nodeIndex];
            if (node.mesh < 0 || static_cast<size_t>(node.mesh) >= meshes.size())
            {
                continue;
            }

            for (const GlbPrimitive& primitive : meshes[static_cast<size_t>(node.mesh)].primitives)
            {
                if (static_cast<size_t>(primitive.position) >= accessors.size() ||
                    static_cast<size_t>(primitive.uv) >= accessors.size() ||
                    static_cast<size_t>(primitive.indices) >= accessors.size())
                {
                    continue;
                }

                const GlbAccessor& positionAccessor = accessors[static_cast<size_t>(primitive.position)];
                const GlbAccessor& indexAccessor = accessors[static_cast<size_t>(primitive.indices)];
                const uint32_t vertexBase = static_cast<uint32_t>(model.vertices.size());
                for (int i = 0; i < positionAccessor.count; ++i)
                {
                    const std::array<float, 3> position = readVec3(bin, accessors, views, primitive.position, i);
                    const std::array<float, 2> uv = readVec2(bin, accessors, views, primitive.uv, i);

                    PlayerModelVertex vertex{};
                    vertex.vertex.x = position[0];
                    vertex.vertex.y = position[1];
                    vertex.vertex.z = position[2];
                    vertex.vertex.u = uv[0];
                    vertex.vertex.v = uv[1];
                    vertex.vertex.ao = 1.0f;
                    vertex.vertex.textureLayer = 0.0f;
                    vertex.vertex.mipDistanceScale = 1.0f;
                    vertex.vertex.alphaBlend = 1.0f;
                    vertex.nodeIndex = static_cast<int>(nodeIndex);
                    model.vertices.push_back(vertex);
                }

                for (int i = 0; i < indexAccessor.count; ++i)
                {
                    model.indices.push_back(vertexBase + readIndex(bin, accessors, views, primitive.indices, i));
                }
            }
        }

        if (model.vertices.empty() || model.indices.empty())
        {
            throw std::runtime_error("Player GLB contains no renderable mesh: " + path.string());
        }

        return model;
    }
}
