#include "config/ConfigLoaders.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace dolbuto::config
{
    namespace
    {
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

        std::optional<std::string> readTextFile(const std::filesystem::path& path)
        {
            std::ifstream file(path);
            if (!file.is_open())
            {
                return std::nullopt;
            }

            std::ostringstream contents;
            contents << file.rdbuf();
            return contents.str();
        }
    }

    WorldConfig loadWorldConfig(const std::filesystem::path& path, const WorldConfig& defaults, int maxSeaLevel)
    {
        WorldConfig config = defaults;
        const std::optional<std::string> text = readTextFile(path);
        if (!text.has_value())
        {
            return config;
        }

        const std::string chunkLoad = jsonObjectField(*text, "chunkLoad").value_or("{}");
        const std::string terrain = jsonObjectField(*text, "terrain").value_or("{}");
        const std::string groundnessDomainWarp = jsonObjectField(terrain, "groundnessDomainWarp").value_or(jsonObjectField(terrain, "domainWarp").value_or("{}"));
        const std::string groundnessNoise = jsonObjectField(terrain, "groundnessNoise").value_or(jsonObjectField(terrain, "baseNoise").value_or("{}"));
        const std::string terrainBaseNoise = jsonObjectField(terrain, "baseNoise").value_or("{}");
        const std::string smoothnessNoise = jsonObjectField(terrain, "smoothnessNoise").value_or("{}");
        const std::string weirdnessDomainWarp = jsonObjectField(terrain, "weirdnessDomainWarp").value_or("{}");
        const std::string weirdnessNoise = jsonObjectField(terrain, "weirdnessNoise").value_or("{}");
        const std::string climate = jsonObjectField(*text, "climate").value_or("{}");
        const std::string temperature = jsonObjectField(climate, "temperature").value_or("{}");
        const std::string precipitation = jsonObjectField(climate, "precipitation").value_or("{}");
        const std::string features = jsonObjectField(*text, "features").value_or("{}");

        if (const std::optional<int> value = jsonIntField(terrain, "seaLevel"); value.has_value())
        {
            config.seaLevel = std::clamp(*value, 0, maxSeaLevel);
        }
        if (const std::optional<int> value = jsonIntField(chunkLoad, "loadGridScale"); value.has_value())
        {
            config.loadGridScale = std::max(0, *value);
        }
        if (const std::optional<int> value = jsonIntField(chunkLoad, "workerCount"); value.has_value())
        {
            config.terrainWorkerCount = std::clamp(*value, 1, 16);
        }
        if (const std::optional<int> value = jsonIntField(chunkLoad, "maxCompletedChunksAppliedPerFrame"); value.has_value())
        {
            config.maxTerrainUploadChunksPerFrame = std::clamp(*value, 1, 64);
        }
        if (const std::optional<int> value = jsonIntField(chunkLoad, "maxUnloadedChunksPerFrame"); value.has_value())
        {
            config.maxTerrainUnloadChunksPerFrame = std::clamp(*value, 1, 64);
        }
        if (const std::optional<int> value = jsonIntField(chunkLoad, "maxRetiredChunksDestroyedPerFrame"); value.has_value())
        {
            config.maxTerrainRetiredDestroyPerFrame = std::clamp(*value, 1, 64);
        }
        if (const std::optional<float> value = jsonFloatField(groundnessNoise, "featureScale"); value.has_value() && *value > 0.0f)
        {
            config.groundnessNoiseFeatureScale = *value;
        }
        if (const std::optional<int> value = jsonIntField(groundnessNoise, "octaveCount"); value.has_value())
        {
            config.groundnessNoiseOctaveCount = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(groundnessNoise, "lacunarity"); value.has_value() && *value > 0.0f)
        {
            config.groundnessNoiseLacunarity = *value;
        }
        if (const std::optional<float> value = jsonFloatField(groundnessNoise, "gain"); value.has_value() && *value >= 0.0f)
        {
            config.groundnessNoiseGain = *value;
        }
        if (const std::optional<bool> value = jsonBoolField(groundnessDomainWarp, "enabled"); value.has_value())
        {
            config.groundnessDomainWarpEnabled = *value;
        }
        if (const std::optional<float> value = jsonFloatField(groundnessDomainWarp, "amplitude"); value.has_value() && *value >= 0.0f)
        {
            config.groundnessDomainWarpAmplitude = *value;
        }
        if (const std::optional<float> value = jsonFloatField(groundnessDomainWarp, "frequency"); value.has_value() && *value > 0.0f)
        {
            config.groundnessDomainWarpFrequency = *value;
        }
        if (const std::optional<int> value = jsonIntField(groundnessDomainWarp, "octaveCount"); value.has_value())
        {
            config.groundnessDomainWarpOctaveCount = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(groundnessDomainWarp, "gain"); value.has_value() && *value >= 0.0f)
        {
            config.groundnessDomainWarpGain = *value;
        }
        if (const std::optional<float> value = jsonFloatField(terrainBaseNoise, "featureScale"); value.has_value() && *value > 0.0f)
        {
            config.baseNoiseFeatureScale = *value;
        }
        if (const std::optional<int> value = jsonIntField(terrainBaseNoise, "octaveCount"); value.has_value())
        {
            config.baseNoiseOctaveCount = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(terrainBaseNoise, "lacunarity"); value.has_value() && *value > 0.0f)
        {
            config.baseNoiseLacunarity = *value;
        }
        if (const std::optional<float> value = jsonFloatField(terrainBaseNoise, "gain"); value.has_value() && *value >= 0.0f)
        {
            config.baseNoiseGain = *value;
        }
        if (const std::optional<float> value = jsonFloatField(smoothnessNoise, "featureScale"); value.has_value() && *value > 0.0f)
        {
            config.smoothnessNoiseFeatureScale = *value;
        }
        if (const std::optional<int> value = jsonIntField(smoothnessNoise, "octaveCount"); value.has_value())
        {
            config.smoothnessNoiseOctaveCount = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(smoothnessNoise, "lacunarity"); value.has_value() && *value > 0.0f)
        {
            config.smoothnessNoiseLacunarity = *value;
        }
        if (const std::optional<float> value = jsonFloatField(smoothnessNoise, "gain"); value.has_value() && *value >= 0.0f)
        {
            config.smoothnessNoiseGain = *value;
        }
        if (const std::optional<float> value = jsonFloatField(weirdnessNoise, "featureScale"); value.has_value() && *value > 0.0f)
        {
            config.weirdnessNoiseFeatureScale = *value;
        }
        if (const std::optional<int> value = jsonIntField(weirdnessNoise, "octaveCount"); value.has_value())
        {
            config.weirdnessNoiseOctaveCount = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(weirdnessNoise, "lacunarity"); value.has_value() && *value > 0.0f)
        {
            config.weirdnessNoiseLacunarity = *value;
        }
        if (const std::optional<float> value = jsonFloatField(weirdnessNoise, "gain"); value.has_value() && *value >= 0.0f)
        {
            config.weirdnessNoiseGain = *value;
        }
        if (const std::optional<bool> value = jsonBoolField(weirdnessDomainWarp, "enabled"); value.has_value())
        {
            config.weirdnessDomainWarpEnabled = *value;
        }
        if (const std::optional<float> value = jsonFloatField(weirdnessDomainWarp, "amplitude"); value.has_value() && *value >= 0.0f)
        {
            config.weirdnessDomainWarpAmplitude = *value;
        }
        if (const std::optional<float> value = jsonFloatField(weirdnessDomainWarp, "frequency"); value.has_value() && *value > 0.0f)
        {
            config.weirdnessDomainWarpFrequency = *value;
        }
        if (const std::optional<int> value = jsonIntField(weirdnessDomainWarp, "octaveCount"); value.has_value())
        {
            config.weirdnessDomainWarpOctaveCount = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(weirdnessDomainWarp, "gain"); value.has_value() && *value >= 0.0f)
        {
            config.weirdnessDomainWarpGain = *value;
        }
        if (const std::optional<float> value = jsonFloatField(temperature, "noiseStrength"); value.has_value() && *value >= 0.0f)
        {
            config.temperatureNoiseStrength = *value;
        }
        if (const std::optional<float> value = jsonFloatField(temperature, "noiseFeatureScale"); value.has_value() && *value > 0.0f)
        {
            config.temperatureNoiseFeatureScale = *value;
        }
        if (const std::optional<int> value = jsonIntField(temperature, "noiseOctaveCount"); value.has_value())
        {
            config.temperatureNoiseOctaveCount = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(temperature, "noiseLacunarity"); value.has_value() && *value > 0.0f)
        {
            config.temperatureNoiseLacunarity = *value;
        }
        if (const std::optional<float> value = jsonFloatField(temperature, "noiseGain"); value.has_value() && *value >= 0.0f)
        {
            config.temperatureNoiseGain = *value;
        }
        if (const std::optional<float> value = jsonFloatField(temperature, "noiseSimplexScale"); value.has_value() && *value > 0.0f)
        {
            config.temperatureNoiseSimplexScale = *value;
        }
        if (const std::optional<float> value = jsonFloatField(precipitation, "featureScale"); value.has_value() && *value > 0.0f)
        {
            config.precipitationNoiseFeatureScale = *value;
        }
        if (const std::optional<int> value = jsonIntField(precipitation, "octaveCount"); value.has_value())
        {
            config.precipitationNoiseOctaveCount = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(precipitation, "lacunarity"); value.has_value() && *value > 0.0f)
        {
            config.precipitationNoiseLacunarity = *value;
        }
        if (const std::optional<float> value = jsonFloatField(precipitation, "gain"); value.has_value() && *value >= 0.0f)
        {
            config.precipitationNoiseGain = *value;
        }
        if (const std::optional<float> value = jsonFloatField(precipitation, "simplexScale"); value.has_value() && *value > 0.0f)
        {
            config.precipitationNoiseSimplexScale = *value;
        }
        if (const std::optional<std::string> ores = jsonArrayField(features, "ores"); ores.has_value())
        {
            config.oreFeatures.clear();
            for (const std::string& object : jsonTopLevelObjects(*ores))
            {
                WorldOreFeatureConfig ore{};
                ore.name = jsonStringField(object, "name").value_or("");
                ore.enabled = jsonBoolField(object, "enabled").value_or(true);
                ore.block = jsonStringField(object, "block").value_or("");
                ore.replace = jsonStringField(object, "replace").value_or("");
                ore.minY = std::clamp(jsonIntField(object, "minY").value_or(0), 0, maxSeaLevel);
                ore.maxY = std::clamp(jsonIntField(object, "maxY").value_or(maxSeaLevel + 1), ore.minY + 1, maxSeaLevel + 1);
                ore.attemptsPerChunk = std::clamp(jsonIntField(object, "attemptsPerChunk").value_or(0), 0, 512);
                ore.size = std::clamp(jsonIntField(object, "size").value_or(0), 0, 256);
                if (!ore.name.empty() && !ore.block.empty() && !ore.replace.empty())
                {
                    config.oreFeatures.push_back(std::move(ore));
                }
            }
        }

        return config;
    }

    RenderConfig loadRenderConfig(const std::filesystem::path& path, const RenderConfig& defaults)
    {
        RenderConfig config = defaults;
        const std::optional<std::string> text = readTextFile(path);
        if (!text.has_value())
        {
            return config;
        }

        const std::string fluid = jsonObjectField(*text, "fluid").value_or("{}");
        const std::string water = jsonObjectField(fluid, "water").value_or("{}");
        const std::string screenBlur = jsonObjectField(water, "screenBlur").value_or("{}");
        const std::string bloom = jsonObjectField(*text, "bloom").value_or("{}");

        if (const std::optional<float> value = jsonFloatField(water, "alpha"); value.has_value())
        {
            config.fluidWaterAlpha = std::clamp(*value, 0.0f, 1.0f);
        }
        if (const std::optional<bool> value = jsonBoolField(screenBlur, "enabled"); value.has_value())
        {
            config.fluidWaterScreenBlurEnabled = *value;
        }
        if (const std::optional<float> value = jsonFloatField(screenBlur, "spread"); value.has_value())
        {
            config.fluidWaterScreenBlurSpread = std::clamp(*value, 0.0f, 8.0f);
        }
        if (const std::optional<float> value = jsonFloatField(screenBlur, "intensity"); value.has_value())
        {
            config.fluidWaterScreenBlurIntensity = std::clamp(*value, 0.0f, 1.0f);
        }
        if (const std::optional<float> value = jsonFloatField(screenBlur, "tint"); value.has_value())
        {
            config.fluidWaterScreenBlurTint = std::clamp(*value, 0.0f, 1.0f);
        }
        if (const std::optional<bool> value = jsonBoolField(bloom, "enabled"); value.has_value())
        {
            config.bloomEnabled = *value;
        }
        if (const std::optional<float> value = jsonFloatField(bloom, "threshold"); value.has_value())
        {
            config.bloomThreshold = std::clamp(*value, 0.0f, 8.0f);
        }
        if (const std::optional<float> value = jsonFloatField(bloom, "intensity"); value.has_value())
        {
            config.bloomIntensity = std::clamp(*value, 0.0f, 2.0f);
        }
        if (const std::optional<float> value = jsonFloatField(bloom, "radius"); value.has_value())
        {
            config.bloomRadius = std::clamp(*value, 0.0f, 8.0f);
        }

        return config;
    }

    ViewmodelConfig loadViewmodelConfig(const std::filesystem::path& path, const ViewmodelConfig& defaults)
    {
        ViewmodelConfig config = defaults;
        const std::optional<std::string> text = readTextFile(path);
        if (!text.has_value())
        {
            return config;
        }

        const std::string hand = jsonObjectField(*text, "hand").value_or("{}");
        const std::string heldItem = jsonObjectField(*text, "heldItem").value_or("{}");
        const std::string heldBlockModelItem = jsonObjectField(*text, "heldBlockModelItem").value_or("{}");

        auto applyHeldItemConfig = [](const std::string& source, ViewmodelHeldItemConfig& target)
        {
            if (const std::optional<float> value = jsonFloatField(source, "x"); value.has_value())
            {
                target.x = *value;
            }
            if (const std::optional<float> value = jsonFloatField(source, "y"); value.has_value())
            {
                target.y = *value;
            }
            if (const std::optional<float> value = jsonFloatField(source, "z"); value.has_value())
            {
                target.z = *value;
            }
            if (const std::optional<float> value = jsonFloatField(source, "scale"); value.has_value() && *value > 0.0f)
            {
                target.scale = *value;
            }
            if (const std::optional<float> value = jsonFloatField(source, "rotationX"); value.has_value())
            {
                target.rotationX = *value;
            }
            if (const std::optional<float> value = jsonFloatField(source, "rotationY"); value.has_value())
            {
                target.rotationY = *value;
            }
            if (const std::optional<float> value = jsonFloatField(source, "rotationZ"); value.has_value())
            {
                target.rotationZ = *value;
            }
        };

        if (const std::optional<float> value = jsonFloatField(hand, "x"); value.has_value())
        {
            config.hand.x = *value;
        }
        else if (const std::optional<float> value = jsonFloatField(hand, "right"); value.has_value())
        {
            config.hand.x = *value;
        }
        if (const std::optional<float> value = jsonFloatField(hand, "y"); value.has_value())
        {
            config.hand.y = *value;
        }
        else if (const std::optional<float> value = jsonFloatField(hand, "down"); value.has_value())
        {
            config.hand.y = -*value;
        }
        if (const std::optional<float> value = jsonFloatField(hand, "z"); value.has_value())
        {
            config.hand.z = *value;
        }
        else if (const std::optional<float> value = jsonFloatField(hand, "forward"); value.has_value())
        {
            config.hand.z = *value;
        }
        if (const std::optional<float> value = jsonFloatField(hand, "scale"); value.has_value() && *value > 0.0f)
        {
            config.hand.scale = *value;
        }
        if (const std::optional<float> value = jsonFloatField(hand, "rotationX"); value.has_value())
        {
            config.hand.rotationX = *value;
        }
        if (const std::optional<float> value = jsonFloatField(hand, "rotationY"); value.has_value())
        {
            config.hand.rotationY = *value;
        }
        if (const std::optional<float> value = jsonFloatField(hand, "rotationZ"); value.has_value())
        {
            config.hand.rotationZ = *value;
        }

        applyHeldItemConfig(heldItem, config.heldItem);
        config.heldBlockModelItem = config.heldItem;
        applyHeldItemConfig(heldBlockModelItem, config.heldBlockModelItem);

        return config;
    }
}
