#include "ui/generation_editor.hpp"
#include "core/world_rules.hpp"
#include "ui/noise_controls.hpp"
#include "world/periodic_noise.hpp"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <limits>
#include <stdexcept>

namespace sandbox {
namespace {
void parameter_help(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
} // namespace
void warp_controls(GenerationConfig& config) {
    auto& w = config.warps.front();
    ImGui::TextWrapped("공유 Shift: 같은 주기적 3D Double Perlin의 (X,0,Z)와 (Z,X,0) 단면입니다.");
    ImGui::Checkbox("Shift 사용", &w.enabled);
    ImGui::DragFloat("변위 강도 (블록)", &w.strength, .1f, 0, 8192, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    parameter_help(
        "원본 Shift 배율4를 월드 좌표로 환산한 초기값16입니다. 모든 옥타브가 같은 변위를 공유합니다.");
    noise_controls("shared-shift", w.noise, true);
}

void temperature_controls(TemperatureSettings& b) {
    const auto previous = b;
    ImGui::PushID("temperature-bands");
    ImGui::DragFloat("적도 온도", &b.equator, 0.01f, b.poles, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    parameter_help("Z=65536에서의 온도 지수입니다. 섭씨가 아닌 -1~+1 값이며 지형에는 영향을 주지 않습니다.");
    ImGui::DragFloat("양 끝 온도", &b.poles, 0.01f, -1, b.equator, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    parameter_help("Z=0과131072의 온도입니다. 두 끝을 같게 해서 남북 순환 경계도 연결합니다.");
    ImGui::DragFloat("따뜻한 띠 집중도", &b.latitude_power, 0.02f, 1, 8, "%.3f",
                     ImGuiSliderFlags_AlwaysClamp);
    parameter_help("클수록 따뜻한 영역이 적도 근처에 좁게 모입니다. 위도 기본 분포는 부드러운 곡선입니다.");
    ImGui::DragFloat("지역 온도 변화", &b.variation, 0.01f, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    parameter_help(
        "위도 분포에 더하는 노이즈의 강도입니다. 적도와 양 끝에서는 영향이0이며 X방향으로 반복됩니다.");
    if (!std::isfinite(b.equator) || !std::isfinite(b.poles) || !std::isfinite(b.latitude_power) ||
        !std::isfinite(b.variation))
        b = previous;
    // Equal ImGui drag bounds disable clamping; keep a flat temperature range valid too.
    b.poles = std::clamp(b.poles, -1.0f, 1.0f);
    b.equator = std::clamp(b.equator, b.poles, 1.0f);
    ImGui::PopID();
}
void noise_controls(const char* id, NoiseSettings& n, bool individual, bool periodic_z, int maximum_octaves,
                    const GenerationConfig* config) {
    const auto previous = n;
    if (config) {
        ImGui::PushID(id);
        std::string selected = "없음";
        for (const auto& w : config->warps)
            if (w.id == n.warp)
                selected = w.name;
        if (ImGui::BeginCombo("적용 워핑", selected.c_str())) {
            if (ImGui::Selectable("없음", n.warp.empty()))
                n.warp.clear();
            for (const auto& w : config->warps) {
                ImGui::PushID(w.id.c_str());
                if (ImGui::Selectable(w.name.c_str(), n.warp == w.id))
                    n.warp = w.id;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("주기적 Double Perlin · %s", periodic_z ? "X/Z 순환" : "X 순환 / Z 직선");
        ImGui::PopID();
    }
    if (!individual) {
        bool explicit_weights = !n.weights.empty();
        ImGui::PushID(id);
        if (ImGui::Checkbox("옥타브별 가중치", &explicit_weights)) {
            if (explicit_weights)
                n.weights = octave_weights(n);
            else
                n.weights.clear();
        }
        parameter_help(
            "켜면 각 겹의 가중치를 직접 정합니다. 끄면 현재 게인으로 모든 가중치를 다시 계산합니다.");
        ImGui::PopID();
        individual = explicit_weights;
    }
    if (individual && n.weights.empty())
        n.weights = octave_weights(n);
    ImGui::PushID(id);
    ImGui::SliderInt("가로 격자 지수###XZ spacing exponent", &n.spacing_log2, 2, 17, "%d",
                     ImGuiSliderFlags_AlwaysClamp);
    parameter_help("간격은 2의 지수승 블록입니다. 다음 옥타브마다 간격이 절반으로 줄어듭니다.");
    ImGui::TextWrapped("격자 간격: %d블록 · %s 주기: %d블록", 1 << n.spacing_log2, periodic_z ? "X/Z" : "X",
                       world_size);
    const int limit = maximum_octaves;
    n.octaves = std::min(n.octaves, limit);
    ImGui::SliderInt("옥타브 수###Octaves", &n.octaves, 1, limit, "%d", ImGuiSliderFlags_AlwaysClamp);
    parameter_help("많을수록 계산량이 늘어납니다. 최대16겹이며 원본처럼 작은 간격도 허용합니다.");
    if (individual) {
        n.weights.resize(n.octaves, 0.0f);
        double total = 0;
        for (int i = 0; i < n.octaves; ++i) {
            ImGui::PushID(i);
            const std::string label = "옥타브 " + std::to_string(i + 1) + " 가중치###weight";
            const float previous = n.weights[i];
            ImGui::DragFloat(label.c_str(), &n.weights[i], 0.01f, 0, std::numeric_limits<float>::max(),
                             "%.6g", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
            if (!std::isfinite(n.weights[i]) || n.weights[i] < 0)
                n.weights[i] = previous;
            parameter_help("각 옥타브의 기여도입니다. 0이면 합성에서 제외하며 음수는 사용할 수 없습니다. "
                           "원본 옥타브별 1/2 감쇄와 Double Perlin 보정이 별도로 적용됩니다. 새 옥타브는 "
                           "0으로 추가됩니다.");
            const double requested = std::ldexp(1.0, n.spacing_log2 - i) / n.frequency_multiplier;
            ImGui::Text("요청 간격: %.6g블록", requested);
            ImGui::Text("실제 간격: %.6g / %.6g", effective_noise_spacing(requested, 0),
                        effective_noise_spacing(requested, 1));
            total += n.weights[i];
            ImGui::PopID();
        }
        if (total == 0)
            ImGui::TextWrapped("모든 가중치가 0이므로 합성 노이즈는 0입니다.");
    } else {
        ImGui::SliderFloat("게인###Gain", &n.gain, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        parameter_help(
            "다음 옥타브의 가중치를 이전 겹의 몇 배로 할지 정합니다. 0.5이면 절반, 1이면 같은 비중입니다. "
            "클수록 작은 굴곡의 비중이 커집니다. 원본의 옥타브별 1/2 감쇄도 추가 적용됩니다.");
    }
    ImGui::DragFloat("입력 주파수 배율", &n.frequency_multiplier, .01f, .001f, 1500, "%.6g",
                     ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextWrapped("각 묶음의 실제 간격은 월드 크기 ÷ 정수 격자 수로 조정됩니다.");
    ImGui::TextUnformatted("옥타브 주파수 배율: 2배 (고정)");
    if (!std::isfinite(n.gain))
        n = previous;
    ImGui::PopID();
}
void GenerationEditor::draw(const GenerationConfig& active) {
    ImGui::SetNextWindowPos(ImVec2(450, 24), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(480, std::min(820.0f, ImGui::GetIO().DisplaySize.y - 48)),
                             ImGuiCond_FirstUseEver);
    if (ImGui::Begin("월드·기후 설정 (F8)###World generation (F8)")) {
        const auto previous_draft = draft_;
        ImGui::TextUnformatted(draft_ == active ? "현재 월드와 같은 설정입니다."
                                                : "편집 중인 설정입니다. 재생성을 눌러 적용하세요.");
        ImGui::TextDisabled("각 항목에 마우스를 올리면 설명이 표시됩니다.");
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
        ImGui::InputScalar("마스터 시드###World seed", ImGuiDataType_U32, &draft_.seed);
        parameter_help("기후 노이즈가 사용하는 기준 번호입니다. 임시 평지 높이는 바뀌지 않습니다. "
                       "시드를 바꾸면 전체 패턴이 달라집니다. 범위는 0~4294967295입니다.");
        if (ImGui::CollapsingHeader("도메인 워핑 목록"))
            warp_controls(draft_);
        ImGui::TextWrapped("임시 돌 평지: Y=0~191 돌, Y=192~511 공기. 자연 생성 물은 없습니다.");
        if (ImGui::CollapsingHeader("Temperature · 온도 (바이옴 준비)")) {
            noise_controls("temperature", draft_.temperature, false, false, 16, &draft_);
            temperature_controls(draft_.temperature_bands);
            ImGui::TextWrapped("원본 온도 노이즈는 X방향만 반복합니다. 남북 위도 곡선과 결합하며 지형 "
                               "높이·밀도에는 사용하지 않습니다.");
        }
        if (ImGui::CollapsingHeader("Precipitation · 강수량 (바이옴 준비)")) {
            noise_controls("precipitation", draft_.precipitation, false, true, 16, &draft_);
            ImGui::TextWrapped("X/Z 모두 반복하는 독립 신호입니다. 낮으면 건조, 높으면 습윤하며 현재 지형과 "
                               "물 배치에는 사용하지 않습니다.");
        }
        ImGui::PopItemWidth();
        try {
            validate(draft_);
        } catch (const std::exception& error) {
            draft_ = previous_draft;
            status_ = std::string("입력값을 확인해 주세요: ") + error.what();
        }
        if (ImGui::Button("기후 미리보기 창 열기###open-preview"))
            preview_open_ = true;
        parameter_help("별도 창에서 기후 지도, 시드, 범위와 해상도를 조정합니다. 편집값은 두 창에서 "
                       "공유합니다.");
        ImGui::Separator();
        ImGui::TextWrapped("재생성하면 블록 편집이 모두 초기화됩니다. 위치와 시점은 유지됩니다. "
                           "기본값 저장은 다음 실행에 적용되며, 현재 지형은 재생성을 눌러야 바뀝니다.");
        if (ImGui::Button("재생성###Regenerate"))
            regeneration_ = draft_;
        parameter_help("편집한 설정으로 현재 월드를 새로 만듭니다. 파괴하거나 놓은 블록은 모두 초기화되며 "
                       "플레이어 위치와 카메라 각도는 유지됩니다. 기본값 파일을 저장하지는 않습니다.");
        ImGui::SameLine();
        if (ImGui::Button("기본값 저장###Save default"))
            saving_ = draft_;
        parameter_help("편집 중인 설정을 실행 파일 옆 worldgen.json에 저장합니다. 다음 실행의 기본값이 되며 "
                       "현재 월드는 자동으로 재생성하지 않습니다. 블록 편집 데이터는 저장하지 않습니다.");
        ImGui::SameLine();
        if (ImGui::Button("편집 되돌리기###Revert draft")) {
            draft_ = active;
        }
        parameter_help("편집 중인 값을 현재 월드에 적용된 설정으로 되돌립니다. 현재 월드와 저장된 파일은 "
                       "바꾸지 않습니다.");
        if (ImGui::Button("확정 파일 불러오기###Load published"))
            loading_ = true;
        parameter_help("웹 편집기가 확정 저장한 worldgen.json을 초안에 불러옵니다. 현재 편집 초안은 "
                       "교체되며, 월드는 재생성을 눌러야 바뀝니다.");
        if (!status_.empty())
            ImGui::TextWrapped("%s", status_.c_str());
        if (!saved_text_.empty() &&
            ImGui::CollapsingHeader("저장한 설정 (JSON)###Saved JSON", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("JSON 복사###Copy JSON"))
                ImGui::SetClipboardText(saved_text_.c_str());
            parameter_help("마지막으로 저장한 설정 텍스트를 클립보드로 복사합니다. 이후 아직 저장하지 않은 "
                           "편집은 포함하지 않습니다.");
            ImGui::InputTextMultiline("##saved-json", saved_text_.data(), saved_text_.size() + 1,
                                      ImVec2(-1, 170), ImGuiInputTextFlags_ReadOnly);
        }
    }
    ImGui::End();
    if (preview_open_)
        preview_.draw(draft_, preview_open_);
}
void GenerationEditor::saved(const GenerationConfig& config) {
    saved_text_ = generation_json(config);
    status_ = "실행 파일 옆 worldgen.json에 저장했습니다. 다음 실행부터 기본값으로 사용합니다.";
}
} // namespace sandbox
