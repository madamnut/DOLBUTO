#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dolbuto::data
{
    struct ParsedBlockDefinition
    {
        uint16_t id = 0;
        std::string name = "unknown";
        std::string renderType = "none";
        bool directional = false;
        bool collision = false;
        bool ao = false;
        std::string faceOcclusion = "none";
        bool sameBlockFaceCulling = false;
        std::string alphaMode = "opaque";
        float alphaCutoff = 0.5f;
        float alphaBlend = 1.0f;
        float mipDistanceScale = 1.0f;
        float hardness = -1.0f;
        uint8_t lightAttenuation = 15;
        uint8_t lightEmission = 0;
        bool randomOffset = false;
        std::unordered_map<std::string, std::string> textures;
        std::string propModel;
        std::string propTexture;
        std::vector<std::string> dropItemKeys;
        std::vector<uint16_t> dropMins;
        std::vector<uint16_t> dropMaxes;
        std::vector<float> dropChances;
    };

    struct ParsedFluidDefinition
    {
        uint16_t id = 0;
        std::string name = "none";
        uint8_t lightAttenuation = 0;
    };

    struct ParsedItemDefinition
    {
        uint16_t id = 0;
        std::string key = "none";
        std::string name = "None";
        uint16_t stackSize = 0;
        std::string texture = "none";
        std::string slotTexture = "none";
        std::string droppedTexture = "none";
        std::string heldTexture = "none";
        std::string droppedRender = "extruded_sprite";
        std::string heldRender = "extruded_sprite";
    };

    std::vector<ParsedItemDefinition> parseItemDefinitions(const std::string& text);
    std::vector<ParsedBlockDefinition> parseBlockDefinitions(const std::string& text);
    std::vector<ParsedFluidDefinition> parseFluidDefinitions(const std::string& text);
}
