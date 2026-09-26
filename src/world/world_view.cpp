#include "world/world_view.hpp"
#include "world/profiling.hpp"
#include "world/spawn.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <random>
#include <stb_image.h>
#include <stdexcept>
#include <tracy/Tracy.hpp>

namespace sandbox {
namespace {
struct Push {
    glm::mat4 view_projection;
    glm::vec4 offset;
    glm::vec4 target;
};
static_assert(sizeof(Push) == 96 && offsetof(Push, target) == 80);
std::array<glm::vec4, 6> frustum(const glm::mat4& matrix) {
    const auto rows = glm::transpose(matrix);
    return {rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1],
            rows[3] - rows[1], rows[2],           rows[3] - rows[2]};
}
bool visible(const std::array<glm::vec4, 6>& planes, glm::vec3 offset, glm::vec3 size = glm::vec3(16)) {
    const auto half = size * 0.5f;
    const auto centre = offset + half;
    for (const auto& p : planes)
        if (glm::dot(glm::vec3(p), centre) + p.w + glm::dot(glm::abs(glm::vec3(p)), half) < 0)
            return false;
    return true;
}
} // namespace
WorldView::WorldView(Renderer& renderer, const GenerationConfig& config, int radius,
                     std::optional<ColumnKey> diagnostic_origin)
    : renderer_(renderer), radius_(radius), generator_(std::make_shared<TerrainGenerator>(config)),
      lod_cache_(std::make_shared<LodCache>(generator_)),
      stream_(std::make_unique<WorldStream>(
          generator_, [lod = lod_cache_](Column column) { lod->submit(std::move(column)); })) {
    if (radius < 1 || radius > 64)
        throw std::runtime_error("Render distance must be 1..64 columns.");
    if (diagnostic_origin) {
        // Preserve fixed coordinates/airborne view for explicitly requested performance captures.
        const auto origin = canonical(*diagnostic_origin);
        camera.position.x = origin.x * 16 + 8;
        camera.position.z = origin.z * 16 + 8;
        camera.position.y =
            terrain_spawn_height(int(camera.position.x), int(camera.position.z), *generator_) + 28.0;
        player_.position = camera.position - glm::dvec3(0, Player::eye_height, 0);
    } else {
        std::random_device entropy;
        const uint64_t random_seed = (uint64_t(entropy()) << 32) ^ uint64_t(entropy()) ^
                                     uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto spawn = find_land_spawn(*generator_, random_seed);
        if (!spawn)
            throw std::runtime_error(
                "No dry, clear land found in the spawn latitude bands after 512 attempts. "
                "Check the world generation settings.");
        player_.position = {spawn->x + 0.5, double(spawn->y), spawn->z + 0.5};
        player_.movement_mode = MovementMode::walk;
        player_.grounded = true;
        camera.position = player_.position + glm::dvec3(0, Player::eye_height, 0);
        std::cout << "SPAWN: feet=" << spawn->x << ".5," << spawn->y << ',' << spawn->z
                  << ".5 world_tick=" << clock_.ticks() << '\n';
    }
    previous_player_position_ = rendered_player_position_ = player_.position;
    try {
        constexpr std::array<const char*, 11> files{
            "blocks/dirt.png", "blocks/grass_top.png", "blocks/grass_side.png", "blocks/grass_bottom.png",
            "blocks/rock.png", "fluid/water.png",      "blocks/sand.png",       "fluid/lava.png",
            nullptr,           "blocks/ice.png",       "blocks/snow.png"};
        static_assert(files.size() == 11); // Eight base materials plus three bit27 extended solids.
        constexpr int atlas_width = static_cast<int>(files.size()) * 32;
        std::vector<unsigned char> pixels(atlas_width * 32 * 4);
        for (int tile = 0; tile < static_cast<int>(files.size()); ++tile) {
            if (!files[tile]) {
                for (int y = 0; y < 32; ++y)
                    std::fill_n(pixels.data() + (y * atlas_width + tile * 32) * 4, 32 * 4,
                                static_cast<unsigned char>(255));
                continue;
            }
            int w{}, h{}, channels{};
            const auto path = std::string("assets/textures/") + files[tile];
            std::unique_ptr<unsigned char, decltype(&stbi_image_free)> source(
                stbi_load(path.c_str(), &w, &h, &channels, 4), stbi_image_free);
            if (!source || w != 32 || h != 32)
                throw std::runtime_error("Expected 32x32 world texture: " + path);
            for (int y = 0; y < 32; ++y)
                std::memcpy(pixels.data() + (y * atlas_width + tile * 32) * 4, source.get() + y * 32 * 4,
                            32 * 4);
        }
        atlas_ = renderer_.create_texture(pixels.data(), atlas_width, 32, true);
        create_pipeline();
        std::array<glm::vec4, 11> palette{};
        for (int tile = 0; tile < 11; ++tile) {
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    for (int c = 0; c < 3; ++c)
                        palette[tile][c] += float(
                            std::pow(pixels[(y * atlas_width + tile * 32 + x) * 4 + c] / 255.0, 2.2) / 1024);
            palette[tile].w = 1;
        }
        lod_renderer_ = std::make_unique<LodRenderer>(renderer_, *scene_effects_, palette);
    } catch (...) {
        shutdown();
        throw;
    }
}
WorldView::~WorldView() { shutdown(); }
void WorldView::shutdown() noexcept {
    vkDeviceWaitIdle(renderer_.device);
    lod_renderer_.reset();
    water_effects_.reset();
    ice_effects_.reset();
    scene_effects_.reset();
    for (const auto& [key, column] : columns_)
        for (const auto& mesh : column.meshes) {
            renderer_.destroy_buffer(mesh.faces);
            renderer_.destroy_buffer(mesh.lights);
        }
    for (const auto& mesh : uploading_) {
        renderer_.destroy_buffer(mesh.faces);
        renderer_.destroy_buffer(mesh.lights);
    }
    if (depth_view_)
        vkDestroyImageView(renderer_.device, depth_view_, nullptr);
    if (depth_)
        vmaDestroyImage(renderer_.allocator, depth_, depth_allocation_);
    if (pipeline_)
        vkDestroyPipeline(renderer_.device, pipeline_, nullptr);
    if (water_pipeline_)
        vkDestroyPipeline(renderer_.device, water_pipeline_, nullptr);
    if (lod_debug_pipeline_)
        vkDestroyPipeline(renderer_.device, lod_debug_pipeline_, nullptr);
    if (player_pipeline_)
        vkDestroyPipeline(renderer_.device, player_pipeline_, nullptr);
    if (layout_)
        vkDestroyPipelineLayout(renderer_.device, layout_, nullptr);
    renderer_.destroy_texture(atlas_);
    // Retired descriptor sets may still be in Renderer::defer; keep their pools alive until those callbacks
    // run.
    const auto device = renderer_.device;
    for (auto pool : face_pools_)
        renderer_.defer([device, pool] { vkDestroyDescriptorPool(device, pool->handle, nullptr); });
    if (face_layout_)
        vkDestroyDescriptorSetLayout(renderer_.device, face_layout_, nullptr);
}
void WorldView::create_pipeline() {
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,
                                         nullptr};
    const VkDescriptorSetLayoutBinding bindings[]{
        binding, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo face_layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    face_layout.bindingCount = 2;
    face_layout.pBindings = bindings;
    vk_check(vkCreateDescriptorSetLayout(renderer_.device, &face_layout, nullptr, &face_layout_),
             "face buffer layout");
    scene_effects_ = std::make_unique<SceneEffects>(renderer_, face_layout_);
    const VkDescriptorSetLayout sets[] = {renderer_.texture_layout, face_layout_,
                                          scene_effects_->environment_layout()};
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 3;
    layout.pSetLayouts = sets;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    vk_check(vkCreatePipelineLayout(renderer_.device, &layout, nullptr, &layout_), "world pipeline layout");
    const auto vert = renderer_.shader("shaders/world.vert.spv");
    VkShaderModule frag{};
    try {
        frag = renderer_.shader("shaders/world.frag.spv");
    } catch (...) {
        if (frag)
            vkDestroyShaderModule(renderer_.device, frag, nullptr);
        vkDestroyShaderModule(renderer_.device, vert, nullptr);
        throw;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto& stage : stages) {
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blend{};
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
    rendering.pColorAttachmentFormats = &Renderer::scene_format;
    rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.pNext = &rendering;
    pipeline.stageCount = 2;
    pipeline.pStages = stages;
    pipeline.pVertexInputState = &vertex;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &ms;
    pipeline.pDepthStencilState = &depth;
    pipeline.pColorBlendState = &blending;
    pipeline.pDynamicState = &dynamic;
    pipeline.layout = layout_;
    const auto result =
        vkCreateGraphicsPipelines(renderer_.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &pipeline_);
    VkResult water_result = result;
    if (result == VK_SUCCESS) {
        depth.depthWriteEnable = VK_FALSE;
        raster.cullMode = VK_CULL_MODE_NONE;
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        water_result = vkCreateGraphicsPipelines(renderer_.device, VK_NULL_HANDLE, 1, &pipeline, nullptr,
                                                 &water_pipeline_);
    }
    VkResult debug_result = water_result;
    if (water_result == VK_SUCCESS) {
        // Filled diagnostic surfaces include water and ice, with ordinary depth testing.
        const VkBool32 enabled = VK_TRUE;
        const VkSpecializationMapEntry entry{0, 0, sizeof(enabled)};
        const VkSpecializationInfo specialization{1, &entry, sizeof(enabled), &enabled};
        stages[1].pSpecializationInfo = &specialization;
        depth.depthWriteEnable = VK_TRUE;
        blend.blendEnable = VK_FALSE;
        debug_result = vkCreateGraphicsPipelines(renderer_.device, VK_NULL_HANDLE, 1, &pipeline, nullptr,
                                                 &lod_debug_pipeline_);
    }
    vkDestroyShaderModule(renderer_.device, vert, nullptr);
    vkDestroyShaderModule(renderer_.device, frag, nullptr);
    vk_check(result, "world graphics pipeline");
    vk_check(water_result, "water graphics pipeline");
    vk_check(debug_result, "LOD debug graphics pipeline");
    const auto player_vert = renderer_.shader("shaders/player.vert.spv");
    VkShaderModule player_frag{};
    try {
        player_frag = renderer_.shader("shaders/player.frag.spv");
    } catch (...) {
        vkDestroyShaderModule(renderer_.device, player_vert, nullptr);
        throw;
    }
    stages[0].module = player_vert;
    stages[1].module = player_frag;
    for (auto& stage : stages)
        stage.pSpecializationInfo = nullptr;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    const auto player_result =
        vkCreateGraphicsPipelines(renderer_.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &player_pipeline_);
    vkDestroyShaderModule(renderer_.device, player_vert, nullptr);
    vkDestroyShaderModule(renderer_.device, player_frag, nullptr);
    vk_check(player_result, "player graphics pipeline");
}
void WorldView::retire(std::array<GpuChunk, chunks_per_column>& meshes) {
    auto* renderer = &renderer_;
    renderer_.defer([renderer, meshes] {
        for (const auto& mesh : meshes) {
            renderer->destroy_buffer(mesh.faces);
            renderer->destroy_buffer(mesh.lights);
            if (mesh.descriptor) {
                vkFreeDescriptorSets(renderer->device, mesh.pool->handle, 1, &mesh.descriptor);
                ++mesh.pool->available;
            }
        }
    });
    meshes = {};
}
void WorldView::describe(GpuChunk& mesh) {
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &face_layout_;
    for (const auto& pool : face_pools_) {
        if (!pool->available)
            continue;
        allocate.descriptorPool = pool->handle;
        const auto result = vkAllocateDescriptorSets(renderer_.device, &allocate, &mesh.descriptor);
        if (result == VK_SUCCESS) {
            mesh.pool = pool;
            --pool->available;
            break;
        }
        pool->available = 0;
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
            vk_check(result, "face descriptor allocation");
    }
    if (!mesh.pool) {
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2048};
        VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        info.maxSets = 1024;
        info.poolSizeCount = 1;
        info.pPoolSizes = &size;
        VkDescriptorPool pool{};
        vk_check(vkCreateDescriptorPool(renderer_.device, &info, nullptr, &pool), "face descriptor pool");
        try {
            auto state = std::make_shared<FacePool>();
            state->handle = pool;
            face_pools_.push_back(std::move(state));
        } catch (...) {
            vkDestroyDescriptorPool(renderer_.device, pool, nullptr);
            throw;
        }
        allocate.descriptorPool = pool;
        vk_check(vkAllocateDescriptorSets(renderer_.device, &allocate, &mesh.descriptor),
                 "face descriptor allocation");
        mesh.pool = face_pools_.back();
        --mesh.pool->available;
    }
    VkDescriptorBufferInfo buffer{mesh.faces.handle, 0, VkDeviceSize{mesh.count} * sizeof(PackedFace)};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = mesh.descriptor;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &buffer;
    VkDescriptorBufferInfo light_buffer{mesh.lights.handle, 0, VkDeviceSize{mesh.count} * sizeof(uint32_t)};
    std::array<VkWriteDescriptorSet, 2> writes{write, write};
    writes[1].dstBinding = 1;
    writes[1].pBufferInfo = &light_buffer;
    vkUpdateDescriptorSets(renderer_.device, 2, writes.data(), 0, nullptr);
}
void WorldView::regenerate(const GenerationConfig& config) {
    // Construct/validate first so an invalid draft leaves the running world intact.
    auto generator = std::make_shared<TerrainGenerator>(config);
    auto lod = std::make_shared<LodCache>(generator);
    auto stream =
        std::make_unique<WorldStream>(generator, [lod](Column column) { lod->submit(std::move(column)); });
    scene_effects_->invalidate_history();
    stream_.swap(stream);
    stream.reset(); // Cancel and join: no old completion can enter the replacement stream.
    lod_cache_ = std::move(lod);
    lod_renderer_->clear();
    generator_ = std::move(generator);
    light_worker_ = std::make_unique<LightWorker>();
    mesh_light_worker_ = std::make_unique<MeshLightWorker>();
    packing_.clear();
    packed_ready_.clear();
    mesh_bytes_ = 0;
    lights_.clear();
    light_dirty_.clear();
    light_uploads_.clear();
    light_job_.reset();
    light_update_job_ = false;
    light_changes_.clear();
    ++light_revision_;
    for (auto& [key, column] : columns_)
        retire(column.meshes);
    retire(uploading_);
    columns_.clear();
    visible_columns_ = 0;
    incoming_.reset();
    uploaded_chunks_.reset();
    remesh_chunks_.reset();
    edits_ = WorldEdits{};
    fluids_ = FluidSimulation{};
    fluid_accumulator_ = 0;
    dirty_chunks_.clear();
    urgent_chunks_.clear();
    target_.reset();
    centre_.reset();
    generation_ms = lighting_ms = meshing_ms = upload_cpu_ms = 0;
    drawn_chunks = triangles = 0;
    uploaded_bytes = 0;
    player_.horizontal_velocity = {};
    player_.vertical_velocity = 0;
    player_.grounded = false;
    reset_walk_bob();
    reset_movement_input();
    // Regeneration preserves the camera position and orientation, even inside the new terrain.
}
void WorldView::set_radius(int radius) {
    if (radius < 1 || radius > 64)
        throw std::runtime_error("Render distance must be 1..64 columns.");
    if (radius != radius_) {
        radius_ = radius;
        centre_.reset();
    }
}
void WorldView::reset_movement_input() {
    wheel_remainder_ = 0;
    suppress_time_action_ = true;
    last_space_press_.reset();
    jump_requested_ = false;
    suppress_space_action_ = true;
}
void WorldView::press_space(uint64_t timestamp_ns) {
    constexpr uint64_t double_tap_ns = 300'000'000;
    if (last_space_press_ && timestamp_ns >= *last_space_press_ &&
        timestamp_ns - *last_space_press_ <= double_tap_ns) {
        set_movement_mode(player_.movement_mode == MovementMode::fly ? MovementMode::walk
                                                                     : MovementMode::fly);
    } else {
        last_space_press_ = timestamp_ns;
        jump_requested_ = player_.movement_mode == MovementMode::walk;
    }
}
void WorldView::set_movement_mode(MovementMode mode) {
    player_.movement_mode = mode;
    player_.horizontal_velocity = {};
    player_.vertical_velocity = 0;
    player_.grounded = false;
    reset_walk_bob();
    reset_movement_input();
}
bool WorldView::teleport(glm::dvec3 position) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        std::abs(position.x) > 1e9 || std::abs(position.z) > 1e9 || position.y < 0 || position.y > 8192)
        return false;
    position.x = wrap_position(position.x);
    position.z = wrap_position(position.z);
    player_.position = previous_player_position_ = rendered_player_position_ = position;
    player_.horizontal_velocity = {};
    player_.vertical_velocity = movement_accumulator_ = 0;
    reset_walk_bob();
    player_.grounded = false;
    target_.reset();
    reset_movement_input();
    sync_camera();
    scene_effects_->invalidate_history();
    return true;
}
bool WorldView::set_time(unsigned hour, unsigned minute) {
    if (hour >= 24 || minute >= 60)
        return false;
    const auto day_start = world_tick() / ticks_per_day * ticks_per_day;
    clock_.set(double(day_start + hour * ticks_per_hour + minute * (ticks_per_hour / 60)));
    scene_effects_->invalidate_history();
    return true;
}
void WorldView::move_player(double seconds, const bool* keys) {
    constexpr double step = std::chrono::duration<double>(physics_step).count();
    if (!keys[SDL_SCANCODE_SPACE])
        suppress_space_action_ = false;
    const bool held_jump = keys[SDL_SCANCODE_SPACE] && !suppress_space_action_;
    const bool flying = player_.movement_mode == MovementMode::fly;
    const bool fast = keys[SDL_SCANCODE_LSHIFT];
    const double speed = flying ? (fast ? 100.0 : 24.0) : 1.0;
    const auto input = camera.movement_velocity(keys, flying, !suppress_space_action_, speed);
    const double yaw = glm::radians(camera.yaw);
    const glm::dvec2 facing{std::cos(yaw), std::sin(yaw)};
    const auto sample = [this](BlockPos p) { return loaded_block(p); };
    const double elapsed = std::clamp(seconds, 0.0, 0.1);
    visual_time_ = std::fmod(visual_time_ + elapsed, 131072.0);
    const bool backward = keys[SDL_SCANCODE_LEFTBRACKET];
    const bool forward = keys[SDL_SCANCODE_RIGHTBRACKET];
    if (!backward && !forward)
        suppress_time_action_ = false;
    const bool scrubbing = !suppress_time_action_ && (backward || forward);
    const double tick_rate =
        scrubbing ? (int(forward) - int(backward)) * double(ticks_per_hour) : double(physics_tps);
    clock_.advance(elapsed * tick_rate);
    movement_accumulator_ += elapsed;
    while (movement_accumulator_ >= step) {
        previous_player_position_ = player_.position;
        previous_walk_phase_ = walk_phase_;
        previous_walk_bob_ = walk_bob_;
        if (!flying) {
            const double target_bob =
                player_.supported(sample)
                    ? std::min(0.1, glm::length(player_.horizontal_velocity) / physics_tps)
                    : 0.0;
            walk_bob_ += (target_bob - walk_bob_) * 0.4;
            player_.walk_tick({input.x, input.z}, facing, fast, jump_requested_ || held_jump, sample);
            const glm::dvec2 moved{world_delta(player_.position.x, previous_player_position_.x),
                                   world_delta(player_.position.z, previous_player_position_.z)};
            walk_phase_ += glm::length(moved) * 0.6;
            const double cycles = std::floor(walk_phase_ / 2.0) * 2.0;
            walk_phase_ -= cycles;
            previous_walk_phase_ -= cycles;
        } else {
            player_.move(input * step, sample);
            player_.horizontal_velocity = {};
            player_.vertical_velocity = 0;
            player_.grounded = false;
            reset_walk_bob();
        }
        jump_requested_ = false;
        movement_accumulator_ -= step;
    }
    const double alpha = movement_accumulator_ / step;
    // Interpolate the nearest periodic image, never all the way across the map at a seam.
    const glm::dvec3 delta{world_delta(player_.position.x, previous_player_position_.x),
                           player_.position.y - previous_player_position_.y,
                           world_delta(player_.position.z, previous_player_position_.z)};
    // Clip the display pose too: straight interpolation must not cut through a wall corner.
    Player display_player = player_;
    display_player.position = previous_player_position_;
    display_player.move(delta * alpha, [this](BlockPos p) { return loaded_block(p); });
    rendered_player_position_ = display_player.position;
}
void WorldView::reset_walk_bob() {
    walk_phase_ = previous_walk_phase_ = walk_bob_ = previous_walk_bob_ = 0;
    camera.visual_rotation = glm::mat4(1);
}
void WorldView::set_view_bobbing(bool enabled) {
    if (view_bobbing_ == enabled)
        return;
    view_bobbing_ = enabled;
    sync_camera();
    scene_effects_->invalidate_history();
}
void WorldView::sync_camera() {
    camera.position = player_eye();
    if (camera.mode != CameraMode::first_person) {
        const auto direction = -camera.view_forward();
        camera.position += direction * camera_distance(camera.position, direction);
    }
    camera.visual_rotation = glm::mat4(1);
    if (view_bobbing_ && player_.movement_mode == MovementMode::walk) {
        const double alpha = movement_accumulator_ * physics_tps;
        const double phase = -glm::mix(previous_walk_phase_, walk_phase_, alpha) * glm::pi<double>();
        const double bob = glm::mix(previous_walk_bob_, walk_bob_, alpha);
        if (bob > 1e-6) {
            const glm::vec3 translation{std::sin(phase) * bob * 0.5, -std::abs(std::cos(phase) * bob), 0};
            auto rotation =
                glm::rotate(glm::mat4(1), glm::radians(float(std::sin(phase) * bob * 3)), glm::vec3(0, 0, 1));
            rotation = glm::rotate(rotation, glm::radians(float(std::abs(std::cos(phase - 0.2) * bob) * 5)),
                                   glm::vec3(1, 0, 0));
            const auto view = glm::lookAt(glm::vec3(0), glm::vec3(camera.view_forward()), glm::vec3(0, 1, 0));
            // Factor the reference's view-space translation into the actual render eye so
            // inverse projection, SSR, clouds, and TAA all share the same camera origin.
            const glm::dvec3 offset = -glm::transpose(glm::mat3(rotation * view)) * translation;
            const double length = glm::length(offset);
            const double allowed =
                length > 1e-8 ? camera_distance(camera.position, offset / length, length) : 0;
            const float fraction = length > 1e-8 ? float(allowed / length) : 1.0f;
            camera.position += offset * double(fraction);
            auto safe_rotation = glm::rotate(
                glm::mat4(1), glm::radians(float(std::sin(phase) * bob * 3) * fraction), glm::vec3(0, 0, 1));
            camera.visual_rotation = glm::rotate(
                safe_rotation, glm::radians(float(std::abs(std::cos(phase - 0.2) * bob) * 5) * fraction),
                glm::vec3(1, 0, 0));
        }
    }
    camera.position.x = wrap_position(camera.position.x);
    camera.position.z = wrap_position(camera.position.z);
}
void WorldView::cycle_camera() {
    camera.cycle_mode();
    sync_camera();
    update_target();
}
void WorldView::set_zoom(bool enabled) {
    if (camera.zoomed == enabled)
        return;
    camera.zoomed = enabled;
    sync_camera();
    scene_effects_->invalidate_history();
}
glm::dvec3 WorldView::player_eye() const {
    return rendered_player_position_ + glm::dvec3(0, Player::eye_height, 0);
}
double WorldView::camera_distance(glm::dvec3 origin, glm::dvec3 direction, double maximum) const {
    // Sweep a conservative box covering the camera and every near-plane corner.
    // Testing the whole segment avoids skipping thin walls or clipping at an oblique angle.
    const double aspect = double(renderer_.extent.width) / std::max(1u, renderer_.extent.height);
    const double tangent = std::tan(glm::radians(double(camera.effective_field_of_view())) * 0.5);
    const double margin =
        FlyCamera::near_clip * std::sqrt(1 + tangent * tangent * (1 + aspect * aspect)) + 0.02;
    double distance = maximum;
    const auto end = origin + direction * distance;
    const glm::ivec3 low(glm::floor(glm::min(origin, end) - glm::dvec3(margin)));
    const glm::ivec3 high(glm::floor(glm::max(origin, end) + glm::dvec3(margin)));
    for (int z = low.z; z <= high.z; ++z) {
        for (int y = low.y; y <= high.y; ++y) {
            if (y < 0 || y >= world_height)
                continue;
            for (int x = low.x; x <= high.x; ++x) {
                const auto block = loaded_block({x, y, z});
                if (block && !is_solid(*block))
                    continue;
                const double height = block ? block_height(*block) : 1.0;
                const glm::dvec3 minimum = glm::dvec3(x, y, z) - glm::dvec3(margin);
                const glm::dvec3 maximum = glm::dvec3(x + 1, y + height, z + 1) + glm::dvec3(margin);
                double entry = 0, exit = distance;
                bool intersects = true;
                for (int axis = 0; axis < 3; ++axis) {
                    if (std::abs(direction[axis]) < 1e-12) {
                        if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) {
                            intersects = false;
                            break;
                        }
                    } else {
                        const double a = (minimum[axis] - origin[axis]) / direction[axis];
                        const double b = (maximum[axis] - origin[axis]) / direction[axis];
                        entry = std::max(entry, std::min(a, b));
                        exit = std::min(exit, std::max(a, b));
                        if (entry > exit) {
                            intersects = false;
                            break;
                        }
                    }
                }
                if (!intersects)
                    continue;
                {
                    distance = std::max(0.0, entry - 0.001);
                    if (distance == 0)
                        return 0;
                }
            }
        }
    }
    return distance;
}
std::optional<FluidCell> WorldView::loaded_cell(BlockPos p) const {
    if (p.y < 0 || p.y >= world_height)
        return {};
    const auto it = columns_.find(canonical(ColumnKey{chunk_coordinate(p.x), chunk_coordinate(p.z)}));
    if (it == columns_.end() || !it->second.published)
        return {};
    const auto& data = it->second.data;
    return FluidCell{
        edits_.override_block(p, data.block_at(local_coordinate(p.x), p.y, local_coordinate(p.z))),
        edits_.override_fluid(p, data.fluid_at(local_coordinate(p.x), p.y, local_coordinate(p.z)))};
}
std::optional<Block> WorldView::loaded_block(BlockPos p) const {
    if (p.y < 0 || p.y >= world_height)
        return Block::air;
    const auto c = loaded_cell(p);
    return c ? std::optional{effective_block(c->block, c->fluid)} : std::nullopt;
}
void WorldView::advance_fluids(double seconds) {
    fluid_accumulator_ += std::clamp(seconds, 0.0, 0.1);
    constexpr double step = 1.0 / physics_tps;
    while (fluid_accumulator_ >= step) {
        tick_fluids();
        fluid_accumulator_ -= step;
    }
}
void WorldView::tick_fluids() {
    const auto changes = fluids_.tick([this](BlockPos p) { return loaded_cell(p); });
    bool relight = false;
    for (const auto& change : changes) {
        const auto p = change.position;
        const auto& column = columns_.at({p.x / 16, p.z / 16}).data;
        edits_.set_fluid(p, change.after, column.fluid_at(p.x % 16, p.y, p.z % 16));
        if (change.before.kind != change.after.kind ||
            fluid_display_level(change.before) != fluid_display_level(change.after) ||
            (change.before.amount == fluid_capacity) != (change.after.amount == fluid_capacity))
            dirty_cell(p);
        if (change.before.kind != change.after.kind ||
            bool(change.before.amount) != bool(change.after.amount)) {
            const Block block = edits_.override_block(p, column.block_at(p.x % 16, p.y, p.z % 16));
            light_changes_.push_back(
                {p, effective_block(block, change.before), effective_block(block, change.after)});
            relight = true;
        }
    }
    if (relight)
        ++light_revision_;
}
void WorldView::update_target() {
    const auto f = camera.forward();
    const auto eye = player_eye();
    target_ = raycast_blocks({eye.x, eye.y, eye.z}, {f.x, f.y, f.z},
                             [this](BlockPos p) { return loaded_block(p); });
}
bool WorldView::interact(bool place) {
    if (place && selected_block() == Block::air)
        return false;
    sync_camera();
    update_target();
    if (!target_)
        return false;
    BlockPos p = target_->block;
    Block edit = place ? selected_block() : Block::air;
    const auto hit_block = loaded_block(p);
    if (!place && hit_block && is_snow(*hit_block))
        edit = snow_block(snow_layers(*hit_block) - 1);
    if (place) {
        const bool add_to_hit =
            is_snow(edit) && hit_block && is_snow(*hit_block) && snow_layers(*hit_block) < 16;
        if (!add_to_hit) {
            if (!target_->adjacent)
                return false;
            p = *target_->adjacent;
        }
        const auto destination = loaded_block(p);
        if (p.y < 0 || p.y >= world_height || !destination)
            return false;
        const bool adding_snow = is_snow(edit) && is_snow(*destination) && snow_layers(*destination) < 16;
        if (adding_snow)
            edit = snow_block(snow_layers(*destination) + 1);
        else if (is_solid(*destination))
            return false;
        if (is_snow(edit) && !adding_snow) {
            const auto below = loaded_block({p.x, p.y - 1, p.z});
            if (!below || !is_full_block(*below))
                return false;
        }
        Player rendered_player = player_;
        rendered_player.position = rendered_player_position_;
        Player previous_player = player_;
        previous_player.position = previous_player_position_;
        const double height = block_height(edit);
        if (!is_fluid(edit) && (is_fluid(*destination) || player_.overlaps(p, height) ||
                                rendered_player.overlaps(p, height) || previous_player.overlaps(p, height)))
            return false;
    }
    const bool changed = apply_edit(p, edit);
    update_target();
    return changed;
}
bool WorldView::apply_edit(BlockPos p, Block block) {
    if (p.y < 0 || p.y >= world_height)
        return false;
    const auto key = canonical(ColumnKey{chunk_coordinate(p.x), chunk_coordinate(p.z)});
    auto it = columns_.find(key);
    if (it == columns_.end() || !it->second.published)
        return false;
    const auto& data = it->second.data;
    const Block base = data.block_at(local_coordinate(p.x), p.y, local_coordinate(p.z));
    const Block dry = edits_.override_block(p, base);
    const Fluid base_fluid = data.fluid_at(local_coordinate(p.x), p.y, local_coordinate(p.z));
    const Fluid fluid = edits_.override_fluid(p, base_fluid);
    const Block before = effective_block(dry, fluid);
    Block after;
    if (is_fluid(block)) {
        // Placement fills once; it does not register an inexhaustible source.
        const auto kind = block == Block::lava ? FluidKind::lava : FluidKind::water;
        if (is_solid(dry) || (fluid.amount && fluid.kind != kind) ||
            !edits_.set_fluid(p, fluid_amount(kind, fluid_capacity), base_fluid))
            return false;
        after = effective_block(dry, fluid_amount(kind, fluid_capacity));
    } else {
        if ((is_solid(block) && fluid.amount) || !edits_.set(p, block, base))
            return false;
        after = effective_block(block, fluid);
    }
    scene_effects_->invalidate_history();
    if (before != after) {
        ++light_revision_;
        light_changes_.push_back({{wrap_block(p.x), p.y, wrap_block(p.z)}, before, after});
    }
    fluids_.wake(p);
    dirty_cell(p);
    return true;
}
void WorldView::dirty_cell(BlockPos p) {
    if (const auto cell = loaded_cell(p))
        lod_cache_->edit(p.x, p.y, p.z, cell->block, cell->fluid);
    const auto key = canonical(ColumnKey{chunk_coordinate(p.x), chunk_coordinate(p.z)});
    const ChunkKey edited{key.x, p.y / 16, key.z};
    std::erase(urgent_chunks_, edited);
    urgent_chunks_.push_front(edited);
    for (auto affected : affected_chunks(p)) {
        packing_.erase(affected); // Late worker results cannot overwrite a newer edit.
        dirty_chunks_.insert(affected);
        if (incoming_ && incoming_->data.key == ColumnKey{affected.x, affected.z}) {
            // A neighbour can change while this column is part-way through uploading.
            remesh_chunks_.set(affected.y);
            retire(uploading_[affected.y]);
            uploaded_chunks_.reset(affected.y);
        }
    }
}
void WorldView::retire(GpuChunk& mesh) {
    if (!mesh.faces.handle && !mesh.lights.handle && !mesh.descriptor)
        return;
    auto* renderer = &renderer_;
    renderer_.defer([renderer, mesh] {
        renderer->destroy_buffer(mesh.faces);
        renderer->destroy_buffer(mesh.lights);
        if (mesh.descriptor) {
            vkFreeDescriptorSets(renderer->device, mesh.pool->handle, 1, &mesh.descriptor);
            ++mesh.pool->available;
        }
    });
    mesh = {};
}
void WorldView::upload(const PackedMesh& source, GpuChunk& destination) {
    profiling::Scope measure(profiling::Stage::upload_cpu, source.key.x, source.key.z, source.key.y);
    if (source.geometry->empty())
        return;
    destination.geometry = source.geometry;
    destination.light_values = source.values;
    const auto bytes = source.geometry->size() * sizeof(PackedFace);
    destination.faces =
        renderer_.upload_buffer(source.geometry->data(), bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    destination.count = static_cast<uint32_t>(source.geometry->size());
    destination.solid_count = source.solid_count;
    destination.ice_count = source.ice_count;
    try {
        destination.lights =
            renderer_.upload_buffer(source.values->data(), bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        describe(destination);
    } catch (...) {
        retire(destination);
        throw;
    }
    uploaded_bytes += bytes * 2;
}
void WorldView::queue_mesh(ChunkKey key, ChunkMesh source, const LightChunk& light, bool urgent) {
    PackedMesh request;
    request.key = key;
    request.ticket = ++mesh_ticket_;
    request.light = light;
    packing_[key] = request.ticket;
    mesh_light_worker_->submit(std::move(request), std::move(source), {}, urgent);
}
void WorldView::accept_meshes(std::chrono::steady_clock::time_point start) {
    auto completed = mesh_light_worker_->take();
    for (auto& result : completed) {
        if (columns_.contains({result.key.x, result.key.z}) && !result.light_only) {
            const auto next =
                std::find_if(packed_ready_.begin(), packed_ready_.end(), [&](const PackedMesh& pending) {
                    return pending.light_only || !columns_.contains({pending.key.x, pending.key.z});
                });
            packed_ready_.insert(next, std::move(result));
        } else
            packed_ready_.push_back(std::move(result));
    }
    unsigned count = 0;
    while (!packed_ready_.empty() && count < 4 && uploaded_bytes < 2 * 1024 * 1024 &&
           (count == 0 || std::chrono::steady_clock::now() - start < std::chrono::milliseconds(2))) {
        auto ready = std::move(packed_ready_.front());
        packed_ready_.pop_front();
        auto pending = packing_.find(ready.key);
        if (pending == packing_.end() || pending->second != ready.ticket)
            continue;
        packing_.erase(pending);
        const ColumnKey key{ready.key.x, ready.key.z};
        auto resident = columns_.find(key);
        const bool incoming = incoming_ && incoming_->data.key == key;
        if (resident == columns_.end() && !incoming)
            continue;
        auto& mesh =
            resident != columns_.end() ? resident->second.meshes[ready.key.y] : uploading_[ready.key.y];
        if (ready.light_only) {
            if (mesh.geometry != ready.geometry)
                continue;
            relight(mesh, ready);
        } else {
            GpuChunk replacement{};
            upload(ready, replacement);
            if (resident != columns_.end() && resident->second.published)
                scene_effects_->invalidate_history();
            if (resident != columns_.end()) {
                mesh_bytes_ -= uint64_t(mesh.count) * 8;
                mesh_bytes_ += uint64_t(replacement.count) * 8;
            }
            retire(mesh);
            mesh = std::move(replacement);
            if (incoming)
                uploaded_chunks_.set(ready.key.y);
        }
        // Geometry can be shown immediately using its sampled light, then brought up to
        // the newest accepted light snapshot without redoing geometry or losing edits.
        auto lit = lights_.find(key);
        if (lit != lights_.end()) {
            const auto& current = lit->second->chunks[ready.key.y];
            if (current.uniform != ready.light.uniform || current.values != ready.light.values ||
                current.uniform_opaque != ready.light.uniform_opaque ||
                current.occluders != ready.light.occluders)
                light_uploads_.insert(ready.key);
        }
        ++count;
    }
}
void WorldView::set_profile_view(double height, double yaw, double pitch, double hour) {
    camera.position.y = height;
    camera.yaw = yaw;
    camera.pitch = pitch;
    player_.position = camera.position - glm::dvec3(0, Player::eye_height, 0);
    previous_player_position_ = rendered_player_position_ = player_.position;
    clock_.set(hour * ticks_per_hour);
    visual_time_ = 0;
    sync_camera();
}
void WorldView::prepare() {
    profiling::Scope measure(profiling::Stage::prepare);
    ZoneScopedN("World streaming and upload recording");
    const auto start = std::chrono::steady_clock::now();
    uploaded_bytes = 0;
    // Changing camera side or pulling it away from a wall must not move the loading centre.
    const ColumnKey centre{static_cast<int>(std::floor(rendered_player_position_.x / 16)),
                           static_cast<int>(std::floor(rendered_player_position_.z / 16))};
    if (!centre_ || *centre_ != centre) {
        lod_cache_->request(centre, std::max(radius_, graphics_settings_.lod_distance), radius_,
                            graphics_settings_.lod, true);
        auto leaving = stream_->request(centre, radius_);
        centre_ = centre;
        for (auto key : leaving) {
            auto it = columns_.find(key);
            if (it == columns_.end())
                continue;
            for (int y = 0; y < chunks_per_column; ++y) {
                mesh_bytes_ -= uint64_t(it->second.meshes[y].count) * 8;
                packing_.erase({key.x, y, key.z});
                light_uploads_.erase({key.x, y, key.z});
            }
            retire(it->second.meshes);
            lights_.erase(key);
            light_dirty_.erase(key);
            if (it->second.published)
                --visible_columns_;
            columns_.erase(it);
        }
        if (incoming_ && !within_radius(incoming_->data.key, centre, radius_)) {
            for (int y = 0; y < chunks_per_column; ++y) {
                packing_.erase({incoming_->data.key.x, y, incoming_->data.key.z});
                light_uploads_.erase({incoming_->data.key.x, y, incoming_->data.key.z});
            }
            retire(uploading_);
            lights_.erase(incoming_->data.key);
            light_dirty_.erase(incoming_->data.key);
            incoming_.reset();
            uploaded_chunks_.reset();
        }
    }
    // The directly edited chunk goes first, before light uploads and terrain streaming.
    // Unavailable neighbour halos must not hold up other ready edited chunks.
    std::vector<ChunkKey> candidates(urgent_chunks_.begin(), urgent_chunks_.end());
    candidates.insert(candidates.end(), dirty_chunks_.begin(), dirty_chunks_.end());
    unsigned rebuilt = 0;
    for (const auto key : candidates) {
        if (!dirty_chunks_.contains(key))
            continue;
        const auto it = columns_.find({key.x, key.z});
        if (it == columns_.end()) {
            dirty_chunks_.erase(key);
            std::erase(urgent_chunks_, key);
            continue;
        }
        const auto lit = lights_.find({key.x, key.z});
        if (lit == lights_.end())
            continue;
        const auto halo = stream_->halo(key);
        if (!halo)
            continue;
        auto cpu = edits_.mesh(key, *halo);
        queue_mesh(key, std::move(cpu), lit->second->chunks[key.y], true);
        dirty_chunks_.erase(key);
        std::erase(urgent_chunks_, key);
        light_uploads_.erase(key);
        if (++rebuilt >= 4 || std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(2))
            break;
    }
    accept_meshes(start);
    prepare_lighting();
    if (!incoming_) {
        incoming_ = stream_->take_ready();
        if (incoming_) {
            remesh_chunks_ = edits_.affected(incoming_->data.key);
            uploaded_chunks_.reset();
            // Base columns already completed local light + boundary propagation on workers.
            // Session edits invalidate this shortcut across the full light reach, not just AO neighbours.
            if (incoming_->light && !edits_.has_nearby(incoming_->data.key))
                lights_[incoming_->data.key] = incoming_->light;
        }
    }
    if (incoming_) {
        const auto key = incoming_->data.key;
        if (!lights_.contains(key)) {
            if (!light_job_ || *light_job_ != key)
                light_dirty_.insert(key);
        } else if (!light_dirty_.contains(key) && (!light_job_ || *light_job_ != key) && !light_update_job_ &&
                   light_changes_.empty()) {
            unsigned queued = 0;
            for (int cy = 0; cy < chunks_per_column && queued < 4 && packing_.size() < 32; ++cy) {
                const ChunkKey chunk{key.x, cy, key.z};
                if (uploaded_chunks_.test(cy) || packing_.contains(chunk))
                    continue;
                if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(2))
                    break;
                auto& cpu = incoming_->meshes[cy];
                if (remesh_chunks_.test(cy)) {
                    const auto halo = stream_->halo(chunk);
                    if (!halo)
                        continue;
                    cpu = edits_.mesh(chunk, *halo);
                    remesh_chunks_.reset(cy);
                }
                if (cpu.faces.empty() && cpu.ice.empty() && cpu.water.empty()) {
                    uploaded_chunks_.set(cy);
                    continue;
                }
                queue_mesh(chunk, std::move(cpu), lights_.at(key)->chunks[cy], false);
                ++queued;
            }
            bool pending = false;
            for (int cy = 0; cy < chunks_per_column; ++cy)
                pending |=
                    light_uploads_.contains({key.x, cy, key.z}) || packing_.contains({key.x, cy, key.z});
            if (uploaded_chunks_.all() && !pending) {
                generation_ms = incoming_->generation_ms;
                lighting_ms = incoming_->lighting_ms;
                meshing_ms = incoming_->meshing_ms;
                auto [entry, inserted] = columns_.try_emplace(key);
                if (!inserted)
                    throw std::runtime_error("Attempted to upload an already resident column.");
                entry->second.data = std::move(incoming_->data);
                entry->second.meshes = uploading_;
                for (const auto& mesh : uploading_)
                    mesh_bytes_ += uint64_t(mesh.count) * 8;
                uploading_ = {};
                incoming_.reset();
                uploaded_chunks_.reset();
            }
        }
    }
    publish_columns();
    const bool near_busy =
        stream_->pending() || pending_lighting() || !dirty_chunks_.empty() || !packing_.empty() || incoming_;
    lod_cache_->request(centre, std::max(radius_, graphics_settings_.lod_distance), radius_,
                        graphics_settings_.lod, near_busy);
    std::vector<ColumnKey> published;
    for (const auto& [key, column] : columns_)
        if (column.published)
            published.push_back(key);
    lod_renderer_->prepare(lod_cache_->scene(), centre, published, lod_debug_);
    if (!graphics_settings_.lod)
        lod_renderer_->tiles = lod_renderer_->triangles = 0;
    upload_cpu_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    TracyPlot("World upload bytes", static_cast<int64_t>(uploaded_bytes));
    TracyPlot("Column generation ms", generation_ms);
    TracyPlot("Column meshing ms", meshing_ms);
}
void WorldView::publish_columns() {
    // Uploads that were started before a move may now be farther away. Keep their GPU
    // buffers resident but hidden, then let the upload path move on to the new nearest.
    for (unsigned count = 0; count < 8; ++count) {
        const auto key = stream_->next_to_publish();
        if (!key)
            return;
        const auto it = columns_.find(*key);
        if (it == columns_.end())
            return;
        if (it->second.published)
            throw std::logic_error("Published column remains in the streaming frontier.");
        if (light_dirty_.contains(*key) || (light_job_ && *light_job_ == *key) || light_update_job_ ||
            !light_changes_.empty())
            return;
        for (int y = 0; y < chunks_per_column; ++y) {
            const ChunkKey chunk{key->x, y, key->z};
            if (packing_.contains(chunk) || dirty_chunks_.contains(chunk) || light_uploads_.contains(chunk))
                return;
        }
        if (!stream_->publish(*key))
            return;
        if (profiling::enabled) {
            const auto published_key = *key;
            renderer_.defer([published_key] {
                profiling::event(profiling::Stage::gpu_retired, published_key.x, published_key.z);
            });
        }
        it->second.published = true;
        ++visible_columns_;
        auto current = it->second.data;
        edits_.apply(current);
        fluids_.seed(current, [this](BlockPos p) { return loaded_cell(p); });
    }
}
void WorldView::scroll_hotbar(float steps) {
    if (!std::isfinite(steps))
        return;
    wheel_remainder_ += std::clamp(steps, -1000.0f, 1000.0f);
    const int whole = static_cast<int>(wheel_remainder_);
    if (!whole)
        return;
    wheel_remainder_ -= whole;
    const int count = static_cast<int>(hotbar_blocks.size());
    const int next = ((selected_slot() - whole) % count + count) % count;
    selected_slot_ = next;
}
float WorldView::daylight() const {
    const double hours = clock_.day_ticks() / ticks_per_hour;
    const float rise = std::clamp(static_cast<float>((hours - 5.0) / 3.0), 0.0f, 1.0f);
    const float set = std::clamp(static_cast<float>((21.0 - hours) / 3.0), 0.0f, 1.0f);
    const float t = std::min(rise, set);
    return t * t * (3 - 2 * t);
}
void WorldView::relight(GpuChunk& mesh, const PackedMesh& prepared) {
    if (!mesh.count || prepared.unchanged)
        return;
    const auto& values = *prepared.values;
    GpuChunk replacement = mesh;
    replacement.lights = {};
    replacement.descriptor = {};
    replacement.pool = {};
    try {
        replacement.lights = renderer_.upload_buffer(values.data(), values.size() * sizeof(uint32_t),
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        replacement.light_values = prepared.values;
        describe(replacement);
    } catch (...) {
        replacement.faces = {};
        retire(replacement);
        throw;
    }
    // Geometry survives unchanged; only the previous light buffer/descriptor are retired.
    auto old = mesh;
    old.faces = {};
    retire(old);
    mesh = std::move(replacement);
    uploaded_bytes += size_t(mesh.count) * sizeof(uint32_t);
}
void WorldView::prepare_lighting() {
    if (auto ready = light_worker_->take()) {
        light_job_.reset();
        light_update_job_ = false;
        // These results advance the shared baseline. Edits received while the worker ran
        // stay queued and are applied to this baseline next; no global revision rejection.
        for (auto& column : ready->columns) {
            const auto key = column->key;
            if (!columns_.contains(key) && (!incoming_ || incoming_->data.key != key))
                continue;
            const auto old = lights_.find(key);
            for (int y = 0; y < chunks_per_column; ++y) {
                const auto& chunk = column->chunks[y];
                if (old != lights_.end()) {
                    const auto& previous = old->second->chunks[y];
                    if (previous.uniform == chunk.uniform && previous.values == chunk.values &&
                        previous.uniform_opaque == chunk.uniform_opaque &&
                        previous.occluders == chunk.occluders)
                        continue;
                }
                if (columns_.contains(key) ||
                    (incoming_ && incoming_->data.key == key && uploaded_chunks_.test(y)))
                    light_uploads_.insert({key.x, y, key.z});
            }
            lights_[key] = std::move(column);
        }
    }
    // Sampling runs on the CPU worker; main only snapshots immutable inputs here.
    unsigned queued = 0;
    for (auto it = light_uploads_.begin();
         it != light_uploads_.end() && queued < 4 && packing_.size() < 32;) {
        const auto key = *it;
        if (dirty_chunks_.contains(key) || packing_.contains(key)) {
            ++it;
            continue;
        }
        auto column = columns_.find({key.x, key.z});
        auto light = lights_.find({key.x, key.z});
        GpuChunk* mesh = column != columns_.end() ? &column->second.meshes[key.y] : nullptr;
        if (!mesh && incoming_ && incoming_->data.key == ColumnKey{key.x, key.z} &&
            uploaded_chunks_.test(key.y))
            mesh = &uploading_[key.y];
        it = light_uploads_.erase(it);
        if (!mesh || !mesh->count || light == lights_.end())
            continue;
        PackedMesh request;
        request.key = key;
        request.ticket = ++mesh_ticket_;
        request.geometry = mesh->geometry;
        request.solid_count = mesh->solid_count;
        request.ice_count = mesh->ice_count;
        request.light = light->second->chunks[key.y];
        request.light_only = true;
        packing_[key] = request.ticket;
        mesh_light_worker_->submit(std::move(request), {}, mesh->light_values);
        ++queued;
    }
    if (light_job_ || light_update_job_)
        return;
    if (!light_changes_.empty()) {
        // All edits since the last completed transaction form one consistent update.
        std::unordered_map<ColumnKey, Column, ColumnHash> region;
        std::set<ColumnKey, bool (*)(const ColumnKey&, const ColumnKey&)> affected(light_dirty_.key_comp());
        auto changed_columns = affected;
        for (const auto& change : light_changes_)
            changed_columns.insert({change.position.x / 16, change.position.z / 16});
        bool incremental = true;
        for (const auto centre : changed_columns) {
            for (int z = -1; z <= 1; ++z)
                for (int x = -1; x <= 1; ++x) {
                    const auto key = canonical(ColumnKey{centre.x + x, centre.z + z});
                    if (lights_.contains(key))
                        affected.insert(key);
                    else
                        incremental = false;
                }
            if (auto data = stream_->lighting_columns(centre)) {
                for (auto& column : *data)
                    region.try_emplace(column.key, std::move(column));
            } else {
                // A contended scheduler lock is not evidence that cached lighting is missing.
                // Retry the snapshot next frame before choosing an incremental/fallback path.
                if (incremental)
                    return;
            }
        }
        if (affected.empty()) {
            // The affected area was unloaded; a later initial solve reads current edits.
            light_changes_.clear();
        } else {
            std::vector<LightInput> inputs;
            std::vector<Column> snapshot;
            std::vector<LightRebuild> rebuilds;
            WorldEdits edits;
            if (incremental) {
                inputs.reserve(region.size());
                snapshot.reserve(region.size());
                for (auto& [key, column] : region) {
                    snapshot.push_back(column);
                    inputs.push_back({std::move(column), lights_.at(key)});
                }
                edits = edits_.subset(snapshot);
            } else {
                // Initial/loading-edge fallback only. Never guess missing neighbour light.
                for (auto key : affected) {
                    auto data = stream_->lighting_columns(key);
                    if (!data)
                        return;
                    auto subset = edits_.subset(*data);
                    rebuilds.push_back({key, std::move(*data), std::move(subset), lights_.at(key)});
                }
            }
            light_worker_->submit_update(light_revision_, std::move(inputs), std::move(edits),
                                         std::move(light_changes_), std::move(rebuilds));
            light_changes_.clear();
            light_update_job_ = true;
            return;
        }
    }
    // Initial solves never leap ahead of pending edits, keeping all cached columns at
    // the same edit baseline even when their initial generation revisions differ.
    for (auto it = light_dirty_.begin(); it != light_dirty_.end();) {
        const auto key = *it;
        if (!columns_.contains(key) && (!incoming_ || incoming_->data.key != key)) {
            it = light_dirty_.erase(it);
            continue;
        }
        auto columns = stream_->lighting_columns(key);
        if (!columns) {
            ++it;
            continue;
        }
        auto edits = edits_.subset(*columns);
        std::array<std::shared_ptr<const ColumnLight>, 9> known;
        for (size_t i = 0; i < columns->size(); ++i) {
            auto cached = lights_.find((*columns)[i].key);
            if (cached != lights_.end())
                known[i] = cached->second;
        }
        light_worker_->submit(key, light_revision_, std::move(*columns), std::move(edits), std::move(known));
        light_job_ = key;
        light_dirty_.erase(it);
        break;
    }
}
void WorldView::ensure_depth() {
    if (depth_view_ && depth_extent_.width == renderer_.extent.width &&
        depth_extent_.height == renderer_.extent.height)
        return;
    // Only resize waits. Streaming uploads never use queue/device idle.
    renderer_.wait_idle();
    if (depth_view_)
        vkDestroyImageView(renderer_.device, depth_view_, nullptr);
    if (depth_)
        vmaDestroyImage(renderer_.allocator, depth_, depth_allocation_);
    depth_view_ = VK_NULL_HANDLE;
    depth_ = VK_NULL_HANDLE;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(renderer_.physical_device, VK_FORMAT_D32_SFLOAT, &properties);
    constexpr auto depth_features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((properties.optimalTilingFeatures & depth_features) != depth_features)
        throw std::runtime_error("D32 depth attachment/copy unavailable.");
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_D32_SFLOAT;
    ci.extent = {renderer_.extent.width, renderer_.extent.height, 1};
    ci.mipLevels = ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vk_check(vmaCreateImage(renderer_.allocator, &ci, &ai, &depth_, &depth_allocation_, nullptr),
             "world depth image");
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = depth_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ci.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    vk_check(vkCreateImageView(renderer_.device, &vi, nullptr, &depth_view_), "world depth view");
    depth_extent_ = renderer_.extent;
}
void WorldView::render() {
    ZoneScopedN("World render");
    ensure_depth();
    renderer_.finish_uploads();
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = barrier.dstStageMask =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = depth_;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(renderer_.command, &dependency);
    const float sun = daylight();
    const std::array<float, 4> sky{0.012f + sun * 0.468f, 0.018f + sun * 0.672f, 0.045f + sun * 0.815f, 1.0f};
    const auto cmd = renderer_.command;
    const auto matrix = scene_effects_->jittered(
        camera.view_projection(float(renderer_.extent.width) / renderer_.extent.height, 8192.0f),
        graphics_settings_.taa && !lod_debug_);
    const glm::vec3 player_offset{
        world_delta(rendered_player_position_.x, camera.position.x) - Player::width * 0.5,
        rendered_player_position_.y - camera.position.y,
        world_delta(rendered_player_position_.z, camera.position.z) - Player::depth * 0.5};
    const BlockPos camera_block{int(std::floor(camera.position.x)), int(std::floor(camera.position.y)),
                                int(std::floor(camera.position.z))};
    const auto camera_cell = loaded_cell(camera_block);
    bool underwater = false;
    if (camera_cell && camera_cell->fluid.amount && camera_cell->fluid.kind == FluidKind::water) {
        std::optional<ChunkHalo> fallback;
        bool tried_fallback = false;
        const auto sample = [&](BlockPos p) -> FluidCell {
            if (p.y < 0 || p.y >= world_height)
                return {};
            const auto it = columns_.find(canonical(ColumnKey{chunk_coordinate(p.x), chunk_coordinate(p.z)}));
            Block block = Block::rock;
            Fluid fluid{};
            if (it != columns_.end()) {
                block = it->second.data.block_at(local_coordinate(p.x), p.y, local_coordinate(p.z));
                fluid = it->second.data.fluid_at(local_coordinate(p.x), p.y, local_coordinate(p.z));
            } else {
                // Meshing can already see the data-only halo outside the published columns.
                if (!tried_fallback) {
                    fallback = stream_->halo(
                        canonical(ChunkKey{chunk_coordinate(camera_block.x), chunk_coordinate(camera_block.y),
                                           chunk_coordinate(camera_block.z)}));
                    tried_fallback = true;
                }
                if (fallback) {
                    const int x = local_coordinate(camera_block.x) + p.x - camera_block.x;
                    const int y = local_coordinate(camera_block.y) + p.y - camera_block.y;
                    const int z = local_coordinate(camera_block.z) + p.z - camera_block.z;
                    const int index = x + 1 + 18 * (y + 1 + 18 * (z + 1));
                    block = (*fallback)[index];
                    fluid = fallback->fluids[index];
                }
            }
            return {edits_.override_block(p, block), edits_.override_fluid(p, fluid)};
        };
        std::array<FluidSurfaceSample, 9> samples{};
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const auto cell = sample({camera_block.x + dx, camera_block.y, camera_block.z + dz});
                const auto above = sample({camera_block.x + dx, camera_block.y + 1, camera_block.z + dz});
                samples[dx + 1 + 3 * (dz + 1)] = {cell.block, cell.fluid, above.fluid};
            }
        const float height =
            fluid_surface_height(fluid_surface(samples), float(camera.position.x - camera_block.x),
                                 float(camera.position.z - camera_block.z));
        underwater = camera.position.y < camera_block.y + height;
    }
    float camera_sky = camera.position.y >= world_height ? 1.0f : 0.0f;
    if (camera.position.y >= 0 && camera.position.y < world_height) {
        const int x = int(std::floor(camera.position.x)), y = int(std::floor(camera.position.y)),
                  z = int(std::floor(camera.position.z));
        const auto lit = lights_.find(canonical(ColumnKey{chunk_coordinate(x), chunk_coordinate(z)}));
        if (lit != lights_.end())
            camera_sky =
                (lit->second->chunks[y / 16].at(local_coordinate(x), y % 16, local_coordinate(z)) & 15) /
                15.0f;
    }
    scene_effects_->prepare(
        graphics_settings_, water_settings_, matrix, camera.position, clock_.day_ticks() / ticks_per_hour,
        sun, visual_time_, lod_debug_, camera_sky, underwater,
        float((graphics_settings_.lod ? std::max(radius_, graphics_settings_.lod_distance) : radius_) * 16));
    renderer_.gpu_mark("scene_setup");
    if (scene_effects_->shadow_update_needed()) {
        const auto shadow_layout = scene_effects_->shadow_layout();
        // Render solid casters once, retain their depth, then add only water to the copy.
        for (const unsigned layer : {1u, 0u}) {
            if (layer == 0) {
                scene_effects_->copy_solid_shadow();
                renderer_.gpu_mark("shadow_depth_copy");
            }
            const auto& light_matrix = scene_effects_->shadow_matrix(layer);
            const auto light_planes = frustum(light_matrix);
            scene_effects_->begin_shadow(layer);
            for (const auto& [key, column] : columns_) {
                if (!column.published)
                    continue;
                const glm::dvec3 base(world_delta(double(key.x) * 16, camera.position.x), -camera.position.y,
                                      world_delta(double(key.z) * 16, camera.position.z));
                // Reject whole distant columns before inspecting their 32 per-chunk meshes.
                if (!visible(light_planes, glm::vec3(base), glm::vec3(16, world_height, 16)))
                    continue;
                for (int cy = 0; cy < chunks_per_column; ++cy) {
                    const auto& mesh = column.meshes[cy];
                    const auto offset = glm::vec3(base + glm::dvec3(0, cy * 16, 0));
                    const uint32_t count = layer == 1 ? mesh.solid_count : mesh.count - mesh.solid_count;
                    if (!count || !visible(light_planes, offset))
                        continue;
                    const Push push{light_matrix,
                                    glm::vec4(offset, 1.0f - 25.6f / graphics_settings_.shadow_distance),
                                    glm::vec4(0)};
                    vkCmdPushConstants(cmd, shadow_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push),
                                       &push);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_layout, 1, 1,
                                            &mesh.descriptor, 0, nullptr);
                    vkCmdDraw(cmd, 6, count, 0, layer == 1 ? 0 : mesh.solid_count);
                }
            }
            if (layer == 1) {
                scene_effects_->shadow_player();
                const Push push{light_matrix,
                                glm::vec4(player_offset, 1.0f - 25.6f / graphics_settings_.shadow_distance),
                                glm::vec4(Player::width, Player::height, Player::depth, 0)};
                vkCmdPushConstants(cmd, shadow_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                vkCmdDraw(cmd, 6, 6, 0, 0);
            }
            if (graphics_settings_.lod)
                lod_renderer_->draw(light_matrix, camera.position, graphics_settings_.lod_distance, false,
                                    int(layer), graphics_settings_.shadow_distance);
            scene_effects_->end_shadow();
            renderer_.gpu_mark(layer == 0 ? "shadow_water" : "shadow_solid");
        }
    }
    scene_effects_->begin_scene();
    renderer_.begin_rendering(depth_view_, sky, true);
    scene_effects_->bind_environment(layout_, 2);
    VkViewport viewport{
        0, 0, static_cast<float>(renderer_.extent.width), static_cast<float>(renderer_.extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, renderer_.extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lod_debug_ ? lod_debug_pipeline_ : pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &atlas_.descriptor, 0,
                            nullptr);
    const auto planes = frustum(matrix);
    struct WaterDraw {
        const GpuChunk* mesh;
        glm::vec3 offset;
        float distance;
    };
    std::vector<WaterDraw> water_draws, ice_draws;
    drawn_chunks = triangles = 0;
    for (const auto& [key, column] : columns_) {
        if (!column.published)
            continue;
        const glm::dvec3 base(world_delta(static_cast<double>(key.x) * 16, camera.position.x),
                              -camera.position.y,
                              world_delta(static_cast<double>(key.z) * 16, camera.position.z));
        for (int cy = 0; cy < chunks_per_column; ++cy) {
            const auto& mesh = column.meshes[cy];
            if (!mesh.count)
                continue;
            const auto offset = glm::vec3(base + glm::dvec3(0, cy * 16, 0));
            if (!visible(planes, offset))
                continue;
            glm::vec4 selected(0);
            if (target_ && wrap_column(chunk_coordinate(target_->block.x)) == key.x &&
                wrap_column(chunk_coordinate(target_->block.z)) == key.z && target_->block.y / 16 == cy)
                selected = glm::vec4(local_coordinate(target_->block.x), target_->block.y % 16,
                                     local_coordinate(target_->block.z), 1);
            if (lod_debug_ || mesh.solid_count) {
                const Push push{matrix, glm::vec4(offset, sun), selected};
                vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &mesh.descriptor,
                                        0, nullptr);
                vkCmdDraw(cmd, 6, lod_debug_ ? mesh.count : mesh.solid_count, 0, 0);
            }
            if (!lod_debug_ && mesh.ice_count) {
                const auto centre = offset + glm::vec3(8);
                ice_draws.push_back({&mesh, offset, glm::dot(centre, centre)});
            }
            if (!lod_debug_ && mesh.count > mesh.solid_count + mesh.ice_count) {
                const auto centre = offset + glm::vec3(8);
                water_draws.push_back({&mesh, offset, glm::dot(centre, centre)});
            }
            ++drawn_chunks;
            triangles += mesh.count * 2;
        }
    }
    if (graphics_settings_.lod) {
        lod_renderer_->draw(matrix, camera.position, std::max(radius_, graphics_settings_.lod_distance),
                            lod_debug_);
        triangles += uint32_t(lod_renderer_->triangles);
        // LOD에서 별도 레이아웃을 사용했으므로 일반 플레이어 디스크립터를 복구한다.
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &atlas_.descriptor, 0,
                                nullptr);
        scene_effects_->bind_environment(layout_, 2);
    }
    // The six procedural faces use the same dimensions as the collision box, without a mesh upload.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, player_pipeline_);
    const int px = static_cast<int>(std::floor(rendered_player_position_.x));
    const int py = static_cast<int>(std::floor(rendered_player_position_.y + Player::eye_height));
    const int pz = static_cast<int>(std::floor(rendered_player_position_.z));
    uint8_t sample = py >= world_height ? 15 : 0;
    if (py >= 0 && py < world_height) {
        auto lit = lights_.find(canonical(ColumnKey{chunk_coordinate(px), chunk_coordinate(pz)}));
        if (lit != lights_.end())
            sample = lit->second->chunks[py / 16].at(local_coordinate(px), py % 16, local_coordinate(pz));
    }
    const Push player_push{matrix, glm::vec4(player_offset, (sample & 15) / 15.0f),
                           glm::vec4(Player::width, Player::height, Player::depth, (sample >> 4) / 15.0f)};
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(player_push), &player_push);
    vkCmdDraw(cmd, 6, 6, 0, 0);
    renderer_.gpu_mark("opaque_terrain_player");
    scene_effects_->atmosphere(depth_, depth_view_);
    if (!ice_draws.empty()) {
        vkCmdEndRendering(cmd);
        if (!ice_effects_)
            ice_effects_ = std::make_unique<WaterEffects>(renderer_, face_layout_,
                                                          scene_effects_->environment_layout(), true);
        // Ice is independent of water options. Its material has no waves, foam or water absorption.
        const WaterSettings ice_settings{};
        ice_effects_->prepare(matrix, camera.position, glm::vec4(sky[0], sky[1], sky[2], sky[3]),
                              ice_settings, float(visual_time_), float(radius_ * chunk_edge), false);
        ice_effects_->snapshot(depth_);
        renderer_.gpu_mark("ice_snapshot");
        const auto ice_layout = ice_effects_->layout();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ice_layout, 0, 1, &atlas_.descriptor, 0,
                                nullptr);
        scene_effects_->bind_environment(ice_layout, 3);
        std::sort(ice_draws.begin(), ice_draws.end(),
                  [](const auto& a, const auto& b) { return a.distance < b.distance; });
        const auto draw_ice = [&] {
            for (const auto& draw : ice_draws) {
                glm::vec4 selected(0);
                if (target_) {
                    const glm::vec3 position{world_delta(double(target_->block.x), camera.position.x),
                                             double(target_->block.y) - camera.position.y,
                                             world_delta(double(target_->block.z), camera.position.z)};
                    const auto local = glm::round(position - draw.offset);
                    if (glm::all(glm::greaterThanEqual(local, glm::vec3(0))) &&
                        glm::all(glm::lessThan(local, glm::vec3(16))))
                        selected = glm::vec4(local, 1);
                }
                const Push push{matrix, glm::vec4(draw.offset, sun), selected};
                vkCmdPushConstants(cmd, ice_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ice_layout, 1, 1,
                                        &draw.mesh->descriptor, 0, nullptr);
                vkCmdDraw(cmd, 6, draw.mesh->ice_count, 0, draw.mesh->solid_count);
            }
        };
        ice_effects_->begin_reflections();
        draw_ice();
        ice_effects_->end_reflections();
        renderer_.gpu_mark("ice_ssr");
        renderer_.resume_world(depth_view_);
        ice_effects_->bind_surface();
        draw_ice();
        renderer_.gpu_mark("ice_surface");
        for (const auto& draw : ice_draws)
            triangles += draw.mesh->ice_count * 2;
        vkCmdEndRendering(cmd);
        // Ice depth must not be mistaken for a water surface by the later refraction pass.
        scene_effects_->refresh_water_background_depth(depth_);
        renderer_.resume_world(depth_view_);
    }
    // Fullscreen passes use other layouts: restore the atlas and environment for water.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &atlas_.descriptor, 0,
                            nullptr);
    scene_effects_->bind_environment(layout_, 2);
    // Both water paths blend against the same preserved opaque background.
    std::sort(water_draws.begin(), water_draws.end(),
              [](const auto& a, const auto& b) { return a.distance > b.distance; });
    const int water_radius =
        graphics_settings_.lod ? std::max(radius_, graphics_settings_.lod_distance) : radius_;
    const bool lod_water = graphics_settings_.lod && !lod_debug_ && lod_renderer_->water_visible();
    const bool has_water = !water_draws.empty() || lod_water;
    VkPipelineLayout water_layout = layout_;
    if (has_water && (water_settings_.active() || underwater)) {
        vkCmdEndRendering(cmd);
        if (!water_effects_)
            water_effects_ =
                std::make_unique<WaterEffects>(renderer_, face_layout_, scene_effects_->environment_layout(),
                                               false, lod_renderer_->coverage_layout());
        water_effects_->prepare(matrix, camera.position, glm::vec4(sky[0], sky[1], sky[2], sky[3]),
                                water_settings_, float(visual_time_), float(water_radius * chunk_edge),
                                underwater);
        water_effects_->snapshot(depth_);
        renderer_.gpu_mark("water_snapshot");
        water_layout = water_effects_->layout();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, water_layout, 0, 1, &atlas_.descriptor,
                                0, nullptr);
        scene_effects_->bind_environment(water_layout, 3);
        if (water_effects_->needs_surface_mask()) {
            water_effects_->begin_reflections();
            for (auto draw = water_draws.rbegin(); draw != water_draws.rend(); ++draw) {
                const Push push{matrix, glm::vec4(draw->offset, sun), glm::vec4(0)};
                vkCmdPushConstants(cmd, water_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, water_layout, 1, 1,
                                        &draw->mesh->descriptor, 0, nullptr);
                vkCmdDraw(cmd, 6, draw->mesh->count - draw->mesh->solid_count - draw->mesh->ice_count, 0,
                          draw->mesh->solid_count + draw->mesh->ice_count);
                triangles += (draw->mesh->count - draw->mesh->solid_count - draw->mesh->ice_count) * 2;
            }
            if (lod_water) {
                water_effects_->bind_lod_reflections();
                const auto lod_layout = water_effects_->layout(true);
                scene_effects_->bind_environment(lod_layout, 3);
                lod_renderer_->draw_water(matrix, camera.position, water_radius, lod_layout);
            }
            water_effects_->end_reflections();
            renderer_.gpu_mark("water_ssr");
        }
        renderer_.resume_world(depth_view_);
        if (lod_water) {
            water_effects_->bind_surface(true);
            const auto lod_layout = water_effects_->layout(true);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lod_layout, 0, 1,
                                    &atlas_.descriptor, 0, nullptr);
            scene_effects_->bind_environment(lod_layout, 3);
            lod_renderer_->draw_water(matrix, camera.position, water_radius, lod_layout);
        }
        // LOD uses coverage instead of block-face descriptors, so restore the near-water layout.
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, water_layout, 0, 1, &atlas_.descriptor,
                                0, nullptr);
        scene_effects_->bind_environment(water_layout, 3);
        water_effects_->bind_surface();
    } else {
        if (lod_water)
            lod_renderer_->draw_water(matrix, camera.position, water_radius);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &atlas_.descriptor, 0,
                                nullptr);
        scene_effects_->bind_environment(layout_, 2);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, water_pipeline_);
    }
    for (const auto& draw : water_draws) {
        const Push push{matrix, glm::vec4(draw.offset, sun), glm::vec4(0)};
        vkCmdPushConstants(cmd, water_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, water_layout, 1, 1,
                                &draw.mesh->descriptor, 0, nullptr);
        vkCmdDraw(cmd, 6, draw.mesh->count - draw.mesh->solid_count - draw.mesh->ice_count, 0,
                  draw.mesh->solid_count + draw.mesh->ice_count);
    }
    renderer_.gpu_mark("water_surface");
    // LOD water already retained its nearest surface. Add normal water depth now
    // for foreground clouds and underwater absorption; opaque depth stays in the snapshot.
    if (scene_effects_->surface_depth_needed() && has_water) {
        scene_effects_->begin_water_depth(depth_, depth_view_);
        const auto depth_layout = scene_effects_->shadow_layout();
        for (const auto& draw : water_draws) {
            const Push push{matrix, glm::vec4(draw.offset, sun), glm::vec4(0)};
            vkCmdPushConstants(cmd, depth_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_layout, 1, 1,
                                    &draw.mesh->descriptor, 0, nullptr);
            vkCmdDraw(cmd, 6, draw.mesh->count - draw.mesh->solid_count - draw.mesh->ice_count, 0,
                      draw.mesh->solid_count + draw.mesh->ice_count);
            triangles += (draw.mesh->count - draw.mesh->solid_count - draw.mesh->ice_count) * 2;
        }
        if (lod_water)
            lod_renderer_->draw_water_depth(matrix, camera.position, water_radius);
    }
    vkCmdEndRendering(cmd);
    renderer_.gpu_mark("water_depth");
    scene_effects_->composite_volume(depth_);
    scene_effects_->finish();
    renderer_.begin_ui();
}
} // namespace sandbox
