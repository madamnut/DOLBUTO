#pragma once
#include "world/generation_config.hpp"
#include <algorithm>
#include <memory>
#include <span>

namespace sandbox {
enum class GenerationMap { temperature, precipitation };
// Immutable graph, shared across all chunk workers; outputs/caller-owned arrays are never shared.
class TerrainGenerator {
  public:
    explicit TerrainGenerator(GenerationConfig config);
    ~TerrainGenerator();
    TerrainGenerator(const TerrainGenerator&) = delete;
    TerrainGenerator& operator=(const TerrainGenerator&) = delete;
    const GenerationConfig& config() const { return config_; }
    uint64_t signature() const { return signature_; }
    // Climate only. The temporary flat terrain never samples noise.
    void map(GenerationMap map, std::span<float> out, std::span<const float> x,
             std::span<const float> z) const;

  private:
    GenerationConfig config_;
    uint64_t signature_{};
    struct Nodes;
    std::unique_ptr<Nodes> nodes_;
    void sample_signals(std::span<const int> kinds, std::span<std::span<float>> outputs,
                        std::span<const float> x, std::span<const float> z) const;
};
} // namespace sandbox
