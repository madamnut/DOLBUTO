#pragma once
#include "world/generation_config.hpp"
#include <memory>

namespace sandbox {
class Renderer;
class ClimatePreview {
  public:
    explicit ClimatePreview(Renderer& renderer);
    ~ClimatePreview();
    ClimatePreview(const ClimatePreview&) = delete;
    ClimatePreview& operator=(const ClimatePreview&) = delete;
    void draw(GenerationConfig& draft, bool& open);
    void prepare(); // Active frame, before rendering. Polls without waiting and uploads completed pixels.
    void release_binding(); // After GPU idle, before reinitializing the ImGui Vulkan backend.

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sandbox
