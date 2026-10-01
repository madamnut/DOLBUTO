#pragma once
#include "core/settings.hpp"
#include "render/renderer.hpp"
#include <array>
#include <glm/glm.hpp>

namespace sandbox {
// A 75%-per-axis SSR pass and full-resolution reference material composite.
// Ice uses a separate instance/snapshot, static normals and nearest-surface depth writes.
// World geometry/streaming stays in WorldView. Each in-flight frame owns its images/UBO.
class WaterEffects {
  public:
    WaterEffects(Renderer& renderer, VkDescriptorSetLayout faces, VkDescriptorSetLayout environment,
                 bool ice = false, VkDescriptorSetLayout lod_coverage = VK_NULL_HANDLE);
    ~WaterEffects();
    WaterEffects(const WaterEffects&) = delete;
    WaterEffects& operator=(const WaterEffects&) = delete;
    void prepare(const glm::mat4& matrix, glm::dvec3 camera, glm::vec4 sky, const WaterSettings& settings,
                 float time, float reflection_distance, bool underwater);
    // Called outside dynamic rendering; preserves the opaque background for blending.
    void snapshot(VkImage depth);
    bool needs_surface_mask() const { return needs_reflection_; }
    void begin_reflections();
    void end_reflections();
    void bind_surface(bool lod = false);
    void bind_lod_reflections();
    void bind_reflections();
    VkPipelineLayout layout(bool lod = false) const { return lod ? lod_layout_ : layout_; }

  private:
    struct Image {
        VkImage handle{};
        VmaAllocation allocation{};
        VkImageView view{};
    };
    struct Frame {
        Image colour, depth, reflection, reflection_depth, metadata;
        Buffer uniform;
        VkDescriptorSet descriptor{};
    };
    struct Uniform {
        glm::mat4 matrix, inverse;
        glm::vec4 camera_time; // Camera X/Z modulo world size, Y height, animation seconds.
        glm::vec4 sky;
        glm::vec4 flags;  // depth, waves, SSR, camera underwater
        glm::vec4 sizes;  // full width/height, reflection width/height
        glm::vec4 extras; // unused, shoreline foam, enabled, unused
        glm::vec4 trace;  // maximum distance, reserved
    };
    Renderer& renderer_;
    bool ice_{};
    VkDescriptorSetLayout scene_layout_{};
    VkDescriptorPool pool_{};
    VkPipelineLayout layout_{}, lod_layout_{};
    VkPipeline surface_{}, reflection_{}, lod_surface_{}, lod_reflection_{};
    VkSampler nearest_{}, linear_{};
    VkExtent2D extent_{}, reflection_extent_{};
    std::array<Frame, Renderer::frames_in_flight> frames_{};
    bool needs_reflection_{};
    Image create_image(VkFormat format, VkExtent2D size, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                       uint32_t levels = 1);
    void release_images() noexcept;
    void shutdown() noexcept;
    void ensure_images();
    void create_pipeline(bool reflection, bool lod = false);
};
} // namespace sandbox
