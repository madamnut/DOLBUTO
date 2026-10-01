#pragma once
#include "render/draw_stats.hpp"
#include "render/scene_effects.hpp"
#include "world/lod.hpp"
#include <functional>
#include <map>
#include <span>

namespace sandbox {
// Emit near chunks up to the next tile's squared camera distance. Return true when
// pipeline/descriptors changed. The final +/-infinity cutoff drains the near list.
struct TerrainDrawMerge {
    std::function<bool(double)> before;
    std::function<void()> bind_lod;
    bool far_to_near{};
    std::function<void(double)> check_distance; // Explicit --profile-near-detail only.
};
class LodRenderer {
  public:
    LodRenderer(Renderer& renderer, SceneEffects& effects, const std::array<glm::vec4, 11>& palette);
    ~LodRenderer();
    // published_changed covers membership changes; centre/debug changes are handled independently.
    void prepare(std::shared_ptr<const LodScene> scene, ColumnKey centre,
                 std::span<const ColumnKey> published, bool published_changed, bool lod_debug);
    void sort_draws(glm::dvec3 camera);
    void draw(const glm::mat4& matrix, glm::dvec3 camera, int radius, bool lod_debug, int shadow_layer = -1,
              int shadow_distance = 192, const TerrainDrawMerge& merge = {});
    // Uses the current WaterEffects pass; colour/depth snapshots are shared with near water.
    void draw_water(const glm::mat4& matrix, glm::dvec3 camera, int radius,
                    VkPipelineLayout water_layout = VK_NULL_HANDLE, const TerrainDrawMerge& merge = {});
    void draw_water_depth(const glm::mat4& matrix, glm::dvec3 camera, int radius,
                          const TerrainDrawMerge& merge = {});
    VkDescriptorSetLayout coverage_layout() const { return coverage_layout_; }
    bool water_visible() const { return water_visible_; }
    void clear();
    size_t gpu_bytes() const { return *allocated_; }
    size_t queued() const { return pending_ ? pending_->meshes.size() - cursor_ : 0; }
    size_t tiles{}, triangles{};
    bool profile_draws{};
    DrawStats draw_stats;
    bool caught_up(const std::shared_ptr<const LodScene>& scene) const {
        return scene && active_ && !pending_ && accepted_ == scene->revision;
    }

  private:
    struct Coverage {
        std::array<int32_t, 4> centre{};
        std::array<glm::vec4, 11> palette;
        std::array<uint32_t, 129 * 129> mask{};
    };
    static_assert(offsetof(Coverage, palette) == 16 && offsetof(Coverage, mask) == 192);
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
    struct DrawMesh {
        LodKey key;
        const Mesh* mesh;
        double distance{};
    };
    Renderer& renderer_;
    SceneEffects& effects_;
    std::array<glm::vec4, 11> palette_;
    ColumnKey coverage_centre_{};
    Coverage coverage_data_{};
    bool coverage_valid_{};
    std::array<Buffer, Renderer::frames_in_flight> coverage_{};
    std::array<VkDescriptorSet, Renderer::frames_in_flight> sets_{};
    VkDescriptorSetLayout coverage_layout_{};
    VkDescriptorPool pool_{};
    VkPipelineLayout layout_{};
    VkPipeline solid_{}, debug_{}, shadow_{}, water_{}, water_depth_{};
    bool water_visible_{};
    std::map<Key, Mesh> meshes_;
    // Stable map nodes, retained by active_; rebuilt before collect can erase old nodes.
    std::vector<DrawMesh> draw_meshes_;
    double sort_cpu_ms_{};
    std::shared_ptr<const LodScene> active_, pending_;
    uint64_t accepted_{};
    size_t cursor_{};
    std::shared_ptr<size_t> allocated_{std::make_shared<size_t>(0)};
    VkPipeline pipeline(bool debug, bool shadow, bool water = false, bool depth_only = false);
    double record(const glm::mat4& matrix, glm::dvec3 camera, int radius, bool lod_debug, int shadow_layer,
                  int shadow_distance, VkPipelineLayout layout, bool water_only,
                  const TerrainDrawMerge& merge);
    void collect();
    void rebuild_draw_meshes();
    void shutdown();
};
} // namespace sandbox
