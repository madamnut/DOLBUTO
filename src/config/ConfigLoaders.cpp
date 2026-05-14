#include "config/ConfigLoaders.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

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
        const std::string terrainDomainWarp = jsonObjectField(terrain, "domainWarp").value_or("{}");
        const std::string terrainBaseNoise = jsonObjectField(terrain, "baseNoise").value_or("{}");
        const std::string climate = jsonObjectField(*text, "climate").value_or("{}");
        const std::string temperature = jsonObjectField(climate, "temperature").value_or("{}");
        const std::string precipitation = jsonObjectField(climate, "precipitation").value_or("{}");

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
        if (const std::optional<float> value = jsonFloatField(terrainBaseNoise, "featureScale"); value.has_value() && *value > 0.0f)
        {
            config.terrainNoiseFeatureScale = *value;
        }
        if (const std::optional<int> value = jsonIntField(terrainBaseNoise, "octaveCount"); value.has_value())
        {
            config.terrainNoiseOctaveCount = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(terrainBaseNoise, "lacunarity"); value.has_value() && *value > 0.0f)
        {
            config.terrainNoiseLacunarity = *value;
        }
        if (const std::optional<float> value = jsonFloatField(terrainBaseNoise, "gain"); value.has_value() && *value >= 0.0f)
        {
            config.terrainNoiseGain = *value;
        }
        if (const std::optional<float> value = jsonFloatField(terrainBaseNoise, "simplexScale"); value.has_value() && *value > 0.0f)
        {
            config.terrainNoiseSimplexScale = *value;
        }
        if (const std::optional<bool> value = jsonBoolField(terrainDomainWarp, "enabled"); value.has_value())
        {
            config.terrainDomainWarpEnabled = *value;
        }
        if (const std::optional<float> value = jsonFloatField(terrainDomainWarp, "amplitude"); value.has_value() && *value >= 0.0f)
        {
            config.terrainDomainWarpAmplitude = *value;
        }
        if (const std::optional<float> value = jsonFloatField(terrainDomainWarp, "frequency"); value.has_value() && *value > 0.0f)
        {
            config.terrainDomainWarpFrequency = *value;
        }
        if (const std::optional<int> value = jsonIntField(terrainDomainWarp, "octaveCount"); value.has_value())
        {
            config.terrainDomainWarpOctaveCount = std::clamp(*value, 1, 16);
        }
        if (const std::optional<float> value = jsonFloatField(terrainDomainWarp, "gain"); value.has_value() && *value >= 0.0f)
        {
            config.terrainDomainWarpGain = *value;
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

        if (const std::optional<float> value = jsonFloatField(water, "alpha"); value.has_value())
        {
            config.fluidWaterAlpha = std::clamp(*value, 0.0f, 1.0f);
        }

        return config;
    }
}
