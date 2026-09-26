#pragma once
#include "ui/groundness_preview.hpp"
#include "ui/terrain_spline_editor.hpp"
#include "world/generation_config.hpp"
#include <optional>
#include <utility>

namespace sandbox {
class GenerationEditor {
  public:
    GenerationEditor(Renderer& renderer, GenerationConfig config)
        : draft_(std::move(config)), preview_(renderer) {}
    void prepare_preview() { preview_.prepare(); }
    void release_preview_binding() { preview_.release_binding(); }
    void draw(const GenerationConfig& active);
    std::optional<GenerationConfig> take_regeneration() { return std::exchange(regeneration_, {}); }
    std::optional<GenerationConfig> take_save() { return std::exchange(saving_, {}); }
    void status(std::string message) { status_ = std::move(message); }
    void saved(const GenerationConfig& config);
    bool take_load() { return std::exchange(loading_, false); }
    void load_draft(GenerationConfig config) {
        draft_ = std::move(config);
        saved_text_.clear();
        status_ = "확정 파일을 초안에 불러왔습니다. 재생성을 눌러야 현재 월드에 적용됩니다.";
    }

  private:
    GenerationConfig draft_;
    std::optional<GenerationConfig> regeneration_, saving_;
    std::string status_, saved_text_;
    bool loading_{};
    bool preview_open_{true};
    GroundnessPreview preview_;
    TerrainSplineEditor spline_editor_;
};
} // namespace sandbox
