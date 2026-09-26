#pragma once
#include <array>
#include <deque>
#include <optional>
#include <string>
#include <vector>

struct ImGuiInputTextCallbackData;
namespace sandbox {
class WorldView;
class GameConsole {
  public:
    bool is_open() const { return open_; }
    void open();
    void close();
    void draw();
    void execute_pending(WorldView& world); // Before frame recording.

  private:
    bool open_{}, focus_{}, scroll_{};
    std::array<char, 512> input_{};
    std::deque<std::string> lines_;
    std::vector<std::string> history_;
    int history_position_{-1};
    std::string draft_;
    std::optional<std::string> pending_;
    void append(std::string line);
    static int history_callback(ImGuiInputTextCallbackData* data);
};
} // namespace sandbox
