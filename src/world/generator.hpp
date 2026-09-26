#pragma once
#include "world/generation_config.hpp"
#include <algorithm>
#include <memory>
#include <span>

namespace sandbox {
enum class GenerationMap {
    groundness,
    smoothness,
    weirdness,
    pv,
    base_height,
    temperature,
    precipitation,
    offset,
    factor,
    jaggedness,
    jagged_noise,
    effective_height
};
// Immutable graph, shared across all chunk workers; outputs/caller-owned arrays are never shared.
class TerrainGenerator {
  public:
    explicit TerrainGenerator(GenerationConfig config);
    ~TerrainGenerator();
    TerrainGenerator(const TerrainGenerator&) = delete;
    TerrainGenerator& operator=(const TerrainGenerator&) = delete;
    const GenerationConfig& config() const { return config_; }
    uint64_t signature() const { return signature_; }
    void groundness(std::span<float> out, std::span<const float> x, std::span<const float> z) const;
    // Share the common Shift displacement across this batch.
    void terrain_inputs(std::span<float> ground, std::span<float> smooth, std::span<float> weird,
                        std::span<const float> x, std::span<const float> z) const;
    void map(GenerationMap map, std::span<float> out, std::span<const float> x,
             std::span<const float> z) const;
    void profile(std::span<float> heights, std::span<float> squash, std::span<const float> x,
                 std::span<const float> z) const;
    void shape(std::span<float> out, std::span<const float> x, std::span<const float> y,
               std::span<const float> z) const;
    // Conservative analytic bound for early uniform chunks, not a clamp on noise output.
    bool shape_active() const { return config_.shape_enabled && config_.amplitude > 0; }
    float vertical_extent_bound(float local_squash) const {
        return shape_active() ? 2.0f * config_.amplitude / std::max(local_squash, 0.000001f) : 0.0f;
    }
    float density(float noise, float height, float squash, float y) const;

  private:
    GenerationConfig config_;
    uint64_t signature_{};
    struct Nodes;
    std::unique_ptr<Nodes> nodes_;
    void sample_signals(std::span<const int> kinds, std::span<std::span<float>> outputs,
                        std::span<const float> x, std::span<const float> z) const;
    void spline_profile(std::span<float> heights, std::span<float> squash, std::span<const float> x,
                        std::span<const float> z, bool jagged) const;
};
} // namespace sandbox
