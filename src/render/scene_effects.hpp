#pragma once
#include "core/settings.hpp"
#include "render/renderer.hpp"
#include <array>
#include <chrono>
#include <glm/glm.hpp>

namespace sandbox {
// Complementary-style lighting in camera-relative Vulkan coordinates.
// UI is drawn after finish(), directly into the presentation target.
class SceneEffects {
  public:
    SceneEffects(Renderer& renderer, VkDescriptorSetLayout faces);
    ~SceneEffects();
    SceneEffects(const SceneEffects&) = delete;
    SceneEffects& operator=(const SceneEffects&) = delete;
    void prepare(const GraphicsSettings& settings, const WaterSettings& water, const glm::mat4& matrix,
                 glm::dvec3 camera, double hours, float daylight, double time, bool lod_debug,
                 float sky_visibility, bool underwater, float render_distance);
    glm::mat4 jittered(const glm::mat4& matrix, bool enabled) const;
    bool volume_enabled() const { return uniform_.flags.y > 0 || uniform_.effects.y > 0; }
    bool foreground_volume_enabled() const { return volume_enabled() && !underwater_; }
    bool underwater_fog_enabled() const { return uniform_.hydro.y > 0; }
    bool surface_depth_needed() const {
        return uniform_.view.w > 0 || foreground_volume_enabled() || underwater_fog_enabled() ||
               (underwater_ && uniform_.effects.y > 0);
    }
    bool shadows_enabled() const { return uniform_.flags.x > 0; }
    bool shadow_update_needed() const { return update_shadows_; }
    void invalidate_history() { history_dirty_ = true; }
    const glm::mat4& shadow_matrix(unsigned layer) const { return uniform_.shadow[layer]; }
    // Layer 1: solid depth + metadata (clear). Copy, then layer 0: water only (load).
    void begin_shadow(unsigned layer);
    void copy_solid_shadow();
    void shadow_player();
    void end_shadow();
    // Explicit diagnostic readback after submission; never used in normal rendering.
    void capture_shadow_maps(const std::filesystem::path& directory);
    VkPipelineLayout shadow_layout() const { return shadow_layout_; }
    VkDescriptorSetLayout environment_layout() const { return environment_layout_; }
    void bind_environment(VkPipelineLayout layout, uint32_t set);
    void begin_scene();
    // Pre-water sky/fog preserves opaque depth for refraction and SSR.
    void atmosphere(VkImage depth, VkImageView view);
    // After static translucent ice, establish the background depth used by water refraction.
    // Called outside rendering, before drawing water.
    void refresh_water_background_depth(VkImage depth);
    void begin_water_depth(VkImage depth, VkImageView view);
    // Called outside rendering, after nearest water depth has been written.
    void composite_volume(VkImage depth);
    void finish();

  private:
    struct Image {
        VkImage handle{};
        VmaAllocation allocation{};
        VkImageView view{};
        VkExtent2D extent{};
    };
    struct Frame {
        Image scene, composite, volume, metadata, toned, history, scene_factor, opaque_depth;
        std::array<Image, 8> bloom;
        std::array<Image, 8> bloom_mips;
        Buffer uniform;
        VkDescriptorSet environment{}, atmosphere{}, volume_composite{}, tone{}, taa{}, present{}, factor{};
        std::array<VkDescriptorSet, 8> bloom_sets{};
        std::array<VkDescriptorSet, 8> bloom_mip_sets{};
        bool history_initialized{}, factor_initialized{};
    };
    struct Uniform {
        glm::mat4 inverse, shadow[2];
        glm::vec4 camera_time, light, solar, sky;
        glm::vec4 flags;        // shadows, clouds, atmosphere, fog
        glm::vec4 effects;      // cloud shadows, shafts, bloom strength, lod_debug
        glm::vec4 cloud;        // altitude, coverage, quality, periodic reference noise wind
        glm::vec4 limits;       // far shadow distance, shadow texel size, shaft steps, sky visibility
        glm::vec4 hydro;        // caustics, underwater fog, sea surface relative Y, animation phase
        glm::vec4 water_origin; // periodic caustic camera X/Z, sun/moon enabled, stars enabled
        glm::mat4 previous_camera;
        glm::vec4 temporal; // history valid, camera movement, frame index, scene-factor update
        glm::vec4 cycle;    // noon factor, night factor, sun factor, sun visibility
        glm::vec4 view;     // render distance, underwater, TAA enabled, water refraction
    } uniform_{};
    Renderer& renderer_;
    std::array<Frame, Renderer::frames_in_flight> frames_{};
    VkDescriptorSetLayout environment_layout_{}, image_layout_{};
    VkPipelineLayout post_layout_{}, shadow_layout_{};
    VkPipeline shadow_pipeline_{}, player_shadow_pipeline_{}, volume_pipeline_{}, water_depth_pipeline_{};
    VkPipeline atmosphere_pipeline_{}, bloom_pipeline_{}, tone_pipeline_{}, taa_pipeline_{},
        present_pipeline_{}, factor_pipeline_{};
    VkDescriptorPool pool_{};
    VkSampler linear_{}, nearest_{}, compare_{}, repeat_{};
    Texture reference_noise_{}, reference_water_{};
    VkExtent2D extent_{};
    std::array<uint32_t, 2> shadow_sizes_{};
    // Same projection: [0] includes water, [1] contains only opaque casters.
    std::array<Image, 2> shadows_{};
    std::array<Image, 2> shadow_colour_{};
    bool shadows_initialized_{}, update_shadows_{};
    glm::mat4 previous_matrix_{1};
    glm::dvec3 previous_position_{};
    glm::vec3 previous_light_{};
    GraphicsSettings previous_settings_{};
    WaterSettings previous_water_{};
    uint32_t frame_index_{}, previous_slot_{};
    bool history_valid_{}, history_dirty_{true}, previous_underwater_{};
    std::chrono::steady_clock::time_point next_factor_update_{};
    VkImage scene_depth_{};
    bool underwater_{};
    void ensure_images(std::array<uint32_t, 2> shadow_sizes);
    Image create_image(VkFormat format, VkExtent2D size, VkImageUsageFlags usage, uint32_t layers = 1);
    VkDescriptorSet allocate(VkDescriptorSetLayout layout);
    void write_images(VkDescriptorSet set, const std::array<VkImageView, 4>& images, bool depth = false);
    VkPipeline create_pipeline(const char* vertex, const char* fragment, VkPipelineLayout layout,
                               const VkFormat* formats, uint32_t colours, bool depth = false,
                               bool bias = true);
    void begin_target(const Image& image, const Image* second = nullptr);
    void draw_post(VkPipeline pipeline, VkDescriptorSet images);
    void release_images() noexcept;
    void shutdown() noexcept;
};
} // namespace sandbox
