#pragma once
#include "core/world_clock.hpp"
#include "render/lod_renderer.hpp"
#include "render/renderer.hpp"
#include "render/scene_effects.hpp"
#include "render/water_effects.hpp"
#include "world/camera.hpp"
#include "world/edit.hpp"
#include "world/fluid.hpp"
#include "world/lighting.hpp"
#include "world/player.hpp"
#include "world/stream.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

namespace sandbox {
class WorldView {
  public:
    WorldView(Renderer& renderer, const GenerationConfig& config, int radius,
              std::optional<ColumnKey> diagnostic_origin = std::nullopt);
    void regenerate(const GenerationConfig& config); // Before begin_frame; cancels all old jobs/edits.
    const GenerationConfig& config() const { return generator_->config(); }
    const TerrainGenerator& generator() const { return *generator_; }
    ~WorldView();
    WorldView(const WorldView&) = delete;
    WorldView& operator=(const WorldView&) = delete;
    FlyCamera camera;
    void set_zoom(bool enabled);
    const Player& player() const { return player_; }
    void move_player(double seconds, const bool* keys);
    void press_space(uint64_t timestamp_ns);
    void release_space() { suppress_space_action_ = false; }
    void reset_movement_input();
    void set_movement_mode(MovementMode mode);
    bool teleport(glm::dvec3 position);
    bool set_time(unsigned hour, unsigned minute);
    void sync_camera();
    // Explicit CLI profiling setup; fixed initial view, no synthesized gameplay input.
    void set_profile_view(double height, double yaw, double pitch, double hour);
    void capture_shadow_maps(const std::filesystem::path& directory) {
        scene_effects_->capture_shadow_maps(directory);
    }
    void cycle_camera();
    void prepare(); // Record uploads outside rendering, after Renderer::begin_frame.
    void render();  // Begin world rendering, then switch to the depth-free UI pass.
    void enable_draw_profile(bool near_detail = false) {
        profile_draws_ = lod_renderer_->profile_draws = true;
        profile_near_detail_ = near_detail;
    }
    const DrawStats& near_draw_stats() const { return near_draw_stats_; }
    const NearDrawDetail& near_draw_detail() const { return near_draw_detail_; }
    const DrawStats& lod_draw_stats() const { return lod_renderer_->draw_stats; }
    bool draw_profile_ready() const {
        if (!graphics_settings_.lod)
            return true;
        const auto stats = lod_cache_->stats();
        return !stats.pending && !stats.paused && !stats.memory_limited &&
               lod_renderer_->caught_up(lod_cache_->scene());
    }
    size_t visible_columns() const { return visible_columns_; }
    size_t pending_columns() const { return stream_->pending(); }
    LodStats lod_stats() const { return lod_cache_->stats(); }
    size_t lod_gpu_bytes() const { return lod_renderer_->gpu_bytes(); }
    size_t lod_upload_queue() const { return lod_renderer_->queued(); }
    size_t lod_tiles() const { return lod_renderer_->tiles; }
    uint32_t seed() const { return config().seed; }
    int radius() const { return radius_; }
    void set_radius(int radius);
    void set_view_bobbing(bool enabled);
    void set_water_settings(WaterSettings settings) { water_settings_ = settings; }
    void set_graphics_settings(GraphicsSettings settings) { graphics_settings_ = settings; }
    void toggle_lod_debug() {
        lod_debug_ = !lod_debug_;
        scene_effects_->invalidate_history();
    }
    bool lod_debug() const { return lod_debug_; }
    void update_target();
    bool interact(bool place);
    void select_slot(int slot) {
        if (slot >= 0 && slot < static_cast<int>(hotbar_blocks.size())) {
            selected_slot_ = slot;
            wheel_remainder_ = 0;
        }
    }
    Block selected_block() const { return hotbar_blocks[selected_slot_]; }
    int selected_slot() const { return selected_slot_; }
    void scroll_hotbar(float steps);
    uint32_t day_tick() const { return static_cast<uint32_t>(clock_.day_ticks()); }
    uint64_t world_tick() const { return clock_.ticks(); }
    WorldDate date() const { return clock_.date(); }
    bool is_day() const { return day_tick() >= 6 * ticks_per_hour && day_tick() < 20 * ticks_per_hour; }
    float daylight() const;
    size_t pending_lighting() const {
        return light_dirty_.size() + (light_job_ || light_update_job_ ? 1 : 0) +
               (!light_changes_.empty() ? 1 : 0);
    }
    const std::optional<BlockHit>& target() const { return target_; }
    size_t edit_count() const { return edits_.count(); }
    void advance_fluids(double seconds);
    uint64_t mesh_bytes() const { return mesh_bytes_; }
    uint32_t drawn_chunks{}, triangles{};
    uint64_t uploaded_bytes{};
    double generation_ms{}, lighting_ms{}, meshing_ms{}, upload_cpu_ms{};

