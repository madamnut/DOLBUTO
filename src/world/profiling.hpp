#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace sandbox::profiling {
enum class Stage {
    generation,
    profile,
    warp,
    noise2d,
    spline,
    surface_search,
    surface_rules,
    blocks,
    noise3d,
    noise3d_surface,
    local_light,
    connect_light,
    halo,
    mesh,
    packing,
    prepare,
    upload_cpu,
    scheduler,
    request,
    task_wait,
    mesh_ready,
    delivered,
    published,
    gpu_retired,
    gpu_upload,
    gpu_frame,
    frame,
    light_init,
    light_seeds,
    light_flood,
    light_halo,
    boundary_scan,
    boundary_flood,
    count
};
inline constexpr size_t count = static_cast<size_t>(Stage::count);
struct Metric {
    double exclusive{}, inclusive{};
    uint64_t calls{}, samples{};
};
struct Record {
    Stage root;
    int x{}, y{-1}, z{};
    double start{}, end{};
    std::array<Metric, count> metrics{};
};
// Enabled once, before workers start; disabled only after they join. No timer reads when disabled.
extern bool enabled;
double now();
bool within(Stage stage);
void start();
void save(const std::filesystem::path& path);
void event(Stage stage, int x = 0, int z = 0, double begin = -1, double duration = -1, int y = -1);
class Scope {
  public:
    explicit Scope(Stage stage, int x = 0, int z = 0, int y = -1, uint64_t samples = 0);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

  private:
    friend bool within(Stage stage);
    Stage stage_;
    Scope* parent_{};
    Record* record_{};
    std::optional<Record> owned_;
    double start_{}, child_{};
    uint64_t samples_{};
};
} // namespace sandbox::profiling
