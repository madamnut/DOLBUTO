#include "ui/game_console.hpp"
#include "world/world_view.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <sstream>
#include <string_view>

namespace sandbox {
namespace {
template <typename T> bool number(std::string_view text, T& value) {
    if (text.empty())
        return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
} // namespace
void GameConsole::open() {
    open_ = focus_ = scroll_ = true;
    input_.fill(0);
    history_position_ = -1;
    draft_.clear();
    if (lines_.empty())
        append("/help · 명령 도움말 | ↑/↓ · 이전 명령 | Esc 또는 빈 입력에서 Enter · 닫기");
}
void GameConsole::close() {
    open_ = false;
    focus_ = false;
}
void GameConsole::append(std::string line) {
    lines_.push_back(std::move(line));
    if (lines_.size() > 200)
        lines_.pop_front();
    scroll_ = true;
}
int GameConsole::history_callback(ImGuiInputTextCallbackData* data) {
    auto& self = *static_cast<GameConsole*>(data->UserData);
    if (self.history_.empty())
        return 0;
    if (data->EventKey == ImGuiKey_UpArrow) {
        if (self.history_position_ < 0) {
            self.draft_ = data->Buf;
            self.history_position_ = static_cast<int>(self.history_.size()) - 1;
        } else if (self.history_position_ > 0)
            --self.history_position_;
    } else if (data->EventKey == ImGuiKey_DownArrow) {
        if (self.history_position_ < 0)
            return 0;
        if (++self.history_position_ == static_cast<int>(self.history_.size()))
            self.history_position_ = -1;
    }
    const auto& text = self.history_position_ < 0 ? self.draft_ : self.history_[self.history_position_];
    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, text.c_str());
    return 0;
}
void GameConsole::draw() {
    if (!open_)
        return;
    const auto* viewport = ImGui::GetMainViewport();
    const auto size = viewport->Size;
    // The input sits at 2/3 of screen height; history grows above it, away from the hotbar.
    const float width = std::max(1.0f, std::min(1000.0f, size.x - 32.0f));
    const float height = std::max(1.0f, std::min(320.0f, size.y * 0.5f));
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + size.x * 0.5f, viewport->Pos.y + size.y * (2.0f / 3.0f)),
                            ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::SetNextWindowBgAlpha(0.94f);
    if (focus_)
        ImGui::SetNextWindowFocus();
    constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("명령 콘솔###game-console", nullptr, flags)) {
        ImGui::TextUnformatted("명령 콘솔 · /help");
        ImGui::Separator();
        const float footer = ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild("history", ImVec2(0, -footer))) {
            for (const auto& line : lines_) {
                ImGui::PushTextWrapPos(0);
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopTextWrapPos();
            }
            if (scroll_)
                ImGui::SetScrollHereY(1.0f);
            scroll_ = false;
        }
        ImGui::EndChild();
        if (focus_) {
            ImGui::SetKeyboardFocusHere();
            focus_ = false;
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##command", "명령 입력 · Enter 실행", input_.data(), input_.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                         ImGuiInputTextFlags_CallbackHistory,
                                     history_callback, this)) {
            std::string command(input_.data());
            const auto first = command.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                close();
            else {
                command = command.substr(first, command.find_last_not_of(" \t\r\n") - first + 1);
                pending_ = command;
                if (history_.empty() || history_.back() != command) {
                    if (history_.size() == 100)
                        history_.erase(history_.begin());
                    history_.push_back(command);
                }
                input_.fill(0);
                history_position_ = -1;
                draft_.clear();
                focus_ = true;
            }
        }
    }
    ImGui::End();
}
void GameConsole::execute_pending(WorldView& world) {
    if (!pending_)
        return;
    const std::string command = std::move(*pending_);
    pending_.reset();
    append("> " + command);
    std::istringstream stream(command);
    std::vector<std::string> words;
    for (std::string word; stream >> word;)
        words.push_back(std::move(word));
    if (words.empty())
        return;
    const auto& name = words[0];
    if (name == "/help" && words.size() == 1) {
        append("/help — 명령 목록");
        append("/time [HH:MM] — 시각 조회/변경 (00:00~23:59, 날짜 유지)");
        append("/tp [x y z] — 발 좌표 조회/이동 (소수 가능, X/Z 순환, Y 0~8192)");
        append("/gamemode [walk|fly] — 이동 모드 조회/변경");
        return;
    }
    if (name == "/time") {
        if (words.size() == 2) {
            const auto colon = words[1].find(':');
            unsigned hour{}, minute{};
            const std::string_view time(words[1]);
            if (colon == std::string::npos || !number(time.substr(0, colon), hour) ||
                !number(time.substr(colon + 1), minute) || !world.set_time(hour, minute)) {
                append("시각 오류: /time 09:00 · 00:00~23:59 범위로 입력하세요.");
                return;
            }
        } else if (words.size() != 1) {
            append("사용법: /time 또는 /time HH:MM");
            return;
        }
        const auto date = world.date();
        char message[160];
        std::snprintf(message, sizeof(message), "%llu년 %02u월 %02u일 %02u:%02u · %llu일차",
                      static_cast<unsigned long long>(date.year), date.month, date.day, date.hour,
                      date.minute, static_cast<unsigned long long>(date.day_number));
        append(message);
        return;
    }
    if (name == "/tp") {
        if (words.size() == 4) {
            glm::dvec3 position{};
            if (!number(words[1], position.x) || !number(words[2], position.y) ||
                !number(words[3], position.z) || !world.teleport(position)) {
                append("좌표 오류: /tp x y z · X/Z ±1,000,000,000, Y 0~8192의 유한한 숫자만 허용합니다.");
                return;
            }
        } else if (words.size() != 1) {
            append("사용법: /tp 또는 /tp x y z (플레이어 발 기준, 절대 좌표)");
            return;
        }
        const auto position = world.player().position;
        char message[160];
        std::snprintf(message, sizeof(message), "발 좌표: %.3f, %.3f, %.3f", position.x, position.y,
                      position.z);
        append(message);
        return;
    }
    if (name == "/gamemode") {
        if (words.size() == 2 && (words[1] == "walk" || words[1] == "fly"))
            world.set_movement_mode(words[1] == "walk" ? MovementMode::walk : MovementMode::fly);
        else if (words.size() != 1) {
            append("사용법: /gamemode walk 또는 /gamemode fly");
            return;
        }
        append(world.player().movement_mode == MovementMode::walk ? "이동 모드: 걷기" : "이동 모드: 플라이");
        return;
    }
    append("알 수 없는 명령 또는 잘못된 인수입니다. /help로 사용법을 확인하세요.");
}
} // namespace sandbox
