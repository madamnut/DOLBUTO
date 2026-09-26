#include "ui/rml_renderer.hpp"
#include <RmlUi/Core/Matrix4.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <limits>
#include <stb_image.h>
#include <tracy/Tracy.hpp>

namespace sandbox {
RmlRenderer::RmlRenderer(Renderer& renderer) : renderer_(renderer) {
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &renderer_.texture_layout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    vk_check(vkCreatePipelineLayout(renderer_.device, &layout, nullptr, &layout_), "UI pipeline layout");
    const auto vert = renderer_.shader("shaders/ui.vert.spv");
    const auto frag = renderer_.shader("shaders/ui.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto& stage : stages) {
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    VkVertexInputBindingDescription binding{0, sizeof(Rml::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Rml::Vertex, position)},
        {1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Rml::Vertex, colour)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Rml::Vertex, tex_coord)}};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount = 1;
    vertex.pVertexBindingDescriptions = &binding;
    vertex.vertexAttributeDescriptionCount = 3;
    vertex.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = states;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &renderer_.colour_format;
    VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.pNext = &rendering;
    pipeline.stageCount = 2;
    pipeline.pStages = stages;
    pipeline.pVertexInputState = &vertex;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &ms;
    pipeline.pColorBlendState = &blending;
    pipeline.pDynamicState = &dynamic;
    pipeline.layout = layout_;
    const auto result =
        vkCreateGraphicsPipelines(renderer_.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &pipeline_);
    vkDestroyShaderModule(renderer_.device, vert, nullptr);
    vkDestroyShaderModule(renderer_.device, frag, nullptr);
    vk_check(result, "RmlUi graphics pipeline");
    const unsigned char white[] = {255, 255, 255, 255};
    white_ = renderer_.create_texture(white, 1, 1, false);
}
RmlRenderer::~RmlRenderer() {
    vkDeviceWaitIdle(renderer_.device);
    for (const auto& [handle, g] : geometry_) {
        renderer_.destroy_buffer(g.vertices);
        renderer_.destroy_buffer(g.indices);
    }
    for (const auto& [handle, t] : textures_)
        renderer_.destroy_texture(t);
    renderer_.destroy_texture(white_);
    vkDestroyPipeline(renderer_.device, pipeline_, nullptr);
    vkDestroyPipelineLayout(renderer_.device, layout_, nullptr);
}
Rml::CompiledGeometryHandle RmlRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                         Rml::Span<const int> indices) {
    ZoneScoped;
    if (vertices.empty() || indices.empty())
        return 0;
    Geometry g{};
    g.count = static_cast<uint32_t>(indices.size());
    g.vertices =
        renderer_.create_buffer(vertices.size() * sizeof(Rml::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    g.indices = renderer_.create_buffer(indices.size() * sizeof(int), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    std::memcpy(g.vertices.mapped, vertices.data(), vertices.size() * sizeof(Rml::Vertex));
    std::memcpy(g.indices.mapped, indices.data(), indices.size() * sizeof(int));
    vk_check(vmaFlushAllocation(renderer_.allocator, g.vertices.allocation, 0, VK_WHOLE_SIZE),
             "UI vertices flush");
    vk_check(vmaFlushAllocation(renderer_.allocator, g.indices.allocation, 0, VK_WHOLE_SIZE),
             "UI indices flush");
    const auto handle = next_handle_++;
    geometry_.emplace(handle, g);
    return handle;
}
void RmlRenderer::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                                 Rml::TextureHandle texture_handle) {
    const auto it = geometry_.find(handle);
    if (it == geometry_.end())
        return;
    const auto& g = it->second;
    VkRect2D scissor{{0, 0}, renderer_.extent};
    if (scissor_enabled_) {
        const int x = std::clamp(scissor_x_, 0, static_cast<int>(renderer_.extent.width));
        const int y = std::clamp(scissor_y_, 0, static_cast<int>(renderer_.extent.height));
        const int right =
            std::clamp(scissor_x_ + scissor_width_, x, static_cast<int>(renderer_.extent.width));
        const int bottom =
            std::clamp(scissor_y_ + scissor_height_, y, static_cast<int>(renderer_.extent.height));
        scissor = {{x, y}, {static_cast<uint32_t>(right - x), static_cast<uint32_t>(bottom - y)}};
    }
    if (!scissor.extent.width || !scissor.extent.height)
        return;
    auto descriptor = white_.descriptor;
    if (auto texture_it = textures_.find(texture_handle); texture_it != textures_.end())
        descriptor = texture_it->second.descriptor;
    glm::mat4 projection(1);
    projection[0][0] = 2.0f / static_cast<float>(renderer_.extent.width);
    projection[1][1] = 2.0f / static_cast<float>(renderer_.extent.height);
    projection[3][0] = -1;
    projection[3][1] = -1;
    const auto matrix =
        projection * transform_ * glm::translate(glm::mat4(1), glm::vec3(translation.x, translation.y, 0));
    const auto cmd = renderer_.command;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkViewport viewport{
        0, 0, static_cast<float>(renderer_.extent.width), static_cast<float>(renderer_.extent.height), 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &descriptor, 0, nullptr);
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(matrix), glm::value_ptr(matrix));
    const VkDeviceSize offset{};
    vkCmdBindVertexBuffers(cmd, 0, 1, &g.vertices.handle, &offset);
    vkCmdBindIndexBuffer(cmd, g.indices.handle, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, g.count, 1, 0, 0, 0);
    ++draw_calls;
}
void RmlRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle handle) {
    const auto it = geometry_.find(handle);
    if (it == geometry_.end())
        return;
    const auto geometry = it->second;
    geometry_.erase(it);
    auto* renderer = &renderer_;
    renderer_.defer([renderer, geometry] {
        renderer->destroy_buffer(geometry.vertices);
        renderer->destroy_buffer(geometry.indices);
    });
}
Rml::TextureHandle RmlRenderer::texture(const unsigned char* pixels, int width, int height, bool nearest) {
    const auto handle = next_handle_++;
    textures_.emplace(handle, renderer_.create_texture(pixels, width, height, nearest));
    return handle;
}
Rml::TextureHandle RmlRenderer::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
    int channels{};
    auto* pixels = stbi_load(source.c_str(), &dimensions.x, &dimensions.y, &channels, 4);
    if (!pixels) {
        ++failed_textures;
        std::cerr << "Cannot load texture: " << source << '\n';
        return 0;
    }
    // RmlUi colours and texture data use premultiplied alpha.
    for (size_t i = 0; i < static_cast<size_t>(dimensions.x) * dimensions.y * 4; i += 4)
        for (size_t c = 0; c < 3; ++c)
            pixels[i + c] = static_cast<unsigned char>(
                (static_cast<unsigned>(pixels[i + c]) * pixels[i + 3] + 127) / 255);
    const auto handle = texture(pixels, dimensions.x, dimensions.y, true);
    stbi_image_free(pixels);
    return handle;
}
Rml::TextureHandle RmlRenderer::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) {
    return texture(source.data(), dimensions.x, dimensions.y, false);
}
void RmlRenderer::ReleaseTexture(Rml::TextureHandle handle) {
    const auto it = textures_.find(handle);
    if (it == textures_.end())
        return;
    const auto t = it->second;
    textures_.erase(it);
    auto* renderer = &renderer_;
    renderer_.defer([renderer, t] { renderer->destroy_texture(t); });
}
void RmlRenderer::SetScissorRegion(Rml::Rectanglei region) {
    scissor_x_ = region.Left();
    scissor_y_ = region.Top();
    scissor_width_ = region.Width();
    scissor_height_ = region.Height();
}
void RmlRenderer::SetTransform(const Rml::Matrix4f* matrix) {
    transform_ = matrix ? glm::make_mat4(matrix->data()) : glm::mat4(1);
}
} // namespace sandbox
