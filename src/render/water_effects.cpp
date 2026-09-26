#include "render/water_effects.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace sandbox {
namespace {
constexpr VkFormat reflection_format = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat metadata_format = VK_FORMAT_R32G32_SFLOAT;
void transition(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout before,
                VkImageLayout after, VkPipelineStageFlags2 source, VkAccessFlags2 source_access,
                VkPipelineStageFlags2 target, VkAccessFlags2 target_access, uint32_t base_mip = 0,
                uint32_t mip_count = 1) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = source;
    barrier.srcAccessMask = source_access;
    barrier.dstStageMask = target;
    barrier.dstAccessMask = target_access;
    barrier.oldLayout = before;
    barrier.newLayout = after;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, base_mip, mip_count, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}
} // namespace
WaterEffects::WaterEffects(Renderer& renderer, VkDescriptorSetLayout faces, VkDescriptorSetLayout environment,
                           bool ice, VkDescriptorSetLayout lod_coverage)
    : renderer_(renderer), ice_(ice) {
    static_assert(sizeof(Uniform) == 224);
    static_assert(offsetof(Uniform, camera_time) == 128 && offsetof(Uniform, extras) == 192 &&
                  offsetof(Uniform, trace) == 208);
    try {
        const VkDescriptorSetLayoutBinding bindings[]{
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo scene{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        scene.bindingCount = 5;
        scene.pBindings = bindings;
        vk_check(vkCreateDescriptorSetLayout(renderer_.device, &scene, nullptr, &scene_layout_),
                 "water scene layout");
        VkDescriptorSetLayout sets[]{renderer_.texture_layout, faces, scene_layout_, environment};
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 96};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 4;
        layout.pSetLayouts = sets;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(renderer_.device, &layout, nullptr, &layout_), "water layout");
        if (lod_coverage) {
            sets[1] = lod_coverage;
            push.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            vk_check(vkCreatePipelineLayout(renderer_.device, &layout, nullptr, &lod_layout_),
                     "LOD water layout");
        }
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = VK_LOD_CLAMP_NONE;
        vk_check(vkCreateSampler(renderer_.device, &sampler, nullptr, &nearest_), "water depth sampler");
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        vk_check(vkCreateSampler(renderer_.device, &sampler, nullptr, &linear_), "water colour sampler");
        const VkDescriptorPoolSize sizes[]{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, Renderer::frames_in_flight * 4},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Renderer::frames_in_flight}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = Renderer::frames_in_flight;
        pool.poolSizeCount = 2;
        pool.pPoolSizes = sizes;
        vk_check(vkCreateDescriptorPool(renderer_.device, &pool, nullptr, &pool_), "water descriptor pool");
        create_pipeline(false);
        create_pipeline(true);
        if (lod_layout_) {
            create_pipeline(false, true);
            create_pipeline(true, true);
        }
    } catch (...) {
        shutdown();
        throw;
    }
}
WaterEffects::~WaterEffects() { shutdown(); }
void WaterEffects::shutdown() noexcept {
    vkDeviceWaitIdle(renderer_.device);
    release_images();
    if (surface_)
        vkDestroyPipeline(renderer_.device, surface_, nullptr);
    if (reflection_)
        vkDestroyPipeline(renderer_.device, reflection_, nullptr);
    for (auto pipeline : {lod_surface_, lod_reflection_})
        if (pipeline)
            vkDestroyPipeline(renderer_.device, pipeline, nullptr);
    if (lod_layout_)
        vkDestroyPipelineLayout(renderer_.device, lod_layout_, nullptr);
    if (pool_)
        vkDestroyDescriptorPool(renderer_.device, pool_, nullptr);
    if (layout_)
        vkDestroyPipelineLayout(renderer_.device, layout_, nullptr);
    if (scene_layout_)
        vkDestroyDescriptorSetLayout(renderer_.device, scene_layout_, nullptr);
    if (linear_)
        vkDestroySampler(renderer_.device, linear_, nullptr);
    if (nearest_)
        vkDestroySampler(renderer_.device, nearest_, nullptr);
}
void WaterEffects::create_pipeline(bool reflection, bool lod) {
    VkShaderModule vertex{}, fragment{};
    try {
        vertex = renderer_.shader(lod ? "shaders/lod_water.vert.spv" : "shaders/world.vert.spv");
        fragment = renderer_.shader(
            lod ? (reflection ? "shaders/lod_water_ssr.frag.spv" : "shaders/lod_water.frag.spv")
                : (reflection ? "shaders/water_ssr.frag.spv" : "shaders/water.frag.spv"));
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (auto& stage : stages) {
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.pName = "main";
        }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        const VkVertexInputBindingDescription binding{0, 16, VK_VERTEX_INPUT_RATE_INSTANCE};
        const VkVertexInputAttributeDescription attribute{0, 0, VK_FORMAT_R32G32B32A32_UINT, 0};
        if (lod) {
            input.vertexBindingDescriptionCount = 1;
            input.pVertexBindingDescriptions = &binding;
            input.vertexAttributeDescriptionCount = 1;
            input.pVertexAttributeDescriptions = &attribute;
        }
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = VK_TRUE;
        // LOD tile order is independent of transparency. Keep only its nearest surface;
        // SSR/refraction still read the separate opaque snapshot taken before this pass.
        depth.depthWriteEnable = reflection || ice_ || lod ? VK_TRUE : VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = 0xf;
        attachment.blendEnable = VK_FALSE; // Surface shader composites in reference gamma space.
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo blending{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        const VkPipelineColorBlendAttachmentState attachments[]{attachment, attachment};
        blending.attachmentCount = reflection ? 2u : 1u;
        blending.pAttachments = attachments;
        const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = states;
        const VkFormat format = reflection ? reflection_format : Renderer::scene_format;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        const VkFormat formats[]{format, metadata_format};
        rendering.colorAttachmentCount = reflection ? 2u : 1u;
        rendering.pColorAttachmentFormats = formats;
        rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.pNext = &rendering;
        pipeline.stageCount = 2;
        pipeline.pStages = stages;
        pipeline.pVertexInputState = &input;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &samples;
        pipeline.pDepthStencilState = &depth;
        pipeline.pColorBlendState = &blending;
        pipeline.pDynamicState = &dynamic;
        pipeline.layout = lod ? lod_layout_ : layout_;
        vk_check(vkCreateGraphicsPipelines(renderer_.device, VK_NULL_HANDLE, 1, &pipeline, nullptr,
                                           lod ? (reflection ? &lod_reflection_ : &lod_surface_)
                                               : (reflection ? &reflection_ : &surface_)),
                 "water effect pipeline");
    } catch (...) {
        if (vertex)
            vkDestroyShaderModule(renderer_.device, vertex, nullptr);
        if (fragment)
            vkDestroyShaderModule(renderer_.device, fragment, nullptr);
        throw;
    }
    vkDestroyShaderModule(renderer_.device, vertex, nullptr);
    vkDestroyShaderModule(renderer_.device, fragment, nullptr);
}
WaterEffects::Image WaterEffects::create_image(VkFormat format, VkExtent2D size, VkImageUsageFlags usage,
                                               VkImageAspectFlags aspect, uint32_t levels) {
    Image result;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(renderer_.physical_device, format, &properties);
    VkFormatFeatureFlags required{};
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
        required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        required |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        required |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        required |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (aspect == VK_IMAGE_ASPECT_COLOR_BIT && format != metadata_format)
        required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
        throw std::runtime_error("Water effect image format is unsupported.");
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {size.width, size.height, 1};
    info.mipLevels = levels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vk_check(
        vmaCreateImage(renderer_.allocator, &info, &allocation, &result.handle, &result.allocation, nullptr),
        "water image");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = result.handle;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect, 0, levels, 0, 1};
    const auto status = vkCreateImageView(renderer_.device, &view, nullptr, &result.view);
    if (status != VK_SUCCESS) {
        vmaDestroyImage(renderer_.allocator, result.handle, result.allocation);
        vk_check(status, "water image view");
    }
    return result;
}
void WaterEffects::release_images() noexcept {
    for (auto& frame : frames_) {
        for (auto* image :
             {&frame.colour, &frame.depth, &frame.reflection, &frame.reflection_depth, &frame.metadata}) {
            if (image->view)
                vkDestroyImageView(renderer_.device, image->view, nullptr);
            if (image->handle)
                vmaDestroyImage(renderer_.allocator, image->handle, image->allocation);
        }
        renderer_.destroy_buffer(frame.uniform);
        frame = {};
    }
    extent_ = {};
}
void WaterEffects::ensure_images() {
    if (extent_.width == renderer_.extent.width && extent_.height == renderer_.extent.height)
        return;
    renderer_.wait_idle(); // Resource recreation only, never for a normal frame or option toggle.
    vk_check(vkResetDescriptorPool(renderer_.device, pool_, 0), "reset water descriptors");
    release_images();
    // ceil(3 * dimension / 4), without overflow in the intermediate product.
    reflection_extent_ = {renderer_.extent.width - renderer_.extent.width / 4,
                          renderer_.extent.height - renderer_.extent.height / 4};
    for (auto& frame : frames_) {
        frame.colour = create_image(Renderer::scene_format, renderer_.extent,
                                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                    VK_IMAGE_ASPECT_COLOR_BIT);
        frame.depth = create_image(VK_FORMAT_D32_SFLOAT, renderer_.extent,
                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   VK_IMAGE_ASPECT_DEPTH_BIT);
        frame.reflection = create_image(reflection_format, reflection_extent_,
                                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                        VK_IMAGE_ASPECT_COLOR_BIT);
        frame.metadata = create_image(metadata_format, reflection_extent_,
                                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                      VK_IMAGE_ASPECT_COLOR_BIT);
        frame.reflection_depth =
            create_image(VK_FORMAT_D32_SFLOAT, reflection_extent_,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        frame.uniform = renderer_.create_buffer(sizeof(Uniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = pool_;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &scene_layout_;
        vk_check(vkAllocateDescriptorSets(renderer_.device, &allocate, &frame.descriptor),
                 "water descriptors");
        const VkDescriptorImageInfo images[]{
            {linear_, frame.colour.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {nearest_, frame.depth.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {nearest_, frame.reflection.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {nearest_, frame.metadata.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkDescriptorBufferInfo buffer{frame.uniform.handle, 0, sizeof(Uniform)};
        std::array<VkWriteDescriptorSet, 5> writes{};
        for (uint32_t i = 0; i < writes.size(); ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = frame.descriptor;
            writes[i].dstBinding = i == 4 ? 5 : i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType =
                i == 3 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            if (i == 3)
                writes[i].pBufferInfo = &buffer;
            else
                writes[i].pImageInfo = &images[i > 3 ? i - 1 : i];
        }
        vkUpdateDescriptorSets(renderer_.device, static_cast<uint32_t>(writes.size()), writes.data(), 0,
                               nullptr);
    }
    extent_ = renderer_.extent;
}
void WaterEffects::prepare(const glm::mat4& matrix, glm::dvec3 camera, glm::vec4 sky,
                           const WaterSettings& settings, float time, float reflection_distance,
                           bool underwater) {
    ensure_images();
    needs_reflection_ = settings.enabled && settings.ssr;
    const Uniform uniform{
        matrix,
        glm::inverse(matrix),
        glm::vec4(float(std::remainder(camera.x, 131072.0)), float(camera.y),
                  float(std::remainder(camera.z, 131072.0)), time),
        sky,
        glm::vec4(!underwater && settings.depth, settings.enabled && settings.waves, needs_reflection_,
                  underwater),
        glm::vec4(float(extent_.width), float(extent_.height), float(reflection_extent_.width),
                  float(reflection_extent_.height)),
        glm::vec4(0, settings.enabled && !underwater && settings.depth && settings.foam, settings.enabled, 0),
        glm::vec4(std::clamp(reflection_distance, 16.0f, 1024.0f), 0, 0, 0)};
    auto& frame = frames_[renderer_.frame_slot()];
    std::memcpy(frame.uniform.mapped, &uniform, sizeof(uniform));
    vk_check(vmaFlushAllocation(renderer_.allocator, frame.uniform.allocation, 0, sizeof(uniform)),
             "water uniform flush");
}
void WaterEffects::snapshot(VkImage depth) {
    auto& frame = frames_[renderer_.frame_slot()];
    const auto cmd = renderer_.command;
    // Each slot's fence has completed. Preserve opaque colour/depth for the
    // reference gamma-space translucent blend, even with only waves enabled.
    transition(cmd, frame.colour.handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
               VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    transition(cmd, frame.depth.handle, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
               VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    renderer_.snapshot_scene(frame.colour.handle, depth, frame.depth.handle);
    transition(cmd, frame.colour.handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
               VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
               VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    transition(cmd, frame.depth.handle, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
               VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
               VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    // Valid descriptor layout also when reflection sampling is disabled in the shader.
    transition(cmd, frame.reflection.handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    transition(cmd, frame.metadata.handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}
void WaterEffects::begin_reflections() {
    auto& frame = frames_[renderer_.frame_slot()];
    const auto cmd = renderer_.command;
    transition(cmd, frame.reflection.handle, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    transition(cmd, frame.metadata.handle, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    transition(cmd, frame.reflection_depth.handle, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
               VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    VkRenderingAttachmentInfo colour{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colour.imageView = frame.reflection.view;
    colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = frame.reflection_depth.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1, 0};
    VkRenderingAttachmentInfo colours[]{colour, colour};
    colours[1].imageView = frame.metadata.view;
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent = reflection_extent_;
    info.layerCount = 1;
    info.colorAttachmentCount = 2;
    info.pColorAttachments = colours;
    info.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &info);
    const VkViewport viewport{0, 0, float(reflection_extent_.width), float(reflection_extent_.height), 0, 1};
    const VkRect2D scissor{{0, 0}, reflection_extent_};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, reflection_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 2, 1, &frame.descriptor, 0,
                            nullptr);
}
void WaterEffects::end_reflections() {
    vkCmdEndRendering(renderer_.command);
    transition(renderer_.command, frames_[renderer_.frame_slot()].reflection.handle,
               VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
               VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    transition(renderer_.command, frames_[renderer_.frame_slot()].metadata.handle, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}
void WaterEffects::bind_surface(bool lod) {
    const VkViewport viewport{0, 0, float(extent_.width), float(extent_.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(renderer_.command, 0, 1, &viewport);
    vkCmdSetScissor(renderer_.command, 0, 1, &scissor);
    vkCmdBindPipeline(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS, lod ? lod_surface_ : surface_);
    vkCmdBindDescriptorSets(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout(lod), 2, 1,
                            &frames_[renderer_.frame_slot()].descriptor, 0, nullptr);
}
void WaterEffects::bind_lod_reflections() {
    vkCmdBindPipeline(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS, lod_reflection_);
    vkCmdBindDescriptorSets(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS, lod_layout_, 2, 1,
                            &frames_[renderer_.frame_slot()].descriptor, 0, nullptr);
}
} // namespace sandbox
