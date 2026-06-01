#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dolbuto::data
{
    struct ParsedBlockTextureDefinition
    {
        std::string texture;
        std::string base;
        std::string mask;
    };

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
        uint16_t breakLevel = 0;
        std::string breakAction = "none";
        uint8_t lightAttenuation = 15;
        uint8_t lightEmission = 0;
        bool randomOffset = false;
        bool breakEffectParticles = true;
        std::string stateKind = "none";
        std::string attachmentFace = "none";
        std::unordered_map<std::string, ParsedBlockTextureDefinition> textures;
        std::string propModel;
        std::string propTexture;
        std::vector<std::string> interactActions;
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
        std::string slotRender = "sprite";
        std::string slotRenderTexture = "none";
        std::string droppedTexture = "none";
        std::string heldTexture = "none";
        std::string droppedRender = "extruded_sprite";
        std::string heldRender = "extruded_sprite";
        std::vector<std::string> useActions;
        std::vector<std::string> breakActions;
        std::vector<std::string> placeActions;
        uint16_t breakLevel = 0;
        uint16_t maxDurability = 0;
        uint32_t burnTimeTicks = 0;
        std::string placeBlock;
        std::string modelBlock;
        std::string modelShape;
    };

    struct ParsedInteractionOutput
    {
        std::string item;
        std::string block;
        std::string placement;
        uint16_t min = 1;
        uint16_t max = 1;
    };

    struct ParsedInteractionCandidate
    {
        std::vector<ParsedInteractionOutput> outputs;
    };

    struct ParsedInteractionIngredient
    {
        std::string item;
        uint16_t count = 1;
    };

    struct ParsedInteractionDefinition
    {
        std::string action;
        std::string target;
        std::string targetBlock;
        std::vector<ParsedInteractionCandidate> candidates;
        std::vector<ParsedInteractionIngredient> ingredients;
        uint16_t targetCount = 1;
        uint16_t resultCountMin = 1;
        uint16_t resultCountMax = 1;
    };

    std::vector<ParsedItemDefinition> parseItemDefinitions(const std::string& text);
    std::vector<ParsedBlockDefinition> parseBlockDefinitions(const std::string& text);
    std::vector<ParsedFluidDefinition> parseFluidDefinitions(const std::string& text);
    std::vector<ParsedInteractionDefinition> parseInteractionDefinitions(const std::string& text);
}