  private:
    bool profile_draws_{};
    bool profile_near_detail_{};
    uint32_t near_detail_frame_{};
    DrawStats near_draw_stats_;
    NearDrawDetail near_draw_detail_;
    struct FacePool {
        VkDescriptorPool handle{};
        uint32_t available{1024};
    };
    struct GpuChunk {
        Buffer faces{}, lights{};
        std::shared_ptr<const std::vector<PackedFace>> geometry;
        std::shared_ptr<const std::vector<uint32_t>> light_values;
        VkDescriptorSet descriptor{};
        std::shared_ptr<FacePool> pool;
        uint32_t count{};
        uint32_t solid_count{};
        uint32_t ice_count{};
    };
    struct Resident {
        Column data;
        std::array<GpuChunk, chunks_per_column> meshes;
        // Updated with resident geometry; lighting-only replacements preserve mesh counts.
        uint32_t nonempty_meshes{};
        bool published{};
    };
    Renderer& renderer_;
    int radius_;
    std::shared_ptr<const TerrainGenerator> generator_;
    std::shared_ptr<LodCache> lod_cache_;
    std::unique_ptr<LodRenderer> lod_renderer_;
    std::unique_ptr<WorldStream> stream_;
    WorldEdits edits_;
    FluidSimulation fluids_;
    double fluid_accumulator_{};
    std::unique_ptr<LightWorker> light_worker_{std::make_unique<LightWorker>()};
    std::unique_ptr<MeshLightWorker> mesh_light_worker_{std::make_unique<MeshLightWorker>()};
    uint64_t mesh_ticket_{}, mesh_bytes_{};
    std::map<ChunkKey, uint64_t> packing_;
    std::deque<PackedMesh> packed_ready_;
    void queue_mesh(ChunkKey key, ChunkMesh source, const LightChunk& light, bool urgent);
    void accept_meshes(std::chrono::steady_clock::time_point start);
    std::unordered_map<ColumnKey, std::shared_ptr<const ColumnLight>, ColumnHash> lights_;
    std::set<ColumnKey, bool (*)(const ColumnKey&, const ColumnKey&)> light_dirty_{
        [](const ColumnKey& a, const ColumnKey& b) { return a.x < b.x || (a.x == b.x && a.z < b.z); }};
    std::set<ChunkKey> light_uploads_;
    std::optional<ColumnKey> light_job_;
    uint64_t light_revision_{};
    bool light_update_job_{};
    std::vector<LightChange> light_changes_;
    WorldClock clock_;
    bool suppress_time_action_{};
    float wheel_remainder_{};
    void prepare_lighting();
    void relight(GpuChunk& mesh, const PackedMesh& prepared);
    int selected_slot_{};
    std::optional<BlockHit> target_;
    std::set<ChunkKey> dirty_chunks_;
    std::deque<ChunkKey> urgent_chunks_;
    std::optional<ColumnKey> centre_;
    std::unordered_map<ColumnKey, Resident, ColumnHash> columns_;
    size_t visible_columns_{};
    std::unique_ptr<BuiltColumn> incoming_;
    std::array<GpuChunk, chunks_per_column> uploading_{};
    std::bitset<chunks_per_column> uploaded_chunks_;
    std::bitset<chunks_per_column> remesh_chunks_;
    Texture atlas_{};
    VkPipelineLayout layout_{};
    VkPipeline pipeline_{};
    VkPipeline water_pipeline_{};
    WaterSettings water_settings_;
    std::unique_ptr<WaterEffects> water_effects_;
    std::unique_ptr<WaterEffects> ice_effects_;
    double visual_time_{};
    GraphicsSettings graphics_settings_;
    std::unique_ptr<SceneEffects> scene_effects_;
    VkPipeline lod_debug_pipeline_{};
    VkPipeline player_pipeline_{};
    Player player_;
    glm::dvec3 previous_player_position_{}, rendered_player_position_{};
    double movement_accumulator_{};
    bool view_bobbing_{true};
    double walk_phase_{}, previous_walk_phase_{}, walk_bob_{}, previous_walk_bob_{};
    void reset_walk_bob();
    std::optional<uint64_t> last_space_press_;
    bool jump_requested_{}, suppress_space_action_{};
    bool lod_debug_{};
    VkDescriptorSetLayout face_layout_{};
    std::vector<std::shared_ptr<FacePool>> face_pools_;
    VkImage depth_{};
    VmaAllocation depth_allocation_{};
    VkImageView depth_view_{};
    VkExtent2D depth_extent_{};
    void create_pipeline();
    void describe(GpuChunk& mesh);
    std::optional<Block> loaded_block(BlockPos position) const;
    glm::dvec3 player_eye() const;
    double camera_distance(glm::dvec3 origin, glm::dvec3 direction,
                           double maximum = FlyCamera::third_person_distance) const;
    bool apply_edit(BlockPos position, Block block);
    std::optional<FluidCell> loaded_cell(BlockPos position) const;
    void tick_fluids();
    void dirty_cell(BlockPos position);
    void retire(GpuChunk& mesh);
    void upload(const PackedMesh& source, GpuChunk& destination);
    void ensure_depth();
    void retire(std::array<GpuChunk, chunks_per_column>& meshes);
    void publish_columns();
    void shutdown() noexcept;
};
} // namespace sandbox
