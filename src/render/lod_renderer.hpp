#pragma once
#include "render/scene_effects.hpp"
#include "world/lod.hpp"
#include <map>
#include <span>

namespace sandbox {
class LodRenderer {
  public:
    LodRenderer(Renderer& renderer, SceneEffects& effects, const std::array<glm::vec4, 11>& palette);
    ~LodRenderer();
    void prepare(std::shared_ptr<const LodScene> scene, ColumnKey centre,
                 std::span<const ColumnKey> published, bool lod_debug);
    void draw(const glm::mat4& matrix, glm::dvec3 camera, int radius, bool lod_debug, int shadow_layer = -1,
              int shadow_distance = 192);
    // Uses the current WaterEffects pass; colour/depth snapshots are shared with near water.
    void draw_water(const glm::mat4& matrix, glm::dvec3 camera, int radius,
                    VkPipelineLayout water_layout = VK_NULL_HANDLE);
    void draw_water_depth(const glm::mat4& matrix, glm::dvec3 camera, int radius);
    VkDescriptorSetLayout coverage_layout() const { return coverage_layout_; }
    bool water_visible() const { return water_visible_; }
    void clear();
    size_t gpu_bytes() const { return *allocated_; }
    size_t queued() const { return pending_ ? pending_->meshes.size() - cursor_ : 0; }
    size_t tiles{}, triangles{};

  private:
    struct Key {
        LodKey tile;
        uint64_t revision;
        auto operator<=>(const Key&) const = default;
    };
    struct Part {
        Buffer buffer;
        uint32_t faces{}, solid_faces{};
    };
    struct Mesh {
        std::vector<Part> parts;
        uint32_t faces{};
    };
    Renderer& renderer_;
    SceneEffects& effects_;
    std::array<glm::vec4, 11> palette_;
    ColumnKey coverage_centre_;
    std::array<uint32_t, 129 * 129> published_{};
    std::array<Buffer, Renderer::frames_in_flight> coverage_{};
    std::array<VkDescriptorSet, Renderer::frames_in_flight> sets_{};
    VkDescriptorSetLayout coverage_layout_{};
    VkDescriptorPool pool_{};
    VkPipelineLayout layout_{};
    VkPipeline solid_{}, debug_{}, shadow_{}, water_{}, water_depth_{};
    bool water_visible_{};
    std::map<Key, Mesh> meshes_;
    std::shared_ptr<const LodScene> active_, pending_;
    uint64_t accepted_{};
    size_t cursor_{};
    std::shared_ptr<size_t> allocated_{std::make_shared<size_t>(0)};
    VkPipeline pipeline(bool debug, bool shadow, bool water = false, bool depth_only = false);
    void record(const glm::mat4& matrix, glm::dvec3 camera, int radius, bool lod_debug, int shadow_layer,
                int shadow_distance, VkPipelineLayout layout, bool water_only);
    void collect();
    void shutdown();
};
} // namespace sandbox
