#include "world/profiling.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace sandbox::profiling {
bool enabled = false;
namespace {
using Clock = std::chrono::steady_clock;
Clock::time_point epoch;
thread_local Scope* active{};
std::mutex mutex;
struct Row {
    Stage root, stage;
    int x, y, z;
    double start, end;
    Metric metric;
};
std::vector<Row> records;
uint64_t dropped{};
constexpr const char* names[]{
    "generation",    "profile",       "warp",          "noise2d",         "spline",      "surface_search",
    "surface_rules", "blocks",        "noise3d",       "noise3d_surface", "local_light", "connect_light",
    "halo",          "mesh",          "packing",       "prepare",         "upload_cpu",  "scheduler",
    "request",       "task_wait",     "mesh_ready",    "delivered",       "published",   "gpu_retired",
    "gpu_upload",    "gpu_frame",     "frame",         "light_init",      "light_seeds", "light_flood",
    "light_halo",    "boundary_scan", "boundary_flood"};
static_assert(std::size(names) == count);
void append(Record&& record) {
    std::lock_guard lock(mutex);
    for (size_t i = 0; i < count; ++i) {
        if (!record.metrics[i].calls)
            continue;
        if (records.size() < 2000000)
            records.push_back({record.root, static_cast<Stage>(i), record.x, record.y, record.z, record.start,
                               record.end, record.metrics[i]});
        else
            ++dropped;
    }
}
} // namespace
bool within(Stage stage) {
    for (auto* s = active; s; s = s->parent_)
        if (s->stage_ == stage)
            return true;
    return false;
}
double now() { return enabled ? std::chrono::duration<double, std::milli>(Clock::now() - epoch).count() : 0; }
void start() {
    epoch = Clock::now();
    records.clear();
    records.reserve(262144);
    dropped = 0;
    enabled = true;
}
Scope::Scope(Stage stage, int x, int z, int y, uint64_t samples) : stage_(stage), samples_(samples) {
    if (!enabled)
        return;
    parent_ = active;
    if (parent_)
        record_ = parent_->record_;
    else {
        owned_.emplace();
        record_ = &*owned_;
        record_->root = stage;
        record_->x = x;
        record_->z = z;
        record_->y = y;
    }
    start_ = now();
    if (!parent_)
        record_->start = start_;
    active = this;
}
Scope::~Scope() {
    if (!record_)
        return;
    const double finish = now(), elapsed = finish - start_;
    auto& m = record_->metrics[static_cast<size_t>(stage_)];
    m.inclusive += elapsed;
    m.exclusive += elapsed - child_;
    ++m.calls;
    m.samples += samples_;
    active = parent_;
    if (parent_)
        parent_->child_ += elapsed;
    else {
        record_->end = finish;
        append(std::move(*owned_));
    }
}
void event(Stage stage, int x, int z, double begin, double duration, int y) {
    if (!enabled)
        return;
    Record r{};
    r.root = stage;
    r.x = x;
    r.z = z;
    r.y = y;
    r.end = now();
    r.start = begin < 0 ? r.end : begin;
    auto& m = r.metrics[static_cast<size_t>(stage)];
    m.calls = 1;
    m.inclusive = m.exclusive = duration < 0 ? r.end - r.start : duration;
    append(std::move(r));
}
void save(const std::filesystem::path& path) {
    // Caller has joined workers. CSV writes are outside all measured work.
    std::ofstream file(path);
    if (!file)
        throw std::runtime_error("Cannot write world profile");
    file << "root,x,y,z,start_ms,end_ms,stage,exclusive_ms,inclusive_ms,calls,samples\n"
         << std::setprecision(12);
    for (const auto& r : records) {
        const auto& m = r.metric;
        file << names[static_cast<size_t>(r.root)] << ',' << r.x << ',' << r.y << ',' << r.z << ',' << r.start
             << ',' << r.end << ',' << names[static_cast<size_t>(r.stage)] << ',' << m.exclusive << ','
             << m.inclusive << ',' << m.calls << ',' << m.samples << '\n';
    }
    if (!file || dropped)
        throw std::runtime_error("Incomplete world profile (write error or record cap)");
}
} // namespace sandbox::profiling
