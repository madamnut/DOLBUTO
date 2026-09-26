#pragma once
#include "world/lighting.hpp"
#include <array>
#include <bitset>
#include <condition_variable>
#include <exception>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace sandbox {
class WorldStream {
  public:
    explicit WorldStream(uint32_t seed);
    explicit WorldStream(std::shared_ptr<const TerrainGenerator> generator,
                         std::function<void(Column)> data_ready = {});
    ~WorldStream();
    WorldStream(const WorldStream&) = delete;
    WorldStream& operator=(const WorldStream&) = delete;
    std::vector<ColumnKey> request(ColumnKey centre, int radius);
    std::unique_ptr<BuiltColumn> take_ready();
    std::optional<ColumnKey> next_to_publish() const;
    bool publish(ColumnKey key); // Main thread: acknowledge a fully uploaded nearest column.
    size_t pending() const { return pending_.load(std::memory_order_relaxed); }
    // Immutable base data snapshot. No procedural fallback when neighbours are missing.
    std::optional<ChunkHalo> halo(ChunkKey key) const;
    std::optional<std::array<Column, 9>> lighting_columns(ColumnKey key) const;

  private:
    using Rank = std::tuple<int, int, int>; // Periodic distance squared, canonical X/Z tie break.
    struct DataColumn {
        ColumnKey key;
        Rank priority;
        std::atomic_bool cancelled{};
        std::shared_ptr<const ColumnLight> local_light;
        std::array<std::shared_ptr<const Chunk>, chunks_per_column> chunks;
        double generation_ms{}, lighting_ms{};
    };
    struct RenderColumn {
        ColumnKey key;
        std::atomic_bool cancelled{};
        std::atomic_uint64_t revision{};
        Rank priority;
        bool active{}, delivered{}, published{}, admitted{};
        std::bitset<chunks_per_column> done;
        std::unique_ptr<BuiltColumn> result;
    };
    enum class Kind { column, local_light, connect_light, mesh };
    struct Task {
        Kind kind;
        std::shared_ptr<DataColumn> data;
        std::shared_ptr<RenderColumn> render;
        int cy{};
        std::shared_ptr<const ChunkNeighbours> neighbours;
        std::shared_ptr<const std::array<LightInput, 9>> lights;
        uint64_t revision{};
        double queued_at{};
        bool cancelled() const {
            return (data && data->cancelled) ||
                   (render &&
                    (render->cancelled || render->revision.load(std::memory_order_relaxed) != revision));
        }
        Rank priority() const { return data ? data->priority : render->priority; }
    };
    struct TaskLater {
        bool operator()(const Task& a, const Task& b) const {
            // Complete a more advanced stage first when both serve the same nearest column.
            const auto ak = a.data ? a.data->key : a.render->key;
            const auto bk = b.data ? b.data->key : b.render->key;
            return std::tuple{a.priority(), -static_cast<int>(a.kind), ak.x, ak.z, a.cy} >
                   std::tuple{b.priority(), -static_cast<int>(b.kind), bk.x, bk.z, b.cy};
        }
    };
    std::shared_ptr<const TerrainGenerator> generator_;
    std::function<void(Column)> data_ready_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_{};
    std::exception_ptr failure_;
    std::unordered_map<ColumnKey, std::shared_ptr<DataColumn>, ColumnHash> data_;
    std::unordered_map<ColumnKey, std::shared_ptr<RenderColumn>, ColumnHash> requested_;
    // request() is called only by the main thread; these describe its last requested disk.
    std::optional<ColumnKey> centre_;
    int radius_{};
    std::unordered_map<ColumnKey, unsigned, ColumnHash> references_;
    ColumnKey priority_centre_{}; // Guarded by mutex_, unlike the main-thread request bookkeeping.
    std::vector<Task> tasks_;     // Min-priority heap; rebuilt only when the loading centre/radius changes.
    std::map<Rank, std::shared_ptr<RenderColumn>> delivery_, publication_;
    std::array<std::shared_ptr<RenderColumn>, 8> window_{};
    bool schedule_dirty_{};
    size_t active_{};
    std::atomic_size_t pending_{};
    std::vector<std::jthread> workers_;
    std::optional<ChunkNeighbours> neighbours_locked(ChunkKey key) const;
    void schedule_locked(const std::shared_ptr<RenderColumn>& column);
    void pump_locked();
    Rank rank_locked(ColumnKey key) const;
    void rank_data_locked(DataColumn& data) const;
    void enqueue_locked(Task task);
    void refresh_window_locked();
    void work(std::stop_token stop);
};
} // namespace sandbox
