#include "render/lod_renderer.hpp"
#include "core/lod_debug.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <set>

namespace sandbox {
namespace {
struct Push {
    glm::mat4 matrix;
    glm::vec4 offset;
    glm::vec4 meta;
};
} // namespace
LodRenderer::LodRenderer(Renderer& renderer, SceneEffects& effects, const std::array<glm::vec4, 11>& palette)
    : renderer_(renderer), effects_(effects), palette_(palette) {
    try {
        const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 1;
        info.pBindings = &binding;
        vk_check(vkCreateDescriptorSetLayout(renderer_.device, &info, nullptr, &coverage_layout_),
                 "LOD coverage layout");
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Renderer::frames_in_flight};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = Renderer::frames_in_flight;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &size;
        vk_check(vkCreateDescriptorPool(renderer_.device, &pi, nullptr, &pool_), "LOD descriptor pool");
        for (unsigned i = 0; i < Renderer::frames_in_flight; ++i) {
            coverage_[i] = renderer_.create_buffer(sizeof(Coverage), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = pool_;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &coverage_layout_;
            vk_check(vkAllocateDescriptorSets(renderer_.device, &ai, &sets_[i]), "LOD coverage descriptor");
            VkDescriptorBufferInfo bi{coverage_[i].handle, 0, sizeof(Coverage)};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = sets_[i];
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &bi;
            vkUpdateDescriptorSets(renderer_.device, 1, &write, 0, nullptr);
        }
        const VkDescriptorSetLayout layouts[]{renderer_.texture_layout, coverage_layout_,
                                              effects_.environment_layout()};
        const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(Push)};
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.setLayoutCount = 3;
        li.pSetLayouts = layouts;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(renderer_.device, &li, nullptr, &layout_), "LOD pipeline layout");
        solid_ = pipeline(false, false);
        debug_ = pipeline(true, false);
        shadow_ = pipeline(false, true);
        water_ = pipeline(false, false, true);
        water_depth_ = pipeline(false, false, true, true);
    } catch (...) {
        shutdown();
        throw;
    }
}
LodRenderer::~LodRenderer() { shutdown(); }
void LodRenderer::shutdown() {
    // 호출자는 GPU idle 후 또는 이전 사용이 끝난 프레임에서만 소멸한다.
    draw_meshes_.clear();
    for (auto& [key, mesh] : meshes_) {
        for (const auto& part : mesh.parts)
            renderer_.destroy_buffer(part.buffer);
        *allocated_ -= mesh.faces * sizeof(LodFace);
    }
    meshes_.clear();
    for (auto buffer : coverage_)
        renderer_.destroy_buffer(buffer);
    for (auto pipeline : {solid_, debug_, shadow_, water_, water_depth_})
        if (pipeline)
            vkDestroyPipeline(renderer_.device, pipeline, nullptr);
    if (layout_)
        vkDestroyPipelineLayout(renderer_.device, layout_, nullptr);
    if (pool_)
        vkDestroyDescriptorPool(renderer_.device, pool_, nullptr);
    if (coverage_layout_)
        vkDestroyDescriptorSetLayout(renderer_.device, coverage_layout_, nullptr);
}
VkPipeline LodRenderer::pipeline(bool debug, bool shadow, bool water, bool depth_only) {
    VkShaderModule vert{}, frag{};
    VkPipeline result{};
    try {
        vert = renderer_.shader("shaders/lod.vert.spv");
        frag = renderer_.shader(depth_only ? "shaders/lod_depth.frag.spv"
                                : shadow   ? "shaders/lod_shadow.frag.spv"
                                           : "shaders/lod.frag.spv");
        const std::array<VkBool32, 2> constants{debug ? VK_TRUE : VK_FALSE, shadow ? VK_TRUE : VK_FALSE};
        const VkSpecializationMapEntry entries[]{{0, 0, 4}, {1, 4, 4}};
        const VkSpecializationInfo spec{2, entries, sizeof(constants), constants.data()};
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[0].pSpecializationInfo = &spec;
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";
        stages[1].pSpecializationInfo = &spec;
        const VkVertexInputBindingDescription binding{0, sizeof(LodFace), VK_VERTEX_INPUT_RATE_INSTANCE};
        const VkVertexInputAttributeDescription attribute{0, 0, VK_FORMAT_R32G32B32A32_UINT, 0};
        VkPipelineVertexInputStateCreateInfo vertex{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex.vertexBindingDescriptionCount = 1;
        vertex.pVertexBindingDescriptions = &binding;
        vertex.vertexAttributeDescriptionCount = 1;
        vertex.pVertexAttributeDescriptions = &attribute;
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = shadow || water || debug ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        std::array<VkPipelineColorBlendAttachmentState, 2> blends{};
        for (auto& blend : blends)
            blend.colorWriteMask = 0xf;
        VkPipelineColorBlendStateCreateInfo blending{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blending.attachmentCount = depth_only ? 0 : shadow ? 2 : 1;
        blending.pAttachments = blends.data();
        const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = states;
        const VkFormat formats[]{shadow ? VK_FORMAT_R8G8B8A8_UNORM : Renderer::scene_format,
                                 VK_FORMAT_R8G8B8A8_UNORM};
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = depth_only ? 0 : shadow ? 2 : 1;
        rendering.pColorAttachmentFormats = formats;
        rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        ci.pNext = &rendering;
        ci.stageCount = 2;
        ci.pStages = stages;
        ci.pVertexInputState = &vertex;
        ci.pInputAssemblyState = &assembly;
        ci.pViewportState = &viewport;
        ci.pRasterizationState = &raster;
        ci.pMultisampleState = &ms;
        ci.pDepthStencilState = &depth;
        ci.pColorBlendState = &blending;
        ci.pDynamicState = &dynamic;
        ci.layout = layout_;
        vk_check(vkCreateGraphicsPipelines(renderer_.device, VK_NULL_HANDLE, 1, &ci, nullptr, &result),
                 "LOD graphics pipeline");
    } catch (...) {
        if (vert)
            vkDestroyShaderModule(renderer_.device, vert, nullptr);
        if (frag)
            vkDestroyShaderModule(renderer_.device, frag, nullptr);
        throw;
    }
    vkDestroyShaderModule(renderer_.device, vert, nullptr);
    vkDestroyShaderModule(renderer_.device, frag, nullptr);
    return result;
}
void LodRenderer::clear() {
    coverage_valid_ = false;
    draw_meshes_.clear();
    active_.reset();
    pending_.reset();
    accepted_ = cursor_ = 0;
    water_visible_ = false;
    collect();
}
void LodRenderer::collect() {
    std::set<Key> keep;
    for (auto scene : {active_, pending_})
        if (scene)
            for (const auto& mesh : scene->meshes)
                keep.insert({mesh->key, mesh->revision});
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (keep.contains(it->first)) {
            ++it;
            continue;
        }
        const auto mesh = it->second;
        auto* renderer = &renderer_;
        auto allocated = allocated_;
        renderer_.defer([renderer, mesh, allocated] {
            for (const auto& part : mesh.parts)
                renderer->destroy_buffer(part.buffer);
            *allocated -= mesh.faces * sizeof(LodFace);
        });
        it = meshes_.erase(it);
    }
}
void LodRenderer::rebuild_draw_meshes() {
    draw_meshes_.clear();
    if (!active_)
        return;
    draw_meshes_.reserve(active_->meshes.size());
    for (const auto& source : active_->meshes) {
        const auto key = source->key;
        // Same coverage rule as record; the mask/centre is shared by all passes.
        if (key.level == 0) {
            const int x = column_delta(key.x, coverage_centre_.x) + 64,
                      z = column_delta(key.z, coverage_centre_.z) + 64;
            if (x >= 0 && x < 129 && z >= 0 && z < 129 && coverage_data_.mask[x + z * 129])
                continue;
        }
        const auto it = meshes_.find({key, source->revision});
        if (it != meshes_.end())
            draw_meshes_.push_back({key, &it->second});
    }
}
void LodRenderer::prepare(std::shared_ptr<const LodScene> scene, ColumnKey centre,
                          std::span<const ColumnKey> published, bool published_changed, bool lod_debug) {
    const bool coverage_changed = !coverage_valid_ || published_changed || centre != coverage_centre_;
    if (coverage_changed) {
        coverage_data_.centre = {centre.x, centre.z, 0, 0};
        coverage_data_.mask.fill(0);
        for (auto key : published) {
            const int x = column_delta(key.x, centre.x) + 64, z = column_delta(key.z, centre.z) + 64;
            if (x >= 0 && x < 129 && z >= 0 && z < 129)
                coverage_data_.mask[x + z * 129] = 1;
        }
        coverage_centre_ = centre;
        coverage_valid_ = true;
    }
    coverage_data_.palette = lod_debug ? lod_debug_palette : palette_;
    // Each frame slot still receives current coverage after its fence, including debug changes.
    auto& coverage = coverage_[renderer_.frame_slot()];
    std::memcpy(coverage.mapped, &coverage_data_, sizeof(coverage_data_));
    vk_check(vmaFlushAllocation(renderer_.allocator, coverage.allocation, 0, sizeof(coverage_data_)),
             "LOD mask flush");
    if (!pending_ && scene && scene->revision != accepted_) {
        pending_ = std::move(scene);
        cursor_ = 0;
    }
    size_t bytes = 0;
    const auto start = std::chrono::steady_clock::now();
    while (pending_ && cursor_ < pending_->meshes.size()) {
        const auto& source = pending_->meshes[cursor_];
        const Key key{source->key, source->revision};
        auto& mesh = meshes_[key];
        if (mesh.faces == source->faces.size()) {
            ++cursor_;
            continue;
        }
        if (bytes >= 1024 * 1024)
            break;
        const size_t count =
            std::min(source->faces.size() - mesh.faces, (1024 * 1024 - bytes) / sizeof(LodFace));
        const size_t size = count * sizeof(LodFace);
        if (*allocated_ + size > lod_gpu_budget)
            break;
        // Keep each bounded upload in solid/water ranges, without an extra persistent mesh copy.
        std::vector<LodFace> faces(source->faces.begin() + mesh.faces,
                                   source->faces.begin() + mesh.faces + count);
        const auto water = std::partition(faces.begin(), faces.end(), [](const LodFace& face) {
            return ((face.cell_axis_material >> 11u) & 15u) != 5u;
        });
        const auto solids = uint32_t(water - faces.begin());
        auto buffer = renderer_.upload_buffer(faces.data(), size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        mesh.parts.push_back({buffer, uint32_t(count), solids});
        mesh.faces += uint32_t(count);
        *allocated_ += size;
        bytes += size;
        if (mesh.faces == source->faces.size())
            ++cursor_;
        if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(1))
            break;
    }
    if (pending_ && cursor_ == pending_->meshes.size()) {
        active_ = std::move(pending_);
        accepted_ = active_->revision;
        rebuild_draw_meshes();
        collect();
    } else if (coverage_changed)
        rebuild_draw_meshes();
}
void LodRenderer::draw(const glm::mat4& matrix, glm::dvec3 camera, int radius, bool lod_debug,
                       int shadow_layer, int shadow_distance) {
    const bool measure = profile_draws && shadow_layer < 0;
    const auto start = measure ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    if (measure)
        draw_stats = {};
    if (shadow_layer < 0) {
        tiles = triangles = 0;
        water_visible_ = false;
    }
    vkCmdBindPipeline(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      shadow_layer >= 0 ? shadow_
                      : lod_debug       ? debug_
                                        : solid_);
    effects_.bind_environment(layout_, 2);
    if (measure)
        ++draw_stats.descriptor_binds;
    record(matrix, camera, radius, lod_debug, shadow_layer, shadow_distance, layout_, shadow_layer == 0);
    if (measure)
        draw_stats.cpu_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
void LodRenderer::draw_water(const glm::mat4& matrix, glm::dvec3 camera, int radius,
                             VkPipelineLayout water_layout) {
    if (!water_layout) {
        water_layout = layout_;
        vkCmdBindPipeline(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS, water_);
        effects_.bind_environment(layout_, 2);
    }
    record(matrix, camera, radius, false, -1, 0, water_layout, true);
}
void LodRenderer::draw_water_depth(const glm::mat4& matrix, glm::dvec3 camera, int radius) {
    vkCmdBindPipeline(renderer_.command, VK_PIPELINE_BIND_POINT_GRAPHICS, water_depth_);
    record(matrix, camera, radius, false, -1, 0, layout_, true);
}
void LodRenderer::record(const glm::mat4& matrix, glm::dvec3 camera, int radius, bool lod_debug,
                         int shadow_layer, int shadow_distance, VkPipelineLayout layout, bool water_only) {
    if (!active_)
        return;
    const auto cmd = renderer_.command;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1,
                            &sets_[renderer_.frame_slot()], 0, nullptr);
    const bool measure = profile_draws && shadow_layer < 0 && !water_only;
    if (measure)
        ++draw_stats.descriptor_binds;
    const auto rows = glm::transpose(matrix);
    const std::array<glm::vec4, 6> planes{rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1],
                                          rows[3] - rows[1], rows[2],           rows[3] - rows[2]};
    for (const auto& entry : draw_meshes_) {
        const auto& mesh = *entry.mesh;
        if (water_only && std::none_of(mesh.parts.begin(), mesh.parts.end(),
                                       [](const Part& part) { return part.faces > part.solid_faces; }))
            continue;
        const auto key = entry.key;
        const float step = float(1 << key.level), width = step * 16;
        const glm::vec3 offset{world_delta(key.x * 16.0, camera.x), -camera.y,
                               world_delta(key.z * 16.0, camera.z)};
        const glm::vec3 half{width * .5f, world_height * .5f, width * .5f}, centre = offset + half;
        if (shadow_layer < 0) {
            bool visible = true;
            for (const auto& p : planes)
                if (glm::dot(glm::vec3(p), centre) + p.w + glm::dot(glm::abs(glm::vec3(p)), half) < 0) {
                    visible = false;
                    break;
                }
            if (!visible)
                continue;
        } else if (glm::length(glm::vec2(centre.x, centre.z)) > shadow_distance * 1.5f + width)
            continue;
        const Push push{matrix, glm::vec4(offset, step),
                        glm::vec4(float(key.x), float(key.z),
                                  shadow_layer < 0 ? float(radius * 16) : 1.0f - 25.6f / shadow_distance,
                                  float(shadow_layer))};
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
        if (measure)
            ++draw_stats.pushes;
        const VkDeviceSize zero = 0;
        for (const auto& part : mesh.parts) {
            const uint32_t first = water_only ? part.solid_faces : 0;
            const uint32_t count = water_only  ? part.faces - part.solid_faces
                                   : lod_debug ? part.faces
                                               : part.solid_faces;
            if (shadow_layer < 0 && !water_only && !lod_debug && part.faces > part.solid_faces)
                water_visible_ = true;
            if (!count)
                continue;
            vkCmdBindVertexBuffers(cmd, 0, 1, &part.buffer.handle, &zero);
            vkCmdDraw(cmd, 6, count, 0, first);
            if (measure) {
                ++draw_stats.vertex_binds;
                ++draw_stats.draws;
            }
        }
        if (shadow_layer < 0 && !water_only) {
            ++tiles;
            triangles += mesh.faces * 2;
        }
    }
}
} // namespace sandbox
