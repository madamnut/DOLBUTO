#include "render/scene_effects.hpp"
#include "core/world_rules.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <stb_image.h>
#include <stdexcept>

namespace sandbox {
namespace {
constexpr VkFormat shadow_colour_format = VK_FORMAT_R8G8B8A8_UNORM;
void barrier(VkCommandBuffer cmd, VkImage image, bool depth, VkImageLayout before, VkImageLayout after,
             uint32_t layers = 1) {
    // Orders shared shadow-cache reads and writes across submissions on the graphics queue.
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.oldLayout = before;
    b.newLayout = after;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    const VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange = {aspect, 0, 1, 0, layers};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dependency);
}
void viewport(VkCommandBuffer cmd, VkExtent2D size) {
    VkViewport v{0, 0, float(size.width), float(size.height), 0, 1};
    VkRect2D s{{0, 0}, size};
    vkCmdSetViewport(cmd, 0, 1, &v);
    vkCmdSetScissor(cmd, 0, 1, &s);
}
} // namespace
SceneEffects::SceneEffects(Renderer& renderer, VkDescriptorSetLayout faces) : renderer_(renderer) {
    static_assert(sizeof(Uniform) == 464 && offsetof(Uniform, flags) == 256 &&
                  offsetof(Uniform, hydro) == 320 && offsetof(Uniform, previous_camera) == 352 &&
                  offsetof(Uniform, temporal) == 416 && offsetof(Uniform, view) == 448);
    try {
        const VkDescriptorSetLayoutBinding env[]{
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 9;
        info.pBindings = env;
        vk_check(vkCreateDescriptorSetLayout(renderer_.device, &info, nullptr, &environment_layout_),
                 "environment layout");
        std::array<VkDescriptorSetLayoutBinding, 8> images{};
        for (uint32_t i = 0; i < images.size(); ++i)
            images[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                         nullptr};
        info.bindingCount = uint32_t(images.size());
        info.pBindings = images.data();
        vk_check(vkCreateDescriptorSetLayout(renderer_.device, &info, nullptr, &image_layout_),
                 "post image layout");
        VkDescriptorSetLayout post_sets[]{image_layout_, environment_layout_};
        VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 2;
        layout.pSetLayouts = post_sets;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(renderer_.device, &layout, nullptr, &post_layout_), "post layout");
        VkDescriptorSetLayout world_sets[]{renderer_.texture_layout, faces, environment_layout_};
        push = {VK_SHADER_STAGE_VERTEX_BIT, 0, 96};
        layout.setLayoutCount = 3;
        layout.pSetLayouts = world_sets;
        vk_check(vkCreatePipelineLayout(renderer_.device, &layout, nullptr, &shadow_layout_),
                 "shadow layout");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vk_check(vkCreateSampler(renderer_.device, &sampler, nullptr, &nearest_), "scene nearest sampler");
        sampler.minFilter = sampler.magFilter = VK_FILTER_LINEAR;
        vk_check(vkCreateSampler(renderer_.device, &sampler, nullptr, &linear_), "scene linear sampler");
        sampler.compareEnable = VK_TRUE;
        sampler.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        vk_check(vkCreateSampler(renderer_.device, &sampler, nullptr, &compare_),
                 "shadow comparison sampler");
        sampler.compareEnable = VK_FALSE;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        vk_check(vkCreateSampler(renderer_.device, &sampler, nullptr, &repeat_), "reference noise sampler");
        auto load_noise = [&](const char* path) {
            int width{}, height{}, channels{};
            std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
                stbi_load(path, &width, &height, &channels, 4), stbi_image_free);
            if (!pixels)
                throw std::runtime_error(std::string("Cannot load lighting noise: ") + path);
            return renderer_.create_texture(pixels.get(), width, height, false);
        };
        reference_noise_ = load_noise("assets/textures/effects/complementary_noise.png");
        reference_water_ = load_noise("assets/textures/effects/complementary_cloud_water.png");
        const VkDescriptorPoolSize sizes[]{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, Renderer::frames_in_flight * 196},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Renderer::frames_in_flight}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = Renderer::frames_in_flight * 24;
        pool.poolSizeCount = 2;
        pool.pPoolSizes = sizes;
        vk_check(vkCreateDescriptorPool(renderer_.device, &pool, nullptr, &pool_), "scene descriptor pool");
        const VkFormat shadow_formats[]{shadow_colour_format, shadow_colour_format};
        shadow_pipeline_ =
            create_pipeline("world.vert", "shadow.frag", shadow_layout_, shadow_formats, 2, true);
        player_shadow_pipeline_ =
            create_pipeline("player.vert", "shadow.frag", shadow_layout_, shadow_formats, 2, true);
        water_depth_pipeline_ =
            create_pipeline("world.vert", nullptr, shadow_layout_, nullptr, 0, true, false);
        const VkFormat volume_formats[]{Renderer::scene_format, VK_FORMAT_R32G32_SFLOAT};
        volume_pipeline_ = create_pipeline("fullscreen.vert", "volume.frag", post_layout_, volume_formats, 2);
        atmosphere_pipeline_ =
            create_pipeline("fullscreen.vert", "atmosphere.frag", post_layout_, volume_formats, 1);
        bloom_pipeline_ = create_pipeline("fullscreen.vert", "bloom.frag", post_layout_, volume_formats, 1);
        tone_pipeline_ = create_pipeline("fullscreen.vert", "tone.frag", post_layout_, volume_formats, 1);
        taa_pipeline_ = create_pipeline("fullscreen.vert", "taa.frag", post_layout_, volume_formats, 1);
        factor_pipeline_ =
            create_pipeline("fullscreen.vert", "scene_factor.frag", post_layout_, volume_formats, 1);
        present_pipeline_ =
            create_pipeline("fullscreen.vert", "present.frag", post_layout_, &renderer_.colour_format, 1);
    } catch (...) {
        shutdown();
        throw;
    }
}
SceneEffects::~SceneEffects() { shutdown(); }
VkPipeline SceneEffects::create_pipeline(const char* vertex_name, const char* fragment_name,
                                         VkPipelineLayout layout, const VkFormat* formats, uint32_t colours,
                                         bool shadow, bool bias) {
    VkShaderModule vertex{}, fragment{};
    VkPipeline result{};
    try {
        vertex = renderer_.shader(std::string("shaders/") + vertex_name + ".spv");
        if (fragment_name)
            fragment = renderer_.shader(std::string("shaders/") + fragment_name + ".spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[0].pName = "main";
        const VkBool32 distort = shadow && bias;
        const VkSpecializationMapEntry entry{1, 0, sizeof(distort)};
        const VkSpecializationInfo specialization{1, &entry, sizeof(distort), &distort};
        if (shadow)
            stages[0].pSpecializationInfo = &specialization;
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE; // Include cave ceilings and detached casters.
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1;
        raster.depthBiasEnable = VK_FALSE; // Original receiver bias precedes shadow distortion.
        raster.depthBiasConstantFactor = 1.25f;
        // The receiver plane supplies the slope correction. Raster slope bias
        // changed by whole depth steps when the rotating sun changed the caster
        // at a texel, making stationary silhouettes breathe.
        raster.depthBiasSlopeFactor = 0.0f;
        VkPipelineMultisampleStateCreateInfo samples{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = depth.depthWriteEnable = shadow;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        std::array<VkPipelineColorBlendAttachmentState, 2> attachments{};
        for (auto& a : attachments)
            a.colorWriteMask = 0xf;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = colours;
        blend.pAttachments = attachments.data();
        const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = states;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = colours;
        rendering.pColorAttachmentFormats = formats;
        rendering.depthAttachmentFormat = shadow ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED;
        VkGraphicsPipelineCreateInfo p{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        p.pNext = &rendering;
        p.stageCount = fragment ? 2 : 1;
        p.pStages = stages;
        p.pVertexInputState = &input;
        p.pInputAssemblyState = &assembly;
        p.pViewportState = &vp;
        p.pRasterizationState = &raster;
        p.pMultisampleState = &samples;
        p.pDepthStencilState = &depth;
        p.pColorBlendState = &blend;
        p.pDynamicState = &dynamic;
        p.layout = layout;
        vk_check(vkCreateGraphicsPipelines(renderer_.device, VK_NULL_HANDLE, 1, &p, nullptr, &result),
                 "scene effect pipeline");
    } catch (...) {
        if (result)
            vkDestroyPipeline(renderer_.device, result, nullptr);
        if (vertex)
            vkDestroyShaderModule(renderer_.device, vertex, nullptr);
        if (fragment)
            vkDestroyShaderModule(renderer_.device, fragment, nullptr);
        throw;
    }
    vkDestroyShaderModule(renderer_.device, vertex, nullptr);
    if (fragment)
        vkDestroyShaderModule(renderer_.device, fragment, nullptr);
    return result;
}
SceneEffects::Image SceneEffects::create_image(VkFormat format, VkExtent2D size, VkImageUsageFlags usage,
                                               uint32_t layers) {
    Image result{};
    result.extent = size;
    const bool depth = format == VK_FORMAT_D32_SFLOAT;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(renderer_.physical_device, format, &properties);
    VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        (depth ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
    if (!depth && format != VK_FORMAT_R32G32_SFLOAT)
        required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if (format == Renderer::scene_format)
        required |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
    if (usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        required |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
        throw std::runtime_error("Required HDR/shadow image format is unsupported.");
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {size.width, size.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = layers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vk_check(
        vmaCreateImage(renderer_.allocator, &info, &allocation, &result.handle, &result.allocation, nullptr),
        "scene image");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = result.handle;
    view.viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    const VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange = {aspect, 0, 1, 0, layers};
    auto status = vkCreateImageView(renderer_.device, &view, nullptr, &result.view);
    if (status != VK_SUCCESS) {
        vmaDestroyImage(renderer_.allocator, result.handle, result.allocation);
        vk_check(status, "scene image view");
    }
    return result;
}
VkDescriptorSet SceneEffects::allocate(VkDescriptorSetLayout layout) {
    VkDescriptorSet result{};
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorPool = pool_;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    vk_check(vkAllocateDescriptorSets(renderer_.device, &info, &result), "scene descriptor");
    return result;
}
void SceneEffects::update_descriptors(uint32_t count, const VkWriteDescriptorSet* writes) {
    vkUpdateDescriptorSets(renderer_.device, count, writes, 0, nullptr);
    if (profile_descriptors) {
        ++descriptor_stats.calls;
        descriptor_stats.writes += count;
    }
}
void SceneEffects::write_images(VkDescriptorSet set, const std::array<VkImageView, 4>& images, bool depth) {
    std::array<VkDescriptorImageInfo, 4> infos{};
    std::array<VkWriteDescriptorSet, 4> writes{};
    for (uint32_t i = 0; i < 4; ++i) {
        infos[i] = {(depth && (i == 1 || i == 3)) ? nearest_ : linear_, images[i],
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &infos[i];
    }
    update_descriptors(4, writes.data());
}
void SceneEffects::ensure_images(std::array<uint32_t, 2> sizes) {
    if (sizes[0] != sizes[1])
        throw std::runtime_error("Shared solid shadow depth requires matching map extents.");
    if (extent_.width == renderer_.extent.width && extent_.height == renderer_.extent.height &&
        shadow_sizes_ == sizes)
        return;
    renderer_.wait_idle();
    release_images();
    vk_check(vkResetDescriptorPool(renderer_.device, pool_, 0), "scene pool reset");
    extent_ = renderer_.extent;
    shadow_sizes_ = sizes;
    shadows_initialized_ = false;
    history_valid_ = false;
    history_dirty_ = true;
    for (uint32_t i = 0; i < shadows_.size(); ++i)
        shadows_[i] = create_image(VK_FORMAT_D32_SFLOAT, {sizes[i], sizes[i]},
                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    const auto colour_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    for (auto& c : shadow_colour_)
        c = create_image(shadow_colour_format, {sizes[0], sizes[0]},
                         colour_usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    for (auto& f : frames_) {
        f.opaque_depth = create_image(VK_FORMAT_D32_SFLOAT, extent_,
                                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        f.toned = create_image(Renderer::scene_format, extent_, colour_usage);
        f.history = create_image(Renderer::scene_format, extent_, colour_usage);
        f.scene_factor = create_image(Renderer::scene_format, {1, 1}, colour_usage);
        f.scene =
            create_image(Renderer::scene_format, extent_, colour_usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        f.composite =
            create_image(Renderer::scene_format, extent_, colour_usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        VkExtent2D half{std::max(1u, (extent_.width + 1) / 2), std::max(1u, (extent_.height + 1) / 2)};
        f.volume = create_image(Renderer::scene_format, half, colour_usage);
        f.metadata = create_image(VK_FORMAT_R32G32_SFLOAT, half, colour_usage);
        half = {std::max(1u, extent_.width / 2), std::max(1u, extent_.height / 2)};
        for (uint32_t i = 0; i < f.bloom.size(); ++i) {
            f.bloom[i] = create_image(Renderer::scene_format, half, colour_usage);
            f.bloom_mips[i] = create_image(Renderer::scene_format, half, colour_usage);
            half = {std::max(1u, half.width / 2), std::max(1u, half.height / 2)};
        }
        f.uniform = renderer_.create_buffer(sizeof(Uniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        f.environment = allocate(environment_layout_);
        VkDescriptorBufferInfo buffer{f.uniform.handle, 0, sizeof(Uniform)};
        const VkDescriptorImageInfo shadows[]{
            {compare_, shadows_[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {compare_, shadows_[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {linear_, shadow_colour_[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[4]{};
        for (uint32_t i = 0; i < 4; ++i) {
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[i].dstSet = f.environment;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
        }
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &buffer;
        for (uint32_t i = 1; i < 4; ++i) {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &shadows[i - 1];
        }
        update_descriptors(4, writes);
        const VkDescriptorImageInfo extras[]{
            {linear_, shadow_colour_[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {nearest_, shadows_[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {repeat_, reference_noise_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {repeat_, reference_water_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet extra_writes[4]{};
        for (uint32_t i = 0; i < 4; ++i) {
            extra_writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            extra_writes[i].dstSet = f.environment;
            extra_writes[i].dstBinding = 5 + i;
            extra_writes[i].descriptorCount = 1;
            extra_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            extra_writes[i].pImageInfo = &extras[i];
        }
        update_descriptors(4, extra_writes);
        f.atmosphere = allocate(image_layout_);
        f.volume_composite = allocate(image_layout_);
        f.tone = allocate(image_layout_);
        f.taa = allocate(image_layout_);
        f.present = allocate(image_layout_);
        f.factor = allocate(image_layout_);
        write_images(f.present, {f.history.view, f.history.view, f.history.view, f.history.view});
        for (uint32_t i = 0; i < f.bloom.size(); ++i) {
            f.bloom_sets[i] = allocate(image_layout_);
            f.bloom_mip_sets[i] = allocate(image_layout_);
            const auto source = i ? f.bloom_mips[i - 1].view : f.scene.view;
            write_images(f.bloom_mip_sets[i], {source, f.opaque_depth.view, source, source}, true);
            write_images(
                f.bloom_sets[i],
                {f.bloom_mips[i].view, f.opaque_depth.view, f.bloom_mips[i].view, f.opaque_depth.view}, true);
        }
        write_images(f.tone, {f.scene.view, f.bloom[1].view, f.bloom[3].view, f.bloom[4].view});
    }
}
glm::mat4 SceneEffects::jittered(const glm::mat4& matrix, bool enabled) const {
    if (!enabled)
        return matrix;
    constexpr glm::vec2 offsets[]{{0.125f, -0.375f}, {-0.125f, 0.375f},  {0.625f, 0.125f},  {0.375f, -0.625f},
                                  {-0.625f, 0.625f}, {-0.875f, -0.125f}, {0.375f, -0.875f}, {0.875f, 0.875f}};
    const auto offset =
        offsets[frame_index_ % 8] * 0.125f / glm::vec2(renderer_.extent.width, renderer_.extent.height);
    auto result = matrix;
    for (int column = 0; column < 4; ++column) {
        result[column][0] += offset.x * matrix[column][3];
        result[column][1] -= offset.y * matrix[column][3];
    }
    return result;
}
void SceneEffects::prepare(const GraphicsSettings& settings, const WaterSettings& water,
                           const glm::mat4& matrix, glm::dvec3 camera, double hours, float daylight,
                           double time, bool lod_debug, float sky_visibility, bool underwater,
                           float render_distance) {
    const int quality = std::clamp(settings.shadow_quality, 1, 3);
    ensure_images({512u << quality, 512u << quality});
    descriptor_stats = {};
    uniform_ = {};
    underwater_ = underwater;
    uniform_.inverse = glm::inverse(matrix);
    // Map our 06:00..20:00 day onto the reference's solar cycle (24 real minutes/day).
    constexpr double pi = 3.141592653589793;
    const double solar_phase = hours >= 6 && hours < 20
                                   ? (hours - 6) / 28.0
                                   : 0.5 + ((hours >= 20 ? hours : hours + 24) - 20) / 20.0;
    // common.glsl maps the host's sunAngle to timeAngle before GetSunVector.
    // Our solar_phase supplies sunrise=0, noon=.25, sunset=.5, midnight=.75.
    const double t_min = solar_phase - 0.033333333 - std::floor(solar_phase - 0.033333333);
    const double t_linear =
        t_min < 0.433333333 ? t_min * 1.15384615385 : t_min * 0.882352941176 + 0.117647058824;
    const double half = t_linear > 0.5 ? 1.0 : 0.0;
    const double fraction = t_linear * 2.0 - std::floor(t_linear * 2.0);
    const double smooth = fraction * fraction * (3.0 - 2.0 * fraction);
    const double blend = half < 0.5 ? 0.3 : -0.1;
    const double time_angle = (fraction * (1.0 - blend) + smooth * blend + half) * 0.5;
    const double raw = time_angle - 0.25 - std::floor(time_angle - 0.25);
    const double angle = (raw + ((-std::cos(raw * pi) * 0.5 + 0.5) - raw) / 3.0) * 2.0 * pi;
    const double rotation = -40.0 * pi / 180.0;
    const auto solar = glm::normalize(glm::dvec3(-std::sin(angle), std::cos(angle) * std::cos(rotation),
                                                 -std::cos(angle) * std::sin(rotation)));
    const auto light = (time_angle < 0.5325 || time_angle > 0.9675) ? solar : -solar;
    const float visibility = float(std::clamp((solar.y + 0.0625) / 0.125, 0.0, 1.0));
    const float sun_factor = float(solar.y < 0 ? std::clamp((solar.y + 0.375) / 0.75, 0.0, 1.0)
                                               : std::clamp((solar.y + 0.03125) / 0.0625, 0.0, 1.0));
    uniform_.cycle = {std::sqrt(std::max(0.0, std::sin(time_angle * 2 * pi))),
                      std::max(0.0, -std::sin(time_angle * 2 * pi)), sun_factor, visibility};
    uniform_.light = glm::vec4(light, std::pow(std::abs(visibility - 0.5f) * 2.0f, 4.0f));
    uniform_.solar = glm::vec4(solar, daylight);
    // Temperate clear-weather vanilla sky input. Biome weather inputs are not implemented.
    const float sky_brightness = float(std::clamp(std::sin(solar_phase * 2 * pi) * 2.0 + 0.5, 0.0, 1.0));
    uniform_.sky = glm::vec4(glm::vec3(0.47f, 0.65f, 1.0f) * sky_brightness, 1);
    uniform_.view = {
        render_distance, underwater ? 1.0f : 0.0f, settings.taa && !lod_debug ? 1.0f : 0.0f,
        water.enabled && water.refraction && water.waves && (underwater || water.depth) && !lod_debug ? 1.0f
                                                                                                      : 0.0f};
    uniform_.camera_time = glm::vec4(camera, float(std::fmod(time, 65536.0)));
    uniform_.flags = glm::vec4(settings.shadows && !lod_debug,
                               settings.clouds && settings.cloud_coverage > 0 && !lod_debug,
                               settings.atmosphere && !lod_debug, settings.fog && !lod_debug);
    uniform_.effects =
        glm::vec4(settings.clouds && settings.cloud_coverage > 0 && settings.cloud_shadows && !lod_debug,
                  settings.shadows && settings.shafts && !lod_debug,
                  settings.bloom && !lod_debug ? settings.bloom_strength / 100.0f : 0.0f, lod_debug);
    const int shaft_steps[]{6, 10, 15};
    uniform_.cloud = glm::vec4(settings.cloud_altitude, settings.cloud_coverage / 100.0f,
                               std::clamp(settings.cloud_quality, 1, 3),
                               float(std::fmod(time * settings.cloud_speed * (16.0 / 131072.0), 24.0)));
    uniform_.limits = glm::vec4(settings.shadow_distance, 1.0f / shadow_sizes_[0],
                                shaft_steps[std::clamp(settings.shaft_quality, 1, 3) - 1], sky_visibility);
    uniform_.hydro =
        glm::vec4(water.enabled && water.caustics && !lod_debug,
                  water.enabled && water.underwater_fog && underwater && !lod_debug,
                  double(sea_level) - 0.125 - camera.y, std::fmod(time * 0.35, 6.283185307179586));
    uniform_.water_origin = glm::vec4(std::fmod(camera.x, 4096.0), std::fmod(camera.z, 4096.0),
                                      settings.sun_moon && !lod_debug, settings.stars && !lod_debug);
    const auto now = std::chrono::steady_clock::now();
    const bool factor_update = now >= next_factor_update_;
    if (factor_update)
        next_factor_update_ = now + std::chrono::microseconds(66666);
    const glm::dvec3 delta{world_delta(camera.x, previous_position_.x), camera.y - previous_position_.y,
                           world_delta(camera.z, previous_position_.z)};
    if (previous_slot_ == renderer_.frame_slot())
        history_valid_ = false;
    const bool compatible = history_valid_ && !history_dirty_ && settings == previous_settings_ &&
                            water == previous_water_ && underwater == previous_underwater_ &&
                            glm::length(delta) < 8.0 && glm::dot(glm::vec3(light), previous_light_) > 0.999f;
    uniform_.previous_camera = previous_matrix_ * glm::translate(glm::mat4(1), glm::vec3(delta));
    uniform_.temporal = {compatible ? 1.0f : 0.0f, float(glm::length(delta)), float(frame_index_ % 3600),
                         factor_update ? 1.0f : 0.0f};
    update_shadows_ = shadows_enabled(); // Every rendered frame, like the reference host pipeline.
    const double radius = settings.shadow_distance;
    const auto light_view = glm::lookAt(light * 2048.0, glm::dvec3(0), glm::dvec3(0, 0, 1));
    auto projection = glm::ortho(-radius, radius, -radius, radius, 0.1, 4096.0);
    projection[1][1] *= -1;
    uniform_.shadow[0] = uniform_.shadow[1] = glm::mat4(projection * light_view);
    previous_matrix_ = matrix;
    previous_position_ = camera;
    previous_light_ = glm::vec3(light);
    previous_settings_ = settings;
    previous_water_ = water;
    previous_underwater_ = underwater;
    history_dirty_ = false;
    if (!history_valid_)
        previous_slot_ = (renderer_.frame_slot() + 1) % Renderer::frames_in_flight;
    auto& f = frames_[renderer_.frame_slot()];
    // Initialize both histories before any descriptor can be sampled. Render clears need no transfer usage.
    for (auto& slot : frames_) {
        if (!slot.history_initialized || !slot.factor_initialized) {
            for (auto* target : {&slot.history, &slot.scene_factor}) {
                begin_target(*target);
                VkClearAttachment clear{};
                clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                VkClearRect rect{{{0, 0}, target->extent}, 0, 1};
                vkCmdClearAttachments(renderer_.command, 1, &clear, 1, &rect);
                vkCmdEndRendering(renderer_.command);
                barrier(renderer_.command, target->handle, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            slot.history_initialized = slot.factor_initialized = true;
        }
    }
    const auto descriptor_start =
        profile_descriptors ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    // The previous frame's one-pixel factor reproduces the reference's persistent scene-aware value.
    if (f.bound_factor != frames_[previous_slot_].scene_factor.view) {
        VkDescriptorImageInfo factor{nearest_, frames_[previous_slot_].scene_factor.view,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet factor_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        factor_write.dstSet = f.environment;
        factor_write.dstBinding = 4;
        factor_write.descriptorCount = 1;
        factor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        factor_write.pImageInfo = &factor;
        update_descriptors(1, &factor_write);
        f.bound_factor = frames_[previous_slot_].scene_factor.view;
    }
    const auto final_view = surface_depth_needed() ? f.scene.view : f.composite.view;
    if (f.bound_final != final_view) {
        write_images(f.bloom_mip_sets[0], {final_view, f.opaque_depth.view, final_view, f.opaque_depth.view},
                     true);
        std::array<VkDescriptorImageInfo, 8> tone_images{};
        std::array<VkWriteDescriptorSet, 8> tone_writes{};
        for (uint32_t i = 0; i < 8; ++i) {
            tone_images[i] = {linear_, i ? f.bloom[i].view : final_view,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            tone_writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            tone_writes[i].dstSet = f.tone;
            tone_writes[i].dstBinding = i;
            tone_writes[i].descriptorCount = 1;
            tone_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            tone_writes[i].pImageInfo = &tone_images[i];
        }
        update_descriptors(8, tone_writes.data());
        f.bound_final = final_view;
    }
    if (profile_descriptors)
        descriptor_stats.cpu_ms +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - descriptor_start)
                .count();
    std::memcpy(f.uniform.mapped, &uniform_, sizeof(uniform_));
    vk_check(vmaFlushAllocation(renderer_.allocator, f.uniform.allocation, 0, sizeof(uniform_)),
             "environment uniform flush");
    if (update_shadows_ || !shadows_initialized_) {
        for (auto& shadow : shadows_)
            barrier(renderer_.command, shadow.handle, true,
                    shadows_initialized_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                         : VK_IMAGE_LAYOUT_UNDEFINED,
                    update_shadows_ ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        for (auto& colour : shadow_colour_)
            barrier(renderer_.command, colour.handle, false,
                    shadows_initialized_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                         : VK_IMAGE_LAYOUT_UNDEFINED,
                    update_shadows_ ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        shadows_initialized_ = true;
    }
}
void SceneEffects::begin_shadow(unsigned layer) {
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = shadows_[layer].view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = layer == 1 ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {1, 0};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, shadows_[layer].extent};
    info.layerCount = 1;
    info.pDepthAttachment = &depth;
    VkRenderingAttachmentInfo colours[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        colours[i] = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colours[i].imageView = shadow_colour_[i].view;
        colours[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colours[i].loadOp = layer == 1 ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        colours[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    info.colorAttachmentCount = 2;
    info.pColorAttachments = colours;
    bind_environment(shadow_layout_, 2);
    vkCmdBeginRendering(renderer_.command, &info);
    viewport(renderer_.command, shadows_[layer].extent);
    vkCmdBindPipeline(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_);
}
void SceneEffects::bind_shadow() {
    vkCmdBindPipeline(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_);
    bind_environment(shadow_layout_, 2);
}
void SceneEffects::bind_water_depth() {
    vkCmdBindPipeline(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS, water_depth_pipeline_);
}
void SceneEffects::shadow_player() {
    vkCmdBindPipeline(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS, player_shadow_pipeline_);
}
void SceneEffects::end_shadow() { vkCmdEndRendering(renderer_.command); }
void SceneEffects::copy_solid_shadow() {
    const auto cmd = renderer_.command;
    barrier(cmd, shadows_[1].handle, true, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    barrier(cmd, shadows_[0].handle, true, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy copy{};
    copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    copy.extent = {shadow_sizes_[0], shadow_sizes_[0], 1};
    vkCmdCopyImage(cmd, shadows_[1].handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shadows_[0].handle,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(cmd, shadows_[1].handle, true, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    barrier(cmd, shadows_[0].handle, true, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    // Solid caster colours/heights must survive until a nearer water fragment replaces them.
    for (const auto& colour : shadow_colour_)
        barrier(cmd, colour.handle, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}
void SceneEffects::capture_shadow_maps(const std::filesystem::path& directory) {
    if (!shadows_initialized_ || !shadows_enabled())
        throw std::runtime_error("Shadow map capture requires an active shadow frame.");
    renderer_.wait_idle();
    std::filesystem::create_directories(directory);
    const std::array<const Image*, 4> images{&shadows_[0], &shadows_[1], &shadow_colour_[0],
                                             &shadow_colour_[1]};
    const std::array<const char*, 4> names{"all-depth.f32", "solid-depth.f32", "colour.rgba8", "shaft.rgba8"};
    std::ofstream metadata(directory / "extent.txt");
    metadata << shadow_sizes_[0] << ' ' << shadow_sizes_[1] << '\n';
    metadata.close();
    if (!metadata)
        throw std::runtime_error("Cannot save shadow capture metadata.");
    for (size_t i = 0; i < images.size(); ++i) {
        const auto& image = *images[i];
        const VkDeviceSize bytes = VkDeviceSize(image.extent.width) * image.extent.height * 4;
        const auto buffer = renderer_.create_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        try {
            renderer_.immediate([&](VkCommandBuffer cmd) {
                barrier(cmd, image.handle, i < 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                VkBufferImageCopy copy{};
                copy.imageSubresource = {
                    VkImageAspectFlags(i < 2 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 0,
                    1};
                copy.imageExtent = {image.extent.width, image.extent.height, 1};
                vkCmdCopyImageToBuffer(cmd, image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle,
                                       1, &copy);
                barrier(cmd, image.handle, i < 2, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            });
            vk_check(vmaInvalidateAllocation(renderer_.allocator, buffer.allocation, 0, VK_WHOLE_SIZE),
                     "shadow capture invalidate");
            std::ofstream output(directory / names[i], std::ios::binary);
            output.write(static_cast<const char*>(buffer.mapped), static_cast<std::streamsize>(bytes));
            output.close();
            if (!output)
                throw std::runtime_error("Cannot save shadow map capture.");
        } catch (...) {
            renderer_.destroy_buffer(buffer);
            throw;
        }
        renderer_.destroy_buffer(buffer);
    }
}
void SceneEffects::bind_environment(VkPipelineLayout layout, uint32_t set) {
    const auto descriptor = frames_[renderer_.frame_slot()].environment;
    vkCmdBindDescriptorSets(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, set, 1, &descriptor,
                            0, nullptr);
}
void SceneEffects::begin_scene() {
    auto& f = frames_[renderer_.frame_slot()];
    if (update_shadows_) {
        for (auto& shadow : shadows_)
            barrier(renderer_.command, shadow.handle, true, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        for (auto& colour : shadow_colour_)
            barrier(renderer_.command, colour.handle, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    barrier(renderer_.command, f.scene.handle, false, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    renderer_.set_world_target(f.scene.handle, f.scene.view);
}
void SceneEffects::begin_target(const Image& image, const Image* second) {
    barrier(renderer_.command, image.handle, false, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (second)
        barrier(renderer_.command, second->handle, false, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo attachments[2]{};
    for (int i = 0; i < (second ? 2 : 1); ++i) {
        auto& a = attachments[i];
        a = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        a.imageView = i ? second->view : image.view;
        a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, image.extent};
    info.layerCount = 1;
    info.colorAttachmentCount = second ? 2 : 1;
    info.pColorAttachments = attachments;
    vkCmdBeginRendering(renderer_.command, &info);
    viewport(renderer_.command, image.extent);
}
void SceneEffects::draw_post(VkPipeline pipeline, VkDescriptorSet images) {
    vkCmdBindPipeline(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS, post_layout_, 0, 1, &images,
                            0, nullptr);
    bind_environment(post_layout_, 1);
    vkCmdDraw(renderer_.command, 3, 1, 0, 0);
}
void SceneEffects::atmosphere(VkImage depth, VkImageView view) {
    auto& f = frames_[renderer_.frame_slot()];
    const auto cmd = renderer_.command;
    vkCmdEndRendering(cmd);
    barrier(cmd, f.scene.handle, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(cmd, depth, true, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(cmd, depth, true, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    barrier(cmd, f.opaque_depth.handle, true, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy copy{};
    copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    copy.extent = {extent_.width, extent_.height, 1};
    vkCmdCopyImage(cmd, depth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, f.opaque_depth.handle,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(cmd, f.opaque_depth.handle, true, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(cmd, depth, true, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    renderer_.gpu_mark("opaque_depth_copy");
    scene_depth_ = depth;
    const auto descriptor_start =
        profile_descriptors ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    if (f.bound_history != frames_[previous_slot_].history.view || f.bound_history_depth != view) {
        write_images(f.taa, {f.toned.view, view, frames_[previous_slot_].history.view, view}, true);
        write_images(
            f.factor,
            {frames_[previous_slot_].scene_factor.view, view, shadow_colour_[1].view, shadows_[0].view},
            true);
        f.bound_history = frames_[previous_slot_].history.view;
        f.bound_history_depth = view;
    }
    if (profile_descriptors)
        descriptor_stats.cpu_ms +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - descriptor_start)
                .count();
    begin_target(f.scene_factor);
    draw_post(factor_pipeline_, f.factor);
    vkCmdEndRendering(cmd);
    barrier(cmd, f.scene_factor.handle, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    renderer_.gpu_mark("scene_factor");
    const auto composite_descriptor_start =
        profile_descriptors ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    if (f.bound_composite_depth != view) {
        VkDescriptorImageInfo opaque{nearest_, f.opaque_depth.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet opaque_writes[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            opaque_writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            opaque_writes[i].dstSet = i ? f.volume_composite : f.atmosphere;
            opaque_writes[i].dstBinding = 4;
            opaque_writes[i].descriptorCount = 1;
            opaque_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            opaque_writes[i].pImageInfo = &opaque;
        }
        update_descriptors(2, opaque_writes);
        write_images(f.atmosphere, {f.scene.view, view, f.volume.view, f.metadata.view}, true);
        // Distinct sets: updating a set referenced earlier in this command buffer would
        // also change that earlier draw when the command buffer is submitted.
        write_images(f.volume_composite, {f.composite.view, view, f.volume.view, f.metadata.view}, true);
        f.bound_composite_depth = view;
    }
    if (profile_descriptors)
        descriptor_stats.cpu_ms += std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - composite_descriptor_start)
                                       .count();
    // Underwater, the transmitted scene must already contain sky/clouds. Above
    // water, clouds remain a foreground pass clipped at the nearest water surface.
    const bool pre_volume = underwater_ && volume_enabled();
    if (pre_volume) {
        begin_target(f.volume, &f.metadata);
        draw_post(volume_pipeline_, f.atmosphere);
        vkCmdEndRendering(cmd);
        renderer_.gpu_mark("cloud_raymarch_underwater");
    }
    const auto volume_layout =
        pre_volume ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier(cmd, f.volume.handle, false, volume_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(cmd, f.metadata.handle, false, volume_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const glm::vec4 phase{pre_volume ? 2.0f : 0.0f, 0, 0, 0};
    vkCmdPushConstants(cmd, post_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(phase), &phase);
    begin_target(f.composite);
    draw_post(atmosphere_pipeline_, f.atmosphere);
    vkCmdEndRendering(cmd);
    barrier(cmd, depth, true, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    renderer_.set_world_target(f.composite.handle, f.composite.view);
    renderer_.resume_world(view);
    viewport(cmd, extent_);
    renderer_.gpu_mark("atmosphere");
}
void SceneEffects::refresh_water_background_depth(VkImage depth) {
    auto& target = frames_[renderer_.frame_slot()].opaque_depth;
    const auto cmd = renderer_.command;
    barrier(cmd, depth, true, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    barrier(cmd, target.handle, true, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy copy{};
    copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    copy.extent = {extent_.width, extent_.height, 1};
    vkCmdCopyImage(cmd, depth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.handle,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(cmd, target.handle, true, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(cmd, depth, true, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}
void SceneEffects::begin_water_depth(VkImage depth, VkImageView view) {
    const auto cmd = renderer_.command;
    vkCmdEndRendering(cmd);
    barrier(cmd, depth, true, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, extent_};
    info.layerCount = 1;
    info.pDepthAttachment = &attachment;
    vkCmdBeginRendering(cmd, &info);
    viewport(cmd, extent_);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, water_depth_pipeline_);
}
void SceneEffects::composite_volume(VkImage depth) {
    if (!surface_depth_needed())
        return;
    auto& f = frames_[renderer_.frame_slot()];
    const auto cmd = renderer_.command;
    barrier(cmd, f.composite.handle, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(cmd, depth, true, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (underwater_) {
        // Depth now contains the nearest submerged terrain OR water exit.
        // Apply absorption once, after refraction and the outside atmosphere.
        begin_target(f.scene);
        const glm::vec4 phase{3, 0, 0, 0};
        vkCmdPushConstants(cmd, post_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(phase), &phase);
        draw_post(atmosphere_pipeline_, f.volume_composite);
        vkCmdEndRendering(cmd);
        barrier(cmd, depth, true, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        renderer_.gpu_mark("underwater_composite");
        return;
    }
    begin_target(f.volume, &f.metadata);
    draw_post(volume_pipeline_, f.volume_composite);
    vkCmdEndRendering(cmd);
    barrier(cmd, f.volume.handle, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(cmd, f.metadata.handle, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    renderer_.gpu_mark("cloud_raymarch");
    begin_target(f.scene);
    const glm::vec4 phase{1, 0, 0, 0};
    vkCmdPushConstants(cmd, post_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(phase), &phase);
    draw_post(atmosphere_pipeline_, f.volume_composite);
    vkCmdEndRendering(cmd);
    barrier(cmd, depth, true, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    renderer_.gpu_mark("volume_composite");
}
void SceneEffects::finish() {
    auto& f = frames_[renderer_.frame_slot()];
    const auto cmd = renderer_.command;
    barrier(cmd, surface_depth_needed() ? f.scene.handle : f.composite.handle, false,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (uniform_.effects.z > 0) {
        // Mip generation and Gaussian filtering are separate: each reference
        // scale blurs the original scene mip, never an already blurred level.
        for (uint32_t i = 0; i < f.bloom_mips.size(); ++i) {
            begin_target(f.bloom_mips[i]);
            const glm::vec4 push{i == 0 ? 0.0f : 1.0f, float(f.bloom_mips[i].extent.width),
                                 float(f.bloom_mips[i].extent.height), 0};
            vkCmdPushConstants(cmd, post_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            draw_post(bloom_pipeline_, f.bloom_mip_sets[i]);
            vkCmdEndRendering(cmd);
            barrier(cmd, f.bloom_mips[i].handle, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }
    for (uint32_t i = 1; i < f.bloom.size(); ++i) {
        if (uniform_.effects.z > 0) {
            begin_target(f.bloom[i]);
            const glm::vec4 push{2, float(f.bloom[i].extent.width), float(f.bloom[i].extent.height), 0};
            vkCmdPushConstants(cmd, post_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            draw_post(bloom_pipeline_, f.bloom_sets[i]);
            vkCmdEndRendering(cmd);
        }
        barrier(cmd, f.bloom[i].handle, false,
                uniform_.effects.z > 0 ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    renderer_.gpu_mark("bloom");
    begin_target(f.toned);
    draw_post(tone_pipeline_, f.tone);
    vkCmdEndRendering(cmd);
    barrier(cmd, f.toned.handle, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(cmd, scene_depth_, true, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    renderer_.gpu_mark("tone_map");
    begin_target(f.history);
    draw_post(taa_pipeline_, f.taa);
    vkCmdEndRendering(cmd);
    barrier(cmd, f.history.handle, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(cmd, scene_depth_, true, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    previous_slot_ = renderer_.frame_slot();
    history_valid_ = uniform_.view.z > 0.5f;
    ++frame_index_;
    renderer_.gpu_mark("taa");
    renderer_.set_world_target(VK_NULL_HANDLE, VK_NULL_HANDLE);
    renderer_.begin_rendering();
    viewport(cmd, extent_);
    draw_post(present_pipeline_, f.present);
    renderer_.gpu_mark("present_composite");
}
void SceneEffects::release_images() noexcept {
    auto destroy = [&](Image& image) {
        if (image.view)
            vkDestroyImageView(renderer_.device, image.view, nullptr);
        if (image.handle)
            vmaDestroyImage(renderer_.allocator, image.handle, image.allocation);
        image = {};
    };
    for (auto& f : frames_) {
        destroy(f.scene);
        destroy(f.composite);
        destroy(f.volume);
        destroy(f.metadata);
        destroy(f.opaque_depth);
        destroy(f.toned);
        destroy(f.history);
        destroy(f.scene_factor);
        for (auto& b : f.bloom)
            destroy(b);
        for (auto& b : f.bloom_mips)
            destroy(b);
        renderer_.destroy_buffer(f.uniform);
        f = {};
    }
    for (auto& shadow : shadows_)
        destroy(shadow);
    for (auto& c : shadow_colour_)
        destroy(c);
    shadows_initialized_ = false;
    history_valid_ = false;
    history_dirty_ = true;
    extent_ = {};
}
void SceneEffects::shutdown() noexcept {
    vkDeviceWaitIdle(renderer_.device);
    release_images();
    for (auto p : {shadow_pipeline_, player_shadow_pipeline_, water_depth_pipeline_, volume_pipeline_,
                   atmosphere_pipeline_, bloom_pipeline_, tone_pipeline_, taa_pipeline_, present_pipeline_,
                   factor_pipeline_})
        if (p)
            vkDestroyPipeline(renderer_.device, p, nullptr);
    if (pool_)
        vkDestroyDescriptorPool(renderer_.device, pool_, nullptr);
    for (auto p : {post_layout_, shadow_layout_})
        if (p)
            vkDestroyPipelineLayout(renderer_.device, p, nullptr);
    for (auto l : {image_layout_, environment_layout_})
        if (l)
            vkDestroyDescriptorSetLayout(renderer_.device, l, nullptr);
    renderer_.destroy_texture(reference_noise_);
    renderer_.destroy_texture(reference_water_);
    for (auto s : {linear_, nearest_, compare_, repeat_})
        if (s)
            vkDestroySampler(renderer_.device, s, nullptr);
}
} // namespace sandbox
