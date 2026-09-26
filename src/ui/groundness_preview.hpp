#pragma once
#include "world/generation_config.hpp"
#include <memory>

namespace sandbox {
class Renderer;
class GroundnessPreview {
  public:
    explicit GroundnessPreview(Renderer& renderer);
    ~GroundnessPreview();
    GroundnessPreview(const GroundnessPreview&) = delete;
    GroundnessPreview& operator=(const GroundnessPreview&) = delete;
    void draw(GenerationConfig& draft, bool& open);
    void prepare(); // Active frame, before rendering. Polls without waiting and uploads completed pixels.
    void release_binding(); // After GPU idle, before reinitializing the ImGui Vulkan backend.

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sandbox
