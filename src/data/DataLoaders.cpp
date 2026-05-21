#include "data/DataLoaders.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>

namespace dolbuto::data
{
    namespace
    {
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

        std::optional<float> jsonFloatField(const std::string& object, const std::string& key)
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
            while (valueEnd < object.size())
            {
                const char c = object[valueEnd];
                if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
                {
                    ++valueEnd;
                    continue;
                }
                break;
            }

            if (valueEnd == valueStart)
            {
                return std::nullopt;
            }

            try
            {
                return std::stof(object.substr(valueStart, valueEnd - valueStart));
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        std::optional<bool> jsonBoolField(const std::string& object, const std::string& key)
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

            const size_t valueStart = object.find_first_not_of(" \t\r\n", colonPos + 1);
            if (valueStart == std::string::npos)
            {
                return std::nullopt;
            }

            if (object.compare(valueStart, 4, "true") == 0)
            {
                return true;
            }
            if (object.compare(valueStart, 5, "false") == 0)
            {
                return false;
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
    }

    std::vector<ParsedItemDefinition> parseItemDefinitions(const std::string& text)
    {
        std::vector<ParsedItemDefinition> definitions;

        for (const std::string& object : jsonTopLevelObjects(text))
        {
            const std::optional<int> id = jsonIntField(object, "id");
            if (!id.has_value() || *id < 0 || *id > std::numeric_limits<uint16_t>::max())
            {
                continue;
            }

            ParsedItemDefinition definition{};
            definition.id = static_cast<uint16_t>(*id);
            if (const std::optional<std::string> key = jsonStringField(object, "key"); key.has_value())
            {
                definition.key = *key;
            }
            if (const std::optional<std::string> name = jsonStringField(object, "name"); name.has_value())
            {
                definition.name = *name;
            }
            if (const std::optional<int> stackSize = jsonIntField(object, "stackSize"); stackSize.has_value())
            {
                definition.stackSize = static_cast<uint16_t>(std::clamp(*stackSize, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
            }
            if (const std::optional<std::string> texture = jsonStringField(object, "texture"); texture.has_value())
            {
                definition.texture = *texture;
            }
            if (const std::optional<std::string> slotTexture = jsonStringField(object, "slotTexture"); slotTexture.has_value())
            {
                definition.slotTexture = *slotTexture;
            }
            if (const std::optional<std::string> render = jsonObjectField(object, "render"); render.has_value())
            {
                if (const std::optional<std::string> type = jsonStringField(*render, "type"); type.has_value())
                {
                    definition.droppedRender = *type;
                    definition.heldRender = *type;
                }
                if (const std::optional<std::string> texture = jsonStringField(*render, "texture"); texture.has_value())
                {
                    definition.droppedTexture = *texture;
                    definition.heldTexture = *texture;
                }
                if (const std::optional<std::string> dropped = jsonStringField(*render, "dropped"); dropped.has_value())
                {
                    definition.droppedRender = *dropped;
                }
                if (const std::optional<std::string> held = jsonStringField(*render, "held"); held.has_value())
                {
                    definition.heldRender = *held;
                }
            }
            if (const std::optional<std::string> droppedRender = jsonObjectField(object, "droppedRender"); droppedRender.has_value())
            {
                if (const std::optional<std::string> type = jsonStringField(*droppedRender, "type"); type.has_value())
                {
                    definition.droppedRender = *type;
                }
                if (const std::optional<std::string> texture = jsonStringField(*droppedRender, "texture"); texture.has_value())
                {
                    definition.droppedTexture = *texture;
                }
            }
            if (const std::optional<std::string> heldRender = jsonObjectField(object, "heldRender"); heldRender.has_value())
            {
                if (const std::optional<std::string> type = jsonStringField(*heldRender, "type"); type.has_value())
                {
                    definition.heldRender = *type;
                }
                if (const std::optional<std::string> texture = jsonStringField(*heldRender, "texture"); texture.has_value())
                {
                    definition.heldTexture = *texture;
                }
            }

            definitions.push_back(std::move(definition));
        }

        return definitions;
    }

    std::vector<ParsedBlockDefinition> parseBlockDefinitions(const std::string& text)
    {
        std::vector<ParsedBlockDefinition> definitions;
        constexpr std::array<const char*, 5> TextureKeys = {"all", "top", "bottom", "side", "topBottom"};

        for (const std::string& object : jsonTopLevelObjects(text))
        {
            const std::optional<int> id = jsonIntField(object, "id");
            if (!id.has_value() || *id < 0 || *id > std::numeric_limits<uint16_t>::max())
            {
                continue;
            }

            ParsedBlockDefinition definition{};
            definition.id = static_cast<uint16_t>(*id);
            if (const std::optional<std::string> name = jsonStringField(object, "name"); name.has_value())
            {
                definition.name = *name;
            }
            if (const std::optional<std::string> renderType = jsonStringField(object, "renderType"); renderType.has_value())
            {
                definition.renderType = *renderType;
            }
            if (const std::optional<bool> directional = jsonBoolField(object, "directional"); directional.has_value())
            {
                definition.directional = *directional;
            }
            if (const std::optional<bool> collision = jsonBoolField(object, "collision"); collision.has_value())
            {
                definition.collision = *collision;
            }
            if (const std::optional<bool> ao = jsonBoolField(object, "ao"); ao.has_value())
            {
                definition.ao = *ao;
            }
            if (const std::optional<std::string> faceOcclusion = jsonStringField(object, "faceOcclusion"); faceOcclusion.has_value())
            {
                definition.faceOcclusion = *faceOcclusion;
            }
            if (const std::optional<bool> sameBlockFaceCulling = jsonBoolField(object, "sameBlockFaceCulling"); sameBlockFaceCulling.has_value())
            {
                definition.sameBlockFaceCulling = *sameBlockFaceCulling;
            }
            if (const std::optional<std::string> alphaMode = jsonStringField(object, "alphaMode"); alphaMode.has_value())
            {
                definition.alphaMode = *alphaMode;
            }
            if (const std::optional<float> alphaCutoff = jsonFloatField(object, "alphaCutoff"); alphaCutoff.has_value())
            {
                definition.alphaCutoff = std::clamp(*alphaCutoff, 0.0f, 1.0f);
            }
            if (const std::optional<float> alphaBlend = jsonFloatField(object, "alphaBlend"); alphaBlend.has_value())
            {
                definition.alphaBlend = std::clamp(*alphaBlend, 0.0f, 1.0f);
            }
            if (const std::optional<float> mipDistanceScale = jsonFloatField(object, "mipDistanceScale"); mipDistanceScale.has_value())
            {
                definition.mipDistanceScale = std::max(0.0f, *mipDistanceScale);
            }
            if (const std::optional<float> hardness = jsonFloatField(object, "hardness"); hardness.has_value())
            {
                definition.hardness = *hardness;
            }
            if (const std::optional<int> lightAttenuation = jsonIntField(object, "lightAttenuation"); lightAttenuation.has_value())
            {
                definition.lightAttenuation = static_cast<uint8_t>(std::clamp(*lightAttenuation, 0, 15));
            }
            if (const std::optional<int> lightEmission = jsonIntField(object, "lightEmission"); lightEmission.has_value())
            {
                definition.lightEmission = static_cast<uint8_t>(std::clamp(*lightEmission, 0, 15));
            }
            if (const std::optional<bool> randomOffset = jsonBoolField(object, "randomOffset"); randomOffset.has_value())
            {
                definition.randomOffset = *randomOffset;
            }
            if (const std::optional<std::string> textures = jsonObjectField(object, "textures"); textures.has_value())
            {
                for (const char* key : TextureKeys)
                {
                    if (const std::optional<std::string> texture = jsonStringField(*textures, key); texture.has_value())
                    {
                        definition.textures[key] = *texture;
                    }
                }
            }
            if (const std::optional<std::string> prop = jsonObjectField(object, "prop"); prop.has_value())
            {
                if (const std::optional<std::string> model = jsonStringField(*prop, "model"); model.has_value())
                {
                    definition.propModel = *model;
                }
                if (const std::optional<std::string> texture = jsonStringField(*prop, "texture"); texture.has_value())
                {
                    definition.propTexture = *texture;
                }
            }
            if (const std::optional<std::string> drops = jsonArrayField(object, "drops"); drops.has_value())
            {
                for (const std::string& dropObject : jsonTopLevelObjects(*drops))
                {
                    const std::optional<std::string> item = jsonStringField(dropObject, "item");
                    if (!item.has_value() || item->empty())
                    {
                        continue;
                    }

                    const int minCount = jsonIntField(dropObject, "min").value_or(1);
                    const int maxCount = jsonIntField(dropObject, "max").value_or(minCount);
                    const float chance = jsonFloatField(dropObject, "chance").value_or(1.0f);
                    definition.dropItemKeys.push_back(*item);
                    definition.dropMins.push_back(static_cast<uint16_t>(std::clamp(minCount, 0, static_cast<int>(std::numeric_limits<uint16_t>::max()))));
                    definition.dropMaxes.push_back(static_cast<uint16_t>(std::clamp(maxCount, 0, static_cast<int>(std::numeric_limits<uint16_t>::max()))));
                    definition.dropChances.push_back(std::clamp(chance, 0.0f, 1.0f));
                }
            }
            definitions.push_back(std::move(definition));
        }

        return definitions;
    }

    std::vector<ParsedFluidDefinition> parseFluidDefinitions(const std::string& text)
    {
        std::vector<ParsedFluidDefinition> definitions;
        for (const std::string& object : jsonTopLevelObjects(text))
        {
            const std::optional<int> id = jsonIntField(object, "id");
            if (!id.has_value() || *id < 0 || *id > std::numeric_limits<uint16_t>::max())
            {
                continue;
            }

            ParsedFluidDefinition definition{};
            definition.id = static_cast<uint16_t>(*id);
            if (const std::optional<std::string> name = jsonStringField(object, "name"); name.has_value())
            {
                definition.name = *name;
            }
            if (const std::optional<int> lightAttenuation = jsonIntField(object, "lightAttenuation"); lightAttenuation.has_value())
            {
                definition.lightAttenuation = static_cast<uint8_t>(std::clamp(*lightAttenuation, 0, 15));
            }
            definitions.push_back(std::move(definition));
        }

        return definitions;
    }
}
