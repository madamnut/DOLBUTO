#include "core/lod_debug.hpp"
#include "core/settings.hpp"
#include "core/world_rules.hpp"
#include "render/renderer.hpp"
#include "ui/climate_debug.hpp"
#include "ui/game_console.hpp"
#include "ui/generation_editor.hpp"
#include "ui/rml_renderer.hpp"
#include "world/biome.hpp"
#include "world/profiling.hpp"
#include "world/world_view.hpp"
#include <RmlUi/Core.h>
#include <RmlUi_Platform_SDL.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <memory>
#include <optional>
#include <renderdoc_app.h>
#include <span>
#include <stdexcept>
#include <string>
#include <tracy/Tracy.hpp>
#include <vector>

// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on

namespace {
const char* facing_direction(double yaw) {
    if (!std::isfinite(yaw))
        return "판정 불가";
    // Logical player yaw: independent of pitch, front third-person view, and view bobbing.
    yaw = std::remainder(yaw, 360.0);
    if (yaw >= -45.0 && yaw < 45.0)
        return "동쪽 (+X)";
    if (yaw >= 45.0 && yaw < 135.0)
        return "남쪽 (+Z)";
    if (yaw >= 135.0 || yaw < -135.0)
        return "서쪽 (−X)";
    return "북쪽 (−Z)";
}
struct ProcessMemory {
    uint64_t resident_bytes{}, private_commit_bytes{};
    bool available{};
    std::chrono::steady_clock::time_point next_update{};

