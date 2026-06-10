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
        std::string trimJsonValue(const std::string& value);

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

        std::optional<std::string> jsonFieldValue(const std::string& object, const std::string& key)
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

            const char first = object[valueStart];
            if (first == '"' || first == '{' || first == '[')
            {
                int objectDepth = 0;
                int arrayDepth = 0;
                bool inString = false;
                bool escaped = false;
                for (size_t i = valueStart; i < object.size(); ++i)
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
                            if (first == '"' && objectDepth == 0 && arrayDepth == 0)
                            {
                                return object.substr(valueStart, i - valueStart + 1);
                            }
                        }
                        continue;
                    }

                    if (c == '"')
                    {
                        inString = true;
                    }
                    else if (c == '{')
                    {
                        ++objectDepth;
                    }
                    else if (c == '}')
                    {
                        --objectDepth;
                        if (first == '{' && objectDepth == 0)
                        {
                            return object.substr(valueStart, i - valueStart + 1);
                        }
                    }
                    else if (c == '[')
                    {
                        ++arrayDepth;
                    }
                    else if (c == ']')
                    {
                        --arrayDepth;
                        if (first == '[' && arrayDepth == 0)
                        {
                            return object.substr(valueStart, i - valueStart + 1);
                        }
                    }
                }
                return std::nullopt;
            }

            size_t valueEnd = valueStart;
            while (valueEnd < object.size() && object[valueEnd] != ',' && object[valueEnd] != '}' && object[valueEnd] != ']')
            {
                ++valueEnd;
            }
            return trimJsonValue(object.substr(valueStart, valueEnd - valueStart));
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

        std::vector<std::string> jsonStringArrayValues(const std::string& array)
        {
            std::vector<std::string> values;
            bool inString = false;
            bool escaped = false;
            std::string value;
            for (size_t i = 0; i < array.size(); ++i)
            {
                const char c = array[i];
                if (!inString)
                {
                    if (c == '"')
                    {
                        inString = true;
                        value.clear();
                    }
                    continue;
                }

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
                    values.push_back(value);
                    inString = false;
                    continue;
                }
                value.push_back(c);
            }
            return values;
        }

        std::string trimJsonValue(const std::string& value)
        {
            const size_t begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
            {
                return "";
            }
            const size_t end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1);
        }

        std::vector<std::string> jsonTopLevelArrayValues(const std::string& array)
        {
            std::vector<std::string> values;
            const size_t openPos = array.find('[');
            const size_t closePos = array.find_last_of(']');
            if (openPos == std::string::npos || closePos == std::string::npos || closePos <= openPos)
            {
                return values;
            }

            int objectDepth = 0;
            int arrayDepth = 0;
            bool inString = false;
            bool escaped = false;
            size_t valueStart = openPos + 1;
            for (size_t i = openPos + 1; i < closePos; ++i)
            {
                const char c = array[i];
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
                    ++objectDepth;
                }
                else if (c == '}')
                {
                    --objectDepth;
                }
                else if (c == '[')
                {
                    ++arrayDepth;
                }
                else if (c == ']')
                {
                    --arrayDepth;
                }
                else if (c == ',' && objectDepth == 0 && arrayDepth == 0)
                {
                    std::string value = trimJsonValue(array.substr(valueStart, i - valueStart));
                    if (!value.empty())
                    {
                        values.push_back(std::move(value));
                    }
                    valueStart = i + 1;
                }
            }

            std::string value = trimJsonValue(array.substr(valueStart, closePos - valueStart));
            if (!value.empty())
            {
                values.push_back(std::move(value));
            }
            return values;
        }

        std::optional<std::string> jsonStringLiteralValue(const std::string& value)
        {
            const std::string trimmed = trimJsonValue(value);
            if (trimmed.size() < 2 || trimmed.front() != '"' || trimmed.back() != '"')
            {
                return std::nullopt;
            }

            std::string result;
            bool escaped = false;
            for (size_t i = 1; i + 1 < trimmed.size(); ++i)
            {
                const char c = trimmed[i];
                if (escaped)
                {
                    result.push_back(c);
                    escaped = false;
                    continue;
                }
                if (c == '\\')
                {
                    escaped = true;
                    continue;
                }
                result.push_back(c);
            }
            return result;
        }

        std::optional<ParsedBlockTextureDefinition> parseBlockTextureDefinition(const std::string& value)
        {
            if (const std::optional<std::string> texture = jsonStringLiteralValue(value); texture.has_value())
            {
                if (texture->empty())
                {
                    return std::nullopt;
                }
                ParsedBlockTextureDefinition definition{};
                definition.texture = *texture;
                return definition;
            }

            const std::string trimmed = trimJsonValue(value);
            if (trimmed.empty() || trimmed.front() != '{')
            {
                return std::nullopt;
            }

            ParsedBlockTextureDefinition definition{};
            if (const std::optional<std::string> texture = jsonStringField(trimmed, "texture"); texture.has_value())
            {
                definition.texture = *texture;
            }
            if (const std::optional<std::string> base = jsonStringField(trimmed, "base"); base.has_value())
            {
                definition.base = *base;
            }
            if (const std::optional<std::string> mask = jsonStringField(trimmed, "mask"); mask.has_value())
            {
                definition.mask = *mask;
            }
            if (definition.texture.empty() && definition.base.empty())
            {
                return std::nullopt;
            }
            return definition;
        }

        uint16_t clampedInteractionCount(int count)
        {
            return static_cast<uint16_t>(std::clamp(count, 1, 65535));
        }

        std::optional<ParsedInteractionOutput> parseInteractionOutput(
            const std::string& value,
            uint16_t defaultMin,
            uint16_t defaultMax)
        {
            if (const std::optional<std::string> item = jsonStringLiteralValue(value); item.has_value())
            {
                if (item->empty())
                {
                    return std::nullopt;
                }
                ParsedInteractionOutput output{};
                output.item = *item;
                output.min = defaultMin;
                output.max = defaultMax;
                return output;
            }

            const std::string trimmed = trimJsonValue(value);
            if (trimmed.empty() || trimmed.front() != '{')
            {
                return std::nullopt;
            }

            const std::optional<std::string> item = jsonStringField(trimmed, "item");
            const std::optional<std::string> block = jsonStringField(trimmed, "block");
            if ((!item.has_value() || item->empty()) && (!block.has_value() || block->empty()))
            {
                return std::nullopt;
            }

            ParsedInteractionOutput output{};
            if (item.has_value())
            {
                output.item = *item;
            }
            if (block.has_value())
            {
                output.block = *block;
            }
            if (const std::optional<std::string> placement = jsonStringField(trimmed, "placement"); placement.has_value())
            {
                output.placement = *placement;
            }
            output.min = clampedInteractionCount(jsonIntField(trimmed, "min").value_or(defaultMin));
            output.max = clampedInteractionCount(jsonIntField(trimmed, "max").value_or(output.min));
            if (output.max < output.min)
            {
                output.max = output.min;
            }
            return output;
        }

        ParsedInteractionCandidate parseInteractionCandidate(
            const std::string& value,
            uint16_t defaultMin,
            uint16_t defaultMax)
        {
            ParsedInteractionCandidate candidate{};
            if (const std::optional<std::string> item = jsonStringLiteralValue(value); item.has_value())
            {
                if (!item->empty())
                {
                    ParsedInteractionOutput output{};
                    output.item = *item;
                    output.min = defaultMin;
                    output.max = defaultMax;
                    candidate.outputs.push_back(output);
                }
                return candidate;
            }

            const std::string trimmed = trimJsonValue(value);
            if (trimmed.empty() || trimmed.front() != '{')
            {
                return candidate;
            }

            std::optional<std::string> items = jsonArrayField(trimmed, "items");
            if (!items.has_value())
            {
                items = jsonArrayField(trimmed, "outputs");
            }
            if (items.has_value())
            {
                for (const std::string& itemValue : jsonTopLevelArrayValues(*items))
                {
                    if (const std::optional<ParsedInteractionOutput> output = parseInteractionOutput(itemValue, defaultMin, defaultMax); output.has_value())
                    {
                        candidate.outputs.push_back(*output);
                    }
                }
                return candidate;
            }

            if (const std::optional<ParsedInteractionOutput> output = parseInteractionOutput(trimmed, defaultMin, defaultMax); output.has_value())
            {
                candidate.outputs.push_back(*output);
            }
            return candidate;
        }

        std::optional<ParsedInteractionIngredient> parseInteractionIngredient(const std::string& value)
        {
            if (const std::optional<std::string> item = jsonStringLiteralValue(value); item.has_value())
            {
                if (!item->empty())
                {
                    return ParsedInteractionIngredient{*item, 1};
                }
                return std::nullopt;
            }

            const std::string trimmed = trimJsonValue(value);
            if (trimmed.empty() || trimmed.front() != '{')
            {
                return std::nullopt;
            }

            const std::optional<std::string> item = jsonStringField(trimmed, "item");
            if (!item.has_value() || item->empty())
            {
                return std::nullopt;
            }

            return ParsedInteractionIngredient{
                *item,
                clampedInteractionCount(jsonIntField(trimmed, "count").value_or(1))
            };
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
            if (const std::optional<std::string> slotRender = jsonObjectField(object, "slotRender"); slotRender.has_value())
            {
                if (const std::optional<std::string> type = jsonStringField(*slotRender, "type"); type.has_value())
                {
                    definition.slotRender = *type;
                }
                if (const std::optional<std::string> texture = jsonStringField(*slotRender, "texture"); texture.has_value())
                {
                    definition.slotRenderTexture = *texture;
                }
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
            const std::optional<std::string> components = jsonObjectField(object, "components");
            const std::string componentObject = components.value_or("{}");
            if (const std::optional<std::string> useActions = jsonArrayField(componentObject, "useActions"); useActions.has_value())
            {
                definition.useActions = jsonStringArrayValues(*useActions);
            }
            if (const std::optional<std::string> breakActions = jsonArrayField(componentObject, "breakActions"); breakActions.has_value())
            {
                definition.breakActions = jsonStringArrayValues(*breakActions);
            }
            if (const std::optional<int> breakLevel = jsonIntField(componentObject, "breakLevel"); breakLevel.has_value())
            {
                definition.breakLevel = static_cast<uint16_t>(std::clamp(*breakLevel, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
            }
            if (const std::optional<std::string> durability = jsonObjectField(componentObject, "durability"); durability.has_value())
            {
                if (const std::optional<int> maxDurability = jsonIntField(*durability, "max"); maxDurability.has_value())
                {
                    definition.maxDurability = static_cast<uint16_t>(std::clamp(*maxDurability, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
                }
            }
            if (const std::optional<std::string> fuel = jsonObjectField(componentObject, "fuel"); fuel.has_value())
            {
                if (const std::optional<int> burnTimeTicks = jsonIntField(*fuel, "burnTimeTicks"); burnTimeTicks.has_value())
                {
                    definition.burnTimeTicks = static_cast<uint32_t>(std::max(*burnTimeTicks, 0));
                }
                if (const std::optional<int> heatLevel = jsonIntField(*fuel, "heatLevel"); heatLevel.has_value())
                {
                    definition.heatLevel = static_cast<uint16_t>(std::clamp(*heatLevel, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
                }
                if (const std::optional<std::string> burnRemainder = jsonObjectField(*fuel, "remainder"); burnRemainder.has_value())
                {
                    if (const std::optional<std::string> item = jsonStringField(*burnRemainder, "item"); item.has_value())
                    {
                        definition.burnRemainderItem = *item;
                    }
                    if (const std::optional<int> count = jsonIntField(*burnRemainder, "count"); count.has_value())
                    {
                        definition.burnRemainderCount = static_cast<uint16_t>(std::clamp(*count, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
                    }
                    else if (!definition.burnRemainderItem.empty())
                    {
                        definition.burnRemainderCount = 1;
                    }
                }
            }
            if (const std::optional<std::string> burnableLight = jsonObjectField(componentObject, "burnableLight"); burnableLight.has_value())
            {
                if (const std::optional<int> maxTicks = jsonIntField(*burnableLight, "maxTicks"); maxTicks.has_value())
                {
                    definition.maxBurnTicks = static_cast<uint16_t>(std::clamp(*maxTicks, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
                }
                if (const std::optional<int> lightEmission = jsonIntField(*burnableLight, "lightEmission"); lightEmission.has_value())
                {
                    definition.portableLightEmission = static_cast<uint16_t>(std::clamp(*lightEmission, 0, 15));
                }
                if (const std::optional<std::string> extinguishedItem = jsonStringField(*burnableLight, "extinguishedItem"); extinguishedItem.has_value())
                {
                    definition.extinguishedItem = *extinguishedItem;
                }
                if (const std::optional<std::string> burnoutItem = jsonStringField(*burnableLight, "burnoutItem"); burnoutItem.has_value())
                {
                    definition.burnoutItem = *burnoutItem;
                }
                if (const std::optional<int> burnoutCount = jsonIntField(*burnableLight, "burnoutCount"); burnoutCount.has_value())
                {
                    definition.burnoutCount = static_cast<uint16_t>(std::clamp(*burnoutCount, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
                }
                else if (!definition.burnoutItem.empty())
                {
                    definition.burnoutCount = 1;
                }
                if (const std::optional<bool> ticksOnlyWhileHeld = jsonBoolField(*burnableLight, "ticksOnlyWhileHeld"); ticksOnlyWhileHeld.has_value())
                {
                    definition.burnTicksOnlyWhileHeld = *ticksOnlyWhileHeld;
                }
            }
            if (const std::optional<std::string> slotGauge = jsonObjectField(componentObject, "slotGauge"); slotGauge.has_value())
            {
                if (const std::optional<std::string> source = jsonStringField(*slotGauge, "source"); source.has_value())
                {
                    definition.slotGaugeSource = *source;
                }
            }
            if (const std::optional<std::string> placeable = jsonObjectField(componentObject, "placeable"); placeable.has_value())
            {
                if (const std::optional<std::string> block = jsonStringField(*placeable, "block"); block.has_value())
                {
                    definition.placeBlock = *block;
                    if (!definition.placeBlock.empty())
                    {
                        definition.placeActions.push_back("place");
                    }
                }
            }
            if (const std::optional<std::string> modelBlock = jsonStringField(object, "modelBlock"); modelBlock.has_value())
            {
                definition.modelBlock = *modelBlock;
            }
            if (const std::optional<std::string> modelShape = jsonStringField(object, "modelShape"); modelShape.has_value())
            {
                definition.modelShape = *modelShape;
            }
            if (const std::optional<std::string> modelTexture = jsonStringField(object, "modelTexture"); modelTexture.has_value())
            {
                definition.modelTexture = *modelTexture;
            }
            definitions.push_back(std::move(definition));
        }

        return definitions;
    }

    std::vector<ParsedBlockDefinition> parseBlockDefinitions(const std::string& text)
    {
        std::vector<ParsedBlockDefinition> definitions;
        constexpr std::array<const char*, 7> TextureKeys = {
            "all",
            "top",
            "bottom",
            "side",
            "topBottom",
            "verticalSection",
            "horizontalSection"
        };

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
            if (const std::optional<int> breakLevel = jsonIntField(object, "breakLevel"); breakLevel.has_value())
            {
                definition.breakLevel = static_cast<uint16_t>(std::clamp(*breakLevel, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
            }
            if (const std::optional<std::string> breakAction = jsonStringField(object, "breakAction"); breakAction.has_value())
            {
                definition.breakAction = breakAction->empty() ? "none" : *breakAction;
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
            if (const std::optional<bool> leafDecayable = jsonBoolField(object, "leafDecayable"); leafDecayable.has_value())
            {
                definition.leafDecayable = *leafDecayable;
            }
            if (const std::optional<bool> leafDecaySupport = jsonBoolField(object, "leafDecaySupport"); leafDecaySupport.has_value())
            {
                definition.leafDecaySupport = *leafDecaySupport;
            }
            if (const std::optional<std::string> stateKind = jsonStringField(object, "stateKind"); stateKind.has_value())
            {
                definition.stateKind = stateKind->empty() ? "none" : *stateKind;
            }
            if (const std::optional<std::string> breakEffects = jsonObjectField(object, "breakEffects"); breakEffects.has_value())
            {
                if (const std::optional<bool> particles = jsonBoolField(*breakEffects, "particles"); particles.has_value())
                {
                    definition.breakEffectParticles = *particles;
                }
            }
            if (const std::optional<std::string> attachment = jsonObjectField(object, "attachment"); attachment.has_value())
            {
                if (const std::optional<std::string> face = jsonStringField(*attachment, "face"); face.has_value())
                {
                    definition.attachmentFace = face->empty() ? "none" : *face;
                }
            }
            if (const std::optional<std::string> interactActions = jsonArrayField(object, "interactActions"); interactActions.has_value())
            {
                definition.interactActions = jsonStringArrayValues(*interactActions);
            }
            if (const std::optional<std::string> textures = jsonObjectField(object, "textures"); textures.has_value())
            {
                for (const char* key : TextureKeys)
                {
                    const std::optional<std::string> textureValue = jsonFieldValue(*textures, key);
                    if (!textureValue.has_value())
                    {
                        continue;
                    }
                    if (std::string(key) == "verticalSection" || std::string(key) == "horizontalSection")
                    {
                        if (const std::optional<std::string> texture = jsonStringLiteralValue(*textureValue); texture.has_value() && !texture->empty())
                        {
                            ParsedBlockTextureDefinition sectionTexture{};
                            sectionTexture.texture = *texture;
                            definition.textures[key] = sectionTexture;
                        }
                        continue;
                    }
                    if (const std::optional<ParsedBlockTextureDefinition> texture = parseBlockTextureDefinition(*textureValue); texture.has_value())
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
            if (const std::optional<std::string> texture = jsonStringField(object, "texture"); texture.has_value())
            {
                definition.texture = *texture;
            }
            if (const std::optional<int> lightAttenuation = jsonIntField(object, "lightAttenuation"); lightAttenuation.has_value())
            {
                definition.lightAttenuation = static_cast<uint8_t>(std::clamp(*lightAttenuation, 0, 15));
            }
            definitions.push_back(std::move(definition));
        }

        return definitions;
    }

    std::vector<ParsedInteractionDefinition> parseInteractionDefinitions(const std::string& text)
    {
        std::vector<ParsedInteractionDefinition> definitions;

        for (const std::string& object : jsonTopLevelObjects(text))
        {
            ParsedInteractionDefinition definition{};
            if (const std::optional<std::string> action = jsonStringField(object, "action"); action.has_value())
            {
                definition.action = *action;
            }
            if (const std::optional<std::string> target = jsonStringField(object, "target"); target.has_value())
            {
                definition.target = *target;
            }
            if (const std::optional<std::string> targetBlock = jsonStringField(object, "targetBlock"); targetBlock.has_value())
            {
                definition.targetBlock = *targetBlock;
            }
            if (const std::optional<std::string> held = jsonStringField(object, "held"); held.has_value())
            {
                definition.held = *held;
            }
            if (const std::optional<std::string> resultTarget = jsonStringField(object, "resultTarget"); resultTarget.has_value())
            {
                definition.resultTarget = resultTarget->empty() ? "target" : *resultTarget;
            }
            definition.targetCount = clampedInteractionCount(jsonIntField(object, "targetCount").value_or(1));
            const int minCount = jsonIntField(object, "min").value_or(1);
            const int maxCount = jsonIntField(object, "max").value_or(minCount);
            definition.resultCountMin = clampedInteractionCount(minCount);
            definition.resultCountMax = clampedInteractionCount(maxCount);
            if (definition.resultCountMax < definition.resultCountMin)
            {
                definition.resultCountMax = definition.resultCountMin;
            }
            if (const std::optional<std::string> candidates = jsonArrayField(object, "candidates"); candidates.has_value())
            {
                for (const std::string& candidateValue : jsonTopLevelArrayValues(*candidates))
                {
                    ParsedInteractionCandidate candidate = parseInteractionCandidate(
                        candidateValue,
                        definition.resultCountMin,
                        definition.resultCountMax);
                    if (!candidate.outputs.empty())
                    {
                        definition.candidates.push_back(std::move(candidate));
                    }
                }
            }
            if (const std::optional<std::string> ingredients = jsonArrayField(object, "ingredients"); ingredients.has_value())
            {
                for (const std::string& ingredientValue : jsonTopLevelArrayValues(*ingredients))
                {
                    if (const std::optional<ParsedInteractionIngredient> ingredient = parseInteractionIngredient(ingredientValue); ingredient.has_value())
                    {
                        definition.ingredients.push_back(*ingredient);
                    }
                }
            }
            if (!definition.action.empty() &&
                (!definition.target.empty() || !definition.targetBlock.empty()) &&
                !definition.candidates.empty())
            {
                definitions.push_back(std::move(definition));
            }
        }

        return definitions;
    }

    std::vector<ParsedProcessingDefinition> parseProcessingDefinitions(const std::string& text)
    {
        std::vector<ParsedProcessingDefinition> definitions;

        for (const std::string& object : jsonTopLevelObjects(text))
        {
            ParsedProcessingDefinition definition{};
            if (const std::optional<std::string> type = jsonStringField(object, "type"); type.has_value())
            {
                definition.type = *type;
            }
            if (const std::optional<std::string> input = jsonStringField(object, "input"); input.has_value())
            {
                definition.input = *input;
            }
            if (const std::optional<std::string> output = jsonStringField(object, "output"); output.has_value())
            {
                definition.output = *output;
            }
            if (const std::optional<std::string> outputFluid = jsonStringField(object, "outputFluid"); outputFluid.has_value())
            {
                definition.outputFluid = *outputFluid;
            }
            if (const std::optional<int> outputCount = jsonIntField(object, "outputCount"); outputCount.has_value())
            {
                definition.outputCount = static_cast<uint16_t>(std::clamp(*outputCount, 1, static_cast<int>(std::numeric_limits<uint16_t>::max())));
            }
            if (const std::optional<int> outputAmount = jsonIntField(object, "outputAmount"); outputAmount.has_value())
            {
                definition.outputAmount = static_cast<uint16_t>(std::clamp(*outputAmount, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
            }
            if (const std::optional<int> requiredHeatLevel = jsonIntField(object, "requiredHeatLevel"); requiredHeatLevel.has_value())
            {
                definition.requiredHeatLevel = static_cast<uint16_t>(std::clamp(*requiredHeatLevel, 0, static_cast<int>(std::numeric_limits<uint16_t>::max())));
            }
            if (const std::optional<int> requiredTicks = jsonIntField(object, "requiredTicks"); requiredTicks.has_value())
            {
                definition.requiredTicks = static_cast<uint32_t>(std::max(*requiredTicks, 0));
            }
            if (!definition.input.empty() &&
                (!definition.output.empty() || !definition.outputFluid.empty()) &&
                definition.requiredTicks > 0)
            {
                definitions.push_back(std::move(definition));
            }
        }

        return definitions;
    }
}
