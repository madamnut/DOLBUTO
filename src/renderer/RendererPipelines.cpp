#include "renderer/Renderer.h"

#include "platform/RuntimePaths.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dolbuto
{
    namespace
    {
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
    }

    void Renderer::createDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorCount = 1;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = 1;
        createInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(vulkan_.device, &createInfo, nullptr, &vulkan_.descriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor set layout.");
        }

    }



    void Renderer::createTerrainVertexDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorCount = 1;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = 1;
        createInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(vulkan_.device, &createInfo, nullptr, &vulkan_.terrainVertexDescriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain vertex descriptor set layout.");
        }
    }



    void Renderer::createShadowDescriptorSetLayout()
    {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorCount = 1;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorCount = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorCount = 1;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        createInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(vulkan_.device, &createInfo, nullptr, &vulkan_.shadowDescriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create shadow descriptor set layout.");
        }
    }



    void Renderer::createGodRayDescriptorSetLayout()
    {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorCount = 1;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorCount = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorCount = 1;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        createInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(vulkan_.device, &createInfo, nullptr, &vulkan_.godRayDescriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create god ray descriptor set layout.");
        }
    }



    void Renderer::createSkyPipeline()
    {
        const std::filesystem::path shaderDir = shaderDirectory();
        VkShaderModule vertShader = createShaderModule((shaderDir / "sky.vert.spv").string());
        VkShaderModule fragShader = createShaderModule((shaderDir / "sky.frag.spv").string());

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.blendEnable = VK_FALSE;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        std::array<VkPipelineColorBlendAttachmentState, 2> sceneColorBlends = {colorBlend, colorBlend};

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = static_cast<uint32_t>(sceneColorBlends.size());
        colorBlending.pAttachments = sceneColorBlends.data();

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(SkyRenderPath::Push);
        static_assert(sizeof(SkyRenderPath::Push) == sizeof(float) * 24);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(vulkan_.device, &layoutInfo, nullptr, &vulkan_.skyPipelineLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create sky pipeline layout.");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = vulkan_.skyPipelineLayout;
        pipelineInfo.renderPass = vulkan_.sceneRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.skyPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create sky pipeline.");
        }

        vkDestroyShaderModule(vulkan_.device, fragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, vertShader, nullptr);
    }



    void Renderer::createGodRayPipeline()
    {
        const std::filesystem::path shaderDir = shaderDirectory();
        VkShaderModule vertShader = createShaderModule((shaderDir / "god_rays.vert.spv").string());
        VkShaderModule fragShader = createShaderModule((shaderDir / "god_rays.frag.spv").string());

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";

        std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState sceneBlend{};
        sceneBlend.blendEnable = VK_TRUE;
        sceneBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        sceneBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        sceneBlend.colorBlendOp = VK_BLEND_OP_ADD;
        sceneBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        sceneBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        sceneBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        sceneBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &sceneBlend;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(GodRayPush);
        static_assert(sizeof(GodRayPush) == sizeof(float) * 28);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &vulkan_.godRayDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(vulkan_.device, &layoutInfo, nullptr, &vulkan_.godRayPipelineLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create god ray pipeline layout.");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = vulkan_.godRayPipelineLayout;
        pipelineInfo.renderPass = vulkan_.postProcessLoadRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.godRayPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create god ray pipeline.");
        }

        vkDestroyShaderModule(vulkan_.device, fragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, vertShader, nullptr);
    }



    void Renderer::createPipeline()
    {
        const std::filesystem::path shaderDir = shaderDirectory();
        VkShaderModule vertShader = createShaderModule((shaderDir / "sprite.vert.spv").string());
        VkShaderModule fragShader = createShaderModule((shaderDir / "sprite.frag.spv").string());
        VkShaderModule sceneFragShader = createShaderModule((shaderDir / "sprite_scene.frag.spv").string());
        VkShaderModule waterBlurFragShader = createShaderModule((shaderDir / "kawase_blur.frag.spv").string());
        VkShaderModule bloomDownsampleFragShader = createShaderModule((shaderDir / "bloom_downsample.frag.spv").string());
        VkShaderModule bloomUpsampleFragShader = createShaderModule((shaderDir / "bloom_upsample.frag.spv").string());

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(TextRenderPath::TextVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attributes{};
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[0].offset = offsetof(TextRenderPath::TextVertex, x);
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[1].offset = offsetof(TextRenderPath::TextVertex, u);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDescription;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlend;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(SpriteRenderPath::Push);
        static_assert(sizeof(SpriteRenderPath::Push) == sizeof(float) * 12);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &vulkan_.descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(vulkan_.device, &layoutInfo, nullptr, &vulkan_.pipelineLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create pipeline layout.");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = vulkan_.pipelineLayout;
        pipelineInfo.renderPass = vulkan_.renderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.pipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create graphics pipeline.");
        }

        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.additiveSpritePipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create additive sprite pipeline.");
        }

        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        std::array<VkPipelineColorBlendAttachmentState, 2> sceneSpriteColorBlends = {colorBlend, colorBlend};
        colorBlending.attachmentCount = static_cast<uint32_t>(sceneSpriteColorBlends.size());
        colorBlending.pAttachments = sceneSpriteColorBlends.data();
        stages[1].module = sceneFragShader;
        pipelineInfo.renderPass = vulkan_.sceneRenderPass;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.sceneSpritePipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create scene sprite pipeline.");
        }

        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlend;
        stages[1].module = waterBlurFragShader;
        if (vkCreatePipelineLayout(vulkan_.device, &layoutInfo, nullptr, &vulkan_.waterBlurPipelineLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create water blur pipeline layout.");
        }

        colorBlend.blendEnable = VK_FALSE;
        pipelineInfo.layout = vulkan_.waterBlurPipelineLayout;
        pipelineInfo.renderPass = vulkan_.waterBlurRenderPass;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.waterBlurPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create water blur pipeline.");
        }

        stages[1].module = bloomDownsampleFragShader;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.bloomDownsamplePipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create bloom downsample pipeline.");
        }

        stages[1].module = bloomUpsampleFragShader;
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        pipelineInfo.renderPass = vulkan_.postProcessLoadRenderPass;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.bloomUpsamplePipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create bloom upsample pipeline.");
        }

        vkDestroyShaderModule(vulkan_.device, bloomUpsampleFragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, bloomDownsampleFragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, waterBlurFragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, sceneFragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, fragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, vertShader, nullptr);
    }



    void Renderer::createUiPipeline()
    {
        const std::filesystem::path shaderDir = shaderDirectory();
        VkShaderModule vertShader = createShaderModule((shaderDir / "rmlui.vert.spv").string());
        VkShaderModule fragShader = createShaderModule((shaderDir / "rmlui.frag.spv").string());

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(UiVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> attributes{};
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[0].offset = offsetof(UiVertex, x);
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[1].offset = offsetof(UiVertex, r);
        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[2].offset = offsetof(UiVertex, u);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDescription;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlend;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(UiPush);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &vulkan_.descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(vulkan_.device, &layoutInfo, nullptr, &vulkan_.uiPipelineLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create RmlUi pipeline layout.");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = vulkan_.uiPipelineLayout;
        pipelineInfo.renderPass = vulkan_.renderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.uiPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create RmlUi pipeline.");
        }

        vkDestroyShaderModule(vulkan_.device, fragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, vertShader, nullptr);
    }



    void Renderer::createTerrainPipeline()
    {
        const std::filesystem::path shaderDir = shaderDirectory();
        VkShaderModule vertShader = createShaderModule((shaderDir / "terrain.vert.spv").string());
        VkShaderModule terrainShadowVertShader = createShaderModule((shaderDir / "terrain_shadow.vert.spv").string());
        VkShaderModule playerVertShader = createShaderModule((shaderDir / "player_model.vert.spv").string());
        VkShaderModule playerShadowVertShader = createShaderModule((shaderDir / "player_shadow.vert.spv").string());
        VkShaderModule fragShader = createShaderModule((shaderDir / "terrain_lit.frag.spv").string());
        VkShaderModule shadowFragShader = createShaderModule((shaderDir / "terrain_shadow.frag.spv").string());
        VkShaderModule fluidFragShader = createShaderModule((shaderDir / "fluid.frag.spv").string());

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkVertexInputBindingDescription playerBindingDescription{};
        playerBindingDescription.binding = 0;
        playerBindingDescription.stride = sizeof(PlayerVertex);
        playerBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 7> playerAttributes{};
        playerAttributes[0].binding = 0;
        playerAttributes[0].location = 0;
        playerAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        playerAttributes[0].offset = offsetof(PlayerVertex, x);
        playerAttributes[1].binding = 0;
        playerAttributes[1].location = 1;
        playerAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        playerAttributes[1].offset = offsetof(PlayerVertex, u);
        playerAttributes[2].binding = 0;
        playerAttributes[2].location = 2;
        playerAttributes[2].format = VK_FORMAT_R32_SFLOAT;
        playerAttributes[2].offset = offsetof(PlayerVertex, ao);
        playerAttributes[3].binding = 0;
        playerAttributes[3].location = 3;
        playerAttributes[3].format = VK_FORMAT_R32_SFLOAT;
        playerAttributes[3].offset = offsetof(PlayerVertex, textureLayer);
        playerAttributes[4].binding = 0;
        playerAttributes[4].location = 4;
        playerAttributes[4].format = VK_FORMAT_R32_SFLOAT;
        playerAttributes[4].offset = offsetof(PlayerVertex, mipDistanceScale);
        playerAttributes[5].binding = 0;
        playerAttributes[5].location = 5;
        playerAttributes[5].format = VK_FORMAT_R32_UINT;
        playerAttributes[5].offset = offsetof(PlayerVertex, nodeIndex);
        playerAttributes[6].binding = 0;
        playerAttributes[6].location = 6;
        playerAttributes[6].format = VK_FORMAT_R8_UINT;
        playerAttributes[6].offset = offsetof(PlayerVertex, packedLight);

        VkPipelineVertexInputStateCreateInfo playerVertexInput{};
        playerVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        playerVertexInput.vertexBindingDescriptionCount = 1;
        playerVertexInput.pVertexBindingDescriptions = &playerBindingDescription;
        playerVertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(playerAttributes.size());
        playerVertexInput.pVertexAttributeDescriptions = playerAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.blendEnable = VK_FALSE;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        std::array<VkPipelineColorBlendAttachmentState, 2> sceneColorBlends = {colorBlend, colorBlend};

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = static_cast<uint32_t>(sceneColorBlends.size());
        colorBlending.pAttachments = sceneColorBlends.data();

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(TerrainPush);
        static_assert(sizeof(TerrainPush) == sizeof(float) * 28);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        std::array<VkDescriptorSetLayout, 3> terrainSetLayouts = {
            vulkan_.descriptorSetLayout,
            vulkan_.terrainVertexDescriptorSetLayout,
            vulkan_.shadowDescriptorSetLayout
        };
        layoutInfo.setLayoutCount = static_cast<uint32_t>(terrainSetLayouts.size());
        layoutInfo.pSetLayouts = terrainSetLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(vulkan_.device, &layoutInfo, nullptr, &vulkan_.terrainPipelineLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain pipeline layout.");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = vulkan_.terrainPipelineLayout;
        pipelineInfo.renderPass = vulkan_.sceneRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.terrainPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain pipeline.");
        }

        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        sceneColorBlends = {colorBlend, colorBlend};

        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.terrainFadePipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain fade pipeline.");
        }

        depthStencil.depthWriteEnable = VK_FALSE;

        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.terrainBlendPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain blend pipeline.");
        }

        stages[1].module = fluidFragShader;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.fluidPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create fluid pipeline.");
        }

        colorBlend.blendEnable = VK_FALSE;
        sceneColorBlends = {colorBlend, colorBlend};
        depthStencil.depthWriteEnable = VK_TRUE;
        stages[1].module = fragShader;
        rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.terrainWireframePipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain wireframe pipeline.");
        }

        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        vertStage.module = playerVertShader;
        stages[0] = vertStage;
        pipelineInfo.pVertexInputState = &playerVertexInput;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.playerPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create player pipeline.");
        }

        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.playerViewmodelPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create player viewmodel pipeline.");
        }

        VkPipelineDepthStencilStateCreateInfo shadowDepthStencil{};
        shadowDepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        shadowDepthStencil.depthTestEnable = VK_TRUE;
        shadowDepthStencil.depthWriteEnable = VK_TRUE;
        shadowDepthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendStateCreateInfo shadowColorBlending{};
        shadowColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 0.35f;
        rasterizer.depthBiasSlopeFactor = 0.75f;
        rasterizer.depthBiasClamp = 0.0f;
        stages[0].module = terrainShadowVertShader;
        stages[1].module = shadowFragShader;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pColorBlendState = &shadowColorBlending;
        pipelineInfo.pDepthStencilState = &shadowDepthStencil;
        pipelineInfo.renderPass = vulkan_.shadowRenderPass;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.terrainShadowPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create terrain shadow pipeline.");
        }

        VkPipelineShaderStageCreateInfo playerShadowStage{};
        playerShadowStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        playerShadowStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        playerShadowStage.module = playerShadowVertShader;
        playerShadowStage.pName = "main";

        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f;
        rasterizer.depthBiasSlopeFactor = 0.0f;
        pipelineInfo.stageCount = 1;
        pipelineInfo.pStages = &playerShadowStage;
        pipelineInfo.pVertexInputState = &playerVertexInput;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.playerShadowPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create player shadow pipeline.");
        }

        vkDestroyShaderModule(vulkan_.device, fragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, fluidFragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, shadowFragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, playerShadowVertShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, playerVertShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, terrainShadowVertShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, vertShader, nullptr);
    }



    void Renderer::createParticlePipeline()
    {
        const std::filesystem::path shaderDir = shaderDirectory();
        VkShaderModule vertShader = createShaderModule((shaderDir / "player.vert.spv").string());
        VkShaderModule itemVertShader = createShaderModule((shaderDir / "item.vert.spv").string());
        VkShaderModule itemViewmodelVertShader = createShaderModule((shaderDir / "item_viewmodel.vert.spv").string());
        VkShaderModule fragShader = createShaderModule((shaderDir / "terrain.frag.spv").string());
        VkShaderModule moltenFragShader = createShaderModule((shaderDir / "molten.frag.spv").string());

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";

        std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(TerrainVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 8> attributes{};
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(TerrainVertex, x);
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[1].offset = offsetof(TerrainVertex, u);
        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32_SFLOAT;
        attributes[2].offset = offsetof(TerrainVertex, ao);
        attributes[3].binding = 0;
        attributes[3].location = 3;
        attributes[3].format = VK_FORMAT_R32_SFLOAT;
        attributes[3].offset = offsetof(TerrainVertex, textureLayer);
        attributes[4].binding = 0;
        attributes[4].location = 4;
        attributes[4].format = VK_FORMAT_R32_SFLOAT;
        attributes[4].offset = offsetof(TerrainVertex, mipDistanceScale);
        attributes[5].binding = 0;
        attributes[5].location = 5;
        attributes[5].format = VK_FORMAT_R8_UINT;
        attributes[5].offset = offsetof(TerrainVertex, packedLight);
        attributes[6].binding = 0;
        attributes[6].location = 6;
        attributes[6].format = VK_FORMAT_R32_SFLOAT;
        attributes[6].offset = offsetof(TerrainVertex, alphaBlend);
        attributes[7].binding = 0;
        attributes[7].location = 7;
        attributes[7].format = VK_FORMAT_R32_SFLOAT;
        attributes[7].offset = offsetof(TerrainVertex, waterTint);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        std::array<VkVertexInputBindingDescription, 2> itemBindings{};
        itemBindings[0].binding = 0;
        itemBindings[0].stride = sizeof(DroppedItemRenderPath::ItemLocalVertex);
        itemBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        itemBindings[1].binding = 1;
        itemBindings[1].stride = sizeof(DroppedItemRenderPath::Instance);
        itemBindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        std::array<VkVertexInputAttributeDescription, 14> itemAttributes{};
        itemAttributes[0].binding = 0;
        itemAttributes[0].location = 0;
        itemAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        itemAttributes[0].offset = offsetof(DroppedItemRenderPath::ItemLocalVertex, x);
        itemAttributes[1].binding = 0;
        itemAttributes[1].location = 1;
        itemAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        itemAttributes[1].offset = offsetof(DroppedItemRenderPath::ItemLocalVertex, u);
        itemAttributes[2].binding = 0;
        itemAttributes[2].location = 2;
        itemAttributes[2].format = VK_FORMAT_R32_SFLOAT;
        itemAttributes[2].offset = offsetof(DroppedItemRenderPath::ItemLocalVertex, ao);
        itemAttributes[3].binding = 0;
        itemAttributes[3].location = 3;
        itemAttributes[3].format = VK_FORMAT_R32_SFLOAT;
        itemAttributes[3].offset = offsetof(DroppedItemRenderPath::ItemLocalVertex, textureLayer);
        itemAttributes[4].binding = 1;
        itemAttributes[4].location = 4;
        itemAttributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        itemAttributes[4].offset = offsetof(DroppedItemRenderPath::Instance, centerX);
        itemAttributes[5].binding = 1;
        itemAttributes[5].location = 5;
        itemAttributes[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        itemAttributes[5].offset = offsetof(DroppedItemRenderPath::Instance, rotationY);
        itemAttributes[6].binding = 1;
        itemAttributes[6].location = 6;
        itemAttributes[6].format = VK_FORMAT_R32G32B32_SFLOAT;
        itemAttributes[6].offset = offsetof(DroppedItemRenderPath::Instance, scaleX);
        itemAttributes[7].binding = 1;
        itemAttributes[7].location = 7;
        itemAttributes[7].format = VK_FORMAT_R32G32_SFLOAT;
        itemAttributes[7].offset = offsetof(DroppedItemRenderPath::Instance, skyLight);
        itemAttributes[8].binding = 1;
        itemAttributes[8].location = 8;
        itemAttributes[8].format = VK_FORMAT_R32_SFLOAT;
        itemAttributes[8].offset = offsetof(DroppedItemRenderPath::Instance, uvMirrorX);
        itemAttributes[9].binding = 1;
        itemAttributes[9].location = 9;
        itemAttributes[9].format = VK_FORMAT_R32_SFLOAT;
        itemAttributes[9].offset = offsetof(DroppedItemRenderPath::Instance, geometryMirrorX);
        itemAttributes[10].binding = 1;
        itemAttributes[10].location = 10;
        itemAttributes[10].format = VK_FORMAT_R32G32B32_SFLOAT;
        itemAttributes[10].offset = offsetof(DroppedItemRenderPath::Instance, basisXX);
        itemAttributes[11].binding = 1;
        itemAttributes[11].location = 11;
        itemAttributes[11].format = VK_FORMAT_R32G32B32_SFLOAT;
        itemAttributes[11].offset = offsetof(DroppedItemRenderPath::Instance, basisYX);
        itemAttributes[12].binding = 1;
        itemAttributes[12].location = 12;
        itemAttributes[12].format = VK_FORMAT_R32G32B32_SFLOAT;
        itemAttributes[12].offset = offsetof(DroppedItemRenderPath::Instance, basisZX);
        itemAttributes[13].binding = 1;
        itemAttributes[13].location = 13;
        itemAttributes[13].format = VK_FORMAT_R32_SFLOAT;
        itemAttributes[13].offset = offsetof(DroppedItemRenderPath::Instance, waterTint);

        VkPipelineVertexInputStateCreateInfo itemVertexInput{};
        itemVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        itemVertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(itemBindings.size());
        itemVertexInput.pVertexBindingDescriptions = itemBindings.data();
        itemVertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(itemAttributes.size());
        itemVertexInput.pVertexAttributeDescriptions = itemAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        std::array<VkPipelineColorBlendAttachmentState, 2> sceneColorBlends = {colorBlend, colorBlend};

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = static_cast<uint32_t>(sceneColorBlends.size());
        colorBlending.pAttachments = sceneColorBlends.data();

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(TerrainPush);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &vulkan_.descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(vulkan_.device, &layoutInfo, nullptr, &vulkan_.particlePipelineLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create particle pipeline layout.");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = vulkan_.particlePipelineLayout;
        pipelineInfo.renderPass = vulkan_.sceneRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.particlePipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create particle pipeline.");
        }

        depthStencil.depthWriteEnable = VK_TRUE;
        colorBlend.blendEnable = VK_FALSE;
        sceneColorBlends = {colorBlend, colorBlend};
        stages[0].module = itemVertShader;
        pipelineInfo.pVertexInputState = &itemVertexInput;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.itemPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create item pipeline.");
        }

        stages[1].module = moltenFragShader;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.crucibleMoltenPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create crucible molten pipeline.");
        }
        stages[1].module = fragShader;

        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        stages[0].module = itemViewmodelVertShader;
        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.itemViewmodelPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create item viewmodel pipeline.");
        }

        vkDestroyShaderModule(vulkan_.device, fragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, moltenFragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, itemViewmodelVertShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, itemVertShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, vertShader, nullptr);
    }



    void Renderer::createSelectionPipeline()
    {
        const std::filesystem::path shaderDir = shaderDirectory();
        VkShaderModule vertShader = createShaderModule((shaderDir / "selection.vert.spv").string());
        VkShaderModule fragShader = createShaderModule((shaderDir / "selection.frag.spv").string());

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(LineVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attribute{};
        attribute.binding = 0;
        attribute.location = 0;
        attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
        attribute.offset = offsetof(LineVertex, x);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDescription;
        vertexInput.vertexAttributeDescriptionCount = 1;
        vertexInput.pVertexAttributeDescriptions = &attribute;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.blendEnable = VK_FALSE;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        std::array<VkPipelineColorBlendAttachmentState, 2> sceneColorBlends = {colorBlend, colorBlend};

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = static_cast<uint32_t>(sceneColorBlends.size());
        colorBlending.pAttachments = sceneColorBlends.data();

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(TerrainPush);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(vulkan_.device, &layoutInfo, nullptr, &vulkan_.selectionPipelineLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create selection pipeline layout.");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = vulkan_.selectionPipelineLayout;
        pipelineInfo.renderPass = vulkan_.sceneRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(vulkan_.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vulkan_.selectionPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create selection pipeline.");
        }

        vkDestroyShaderModule(vulkan_.device, fragShader, nullptr);
        vkDestroyShaderModule(vulkan_.device, vertShader, nullptr);
    }



    void Renderer::createSampler()
    {
        VkSamplerCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        createInfo.magFilter = VK_FILTER_NEAREST;
        createInfo.minFilter = VK_FILTER_NEAREST;
        createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        createInfo.minLod = 0.0f;
        createInfo.maxLod = VK_LOD_CLAMP_NONE;

        if (vkCreateSampler(vulkan_.device, &createInfo, nullptr, &vulkan_.sampler) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create texture sampler.");
        }

        createInfo.magFilter = VK_FILTER_LINEAR;
        createInfo.minFilter = VK_FILTER_LINEAR;
        createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.maxLod = 0.0f;

        if (vkCreateSampler(vulkan_.device, &createInfo, nullptr, &vulkan_.linearSampler) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create linear texture sampler.");
        }

    }



    void Renderer::createDescriptorPool()
    {
        constexpr uint32_t MaxTextureDescriptorSets = 320;
        constexpr uint32_t MaxTerrainVertexDescriptorSets = 65536;
        constexpr uint32_t MaxShadowDescriptorSets = static_cast<uint32_t>(RendererVulkanState::FrameInFlightCount);
        constexpr uint32_t MaxShadowImageDescriptors = MaxShadowDescriptorSets * 2;
        constexpr uint32_t MaxGodRayDescriptorSets = static_cast<uint32_t>(RendererVulkanState::FrameInFlightCount);
        constexpr uint32_t MaxGodRayImageDescriptors = MaxGodRayDescriptorSets * 2;
        std::array<VkDescriptorPoolSize, 3> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = MaxTextureDescriptorSets + MaxShadowImageDescriptors + MaxGodRayImageDescriptors;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[1].descriptorCount = MaxTerrainVertexDescriptorSets;
        poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[2].descriptorCount = MaxShadowDescriptorSets + MaxGodRayDescriptorSets;

        VkDescriptorPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        createInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        createInfo.pPoolSizes = poolSizes.data();
        createInfo.maxSets = MaxTextureDescriptorSets + MaxTerrainVertexDescriptorSets + MaxShadowDescriptorSets + MaxGodRayDescriptorSets;

        if (vkCreateDescriptorPool(vulkan_.device, &createInfo, nullptr, &vulkan_.descriptorPool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor pool.");
        }
    }



    VkShaderModule Renderer::createShaderModule(const std::string& path) const
    {
        std::vector<char> code = readFile(path);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(vulkan_.device, &createInfo, nullptr, &module) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create shader module: " + path);
        }

        return module;
    }

}