    void refresh(std::chrono::steady_clock::time_point now) {
        if (now < next_update)
            return;
        next_update = now + std::chrono::seconds(1);
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        available = K32GetProcessMemoryInfo(GetCurrentProcess(),
                                            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                            sizeof(counters)) != 0;
        if (available) {
            // 작업 집합에는 공유 페이지도 포함한다. 전용 커밋은 RAM 상주량과 별개다.
            resident_bytes = counters.WorkingSetSize;
            private_commit_bytes = counters.PrivateUsage;
        }
    }
};
std::filesystem::path screenshot_path(const std::filesystem::path& directory) {
    SDL_Time ticks{};
    SDL_DateTime time{};
    if (!SDL_GetCurrentTime(&ticks) || !SDL_TimeToDateTime(ticks, &time, true))
        throw std::runtime_error(SDL_GetError());
    char stamp[64];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d_%02d-%02d-%02d_%03d", time.year, time.month, time.day,
                  time.hour, time.minute, time.second, time.nanosecond / 1000000);
    auto result = directory / (std::string(stamp) + ".png");
    for (uint64_t suffix = 1; std::filesystem::exists(result); ++suffix)
        result = directory / (std::string(stamp) + "_" + std::to_string(suffix) + ".png");
    return result;
}
struct SdlLifetime {
    SdlLifetime() {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
            throw std::runtime_error(SDL_GetError());
    }
    ~SdlLifetime() { SDL_Quit(); }
};
struct RenderDocCapture {
    SDL_SharedObject* library{};
    RENDERDOC_API_1_6_0* api{};
    explicit RenderDocCapture(const std::filesystem::path& output) {
        if (output.empty())
            return;
        const char* dll = SDL_getenv("SANDBOX_RENDERDOC_DLL");
        if (!dll)
            throw std::runtime_error("Use tools/capture.ps1 for RenderDoc captures.");
        library = SDL_LoadObject(dll);
        if (!library)
            throw std::runtime_error(SDL_GetError());
        const auto get_api =
            reinterpret_cast<pRENDERDOC_GetAPI>(SDL_LoadFunction(library, "RENDERDOC_GetAPI"));
        if (!get_api || !get_api(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&api)))
            throw std::runtime_error("RenderDoc API unavailable.");
        std::filesystem::create_directories(output.parent_path());
        api->SetCaptureFilePathTemplate(output.string().c_str());
        api->MaskOverlayBits(0, 0);
    }
    ~RenderDocCapture() {
        if (library)
            SDL_UnloadObject(library);
    }
};
class System final : public SystemInterface_SDL {
  public:
    using SystemInterface_SDL::SystemInterface_SDL;
    unsigned errors{}, warnings{};
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT)
            ++errors;
        if (type == Rml::Log::LT_WARNING)
            ++warnings;
        std::cerr << "[RmlUi] " << message << '\n';
        return true;
    }
};
struct RmlLifetime {
    RmlLifetime(System& system, sandbox::RmlRenderer& renderer) {
        Rml::SetSystemInterface(&system);
        Rml::SetRenderInterface(&renderer);
        if (!Rml::Initialise())
            throw std::runtime_error("RmlUi initialization failed.");
    }
    ~RmlLifetime() { Rml::Shutdown(); }
};
struct ImGuiLifetime {
    sandbox::Renderer& renderer;
    bool platform_ready{}, renderer_ready{};
    ImFont* debug_font{};
    explicit ImGuiLifetime(sandbox::Renderer& r) : renderer(r) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
    }
    void initialize(SDL_Window* window) {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        // The Vulkan backend supports dynamic glyph uploads; Korean glyphs are baked as needed.
        if (!io.Fonts->AddFontFromFileTTF("assets/fonts/NotoSansKR.ttf", 17.0f))
            throw std::runtime_error("Cannot load Korean font for the development UI.");
        debug_font = io.Fonts->AddFontFromFileTTF("assets/fonts/NotoSansKR-SemiBold.ttf", 25.5f);
        if (!debug_font)
            throw std::runtime_error("Cannot load semibold Korean font for the debug overlay.");
        platform_ready = ImGui_ImplSDL3_InitForVulkan(window);
        if (!platform_ready)
            throw std::runtime_error("ImGui SDL3 initialization failed.");
        initialize_renderer();
    }
    void initialize_renderer() {
        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = VK_API_VERSION_1_4;
        info.Instance = renderer.instance;
        info.PhysicalDevice = renderer.physical_device;
        info.Device = renderer.device;
        info.QueueFamily = renderer.queue_family;
        info.Queue = renderer.queue;
        info.DescriptorPoolSize = 64;
        info.MinImageCount = 2;
        info.ImageCount = renderer.image_count();
        info.UseDynamicRendering = true;
        info.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &renderer.colour_format;
        info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        info.CheckVkResultFn = [](VkResult result) { sandbox::vk_check(result, "ImGui Vulkan"); };
        renderer_ready = ImGui_ImplVulkan_Init(&info);
        if (!renderer_ready)
            throw std::runtime_error("ImGui Vulkan initialization failed.");
    }
    ~ImGuiLifetime() {
        vkDeviceWaitIdle(renderer.device);
        if (renderer_ready)
            ImGui_ImplVulkan_Shutdown();
        if (platform_ready)
            ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
};
struct MenuActions final : Rml::EventListener {
    Rml::ElementDocument* document{};
    Rml::ElementDocument* hud{};
    bool running{true}, debug{}, generation_open{}, play{}, options{}, paused{};
    void show_options(bool open) {
        options = open;
        document->SetClass("options-open", open);
        hud->SetClass("options-open", open);
    }
    void listen_tabs(bool add) {
        for (auto* doc : {document, hud}) {
            const std::string prefix = doc == document ? "menu-" : "";
            for (const char* page : {"display", "water", "clouds", "shadows", "lighting"}) {
                auto* tab = doc->GetElementById(prefix + "settings-tab-" + page);
                if (add)
                    tab->AddEventListener("click", this);
                else
                    tab->RemoveEventListener("click", this);
            }
        }
    }
    void ProcessEvent(Rml::Event& event) override {
        const auto id = event.GetCurrentElement()->GetId();
        const auto control = id.starts_with("menu-") ? id.substr(5) : id;
        if (control.starts_with("settings-tab-")) {
            const auto selected = control.substr(13);
            for (auto* doc : {document, hud}) {
                const std::string prefix = doc == document ? "menu-" : "";
                for (const char* page : {"display", "water", "clouds", "shadows", "lighting"}) {
                    doc->GetElementById(prefix + "settings-tab-" + page)
                        ->SetClass("active", selected == page);
                    doc->GetElementById(prefix + "settings-page-" + page)
                        ->SetClass("active", selected == page);
                }
                doc->GetElementById(prefix + "settings-content")->SetScrollTop(0);
            }
            return;
        }
        if (id == "play") {
            play = true;
            paused = false;
            show_options(false);
        } else if (id == "quit")
            running = false;
        else if (id == "resume") {
            paused = false;
            show_options(false);
        } else if (id == "return-menu") {
            play = paused = false;
            show_options(false);
        } else if (id == "options" || id == "pause-options")
            show_options(true);
        else if (id == "options-back" || id == "settings-back")
            show_options(false);
    }
};
struct WorldSettings final : Rml::EventListener {
    Rml::ElementDocument* hud{};
    Rml::ElementDocument* menu{};
    sandbox::WorldView* world{};
    sandbox::Renderer* renderer{};
    sandbox::GameSettings values, persisted;
    int finite_fps{120};
    bool refreshing{}, cloud_save_pending{};
    std::filesystem::path save_path;
    std::string notice;
    void save() {
        try {
            sandbox::save_game_settings(save_path, persisted);
            notice = "저장했어요. 다음 실행에도 이 설정을 사용해요.";
        } catch (const std::exception& error) {
            notice = "설정은 적용했지만 저장하지 못했어요. 값을 다시 바꾸면 저장을 재시도해요.";
            std::cerr << "Cannot save game settings: " << error.what() << '\n';
        }
    }
    bool adjust_cloud(int delta) {
        const int next = std::clamp(values.graphics.cloud_coverage + delta, 0, 100);
        if (next == values.graphics.cloud_coverage)
            return false;
        values.graphics.cloud_coverage = next;
        if (world)
            world->set_graphics_settings(values.graphics);
        cloud_save_pending = true;
        refresh();
        return true;
    }
    void flush_cloud() {
        if (!cloud_save_pending)
            return;
        // Preserve a command-line render-distance override as an in-memory value.
        persisted.graphics = values.graphics;
        cloud_save_pending = false;
        save();
        refresh();
    }
    void refresh() {
        if (refreshing)
            return;
        refreshing = true; // Setting range/checkbox attributes can dispatch change synchronously.
        if (values.fps_limit)
            finite_fps = values.fps_limit;
        for (auto* document : {hud, menu}) {
            const std::string prefix = document == menu ? "menu-" : "";
            const auto element = [&](const char* id) { return document->GetElementById(prefix + id); };
            const auto value = [&](const char* id, int number) {
                auto* input = element(id);
                const auto text = std::to_string(number);
                if (input->GetAttribute<Rml::String>("value", "") != text)
                    input->SetAttribute("value", text);
            };
            const auto checked = [&](const char* id, bool enabled) {
                auto* input = element(id);
                if (enabled && !input->HasAttribute("checked"))
                    input->SetAttribute("checked", "");
                else if (!enabled && input->HasAttribute("checked"))
                    input->RemoveAttribute("checked");
            };
            const auto disabled = [&](const char* id, bool enabled) {
                auto* input = element(id);
                if (enabled && !input->HasAttribute("disabled"))
                    input->SetAttribute("disabled", "");
                else if (!enabled && input->HasAttribute("disabled"))
                    input->RemoveAttribute("disabled");
            };
            element("distance-label")
                ->SetInnerRML("렌더 거리 · " + std::to_string(values.render_distance) + "컬럼 (" +
                              std::to_string(values.render_distance * 16) + "블록)");
            element("fov-label")->SetInnerRML("수직 시야각 · " + std::to_string(values.field_of_view) + "°");
            element("fps-label")
                ->SetInnerRML(values.vsync
                                  ? "최대 FPS · 모니터에 동기화"
                                  : (values.fps_limit ? "최대 FPS · " + std::to_string(values.fps_limit)
                                                      : "최대 FPS · 무제한"));
            element("fps-help")
                ->SetInnerRML(values.vsync ? "VSync를 끄면 이전 FPS 제한을 다시 사용해요."
                                           : "30~500 FPS · 무제한을 해제하면 숫자를 입력할 수 있어요.");
            value("render-distance", values.render_distance);
            value("fov", values.field_of_view);
            value("fov-number", values.field_of_view);
            value("fps", finite_fps);
            value("fps-number", finite_fps);
            checked("vsync", values.vsync);
            checked("view-bobbing", values.view_bobbing);
            checked("fps-unlimited", values.fps_limit == 0);
            disabled("fps-unlimited", values.vsync);
            disabled("fps", values.vsync || !values.fps_limit);
            disabled("fps-number", values.vsync || !values.fps_limit);
            checked("water-enabled", values.water.enabled);
            checked("water-depth", values.water.depth);
            checked("water-waves", values.water.waves);
            checked("water-ssr", values.water.ssr);
            for (const char* id : {"water-depth", "water-waves", "water-ssr"})
                disabled(id, !values.water.enabled);
            checked("water-refraction", values.water.refraction);
            checked("water-foam", values.water.foam);
            checked("water-caustics", values.water.caustics);
            checked("water-underwater-fog", values.water.underwater_fog);
            disabled("water-foam", !values.water.enabled || !values.water.depth);
            disabled("water-caustics", !values.water.enabled);
            disabled("water-underwater-fog", !values.water.enabled);
            disabled("water-refraction", !values.water.enabled || !values.water.waves);
            for (const auto& toggle : sandbox::graphics_toggles) {
                const auto id = std::string("graphics-") + toggle.key;
                checked(id.c_str(), values.graphics.*(toggle.member));
            }
            disabled("graphics-lod_distance", !values.graphics.lod);
            disabled("graphics-lod_distance-number", !values.graphics.lod);
            disabled("graphics-cloud_shadows", !values.graphics.clouds);
            disabled("graphics-shafts", !values.graphics.shadows);
            for (const auto& range : sandbox::graphics_ranges) {
                const auto id = std::string("graphics-") + range.key;
                const auto number = id + "-number";
                const int current = values.graphics.*(range.member);
                value(id.c_str(), current);
                value(number.c_str(), current);
                element((id + "-label").c_str())
                    ->SetInnerRML(std::string(range.label) + " · " + std::to_string(current));
                const bool inactive = (id.starts_with("graphics-cloud_") && !values.graphics.clouds) ||
                                      (id.starts_with("graphics-shadow_") && !values.graphics.shadows) ||
                                      (id == "graphics-shaft_quality" &&
                                       (!values.graphics.shadows || !values.graphics.shafts)) ||
                                      (id == "graphics-bloom_strength" && !values.graphics.bloom) ||
                                      (id == "graphics-lod_distance" && !values.graphics.lod);
                disabled(id.c_str(), inactive);
                disabled(number.c_str(), inactive);
            }
            document->GetElementById(document == menu ? "options-help" : "settings-help")
                ->SetInnerRML(notice.empty() ? "변경하면 바로 적용되고 다음 실행에도 유지돼요." : notice);
        }
        refreshing = false;
    }
    void listen(bool add) {
        for (auto* document : {hud, menu}) {
            const std::string prefix = document == menu ? "menu-" : "";
            const auto hook = [&](const char* id, const char* type) {
                auto* input = document->GetElementById(prefix + id);
                if (add)
                    input->AddEventListener(type, this);
                else
                    input->RemoveEventListener(type, this);
            };
            for (const char* id :
                 {"render-distance", "fov", "fps", "vsync", "view-bobbing", "fps-unlimited", "fov-number",
                  "fps-number", "water-enabled", "water-depth", "water-waves", "water-ssr",
                  "water-refraction", "water-foam", "water-caustics", "water-underwater-fog"})
                hook(id, "change");
            for (const char* id : {"fov-number", "fps-number"})
                hook(id, "blur");
            for (const char* id : {"distance-less", "distance-more"})
                hook(id, "click");
            for (const auto& toggle : sandbox::graphics_toggles)
                hook((std::string("graphics-") + toggle.key).c_str(), "change");
            for (const auto& range : sandbox::graphics_ranges) {
                const auto id = std::string("graphics-") + range.key;
                hook(id.c_str(), "change");
                hook((id + "-number").c_str(), "change");
                hook((id + "-number").c_str(), "blur");
            }
        }
    }
    void ProcessEvent(Rml::Event& event) override {
        if (refreshing)
            return;
        auto* input = event.GetCurrentElement();
        const auto id = input->GetId();
        const auto control = id.starts_with("menu-") ? id.substr(5) : id;
        if (input->HasAttribute("disabled"))
            return;
        const auto previous = values;
        const bool number = control == "fov-number" || control == "fps-number";
        if (control.starts_with("graphics-")) {
            for (const auto& toggle : sandbox::graphics_toggles)
                if (control == std::string("graphics-") + toggle.key)
                    values.graphics.*(toggle.member) = input->HasAttribute("checked");
            for (const auto& range : sandbox::graphics_ranges) {
                const auto range_id = std::string("graphics-") + range.key;
                if (control == range_id) {
                    values.graphics.*(range.member) =
                        std::clamp(event.GetParameter<int>("value", values.graphics.*(range.member)),
                                   range.minimum, range.maximum);
                } else if (control == range_id + "-number") {
                    if (event.GetType() != "blur" && !event.GetParameter<bool>("linebreak", false))
                        return;
                    const auto text = input->GetAttribute<Rml::String>("value", "");
                    int parsed{};
                    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
                    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
                        parsed < range.minimum || parsed > range.maximum) {
                        notice = std::string(range.label) + "은(는) " + std::to_string(range.minimum) + "~" +
                                 std::to_string(range.maximum) + " 사이의 정수로 입력해 주세요.";
                        refresh();
                        return;
                    }
                    values.graphics.*(range.member) = parsed;
                }
            }
        } else if (number) {
            // Text changes every keystroke: commit only Enter or focus loss, not partial numbers.
            if (event.GetType() != "blur" && !event.GetParameter<bool>("linebreak", false))
                return;
            const auto text = input->GetAttribute<Rml::String>("value", "");
            int parsed{};
            const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
            const int maximum = control == "fov-number" ? 110 : 500;
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed < 30 ||
                parsed > maximum) {
                notice = control == "fov-number" ? "시야각은 30~110 사이의 정수로 입력해 주세요."
                                                 : "FPS는 30~500 사이의 정수로 입력해 주세요.";
                refresh();
                return;
            }
            if (control == "fov-number")
                values.field_of_view = parsed;
            else if (!values.vsync && values.fps_limit)
                values.fps_limit = parsed;
        } else if (control == "distance-less" || control == "distance-more") {
            values.render_distance =
                std::clamp(values.render_distance + (control == "distance-more" ? 1 : -1), 1, 64);
        } else if (control == "render-distance") {
            values.render_distance =
                std::clamp(event.GetParameter<int>("value", values.render_distance), 1, 64);
        } else if (control == "fov") {
            values.field_of_view =
                std::clamp(event.GetParameter<int>("value", values.field_of_view), 30, 110);
        } else if (control == "fps" && !values.vsync && values.fps_limit) {
            values.fps_limit = std::clamp(event.GetParameter<int>("value", values.fps_limit), 30, 500);
        } else if (control == "view-bobbing") {
            values.view_bobbing = input->HasAttribute("checked");
        } else if (control == "vsync") {
            values.vsync = input->HasAttribute("checked");
        } else if (control == "fps-unlimited" && !values.vsync) {
            values.fps_limit = input->HasAttribute("checked") ? 0 : finite_fps;
        } else if (control == "water-enabled") {
            values.water.enabled = input->HasAttribute("checked");
        } else if (values.water.enabled && control == "water-depth") {
            values.water.depth = input->HasAttribute("checked");
        } else if (values.water.enabled && control == "water-waves") {
            values.water.waves = input->HasAttribute("checked");
        } else if (values.water.enabled && control == "water-ssr") {
            values.water.ssr = input->HasAttribute("checked");
        }
        if (values.water.enabled && values.water.waves && control == "water-refraction")
            values.water.refraction = input->HasAttribute("checked");
        if (values.water.enabled && values.water.depth) {
            if (control == "water-foam")
                values.water.foam = input->HasAttribute("checked");
        }
        if (values.water.enabled && control == "water-caustics")
            values.water.caustics = input->HasAttribute("checked");
        if (values.water.enabled && control == "water-underwater-fog")
            values.water.underwater_fog = input->HasAttribute("checked");
        if (values != previous) {
            if (world) {
                if (values.render_distance != previous.render_distance)
                    world->set_radius(values.render_distance);
                world->camera.field_of_view = float(values.field_of_view);
                world->set_view_bobbing(values.view_bobbing);
                world->set_water_settings(values.water);
                world->set_graphics_settings(values.graphics);
            }
            renderer->set_vsync(values.vsync);
            // An unrelated option must not persist a command-line render-distance override.
            if (values.render_distance != previous.render_distance)
                persisted.render_distance = values.render_distance;
            persisted.field_of_view = values.field_of_view;
            persisted.fps_limit = values.fps_limit;
            persisted.vsync = values.vsync;
            persisted.view_bobbing = values.view_bobbing;
            persisted.water = values.water;
            persisted.graphics = values.graphics;
            save();
        }
        refresh();
    }
};
float lod_legend_top(ImVec2 display, bool f3) {
    return f3 ? std::max(12.0f, display.y - 112.0f - 216.0f) : 12.0f;
}
void lod_legend(bool f3, ImFont* font) {
    ImGui::PushFont(font, 20.0f);
    const auto display = ImGui::GetIO().DisplaySize;
    const ImVec2 origin(std::max(12.0f, display.x - 432.0f), lod_legend_top(display, f3));
    auto* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + 420, origin.y + 216), IM_COL32(15, 20, 26, 230), 6);
    draw->AddText(ImVec2(origin.x + 12, origin.y + 10), IM_COL32_WHITE, "F4 · LOD 단계  /  기둥 X×Z (블록)");
    for (size_t i = 0; i < sandbox::lod_debug_palette.size(); ++i) {
        const auto c = sandbox::lod_debug_palette[i];
        const ImVec2 at(origin.x + 12 + float(i / 6) * 202, origin.y + 42 + float(i % 6) * 22);
        draw->AddRectFilled(at, ImVec2(at.x + 14, at.y + 14), ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, 1)));
        char label[80];
        if (i == 0)
            std::snprintf(label, sizeof(label), "실제 청크 · 원본");
        else
            std::snprintf(label, sizeof(label), "LOD %zu · %u×%u", i - 1, 1u << (i - 1), 1u << (i - 1));
        draw->AddText(ImVec2(at.x + 22, at.y - 2), IM_COL32_WHITE, label);
    }
    draw->AddText(ImVec2(origin.x + 12, origin.y + 186), IM_COL32(210, 218, 230, 255),
                  "검은 선: 타일 경계  ·  F4: 일반 화면");
    ImGui::PopFont();
}
void debug_overlay(sandbox::Renderer& renderer, const sandbox::RmlRenderer& ui, double frame_ms,
                   const sandbox::WorldView* world, ImFont* font, const ProcessMemory& memory) {
    ImGuiTextBuffer left, right;
    if (frame_ms > 0)
        left.appendf("FPS: %.1f · 프레임: %.3f ms\n", 1000.0 / frame_ms, frame_ms);
    else
        left.append("FPS: -- · 프레임: --\n");
    right.appendf("%s\nPresent: %s\n", renderer.gpu_name().c_str(), renderer.present_mode_description());
    if (renderer.gpu_timing_supported())
        right.appendf("GPU 렌더: %.3f ms · 업로드: %.3f ms\n", renderer.gpu_ms(), renderer.upload_gpu_ms());
    else
        right.append("GPU 시간 측정 미지원\n");
    uint64_t used{}, reserved{};
    renderer.memory_usage(used, reserved);
    right.appendf("GPU 메모리: %.2f MiB · 예약: %.2f MiB\n", double(used) / 1048576.0,
                  double(reserved) / 1048576.0);
    if (memory.available)
        right.appendf("RAM: %.2f GiB · 전용 커밋: %.2f GiB\n", double(memory.resident_bytes) / 1073741824.0,
                      double(memory.private_commit_bytes) / 1073741824.0);
    else
        right.append("RAM: -- · 전용 커밋: -- (조회 실패)\n");
    right.appendf("UI draw: %u · Validation 오류/경고: %u/%u\n", ui.draw_calls,
                  renderer.validation_errors.load(), renderer.validation_warnings.load());
    right.appendf("청크: %d³ · 높이: %d · 물리: %d TPS\n", sandbox::chunk_edge, sandbox::world_height,
                  sandbox::physics_tps);
    if (world) {
        const auto lod = world->lod_stats();
        right.appendf("LOD CPU: %.1f/512 MiB · GPU: %.1f/256 MiB\n", double(lod.cpu_bytes) / 1048576.0,
                      double(world->lod_gpu_bytes()) / 1048576.0);
        right.appendf("LOD 타일: %zu · 대기: %zu · 업로드: %zu\n", world->lod_tiles(), lod.pending,
                      world->lod_upload_queue());
        right.appendf("LOD 편집 기록: %.2f MiB\n", double(lod.session_edit_bytes) / 1048576.0);
        right.appendf("LOD 생성: %zu · %.2f ms%s%s\n", lod.generated, lod.last_generation_ms,
                      lod.paused ? " · 근거리 우선" : "", lod.memory_limited ? " · 캐시 한도" : "");
        const auto& player = world->player();
        left.appendf("플레이어 XYZ: %.2f / %.2f / %.2f\n", player.position.x, player.position.y,
                     player.position.z);
        left.appendf("카메라 XYZ: %.2f / %.2f / %.2f\n", world->camera.position.x, world->camera.position.y,
                     world->camera.position.z);
        left.appendf("시드: %u · %s · 접지: %s\n", world->seed(),
                     player.movement_mode == sandbox::MovementMode::fly ? "플라이" : "걷기",
                     player.grounded ? "예" : "아니요");
        left.appendf("몸체: 0.7 × 0.7 × 1.75 · 수직 속도: %.2f\n", player.vertical_velocity);
        const char* mode =
            world->camera.mode == sandbox::CameraMode::first_person
                ? "1인칭"
                : (world->camera.mode == sandbox::CameraMode::third_person_back ? "뒤 3인칭" : "앞 3인칭");
        left.appendf("시점: %s · 3인칭 거리: 6블록\n", mode);
        left.appendf("바라보는 방향: %s\n", facing_direction(world->camera.yaw));
        const auto tick = world->day_tick();
        const auto date = world->date();
        left.appendf("%llu년 %02u월 %02u일 %02u:%02u · %llu일차 · %s\n",
                     static_cast<unsigned long long>(date.year), date.month, date.day, date.hour, date.minute,
                     static_cast<unsigned long long>(date.day_number), world->is_day() ? "낮" : "밤");
        left.appendf("누적 틱: %llu · 하루 틱: %u / 28800 · 햇빛: %.3f\n",
                     static_cast<unsigned long long>(world->world_tick()), tick, world->daylight());
        // Cache only diagnostics, never the generator or draft settings. Hidden F3 does no sampling.
        static std::array<float, 12> values{};
        static float shape{}, raw_factor{};
        static uint64_t signature{};
        static auto sampled_at = std::chrono::steady_clock::time_point::min();
        static glm::dvec3 sampled_position{};
        const auto now = std::chrono::steady_clock::now();
        const auto& generator = world->generator();
        if (signature != generator.signature() ||
            sampled_at == std::chrono::steady_clock::time_point::min() ||
            now - sampled_at >= std::chrono::milliseconds(100)) {
            const std::array<float, 1> x{float(sandbox::wrap_position(player.position.x))},
                y{float(player.position.y)}, z{float(sandbox::wrap_position(player.position.z))};
            auto samples = std::span<float>(values);
            generator.terrain_inputs(samples.subspan(0, 1), samples.subspan(1, 1), samples.subspan(2, 1), x,
                                     z);
            values[3] = sandbox::spline_pv(values[2]);
            for (size_t i = 4; i < values.size(); ++i)
                generator.map(static_cast<sandbox::GenerationMap>(i), samples.subspan(i, 1), x, z);
            generator.shape(std::span<float>(&shape, 1), x, y, z);
            raw_factor = world->config().splines.factor.evaluate(values[0], values[1], values[2]);
            sampled_at = now;
            sampled_position = player.position;
            signature = generator.signature();
        }
        left.appendf("노이즈: 현재 월드 · 워핑 %s · 10Hz\n", has_active_warp(world->config()) ? "ON" : "OFF");
        left.appendf("표본 XYZ: %.1f / %.1f / %.1f (격자 보간 전)\n", sampled_position.x, sampled_position.y,
                     sampled_position.z);
        left.appendf("Groundness: %.4f · Smoothness: %.4f\n", values[0], values[1]);
        left.appendf("Weirdness: %.4f · PV: %.4f\n", values[2], values[3]);
        const auto climate_line = [&](const char* label, float value, const auto& names) {
            const int stage = sandbox::climate_debug_stage(value);
            if (stage < 0)
                left.appendf("%s: %.4f · 판정 불가\n", label, value);
            else
                left.appendf("%s: %.4f · %s (%d/9)\n", label, value, names[stage], stage + 1);
        };
        climate_line("온도", values[5], sandbox::temperature_stage_names);
        climate_line("강수량", values[6], sandbox::precipitation_stage_names);
        const char* biome_name = "판정 불가";
        if (std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2])) {
            switch (sandbox::surface_biome(values[0], values[1], values[2])) {
            case sandbox::Biome::ocean:
                biome_name = "바다";
                break;
            case sandbox::Biome::river:
                biome_name = "강";
                break;
            case sandbox::Biome::land:
                biome_name = "육지";
                break;
            }
        }
        left.appendf("바이옴: %s\n", biome_name);
        left.appendf("기준 높이: %.2f · 잔굴곡 적용: %.2f\n", values[4], values[11]);
        left.appendf("Offset: %.4f · Factor: %.4f\n", values[7], raw_factor);
        left.appendf("Jaggedness: %.4f · 잔굴곡 노이즈: %.4f\n", values[9], values[10]);
        left.appendf("최종 압축: %.4f · 3D 노이즈: %.4f%s\n", values[8], shape,
                     generator.shape_active() ? "" : " (비활성)");
        right.appendf("렌더 거리: %d컬럼\n", world->radius());
        right.appendf("조명 갱신 대기: %zu컬럼\n", world->pending_lighting());
        right.appendf("공개/대기 컬럼: %zu / %zu\n", world->visible_columns(), world->pending_columns());
        right.appendf("그린 청크: %u · 삼각형: %u\n", world->drawn_chunks, world->triangles);
        right.appendf("면 데이터: %.2f MiB (면당8바이트)\n", double(world->mesh_bytes()) / 1048576.0);
        right.appendf("컬럼 생성: %.3f ms\n초기 조명: %.3f ms · 메싱: %.3f ms\n", world->generation_ms,
                      world->lighting_ms, world->meshing_ms);
        right.appendf("CPU 월드 준비: %.3f ms · %.1f KiB\n", world->upload_cpu_ms,
                      double(world->uploaded_bytes) / 1024.0);
    }
    ImGui::PushFont(font, font->LegacySize);
    auto* draw = ImGui::GetBackgroundDrawList();
    const auto display = ImGui::GetIO().DisplaySize;
    const bool stacked = display.x < 900;
    const float available = std::max(1.0f, display.x - 24);
    const float wrap = stacked ? available : std::max(1.0f, (available - 24) * .5f);
    const auto left_size = ImGui::CalcTextSize(left.begin(), left.end(), false, wrap);
    const auto right_size = ImGui::CalcTextSize(right.begin(), right.end(), false, wrap);
    const auto paint = [&](const ImGuiTextBuffer& text, ImVec2 at, ImVec2 clip_min, ImVec2 clip_max) {
        draw->PushClipRect(clip_min, clip_max, true);
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                if (x || y)
                    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + x, at.y + y),
                                  IM_COL32(0, 0, 0, 255), text.begin(), text.end(), wrap);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), at, IM_COL32(255, 255, 255, 255), text.begin(),
                      text.end(), wrap);
        draw->PopClipRect();
    };
    paint(left, ImVec2(12, 12), ImVec2(0, 0), ImVec2(stacked ? display.x : display.x * .5f, display.y));
    const ImVec2 right_at(stacked ? 12 : std::max(display.x * .5f + 12, display.x - 12 - right_size.x),
                          stacked ? 24 + left_size.y : 12);
    const float right_bottom = world && world->lod_debug() ? lod_legend_top(display, true) - 8 : display.y;
    paint(right, right_at, ImVec2(stacked ? 0 : display.x * .5f, 0), ImVec2(display.x, right_bottom));
    ImGui::PopFont();
}
} // namespace
int main(int argc, char** argv) {
    try {
        bool debug_initial = false, start_world = false, lod_debug_initial = false;
        bool validation_requested = SANDBOX_VALIDATION != 0;
        uint32_t seed = 1337;
        bool seed_override = false;
        int render_distance = 12;
        bool distance_override = false;
        unsigned frame_limit = 0;
        double seconds_limit = 0;
        std::filesystem::path capture, rdc_path, world_profile, gpu_profile, shadow_capture;
        std::optional<std::array<double, 4>> profile_view;
        unsigned profile_samples = 600;
        sandbox::ColumnKey profile_origin{};
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--profile-gpu" && i + 1 < argc)
                gpu_profile = std::filesystem::absolute(argv[++i]);
            else if (arg == "--capture-shadow-maps" && i + 1 < argc)
                shadow_capture = std::filesystem::absolute(argv[++i]);
            else if (arg == "--profile-samples" && i + 1 < argc)
                profile_samples = static_cast<unsigned>(std::stoul(argv[++i]));
            else if (arg == "--profile-view" && i + 4 < argc) {
                std::array<double, 4> view{};
                for (auto& value : view)
                    value = std::stod(argv[++i]);
                profile_view = view;
            } else if (arg == "--profile-world" && i + 1 < argc)
                world_profile = std::filesystem::absolute(argv[++i]);
            else if (arg == "--profile-origin" && i + 2 < argc) {
                profile_origin.x = std::stoi(argv[++i]);
                profile_origin.z = std::stoi(argv[++i]);
            } else if (arg == "--world")
                start_world = true;
            else if (arg == "--seed" && i + 1 < argc) {
                seed = static_cast<uint32_t>(std::stoul(argv[++i]));
                seed_override = true;
            } else if (arg == "--render-distance" && i + 1 < argc) {
                render_distance = std::stoi(argv[++i]);
                distance_override = true;
            } else if (arg == "--debug-ui")
                debug_initial = true;
            else if (arg == "--lod-debug")
                lod_debug_initial = true;
            else if (arg == "--validation")
                validation_requested = true;
            else if (arg == "--rdc" && i + 1 < argc)
                rdc_path = std::filesystem::absolute(argv[++i]);
            else if (arg == "--frames" && i + 1 < argc)
                frame_limit = static_cast<unsigned>(std::stoul(argv[++i]));
            else if (arg == "--seconds" && i + 1 < argc)
                seconds_limit = std::stod(argv[++i]);
            else if (arg == "--capture" && i + 1 < argc)
                capture = std::filesystem::absolute(argv[++i]);
            else
                throw std::runtime_error(
                    "Usage: sandbox [--debug-ui] [--lod-debug] [--validation] [--frames N] [--seconds N] "
                    "[--capture file.png] [--rdc path] [--world] "
                    "[--seed N] [--render-distance 1..64] [--profile-world file.csv] "
                    "[--profile-origin columnX columnZ] [--profile-gpu file.csv] "
                    "[--profile-view height yaw pitch hour] [--profile-samples N] "
                    "[--capture-shadow-maps directory]");
        }
        if (render_distance < 1 || render_distance > 64)
            throw std::runtime_error("Render distance must be 1..64 columns.");
        if (!shadow_capture.empty() && gpu_profile.empty())
            throw std::runtime_error("--capture-shadow-maps requires --profile-gpu.");
        if (!gpu_profile.empty()) {
            start_world = true;
            if (seconds_limit <= 0)
                seconds_limit = 120;
            if (profile_samples == 0 || profile_samples > 100000)
                throw std::runtime_error("GPU profile samples must be 1..100000.");
        }
        if (profile_view) {
            const auto& v = *profile_view;
            if (gpu_profile.empty() ||
                !std::all_of(v.begin(), v.end(), [](double x) { return std::isfinite(x); }) || v[0] < -1024 ||
                v[0] > 4096 || std::abs(v[1]) > 360 || std::abs(v[2]) > 89 || v[3] < 0 || v[3] >= 24)
                throw std::runtime_error(
                    "Profile view requires --profile-gpu and valid height/yaw/pitch/hour.");
        }
        if (!capture.empty() && !frame_limit && gpu_profile.empty())
            frame_limit = 100;
        if (!rdc_path.empty() && !frame_limit)
            frame_limit = 100;
        if (!world_profile.empty())
            sandbox::profiling::start();
        else if (gpu_profile.empty() && profile_origin != sandbox::ColumnKey{})
            throw std::runtime_error("--profile-origin requires --profile-world or --profile-gpu");
        SdlLifetime sdl;
        RenderDocCapture renderdoc(rdc_path);
        const char* base = SDL_GetBasePath();
        if (!base)
            throw std::runtime_error(SDL_GetError());
        const auto screenshot_directory =
            std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(base))) / "screenshots";
        std::filesystem::current_path(screenshot_directory.parent_path());
        const auto settings_path = screenshot_directory.parent_path() / "settings.json";
        std::string settings_notice;
        sandbox::GameSettings saved_settings;
        try {
            saved_settings = sandbox::load_game_settings(settings_path);
            if (!distance_override)
                render_distance = saved_settings.render_distance;
        } catch (const std::exception& error) {
            settings_notice = "저장값을 읽지 못해 기본 설정을 사용해요. 실행 옵션의 렌더 거리는 우선해요.";
            std::cerr << "Cannot load game settings: " << error.what() << '\n';
        }
        const auto generation_path = screenshot_directory.parent_path() / "worldgen.json";
        sandbox::GenerationConfig generation;
        std::string generation_notice;
        try {
            generation =
                sandbox::load_generation_config(std::filesystem::exists(generation_path)
                                                    ? generation_path
                                                    : std::filesystem::path("assets/worldgen/default.json"));
        } catch (const std::exception& e) {
            generation_notice = std::string("설정을 불러오지 못해 배포 기본값을 사용합니다: ") + e.what();
            std::cerr << generation_notice << '\n';
            try {
                generation = sandbox::load_generation_config("assets/worldgen/default.json");
            } catch (const std::exception&) {
                generation = sandbox::GenerationConfig{};
            }
        }
        if (seed_override)
            generation.seed = seed;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Sandbox", 1280, 900,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY),
            SDL_DestroyWindow);
        if (!window)
            throw std::runtime_error(SDL_GetError());
        SDL_SetWindowMinimumSize(window.get(), 960, 760);
        sandbox::Renderer renderer;
        if (!gpu_profile.empty())
            renderer.enable_gpu_profile();
        renderer.set_vsync(saved_settings.vsync);
        renderer.initialize(window.get(), validation_requested);
        if (!gpu_profile.empty() && !renderer.gpu_timing_supported())
            throw std::runtime_error("GPU timestamps are unavailable.");
        unsigned ui_errors{}, texture_errors{};
        {
            sandbox::RmlRenderer ui(renderer);
            System system(window.get());
            RmlLifetime rml(system, ui);
            if (!Rml::LoadFontFace("assets/fonts/NotoSansKR.ttf"))
                throw std::runtime_error("Cannot load Korean font.");
            auto* context = Rml::CreateContext(
                "main", {static_cast<int>(renderer.extent.width), static_cast<int>(renderer.extent.height)});
            if (!context)
                throw std::runtime_error("Cannot create UI context.");
            context->SetDensityIndependentPixelRatio(SDL_GetWindowDisplayScale(window.get()));
            MenuActions actions;
            actions.debug = debug_initial;
            actions.play = start_world;
            actions.document = context->LoadDocument("assets/ui/main.rml");
            if (!actions.document)
                throw std::runtime_error("Cannot load game menu.");
            for (const char* id : {"play", "options", "options-back", "quit"})
                actions.document->GetElementById(id)->AddEventListener("click", &actions);
            actions.document->Show();
            auto* hud = context->LoadDocument("assets/ui/world.rml");
            if (!hud)
                throw std::runtime_error("Cannot load world HUD.");
            for (const char* id : {"resume", "pause-options", "settings-back", "return-menu"})
                hud->GetElementById(id)->AddEventListener("click", &actions);
            actions.hud = hud;
            actions.listen_tabs(true);
            auto* notifications = context->LoadDocument("assets/ui/notifications.rml");
            if (!notifications)
                throw std::runtime_error("Cannot load notifications.");
            notifications->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
            auto* screenshot_notice = notifications->GetElementById("screenshot-notice");
            WorldSettings settings;
            settings.hud = hud;
            settings.menu = actions.document;
            settings.renderer = &renderer;
            settings.persisted = saved_settings;
            settings.values = saved_settings;
            settings.values.render_distance = render_distance;
            settings.save_path = settings_path;
            settings.notice = settings_notice;
            settings.refresh();
            settings.listen(true);
            ImGuiLifetime imgui(renderer);
            imgui.initialize(window.get());
            sandbox::GenerationEditor generation_editor(renderer, generation);
            sandbox::GameConsole game_console;
            generation_editor.status(generation_notice);
            std::unique_ptr<sandbox::WorldView> world;
            bool in_world = false, mouse_captured = false;
            const auto capture_mouse = [&](bool capture_input) {
                if (!SDL_SetWindowRelativeMouseMode(window.get(), capture_input))
                    throw std::runtime_error(SDL_GetError());
                mouse_captured = capture_input;
                auto& io = ImGui::GetIO();
                io.ClearInputKeys();
                io.ClearInputMouse();
                if (capture_input)
                    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
                else
                    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            };
            uint32_t previous_image_count = renderer.image_count();
            auto previous = std::chrono::steady_clock::now();
            const auto start = previous;
            auto next_stats_update = start;
            double displayed_frame_ms = 0.0;
            ProcessMemory process_memory;
            unsigned rendered = 0;
            unsigned profile_ready_frames = 0, profile_measured_frames = 0;
            bool capture_done = false;
            bool screenshot_requested = false;
            auto notice_until = start;
            const auto notify_screenshot = [&](const std::string& message) {
                screenshot_notice->SetInnerRML(message);
                screenshot_notice->SetClass("visible", true);
                notice_until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            };
            int cloud_direction = 0;
            double cloud_repeat = -0.3;
            bool cloud_keys_blocked = false;
            const auto adjust_cloud = [&](int delta) {
                if (settings.adjust_cloud(delta))
                    notify_screenshot("구름 양 · " + std::to_string(settings.values.graphics.cloud_coverage) +
                                      "%" + (settings.values.graphics.clouds ? "" : " · 구름 표시 꺼짐"));
            };
            while (actions.running && (!frame_limit || rendered < frame_limit)) {
                sandbox::profiling::Scope frame_measure(sandbox::profiling::Stage::frame);
                ZoneScopedN("Application frame");
                const auto now = std::chrono::steady_clock::now();
                if (seconds_limit > 0 && std::chrono::duration<double>(now - start).count() >= seconds_limit)
                    break;
                const double frame_ms = std::chrono::duration<double, std::milli>(now - previous).count();
                previous = now;
                bool gameplay_input_blocked =
                    actions.paused || actions.generation_open || game_console.is_open() || !in_world;
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    const bool mouse =
                        event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                        event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL;
                    const bool keyboard =
                        event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
                        event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING;
                    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                        (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) && in_world &&
                        actions.play && !actions.paused && !actions.generation_open &&
                        !game_console.is_open()) {
                        game_console.open();
                        gameplay_input_blocked = true;
                        world->reset_movement_input();
                        if (mouse_captured)
                            capture_mouse(false);
                        // Do not feed the opening Enter to the new input field.
                        continue;
                    }
                    if (!(in_world && (actions.paused || mouse_captured) && (mouse || keyboard)))
                        ImGui_ImplSDL3_ProcessEvent(&event);
                    if (game_console.is_open() && (mouse || keyboard)) {
                        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                            event.key.key == SDLK_ESCAPE)
                            game_console.close();
                        // Console input belongs exclusively to ImGui, including hotbar and movement keys.
                        continue;
                    }
                    if (event.type == SDL_EVENT_QUIT)
                        actions.running = false;
                    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                        if (event.key.key == SDLK_ESCAPE) {
                            gameplay_input_blocked = true;
                            if (in_world && actions.play) {
                                if (actions.options)
                                    actions.show_options(false);
                                else
                                    actions.paused = !actions.paused;
                            } else if (actions.options)
                                actions.show_options(false);
                            // Escape only navigates UI; it never exits the application.
                            continue;
                        }
                        if (event.key.key == SDLK_F3)
                            actions.debug = !actions.debug;
                        if (event.key.key == SDLK_F8 && in_world && actions.play && !actions.paused) {
                            actions.generation_open = !actions.generation_open;
                            gameplay_input_blocked = true;
                        }
                        if (event.key.key == SDLK_F2)
                            screenshot_requested = true;
                        if (event.key.key == SDLK_F4 && in_world && actions.play)
                            world->toggle_lod_debug();
                        if (event.key.key == SDLK_F11) {
                            const bool fullscreen =
                                (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) != 0;
                            if (!SDL_SetWindowFullscreen(window.get(), !fullscreen))
                                throw std::runtime_error(SDL_GetError());
                        }
                        if (in_world && actions.play && !actions.paused && !gameplay_input_blocked &&
                            mouse_captured) {
                            if (!cloud_keys_blocked && (event.key.scancode == SDL_SCANCODE_MINUS ||
                                                        event.key.scancode == SDL_SCANCODE_EQUALS)) {
                                const auto* keys = SDL_GetKeyboardState(nullptr);
                                const int direction = event.key.scancode == SDL_SCANCODE_EQUALS ? 1 : -1;
                                if (!(keys[SDL_SCANCODE_MINUS] && keys[SDL_SCANCODE_EQUALS])) {
                                    adjust_cloud(direction);
                                    cloud_direction = direction;
                                    cloud_repeat = -0.3;
                                }
                            }
                            if (event.key.key == SDLK_SPACE)
                                world->press_space(event.key.timestamp);
                            if (event.key.key == SDLK_F5)
                                world->cycle_camera();
                            if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9)
                                world->select_slot(static_cast<int>(event.key.key - SDLK_1));
                            else if (event.key.key == SDLK_0)
                                world->select_slot(9);
                        }
                    }
                    if (event.type == SDL_EVENT_KEY_UP && event.key.key == SDLK_SPACE && world)
                        world->release_space();
                    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && in_world && actions.play) {
                        game_console.close();
                        actions.paused = true;
                        gameplay_input_blocked = true;
                    }
                    if (event.type == SDL_EVENT_MOUSE_MOTION && in_world && actions.play && !actions.paused &&
                        !gameplay_input_blocked && mouse_captured &&
                        (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS)) {
                        world->set_zoom(SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_Z]);
                        world->camera.look(event.motion.xrel, event.motion.yrel);
                    }
                    if (event.type == SDL_EVENT_MOUSE_WHEEL && in_world && actions.play && !actions.paused &&
                        !gameplay_input_blocked && mouse_captured) {
                        world->scroll_hotbar(event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y
                                                                                             : event.wheel.y);
                    }
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && in_world && actions.play &&
                        !actions.paused && !gameplay_input_blocked && mouse_captured &&
                        (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS)) {
                        if (event.button.button == SDL_BUTTON_LEFT)
                            world->interact(false);
                        if (event.button.button == SDL_BUTTON_RIGHT)
                            world->interact(true);
                    }
                    if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                        event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
                        event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
                        renderer.request_resize();
                    const auto& io = ImGui::GetIO();
                    if (!(in_world && !actions.paused && mouse_captured && (mouse || keyboard)) &&
                        !(actions.generation_open && in_world && !actions.paused &&
                          ((mouse && io.WantCaptureMouse) || (keyboard && io.WantCaptureKeyboard))))
                        RmlSDL::InputEventHandler(context, window.get(), event);
                }
                if (!actions.running)
                    break;
                if (actions.play != in_world) {
                    if (actions.play) {
                        if (!world) {
                            world = std::make_unique<sandbox::WorldView>(
                                renderer, generation, settings.values.render_distance,
                                !world_profile.empty() || !gpu_profile.empty()
                                    ? std::optional<sandbox::ColumnKey>(profile_origin)
                                    : std::nullopt);
                            if (lod_debug_initial)
                                world->toggle_lod_debug();
                        }
                        if (profile_view) {
                            const auto& v = *profile_view;
                            world->set_profile_view(v[0], v[1], v[2], v[3]);
                        }
                        if (!gpu_profile.empty())
                            std::cout << "GPU PROFILE VIEW: " << world->camera.position.x << ','
                                      << world->camera.position.y << ',' << world->camera.position.z
                                      << " yaw=" << world->camera.yaw << " pitch=" << world->camera.pitch
                                      << " tick=" << world->day_tick() << '\n';
                        settings.world = world.get();
                        world->camera.field_of_view = float(settings.values.field_of_view);
                        world->set_view_bobbing(settings.values.view_bobbing);
                        world->set_water_settings(settings.values.water);
                        world->set_graphics_settings(settings.values.graphics);
                        actions.document->Hide();
                        hud->Show();
                    } else {
                        actions.paused = false;
                        hud->Hide();
                        actions.show_options(false);
                        actions.document->Show();
                    }
                    actions.generation_open = false;
                    game_console.close();
                    in_world = actions.play;
                }
                // UI requests arrive in the previous frame. Mutate world resources before command recording.
                if (world)
                    game_console.execute_pending(*world);
                if (generation_editor.take_load()) {
                    try {
                        generation_editor.load_draft(sandbox::load_generation_config(generation_path));
                    } catch (const std::exception& e) {
                        generation_editor.status(std::string("확정 파일 불러오기 실패: ") + e.what());
                    }
                }
                if (auto request = generation_editor.take_regeneration()) {
                    try {
                        if (world)
                            world->regenerate(*request);
                        generation = *request;
                        generation_editor.status(
                            "재생성했습니다. 블록 편집을 초기화하고 새 지형을 불러오는 중입니다.");
                        gameplay_input_blocked = true;
                    } catch (const std::exception& e) {
                        generation_editor.status(std::string("재생성 실패: ") + e.what());
                    }
                }
                if (auto request = generation_editor.take_save()) {
                    try {
                        sandbox::save_generation_config(generation_path, *request);
                        generation_editor.saved(*request);
                    } catch (const std::exception& e) {
                        generation_editor.status(std::string("저장 실패: ") + e.what());
                    }
                }
                hud->SetClass("paused", in_world && actions.paused);
                const bool want_capture = in_world && !actions.paused && !actions.generation_open &&
                                          !game_console.is_open() && !frame_limit && seconds_limit <= 0 &&
                                          gpu_profile.empty() &&
                                          (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS);
                const bool capture_changed = want_capture != mouse_captured;
                if (capture_changed)
                    capture_mouse(want_capture);
                if (world)
                    world->set_zoom(want_capture && !gameplay_input_blocked && !capture_changed &&
                                    SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_Z]);
                // Do not apply the opening/resume frame's elapsed time to camera movement.
                if (in_world && !actions.paused && !gameplay_input_blocked && mouse_captured &&
                    !capture_changed && (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) &&
                    !(actions.generation_open && ImGui::GetIO().WantCaptureKeyboard))
                    world->move_player(frame_ms / 1000.0, SDL_GetKeyboardState(nullptr));
                else if (world)
                    world->reset_movement_input();
                // World simulation advances independently of keyboard events and mouse capture.
                if (in_world && !actions.paused)
                    world->advance_fluids(frame_ms / 1000.0);
                const auto* keys = SDL_GetKeyboardState(nullptr);
                const bool cloud_held = keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_EQUALS];
                const bool cloud_allowed = in_world && !actions.paused && !actions.generation_open &&
                                           !gameplay_input_blocked && !capture_changed && mouse_captured &&
                                           (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS);
                if (!cloud_allowed || !cloud_held) {
                    cloud_keys_blocked = cloud_held;
                    cloud_direction = 0;
                    cloud_repeat = -0.3;
                    settings.flush_cloud();
                } else if (!cloud_keys_blocked) {
                    const int direction = int(keys[SDL_SCANCODE_EQUALS]) - int(keys[SDL_SCANCODE_MINUS]);
                    if (direction != cloud_direction || direction == 0) {
                        cloud_direction = direction;
                        cloud_repeat = -0.3;
                    } else {
                        cloud_repeat += std::clamp(frame_ms / 1000.0, 0.0, 0.1);
                        while (cloud_repeat >= 0.05) {
                            adjust_cloud(direction);
                            cloud_repeat -= 0.05;
                        }
                    }
                }
                const bool rdc_frame = renderdoc.api && rendered == 10;
                if (rdc_frame)
                    renderdoc.api->StartFrameCapture(
                        RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(renderer.instance), nullptr);
                if (!renderer.begin_frame()) {
                    if (rdc_frame)
                        renderdoc.api->DiscardFrameCapture(
                            RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(renderer.instance), nullptr);
                    SDL_Delay(10);
                    continue;
                }
                if (renderer.image_count() != previous_image_count) {
                    renderer.wait_idle();
                    generation_editor.release_preview_binding();
                    ImGui_ImplVulkan_Shutdown();
                    imgui.renderer_ready = false;
                    imgui.initialize_renderer();
                    previous_image_count = renderer.image_count();
                }
                generation_editor.prepare_preview();
                if (in_world) {
                    world->prepare();
                    // Published/unloaded columns and edits can change the camera obstruction while idle.
                    world->sync_camera();
                    world->update_target();
                    world->render();
                    if (!gpu_profile.empty()) {
                        const bool ready = world->visible_columns() > 0 && world->pending_columns() == 0 &&
                                           world->pending_lighting() == 0 && world->uploaded_bytes == 0;
                        profile_ready_frames = ready ? profile_ready_frames + 1 : 0;
                        const bool steady = profile_ready_frames > 240;
                        if (steady)
                            ++profile_measured_frames;
                        renderer.gpu_profile_frame(steady, static_cast<uint32_t>(world->visible_columns()),
                                                   world->drawn_chunks, world->triangles);
                    }
                    for (int slot = 1; slot <= static_cast<int>(sandbox::hotbar_blocks.size()); ++slot)
                        hud->GetElementById("block-" + std::to_string(slot))
                            ->SetClass("selected", slot == world->selected_slot() + 1);
                } else
                    renderer.begin_rendering();
                ImGui_ImplVulkan_NewFrame();
                ImGui_ImplSDL3_NewFrame();
                ImGui::NewFrame();
                if (rendered > 0 && now >= next_stats_update) {
                    // Hold the latest single-frame sample for readability; do not average it.
                    displayed_frame_ms = frame_ms;
                    next_stats_update = now + std::chrono::milliseconds(250);
                }
                if (actions.debug) {
                    process_memory.refresh(now);
                    debug_overlay(renderer, ui, displayed_frame_ms, in_world ? world.get() : nullptr,
                                  imgui.debug_font, process_memory);
                }
                if (in_world && world->lod_debug())
                    lod_legend(actions.debug, imgui.debug_font);
                if (actions.generation_open && in_world && !actions.paused)
                    generation_editor.draw(world->config());
                if (in_world && !actions.paused)
                    game_console.draw();
                ImGui::Render();
                {
                    ZoneScopedN("Game UI update");
                    if (now >= notice_until)
                        screenshot_notice->SetClass("visible", false);
                    context->Update();
                }
                ui.draw_calls = 0;
                const bool options_overlay = in_world && actions.paused;
                if (options_overlay) {
                    // Modal game options also cover the development panel and own its input area.
                    ZoneScopedN("Debug UI render");
                    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), renderer.command);
                }
                {
                    ZoneScopedN("Game UI render");
                    context->Render();
                }
                if (!options_overlay) {
                    ZoneScopedN("Debug UI render");
                    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), renderer.command);
                }
                const bool profile_done = !gpu_profile.empty() && profile_measured_frames >= profile_samples;
                const bool capture_now =
                    !capture_done && !capture.empty() &&
                    (profile_done || (gpu_profile.empty() && rendered + 1 == frame_limit));
                std::filesystem::path frame_capture = capture_now ? capture : std::filesystem::path{};
                const bool interactive_capture = screenshot_requested && !capture_now;
                if (interactive_capture) {
                    screenshot_requested = false;
                    try {
                        frame_capture = screenshot_path(screenshot_directory);
                    } catch (const std::exception& error) {
                        std::cerr << "Screenshot path: " << error.what() << '\n';
                        notify_screenshot("스크린샷 저장 실패 · 저장 폴더를 확인해 주세요.");
                    }
                }
                const bool saved = renderer.end_frame(frame_capture);
                if (profile_done && !shadow_capture.empty())
                    world->capture_shadow_maps(shadow_capture);
                if (capture_now && !saved)
                    throw std::runtime_error("Requested capture failed.");
                if (interactive_capture && !frame_capture.empty())
                    notify_screenshot(saved
                                          ? "스크린샷 저장: screenshots/" + frame_capture.filename().string()
                                          : "스크린샷 저장 실패 · 저장 폴더를 확인해 주세요.");
                if (renderer.gpu_timing_supported())
                    TracyPlot("GPU render milliseconds", renderer.gpu_ms());
                if (rdc_frame && !renderdoc.api->EndFrameCapture(
                                     RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(renderer.instance), nullptr))
                    throw std::runtime_error("RenderDoc frame capture failed.");
                capture_done |= capture_now;
                if (profile_done)
                    actions.running = false;
                if (gpu_profile.empty() && !world_profile.empty() && world && world->visible_columns() > 0 &&
                    world->pending_columns() == 0)
                    actions.running = false;
                ++rendered;
                if (!settings.values.vsync && settings.values.fps_limit > 0) {
                    ZoneScopedN("FPS limit wait");
                    const auto deadline =
                        now + std::chrono::nanoseconds(1'000'000'000 / settings.values.fps_limit);
                    const auto remaining = deadline - std::chrono::steady_clock::now();
                    if (remaining > std::chrono::steady_clock::duration::zero())
                        SDL_DelayPrecise(static_cast<Uint64>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count()));
                }
                FrameMark;
            }
            settings.flush_cloud();
            renderer.wait_idle();
            if (!gpu_profile.empty()) {
                renderer.save_gpu_profile(gpu_profile);
                if (profile_measured_frames < profile_samples)
                    throw std::runtime_error("GPU profile ended before enough steady frames were collected.");
            }
            if (world)
                std::cout << "WORLD: columns=" << world->visible_columns()
                          << ", drawn chunks=" << world->drawn_chunks
                          << ", face bytes=" << world->mesh_bytes() << ", LOD tiles=" << world->lod_tiles()
                          << ", LOD generated=" << world->lod_stats().generated
                          << ", LOD pending=" << world->lod_stats().pending
                          << ", LOD CPU=" << world->lod_stats().cpu_bytes
                          << ", LOD evicted=" << world->lod_stats().evicted
                          << ", LOD GPU=" << world->lod_gpu_bytes() << '\n';
            if (!capture.empty() && !capture_done)
                throw std::runtime_error("Capture did not complete.");
            std::cout << "UI: " << rendered << " frames, GPU=" << renderer.gpu_ms() << "ms\n";
            ui_errors = system.errors + system.warnings;
            texture_errors = ui.failed_textures;
            for (const char* id : {"play", "options", "options-back", "quit"})
                actions.document->GetElementById(id)->RemoveEventListener("click", &actions);
            capture_mouse(false);
            for (const char* id : {"resume", "pause-options", "settings-back", "return-menu"})
                hud->GetElementById(id)->RemoveEventListener("click", &actions);
            actions.listen_tabs(false);
            settings.listen(false);
            hud->Close();
            notifications->Close();
            actions.document->Close();
        }
        renderer.wait_idle();
        renderer.shutdown();
        if (!world_profile.empty())
            sandbox::profiling::save(world_profile);
        const auto errors = renderer.validation_errors.load();
        std::cout << "CHECK: Vulkan errors=" << errors << ", warnings=" << renderer.validation_warnings.load()
                  << ", UI issues=" << ui_errors << ", texture failures=" << texture_errors << '\n';
        return errors || ui_errors || texture_errors ? 1 : 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ERROR: %s\n", error.what());
        return 1;
    } catch (...) {
        std::fputs("ERROR: Unknown failure.\n", stderr);
        return 1;
    }
}
