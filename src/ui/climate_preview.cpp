#include "ui/climate_preview.hpp"
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
constexpr const char* map_names[] = {"Temperature · 온도", "Precipitation · 강수량"};
const char* legend(GenerationMap kind) {
    return kind == GenerationMap::temperature ? "파랑 -1 (추움) → 빨강 +1 (더움) · 섭씨가 아닌 온도 지수"
                                              : "갈색 -1 (건조) → 파랑 +1 (습윤) · 실제 강수량 단위 아님";
}
std::array<unsigned char, 3> map_colour(GenerationMap kind, float value) {
    const float t = std::clamp(value * .5f + .5f, 0.0f, 1.0f);
    const std::array<float, 3> a = kind == GenerationMap::temperature ? std::array<float, 3>{30, 80, 230}
                                                                      : std::array<float, 3>{165, 105, 45};
    const std::array<float, 3> b = kind == GenerationMap::temperature ? std::array<float, 3>{240, 55, 25}
                                                                      : std::array<float, 3>{35, 120, 235};
    std::array<unsigned char, 3> colour;
    for (size_t i = 0; i < 3; ++i)
        colour[i] = static_cast<unsigned char>(std::lround(std::lerp(a[i], b[i], t)));
    return colour;
}
struct PreviewRange {
    int x0{}, z0{}, x1{world_size}, z1{world_size};
    bool valid() const {
        return x0 >= 0 && z0 >= 0 && x1 <= world_size && z1 <= world_size && x1 > x0 && z1 > z0;
    }
    bool operator==(const PreviewRange&) const = default;
};
struct Request {
    GenerationConfig config;
    PreviewRange range;
    int resolution{512};
    bool before_warp{};
    uint64_t revision{};
    GenerationMap kind{GenerationMap::temperature};
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
struct ClimatePreview::Impl {
    Renderer& renderer;
    PreviewRange range;
    int resolution_index{1};
    int map_index{};
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
                const TerrainGenerator generator(sample_config);
                for (int z = 0; z < h; ++z) {
                    if (stop.stop_requested() || revision.load() != id)
                        break;
                    std::fill(zs.begin(), zs.end(), float(wrap_position(r.z0 + double(dz) * z / (h - 1))));
                    auto row = std::span(result->values).subspan(size_t(z) * w, w);
                    generator.map(result->request.kind, row, xs, zs);
                    for (int x = 0; x < w; ++x) {
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
        ImGui::InputScalar("마스터 시드###preview-world-seed", ImGuiDataType_U32, &draft.seed);
        help("F8 편집창과 공유하는 시드입니다. 미리보기를 생성해야 이미지가 갱신됩니다. 실제 월드는 재생성 "
             "버튼을 누를 때 바뀝니다.");
        bool view_changed =
            ImGui::Combo("지도###preview-map", &map_index, map_names, IM_ARRAYSIZE(map_names));
        const auto kind = static_cast<GenerationMap>(map_index);
        if (view_changed) {
            if (kind == GenerationMap::temperature || kind == GenerationMap::precipitation)
                range = {0, 0, world_size, world_size};
        }
        if (ImGui::CollapsingHeader("도메인 워핑 목록"))
            warp_controls(draft);
        view_changed |= ImGui::Checkbox("워핑 전 보기 (비교용)", &before_warp);
        help("켜면 이 미리보기에서만 워핑을 생략합니다. 같은 시드·범위로 전후를 비교하며 "
             "실제 편집값의 워핑 스위치는 바꾸지 않습니다. 지도 선택에도 유지됩니다.");
        if (kind == GenerationMap::temperature) {
            noise_controls("preview-temperature", draft.temperature, false, false, 16, &draft);
            temperature_controls(draft.temperature_bands);
        } else {
            noise_controls("preview-precipitation", draft.precipitation, false, true, 16, &draft);
        }
        ImGui::TextWrapped("기후는 현재 돌 평지의 높이와 재질에 영향을 주지 않습니다.");
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
        const bool stale = source.before_warp != before_warp || source.config != draft ||
                           source.kind != static_cast<GenerationMap>(map_index) || source.range != range ||
                           source.resolution != resolutions[resolution_index];
        ImGui::TextWrapped(stale ? "이전 요청의 이미지입니다. 새 편집값을 보려면 미리보기를 생성하세요."
                                 : "현재 편집값·범위·해상도·표시 대상과 일치하는 이미지입니다.");
        const auto& r = source.range;
        ImGui::Text("지도: %s", map_names[static_cast<int>(source.kind)]);
        ImGui::TextWrapped("%s", legend(source.kind));
        const auto bar = ImGui::GetCursorScreenPos();
        const float bar_width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        auto* draw = ImGui::GetWindowDrawList();
        for (int i = 0; i < 128; ++i) {
            const float v = -1 + 2.0f * i / 127;
            const auto c = map_colour(source.kind, v);
            draw->AddRectFilled(ImVec2(bar.x + bar_width * i / 128, bar.y),
                                ImVec2(bar.x + bar_width * (i + 1) / 128, bar.y + 12),
                                IM_COL32(c[0], c[1], c[2], 255));
        }
        ImGui::Dummy(ImVec2(bar_width, 16));
        ImGui::Text("이미지 워핑 설정: %s",
                    source.before_warp ? "워핑 전 (비교용)"
                                       : (has_active_warp(source.config) ? "워핑 적용" : "워핑 꺼짐"));
        ImGui::Text("이미지 시드: %u", source.config.seed);
        ImGui::TextWrapped("이미지 범위: (%d, %d) ~ (%d, %d)", r.x0, r.z0, r.x1, r.z1);
        ImGui::Text("%d × %d 표본 · 계산 %.2f ms", shown->width, shown->height, shown->milliseconds);
        ImGui::Text("표본 최솟값 %.4f · 최댓값 %.4f", shown->minimum, shown->maximum);
        ImGui::TextWrapped("넓은 범위는 간격을 두고 샘플링하므로 작은 변화은 생략될 수 있습니다. 범위를 "
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
        if (ImGui::Begin("기후 미리보기###climate-preview-window", &open)) {
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
ClimatePreview::ClimatePreview(Renderer& renderer) : impl_(std::make_unique<Impl>(renderer)) {}
ClimatePreview::~ClimatePreview() = default;
void ClimatePreview::prepare() { impl_->prepare(); }
void ClimatePreview::release_binding() { impl_->release_binding(); }
void ClimatePreview::draw(GenerationConfig& draft, bool& open) { impl_->draw(draft, open); }
} // namespace sandbox
