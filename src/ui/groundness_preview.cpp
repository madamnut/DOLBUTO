#include "ui/groundness_preview.hpp"
#include "core/world_rules.hpp"
#include "render/renderer.hpp"
#include "ui/noise_controls.hpp"
#include "world/generator.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sandbox {
namespace {
constexpr const char* map_names[] = {
    "Groundness · 대륙성",      "Smoothness · 완만함",    "Weirdness",
    "PV · 봉우리/계곡",         "기준 높이 (3D 적용 전)", "Temperature · 온도",
    "Precipitation · 강수량",   "Offset · 높이 오프셋",   "Factor · 최종 압축",
    "Jaggedness · 잔굴곡 강도", "잔굴곡 원본 노이즈",     "잔굴곡 적용 높이 (3D 적용 전)"};
const char* legend(GenerationMap kind) {
    switch (kind) {
    case GenerationMap::temperature:
        return "파랑 -1 (추움) → 빨강 +1 (더움) · 섭씨가 아닌 온도 지수";
    case GenerationMap::precipitation:
        return "갈색 -1 (건조) → 파랑 +1 (습윤) · 실제 강수량 단위 아님";
    case GenerationMap::base_height:
    case GenerationMap::effective_height:
        return "파랑: 해수면192 아래 · 초록→흰색:192~512 · 실제3D 지표와 다를 수 있음";
    case GenerationMap::factor:
        return "검정0 → 흰색0.05 · 블록 높이당 압축 (factor × 전체 압축 ÷ 높이 배율)";
    case GenerationMap::jaggedness:
        return "검정0 → 흰색1 · 스플라인의 잔굴곡 강도 (켜짐 여부/배율 적용 전)";
    default:
        return "검정 -1 · 회색 0 · 흰색 +1 · 원신호는 범위 밖 값이 있을 수 있음";
    }
}
std::array<unsigned char, 3> map_colour(GenerationMap kind, float value) {
    float t = std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f);
    std::array<float, 3> a{0, 0, 0}, b{255, 255, 255};
    if (kind == GenerationMap::temperature) {
        a = {30, 80, 230};
        b = {240, 55, 25};
    }
    if (kind == GenerationMap::precipitation) {
        a = {165, 105, 45};
        b = {35, 120, 235};
    }
    if (kind == GenerationMap::factor)
        t = std::clamp(value / .05f, 0.0f, 1.0f);
    if (kind == GenerationMap::jaggedness)
        t = std::clamp(value, 0.0f, 1.0f);
    if (kind == GenerationMap::base_height || kind == GenerationMap::effective_height) {
        if (value < sea_level) {
            a = {10, 30, 95};
            b = {60, 155, 225};
            t = std::clamp(value / sea_level, 0.0f, 1.0f);
        } else {
            a = {65, 130, 55};
            b = {250, 250, 245};
            t = std::clamp((value - sea_level) / (world_height - sea_level), 0.0f, 1.0f);
        }
    }
    std::array<unsigned char, 3> colour;
    for (size_t i = 0; i < 3; ++i)
        colour[i] = static_cast<unsigned char>(std::lround(std::lerp(a[i], b[i], t)));
    return colour;
}
struct PreviewRange {
    int x0{}, z0{}, x1{16384}, z1{16384};
    bool valid() const {
        return x0 >= 0 && z0 >= 0 && x1 <= world_size && z1 <= world_size && x1 > x0 && z1 > z0;
    }
    bool operator==(const PreviewRange&) const = default;
};
struct Request {
    GenerationConfig config;
    PreviewRange range;
    int resolution{512};
    int octave{-1}; // -1 is the final composite; other indices are zero-based.
    bool weighted{};
    bool before_warp{};
    uint64_t revision{};
    GenerationMap kind{GenerationMap::groundness};
};
struct Result {
    Request request;
    int width{}, height{};
    std::vector<float> values;
    std::vector<unsigned char> pixels;
    float minimum{std::numeric_limits<float>::max()}, maximum{std::numeric_limits<float>::lowest()};
    double milliseconds{};
    std::string error;
};
void help(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
} // namespace
struct GroundnessPreview::Impl {
    Renderer& renderer;
    PreviewRange range;
    int resolution_index{1};
    int map_index{};
    int selection{}; // 0 is composite, 1..N select an octave.
    bool weighted{};
    bool before_warp{};
    float zoom{1.0f};
    static constexpr int resolutions[] = {256, 512, 1024, 2048};
    Texture texture{};
    VkDescriptorSet binding{};
    struct Retired {
        Texture texture;
        VkDescriptorSet binding;
        std::shared_ptr<bool> completed;
    };
    std::vector<Retired> retired;
    std::unique_ptr<Result> shown;
    std::string message;
    bool busy{};
    std::mutex mutex;
    std::condition_variable_any wake;
    std::optional<Request> pending;
    std::unique_ptr<Result> ready;
    std::atomic_uint64_t revision{};
    std::atomic_int progress{};
    std::mt19937 random{std::random_device{}()};
    std::jthread worker;

    explicit Impl(Renderer& r) : renderer(r), worker([this](std::stop_token stop) { run(stop); }) {}
    ~Impl() {
        worker.request_stop();
        wake.notify_all();
        worker.join();
        // Exit only. Replacing previews during play uses frame completion markers, never GPU idle.
        vkDeviceWaitIdle(renderer.device);
        release_binding();
        renderer.destroy_texture(texture);
        for (const auto& item : retired)
            renderer.destroy_texture(item.texture);
    }
    void release_binding() {
        if (binding)
            ImGui_ImplVulkan_RemoveTexture(std::exchange(binding, VK_NULL_HANDLE));
        for (auto& item : retired)
            if (item.binding)
                ImGui_ImplVulkan_RemoveTexture(std::exchange(item.binding, VK_NULL_HANDLE));
    }
    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            Request request;
            {
                std::unique_lock lock(mutex);
                if (!wake.wait(lock, stop, [this] { return pending.has_value(); }))
                    return;
                request = std::move(*pending);
                pending.reset();
            }
            auto result = std::make_unique<Result>();
            result->request = std::move(request);
            const auto id = result->request.revision;
            try {
                const auto start = std::chrono::steady_clock::now();
                const auto& r = result->request.range;
                const int dx = r.x1 - r.x0, dz = r.z1 - r.z0;
                // Preserve map proportions using the requested long-axis sample count.
                result->width = std::max(2, result->request.resolution * dx / std::max(dx, dz));
                result->height = std::max(2, result->request.resolution * dz / std::max(dx, dz));
                const int w = result->width, h = result->height;
                result->values.resize(size_t(w) * h);
                result->pixels.resize(size_t(w) * h * 4);
                std::vector<float> xs(w), zs(w);
                for (int x = 0; x < w; ++x)
                    xs[x] = float(wrap_position(r.x0 + double(dx) * x / (w - 1)));
                auto sample_config = result->request.config;
                if (result->request.before_warp)
                    disable_warps(sample_config);
                float contribution = 1;
                if (result->request.octave >= 0) {
                    const int octave = result->request.octave;
                    sample_config.groundness.preview_octave = octave;
                    sample_config.groundness.preview_weighted = result->request.weighted;
                }
                const TerrainGenerator generator(sample_config);
                for (int z = 0; z < h; ++z) {
                    if (stop.stop_requested() || revision.load() != id)
                        break;
                    std::fill(zs.begin(), zs.end(), float(wrap_position(r.z0 + double(dz) * z / (h - 1))));
                    auto row = std::span(result->values).subspan(size_t(z) * w, w);
                    if (contribution == 0)
                        std::fill(row.begin(), row.end(), 0.0f);
                    else
                        generator.map(result->request.kind, row, xs, zs);
                    for (int x = 0; x < w; ++x) {
                        row[x] *= contribution;
                        const float value = row[x];
                        if (!std::isfinite(value))
                            throw std::runtime_error("Non-finite map sample.");
                        result->minimum = std::min(result->minimum, value);
                        result->maximum = std::max(result->maximum, value);
                        const auto colour = map_colour(result->request.kind, value);
                        const size_t pixel = (size_t(z) * w + x) * 4;
                        for (size_t c = 0; c < 3; ++c)
                            result->pixels[pixel + c] = colour[c];
                        result->pixels[pixel + 3] = 255;
                    }
                    if (revision.load() == id)
                        progress.store((z + 1) * 100 / h);
                }
                result->milliseconds =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                        .count();
            } catch (const std::exception& error) {
                result->error = error.what();
            }
            if (stop.stop_requested())
                return;
            std::lock_guard lock(mutex);
            if (revision.load() == id)
                ready = std::move(result);
        }
    }
    void request(const GenerationConfig& draft) {
        if (!range.valid()) {
            message = "범위는 0~131072 안에서 시작보다 끝이 커야 합니다.";
            return;
        }
        validate(draft);
        Request next{draft, range, resolutions[resolution_index]};
        next.kind = static_cast<GenerationMap>(map_index);
        next.before_warp = before_warp;
        next.octave = next.kind == GenerationMap::groundness
                          ? std::clamp(selection, 0, draft.groundness.octaves) - 1
                          : -1;
        next.weighted = next.octave >= 0 && weighted;
        {
            std::lock_guard lock(mutex);
            next.revision = revision.fetch_add(1) + 1;
            pending = std::move(next);
            ready.reset();
            progress.store(0);
        }
        busy = true;
        message.clear();
        wake.notify_one();
    }
    void prepare() {
        std::erase_if(retired, [this](const Retired& item) {
            if (!*item.completed)
                return false;
            if (item.binding)
                ImGui_ImplVulkan_RemoveTexture(item.binding);
            renderer.destroy_texture(item.texture);
            return true;
        });
        if (texture.image && !binding)
            binding = ImGui_ImplVulkan_AddTexture(texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        std::unique_ptr<Result> result;
        {
            std::lock_guard lock(mutex);
            result = std::move(ready);
        }
        if (!result || result->request.revision != revision.load())
            return;
        busy = false;
        if (!result->error.empty()) {
            message = "미리보기 계산 실패: " + result->error;
            return;
        }
        Texture replacement{};
        VkDescriptorSet next_binding{};
        try {
            replacement =
                renderer.create_texture(result->pixels.data(), result->width, result->height, false, true);
            next_binding =
                ImGui_ImplVulkan_AddTexture(replacement.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (texture.image) {
                auto completed = std::make_shared<bool>(false);
                renderer.defer([completed] { *completed = true; });
                retired.push_back({texture, binding, completed});
            }
        } catch (const std::exception& error) {
            if (next_binding)
                ImGui_ImplVulkan_RemoveTexture(next_binding);
            // The replacement may already be referenced by an upload in this frame.
            if (replacement.image) {
                auto* r = &renderer;
                renderer.defer([r, replacement] { r->destroy_texture(replacement); });
            }
            message = "미리보기 이미지 준비 실패: " + std::string(error.what());
            return;
        }
        texture = replacement;
        binding = next_binding;
        std::vector<unsigned char>().swap(result->pixels);
        shown = std::move(result);
        message = "미리보기를 생성했습니다.";
    }
    void controls(GenerationConfig& draft) {
        ImGui::TextUnformatted("두 창이 같은 편집값을 사용합니다.");
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
        ImGui::InputScalar("월드 시드###preview-world-seed", ImGuiDataType_U32, &draft.seed);
        help("F8 편집창과 공유하는 시드입니다. 미리보기를 생성해야 이미지가 갱신됩니다. 실제 월드는 재생성 "
             "버튼을 누를 때 바뀝니다.");
        bool view_changed =
            ImGui::Combo("지도###preview-map", &map_index, map_names, IM_ARRAYSIZE(map_names));
        const auto kind = static_cast<GenerationMap>(map_index);
        if (view_changed) {
            selection = 0;
            weighted = false;
            if (kind == GenerationMap::temperature || kind == GenerationMap::precipitation)
                range = {0, 0, world_size, world_size};
        }
        if (ImGui::CollapsingHeader("도메인 워핑 목록"))
            warp_controls(draft);
        view_changed |= ImGui::Checkbox("워핑 전 보기 (비교용)", &before_warp);
        help("켜면 이 미리보기에서만 워핑을 생략합니다. 같은 시드·범위·옥타브로 전후를 비교하며 "
             "실제 편집값의 워핑 스위치는 바꾸지 않습니다. 지도 선택에도 유지됩니다.");
        switch (kind) {
        case GenerationMap::groundness:
            noise_controls("preview-groundness", draft.groundness, true, true, 16, &draft);
            break;
        case GenerationMap::smoothness:
            noise_controls("preview-smoothness", draft.smoothness, false, true, 16, &draft);
            break;
        case GenerationMap::weirdness:
        case GenerationMap::pv:
            noise_controls("preview-weirdness", draft.weirdness, false, true, 16, &draft);
            break;
        case GenerationMap::temperature:
            noise_controls("preview-temperature", draft.temperature, false, false, 16, &draft);
            temperature_controls(draft.temperature_bands);
            ImGui::TextWrapped("Z=65536 적도 · Z=0/131072 한랭대 · 지형에는 영향 없음");
            break;
        case GenerationMap::precipitation:
            noise_controls("preview-precipitation", draft.precipitation, false, true, 16, &draft);
            ImGui::TextWrapped("X/Z 주기131072 · 지형·물 배치에는 영향 없음");
            break;
        case GenerationMap::base_height:
            ImGui::TextWrapped("잔굴곡을 더하기 전의 높이입니다. 지형 스플라인 표의 offset으로 정합니다.");
            break;
        case GenerationMap::jagged_noise:
            noise_controls("preview-jagged", draft.jagged, false, true, 16, &draft);
            break;
        case GenerationMap::effective_height:
            ImGui::TextWrapped("잔굴곡까지 반영한 수직 그라디언트의 중심 높이입니다. 실제 블록은 4블록 "
                               "격자로 보간하고 3D 노이즈를 더합니다.");
            break;
        case GenerationMap::offset:
        case GenerationMap::factor:
        case GenerationMap::jaggedness:
            ImGui::TextWrapped("스플라인 편집창의 표와 동일한 계산입니다. 기후 값은 사용하지 않습니다.");
            break;
        }
        ImGui::Separator();
        ImGui::BeginDisabled(kind != GenerationMap::groundness);
        const int previous_selection = selection;
        selection = std::clamp(selection, 0, draft.groundness.octaves);
        view_changed |= selection != previous_selection;
        const std::string selected_label =
            selection == 0 ? "최종 합성" : "옥타브 " + std::to_string(selection);
        if (ImGui::BeginCombo("표시 대상###preview-octave", selected_label.c_str())) {
            for (int i = 0; i <= draft.groundness.octaves; ++i) {
                const std::string label = i == 0 ? "최종 합성" : "옥타브 " + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), selection == i) && selection != i) {
                    selection = i;
                    view_changed = true;
                }
            }
            ImGui::EndCombo();
        }
        help("최종 합성 또는 개별 옥타브를 선택합니다. 선택을 바꾸면 현재 편집값과 범위로 미리보기를 다시 "
             "계산합니다.");
        ImGui::BeginDisabled(selection == 0);
        view_changed |= ImGui::Checkbox("가중치 적용###preview-weighted", &weighted);
        help("끄면 옥타브 원신호, 켜면 원신호 × 해당 가중치 ÷ 전체 가중치 합을 봅니다. "
             "적용 결과들을 더하면 최종 합성입니다. 모두 0이면 적용 결과도 0입니다.");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::Combo("해상도###preview-resolution", &resolution_index,
                     "256\0"
                     "512\0"
                     "1024\0"
                     "2048\0");
        help("이미지의 긴 변에 사용할 표본 수입니다. 기본 512이며 클수록 더 자세하지만 계산 시간과 메모리가 "
             "늘어납니다. 변경 후 미리보기 생성을 누르세요.");
        ImGui::PopItemWidth();
        ImGui::TextWrapped("%s", legend(kind));
        ImGui::TextUnformatted("가로 X (동서) / 세로 Z (남북)");
        if (ImGui::Button("전체 월드 범위###preview-entire-world")) {
            range = {0, 0, world_size, world_size};
            view_changed = true;
        }
        help("0,0부터131072,131072까지 봅니다. 실제 청크를 생성하지 않고 선택한 해상도로 표본을 계산합니다.");
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
        ImGui::InputInt("시작 X###preview-x0", &range.x0, 0, 0);
        help("이미지 왼쪽의 X 좌표입니다. 0 이상, 끝 X 미만으로 입력하세요.");
        ImGui::InputInt("시작 Z###preview-z0", &range.z0, 0, 0);
        help("이미지 위쪽의 Z 좌표입니다. 0 이상, 끝 Z 미만으로 입력하세요.");
        ImGui::InputInt("끝 X###preview-x1", &range.x1, 0, 0);
        help("이미지 오른쪽의 X 좌표입니다. 시작보다 크고 131072 이하여야 합니다.");
        ImGui::InputInt("끝 Z###preview-z1", &range.z1, 0, 0);
        help("이미지 아래쪽의 Z 좌표입니다. 시작보다 크고 131072 이하여야 합니다.");
        ImGui::PopItemWidth();
        if (!range.valid())
            ImGui::TextWrapped("0 ≤ 시작 < 끝 ≤ 131072인 범위를 입력하세요.");
        ImGui::BeginDisabled(!range.valid());
        try {
            if (view_changed && range.valid())
                request(draft);
            if (ImGui::Button("미리보기 생성###preview-generate"))
                request(draft);
            help("현재 편집값과 범위로 이미지를 계산합니다. 계산 중 다시 누르면 최신 요청으로 대체합니다. "
                 "실제 월드나 저장 파일은 바꾸지 않습니다.");
            if (ImGui::Button("랜덤 시드###preview-seed")) {
                const uint32_t old = draft.seed;
                do {
                    draft.seed = static_cast<uint32_t>(random());
                } while (draft.seed == old);
                request(draft);
            }
            help("새 시드를 편집값에 넣고 미리보기를 생성합니다. 실제 월드 반영은 재생성, 다음 실행 기본값 "
                 "반영은 저장으로 따로 합니다.");
            ImGui::SameLine();
            if (ImGui::Button("랜덤 위치###preview-position")) {
                const int dx = range.x1 - range.x0, dz = range.z1 - range.z0;
                range.x0 = std::uniform_int_distribution<int>(0, world_size - dx)(random);
                range.z0 = std::uniform_int_distribution<int>(0, world_size - dz)(random);
                range.x1 = range.x0 + dx;
                range.z1 = range.z0 + dz;
                request(draft);
            }
            help("현재 가로·세로 범위 크기와 시드를 유지하고 월드 안에서 위치를 무작위로 옮겨 계산합니다.");
        } catch (const std::exception& error) {
            message = "미리보기 요청 실패: " + std::string(error.what());
        }
        ImGui::EndDisabled();
        if (busy)
            ImGui::Text("계산 중: %d%% · 편집을 계속할 수 있습니다.", progress.load());
        if (!message.empty())
            ImGui::TextWrapped("%s", message.c_str());
    }
    void image(const GenerationConfig& draft) {
        ImGui::PushItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x * 0.45f));
        ImGui::SliderFloat("확대###preview-zoom", &zoom, 0.25f, 4.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        if (!std::isfinite(zoom))
            zoom = 1;
        help("표시 크기만 바꿉니다. 해상도나 범위는 바뀌지 않으며 재계산하지 않습니다. 확대 후 스크롤로 "
             "이동할 수 있습니다.");
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("화면 맞춤###preview-fit"))
            zoom = 1;
        if (!shown || !binding) {
            ImGui::TextWrapped("왼쪽 설정에서 미리보기 생성을 누르세요.");
            return;
        }
        const auto& source = shown->request;
        const bool ground = map_index == static_cast<int>(GenerationMap::groundness);
        const bool stale = source.before_warp != before_warp || source.config != draft ||
                           source.kind != static_cast<GenerationMap>(map_index) || source.range != range ||
                           source.resolution != resolutions[resolution_index] ||
                           source.octave != (ground ? selection - 1 : -1) ||
                           source.weighted != (ground && selection > 0 && weighted);
        ImGui::TextWrapped(stale ? "이전 요청의 이미지입니다. 새 편집값을 보려면 미리보기를 생성하세요."
                                 : "현재 편집값·범위·해상도·표시 대상과 일치하는 이미지입니다.");
        const auto& r = source.range;
        ImGui::Text("지도: %s", map_names[static_cast<int>(source.kind)]);
        ImGui::TextWrapped("%s", legend(source.kind));
        const auto bar = ImGui::GetCursorScreenPos();
        const float bar_width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        auto* draw = ImGui::GetWindowDrawList();
        for (int i = 0; i < 128; ++i) {
            const float v = source.kind == GenerationMap::base_height ? float(world_height) * i / 127
                                                                      : -1 + 2.0f * i / 127;
            const auto c = map_colour(source.kind, v);
            draw->AddRectFilled(ImVec2(bar.x + bar_width * i / 128, bar.y),
                                ImVec2(bar.x + bar_width * (i + 1) / 128, bar.y + 12),
                                IM_COL32(c[0], c[1], c[2], 255));
        }
        ImGui::Dummy(ImVec2(bar_width, 16));
        if (source.octave < 0)
            ImGui::TextUnformatted("표시: 최종 합성");
        else
            ImGui::Text("표시: 옥타브 %d · %s", source.octave + 1,
                        source.weighted ? "가중치 적용 (합성 기여분)" : "원신호");
        ImGui::Text("이미지 워핑 설정: %s",
                    source.before_warp ? "워핑 전 (비교용)"
                                       : (has_active_warp(source.config) ? "워핑 적용" : "워핑 꺼짐"));
        ImGui::Text("이미지 시드: %u", source.config.seed);
        ImGui::TextWrapped("이미지 범위: (%d, %d) ~ (%d, %d)", r.x0, r.z0, r.x1, r.z1);
        ImGui::Text("%d × %d 표본 · 계산 %.2f ms", shown->width, shown->height, shown->milliseconds);
        ImGui::Text("표본 최솟값 %.4f · 최댓값 %.4f", shown->minimum, shown->maximum);
        ImGui::TextWrapped("넓은 범위는 간격을 두고 샘플링하므로 작은 지형은 생략될 수 있습니다. 범위를 "
                           "좁히면 더 자세히 볼 수 있습니다.");
        ImGui::BeginChild("preview-image-viewport", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        const float aspect = float(r.x1 - r.x0) / float(r.z1 - r.z0);
        // Base the fit on viewport size, never on the previous zoomed content extent.
        const auto viewport = ImGui::GetWindowSize();
        const auto& style = ImGui::GetStyle();
        const float available_x =
            std::max(1.0f, viewport.x - style.WindowPadding.x * 2 - style.ScrollbarSize);
        const float available_y =
            std::max(1.0f, viewport.y - style.WindowPadding.y * 2 - style.ScrollbarSize);
        const float w = std::min(available_x, available_y * aspect);
        const ImVec2 size(w * zoom, w / aspect * zoom);
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::Image(ImTextureRef((ImTextureID)binding), size);
        if (ImGui::IsItemHovered()) {
            const auto mouse = ImGui::GetIO().MousePos;
            const int x = std::clamp(int((mouse.x - origin.x) / size.x * shown->width), 0, shown->width - 1);
            const int z =
                std::clamp(int((mouse.y - origin.y) / size.y * shown->height), 0, shown->height - 1);
            ImGui::BeginTooltip();
            ImGui::Text("표본 X %.2f / Z %.2f", r.x0 + double(r.x1 - r.x0) * x / (shown->width - 1),
                        r.z0 + double(r.z1 - r.z0) * z / (shown->height - 1));
            ImGui::Text("%s: %.5f", map_names[static_cast<int>(source.kind)],
                        shown->values[size_t(z) * shown->width + x]);
            ImGui::EndTooltip();
        }
        ImGui::EndChild();
    }
    void draw(GenerationConfig& draft, bool& open) {
        const auto display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::min(1080.0f, display.x - 48), std::min(820.0f, display.y - 48)),
                                 ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(640, 440), ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::Begin("지형·기후 미리보기###groundness-preview-window", &open)) {
            const float height = std::max(1.0f, ImGui::GetContentRegionAvail().y);
            if (ImGui::BeginTable("preview-layout", 2,
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("설정", ImGuiTableColumnFlags_WidthFixed, 350);
                ImGui::TableSetupColumn("이미지", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextColumn();
                ImGui::BeginChild("preview-controls", ImVec2(0, height));
                controls(draft);
                ImGui::EndChild();
                ImGui::TableNextColumn();
                ImGui::BeginChild("preview-image-panel", ImVec2(0, height));
                image(draft);
                ImGui::EndChild();
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
};
GroundnessPreview::GroundnessPreview(Renderer& renderer) : impl_(std::make_unique<Impl>(renderer)) {}
GroundnessPreview::~GroundnessPreview() = default;
void GroundnessPreview::prepare() { impl_->prepare(); }
void GroundnessPreview::release_binding() { impl_->release_binding(); }
void GroundnessPreview::draw(GenerationConfig& draft, bool& open) { impl_->draw(draft, open); }
} // namespace sandbox
