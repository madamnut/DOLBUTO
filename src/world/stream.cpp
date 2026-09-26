#include "world/stream.hpp"
#include "world/profiling.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sandbox {
WorldStream::WorldStream(uint32_t seed)
    : WorldStream([seed] {
          GenerationConfig config;
          config.seed = seed;
          return std::make_shared<TerrainGenerator>(config);
      }()) {}
WorldStream::WorldStream(std::shared_ptr<const TerrainGenerator> generator,
                         std::function<void(Column)> data_ready)
    : generator_(std::move(generator)), data_ready_(std::move(data_ready)) {
    if (!generator_)
        throw std::invalid_argument("Missing terrain generator.");
    const auto count = std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u);
    try {
        for (unsigned i = 0; i < count; ++i)
            workers_.emplace_back([this](std::stop_token stop) { work(stop); });
    } catch (...) {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        workers_.clear();
        throw;
    }
}
WorldStream::~WorldStream() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        for (auto& [key, data] : data_)
            data->cancelled = true;
        for (auto& [key, column] : requested_)
            column->cancelled = true;
    }
    wake_.notify_all();
    workers_.clear();
}
std::vector<ColumnKey> WorldStream::request(ColumnKey centre, int radius) {
    centre = canonical(centre);
    if (radius < 1 || radius > 64)
        throw std::runtime_error("Render distance must be 1..64 columns.");
    if (centre_ && *centre_ == centre && radius_ == radius)
        return {};
    std::vector<ColumnKey> entering, leaving;
    const int old_x = centre_ ? column_delta(centre_->x, centre.x) : 0;
    const int old_z = centre_ ? column_delta(centre_->z, centre.z) : 0;
    // Subtract scanline intervals. A one-column move visits O(radius) boundary cells,
    // rather than rebuilding the complete disk. Pending work is reprioritized separately.
    const auto interval = [](int cx, int cz, int r, int z) -> std::pair<int, int> {
        const int dy = z - cz;
        if (std::abs(dy) > r)
            return {1, 0};
        int width = static_cast<int>(std::sqrt(double(r * r - dy * dy)));
        while ((width + 1) * (width + 1) <= r * r - dy * dy)
            ++width;
        while (width * width > r * r - dy * dy)
            --width;
        return {cx - width, cx + width};
    };
    const auto append_difference = [&](std::pair<int, int> a, std::pair<int, int> b, int z,
                                       std::vector<ColumnKey>& out) {
        if (a.first > a.second)
            return;
        if (b.first > b.second || b.second < a.first || b.first > a.second) {
            for (int x = a.first; x <= a.second; ++x)
                out.push_back(canonical(ColumnKey{centre.x + x, centre.z + z}));
            return;
        }
        for (int x = a.first; x < b.first; ++x)
            out.push_back(canonical(ColumnKey{centre.x + x, centre.z + z}));
        for (int x = std::max(a.first, b.second + 1); x <= a.second; ++x)
            out.push_back(canonical(ColumnKey{centre.x + x, centre.z + z}));
    };
    // Separate disjoint Z ranges avoid scanning the gap after a large teleport.
    const auto visit_rows = [&](int first, int last, bool old_only) {
        for (int z = first; z <= last; ++z) {
            if (old_only && z >= -radius && z <= radius)
                continue;
            const auto current = interval(0, 0, radius, z);
            const auto previous = centre_ ? interval(old_x, old_z, radius_, z) : std::pair<int, int>{1, 0};
            append_difference(current, previous, z, entering);
            append_difference(previous, current, z, leaving);
        }
    };
    visit_rows(-radius, radius, false);
    if (centre_)
        visit_rows(old_z - radius_, old_z + radius_, true);
    std::unordered_map<ColumnKey, int, ColumnHash> deltas;
    for (const auto key : entering)
        for (int z = -1; z <= 1; ++z)
            for (int x = -1; x <= 1; ++x)
                ++deltas[canonical(ColumnKey{key.x + x, key.z + z})];
    for (const auto key : leaving)
        for (int z = -1; z <= 1; ++z)
            for (int x = -1; x <= 1; ++x)
                --deltas[canonical(ColumnKey{key.x + x, key.z + z})];
    std::vector<ColumnKey> add_data, remove_data;
    for (const auto& [key, delta] : deltas) {
        if (!delta)
            continue;
        auto it = references_.find(key);
        const int before = it == references_.end() ? 0 : static_cast<int>(it->second);
        const int after = before + delta;
        if (after < 0)
            throw std::logic_error("Negative column dependency count.");
        if (!before && after)
            add_data.push_back(key);
        if (before && !after)
            remove_data.push_back(key);
        if (after)
            references_[key] = static_cast<unsigned>(after);
        else
            references_.erase(key);
    }
    const auto distance = [centre](ColumnKey key) {
        const int64_t x = column_delta(key.x, centre.x), z = column_delta(key.z, centre.z);
        return x * x + z * z;
    };
    std::sort(entering.begin(), entering.end(), [&](auto a, auto b) { return distance(a) < distance(b); });
    std::sort(add_data.begin(), add_data.end(), [&](auto a, auto b) { return distance(a) < distance(b); });
    // Allocate new state and release removed state outside the scheduler lock.
    std::vector<std::shared_ptr<DataColumn>> additions, retired_data;
    std::vector<std::shared_ptr<RenderColumn>> renders, retired_renders;
    std::vector<std::unique_ptr<BuiltColumn>> retired_results;
    for (auto key : add_data) {
        auto data = std::make_shared<DataColumn>();
        data->key = key;
        additions.push_back(std::move(data));
    }
    for (auto key : entering) {
        auto render = std::make_shared<RenderColumn>();
        render->key = key;
        renders.push_back(std::move(render));
    }
    retired_data.reserve(remove_data.size());
    retired_renders.reserve(leaving.size());
    {
        std::lock_guard lock(mutex_);
        if (failure_)
            std::rethrow_exception(failure_);
        priority_centre_ = centre;
        for (auto key : leaving) {
            auto it = requested_.find(key);
            if (it == requested_.end())
                continue;
            auto column = std::move(it->second);
            column->cancelled = true;
            if (column->active) {
                --active_;
                column->active = false;
            }
            if (!column->published)
                --pending_;
            delivery_.erase(column->priority);
            publication_.erase(column->priority);
            retired_renders.push_back(std::move(column));
            requested_.erase(it);
        }
        for (auto key : remove_data) {
            auto it = data_.find(key);
            if (it == data_.end())
                continue;
            it->second->cancelled = true;
            retired_data.push_back(std::move(it->second));
            data_.erase(it);
        }
        for (const auto& data : additions) {
            data_.emplace(data->key, data);
            tasks_.push_back({Kind::column, data, {}, {}, {}, {}});
            tasks_.back().queued_at = profiling::now();
        }
        for (const auto& column : renders) {
            profiling::event(profiling::Stage::request, column->key.x, column->key.z);
            requested_.emplace(column->key, column);
            ++pending_;
            column->priority = rank_locked(column->key);
            publication_.emplace(column->priority, column);
        }
        // Only unpublished columns need new ranks; already visible columns stay out.
        std::map<Rank, std::shared_ptr<RenderColumn>> next_delivery, next_publication;
        for (const auto& [old_rank, column] : publication_) {
            column->priority = rank_locked(column->key);
            next_publication.emplace(column->priority, column);
            if (!column->delivered)
                next_delivery.emplace(column->priority, column);
        }
        delivery_.swap(next_delivery);
        publication_.swap(next_publication);
        refresh_window_locked();
        // Revoke far chains as well as completed results. Otherwise eight old active
        // columns could block admission of the new nearest columns. A worker may finish
        // its current unit, but revision checks keep it out of any replacement chain.
        for (const auto& [rank, column] : delivery_) {
            if (!column->admitted && (column->active || column->done.all())) {
                ++column->revision;
                if (column->active) {
                    column->active = false;
                    --active_;
                }
                retired_results.push_back(std::move(column->result));
                column->done.reset();
            }
        }
        std::erase_if(tasks_, [](const Task& task) { return task.cancelled(); });
        for (auto& task : tasks_)
            if (task.data)
                rank_data_locked(*task.data);
        std::make_heap(tasks_.begin(), tasks_.end(), TaskLater{});
    }
    centre_ = centre;
    radius_ = radius;
    wake_.notify_all();
    return leaving;
}
std::optional<ChunkNeighbours> WorldStream::neighbours_locked(ChunkKey key) const {
    ChunkNeighbours neighbours{};
    for (int z = -1; z <= 1; ++z)
        for (int x = -1; x <= 1; ++x) {
            const auto it = data_.find(canonical(ColumnKey{key.x + x, key.z + z}));
            if (it == data_.end())
                return {};
            for (int y = -1; y <= 1; ++y) {
                const int cy = key.y + y;
                if (cy < 0 || cy >= chunks_per_column)
                    continue;
                const auto& chunk = it->second->chunks[cy];
                if (!chunk)
                    return {};
                neighbours[x + 1 + 3 * (y + 1 + 3 * (z + 1))] = chunk;
            }
        }
    return neighbours;
}
WorldStream::Rank WorldStream::rank_locked(ColumnKey key) const {
    const int x = column_delta(key.x, priority_centre_.x), z = column_delta(key.z, priority_centre_.z);
    return {x * x + z * z, key.x, key.z};
}
void WorldStream::rank_data_locked(DataColumn& data) const {
    // A halo is as urgent as its nearest unfinished consumer, including the outer data ring.
    data.priority = {std::numeric_limits<int>::max(), data.key.x, data.key.z};
    for (int z = -1; z <= 1; ++z)
        for (int x = -1; x <= 1; ++x) {
            const auto it = requested_.find(canonical(ColumnKey{data.key.x + x, data.key.z + z}));
            if (it != requested_.end() && !it->second->delivered)
                data.priority = std::min(data.priority, it->second->priority);
        }
}
void WorldStream::enqueue_locked(Task task) {
    if (task.data)
        rank_data_locked(*task.data);
    if (task.render)
        task.revision = task.render->revision.load(std::memory_order_relaxed);
    task.queued_at = profiling::now();
    tasks_.push_back(std::move(task));
    std::push_heap(tasks_.begin(), tasks_.end(), TaskLater{});
}
void WorldStream::refresh_window_locked() {
    for (auto& column : window_) {
        if (column)
            column->admitted = false;
        column.reset();
    }
    size_t i = 0;
    for (const auto& [rank, column] : delivery_) {
        if (i == window_.size())
            break;
        column->admitted = true;
        window_[i++] = column;
    }
    schedule_dirty_ = true;
}
void WorldStream::schedule_locked(const std::shared_ptr<RenderColumn>& column) {
    if (column->cancelled || column->delivered || column->active || !column->admitted || column->done.all())
        return;
    std::array<LightInput, 9> inputs;
    for (int z = -1; z <= 1; ++z)
        for (int x = -1; x <= 1; ++x) {
            const auto key = canonical(ColumnKey{column->key.x + x, column->key.z + z});
            const auto it = data_.find(key);
            if (it == data_.end() || !it->second->local_light)
                return;
            auto& input = inputs[x + 1 + 3 * (z + 1)];
            input.blocks.key = key;
            input.light = it->second->local_light;
            for (int y = 0; y < chunks_per_column; ++y)
                input.blocks.chunks[y] = *it->second->chunks[y];
        }
    column->result = std::make_unique<BuiltColumn>();
    column->result->data = inputs[4].blocks;
    column->result->generation_ms = data_.at(column->key)->generation_ms;
    column->result->lighting_ms = data_.at(column->key)->lighting_ms;
    column->active = true;
    ++active_;
    enqueue_locked({Kind::connect_light,
                    {},
                    column,
                    {},
                    {},
                    std::make_shared<std::array<LightInput, 9>>(std::move(inputs))});
}
void WorldStream::pump_locked() {
    if (!schedule_dirty_)
        return;
    schedule_dirty_ = false;
    // Reserve the window for the nearest undelivered columns even while their data is
    // unfinished. Far completed columns cannot consume every slot and block the head.
    for (const auto& column : window_) {
        if (active_ >= window_.size())
            break;
        if (column)
            schedule_locked(column);
    }
}
std::unique_ptr<BuiltColumn> WorldStream::take_ready() {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return {};
    if (failure_)
        std::rethrow_exception(failure_);
    if (delivery_.empty())
        return {};
    auto it = delivery_.begin();
    const auto column = it->second;
    if (!column->done.all())
        return {}; // Never skip an unfinished nearer column.
    profiling::event(profiling::Stage::delivered, column->key.x, column->key.z);
    column->delivered = true;
    auto result = std::move(column->result);
    delivery_.erase(it);
    refresh_window_locked();
    wake_.notify_all();
    return result;
}
std::optional<ColumnKey> WorldStream::next_to_publish() const {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return {};
    if (failure_)
        std::rethrow_exception(failure_);
    return publication_.empty() ? std::nullopt : std::optional{publication_.begin()->second->key};
}
bool WorldStream::publish(ColumnKey key) {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    if (failure_)
        std::rethrow_exception(failure_);
    if (publication_.empty())
        return false;
    const auto it = publication_.begin();
    auto& column = *it->second;
    if (column.key != key || !column.delivered)
        return false;
    profiling::event(profiling::Stage::published, key.x, key.z);
    column.published = true;
    --pending_;
    publication_.erase(it);
    return true;
}
std::optional<ChunkHalo> WorldStream::halo(ChunkKey key) const {
    std::optional<ChunkNeighbours> neighbours;
    {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock())
            return {};
        if (failure_)
            std::rethrow_exception(failure_);
        neighbours = neighbours_locked(key);
    }
    if (!neighbours)
        return {};
    return make_halo(key, *neighbours);
}
std::optional<std::array<Column, 9>> WorldStream::lighting_columns(ColumnKey key) const {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return {};
    if (failure_)
        std::rethrow_exception(failure_);
    std::array<Column, 9> result;
    for (int z = -1; z <= 1; ++z)
        for (int x = -1; x <= 1; ++x) {
            const auto ck = canonical(ColumnKey{key.x + x, key.z + z});
            auto it = data_.find(ck);
            if (it == data_.end())
                return {};
            auto& column = result[x + 1 + 3 * (z + 1)];
            column.key = ck;
            for (int y = 0; y < chunks_per_column; ++y) {
                if (!it->second->chunks[y])
                    return {};
                column.chunks[y] = *it->second->chunks[y];
            }
        }
    return result;
}
void WorldStream::work(std::stop_token stop) {
    using Clock = std::chrono::steady_clock;
    try {
        while (true) {
            Task task;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return stopping_ || !tasks_.empty() || schedule_dirty_; });
                if (stopping_)
                    return;
                profiling::Scope scheduling(profiling::Stage::scheduler);
                pump_locked();
                if (tasks_.empty())
                    continue;
                std::pop_heap(tasks_.begin(), tasks_.end(), TaskLater{});
                task = std::move(tasks_.back());
                tasks_.pop_back();
                if (task.cancelled())
                    continue;
            }
            const auto measure_key = task.data ? task.data->key : task.render->key;
            profiling::event(profiling::Stage::task_wait, measure_key.x, measure_key.z, task.queued_at, -1,
                             static_cast<int>(task.kind));
            const auto measure_stage = task.kind == Kind::column          ? profiling::Stage::generation
                                       : task.kind == Kind::local_light   ? profiling::Stage::local_light
                                       : task.kind == Kind::connect_light ? profiling::Stage::connect_light
                                                                          : profiling::Stage::mesh;
            const auto start = Clock::now();
            std::array<std::shared_ptr<const Chunk>, chunks_per_column> generated;
            std::shared_ptr<const ColumnLight> light;
            ChunkMesh mesh;
            {
                profiling::Scope measure(measure_stage, measure_key.x, measure_key.z, task.cy);
                if (task.kind == Kind::column) {
                    const auto tile = generate_tile(task.data->key, *generator_);
                    for (int cy = 0; cy < chunks_per_column; ++cy) {
                        if (stop.stop_requested())
                            return;
                        if (task.cancelled())
                            break;
                        generated[cy] = std::make_shared<Chunk>(
                            generate_chunk({task.data->key.x, cy, task.data->key.z}, *generator_, &tile));
                    }
                    if (data_ready_ && !task.cancelled()) {
                        Column column;
                        column.key = task.data->key;
                        for (int cy = 0; cy < chunks_per_column; ++cy)
                            column.chunks[cy] = *generated[cy];
                        data_ready_(std::move(column));
                    }
                } else if (task.kind == Kind::local_light) {
                    Column column;
                    column.key = task.data->key;
                    for (int cy = 0; cy < chunks_per_column; ++cy)
                        column.chunks[cy] = *task.data->chunks[cy];
                    light = local_column_light(column, stop);
                } else if (task.kind == Kind::connect_light) {
                    light = connect_column_light(task.render->key, *task.lights, stop);
                } else {
                    const auto halo = [&] {
                        profiling::Scope measure(profiling::Stage::halo);
                        return make_halo({task.render->key.x, task.cy, task.render->key.z}, *task.neighbours);
                    }();
                    mesh = mesh_chunk(halo);
                }
            }
            if (stop.stop_requested())
                return;
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            std::unique_ptr<BuiltColumn> retired_result;
            {
                profiling::Scope scheduling(profiling::Stage::scheduler, measure_key.x, measure_key.z);
                std::lock_guard lock(mutex_);
                if (stopping_)
                    return;
                if (task.cancelled())
                    continue;
                if (task.kind == Kind::column) {
                    task.data->chunks = std::move(generated);
                    task.data->generation_ms = ms;
                    enqueue_locked({Kind::local_light, task.data, {}, {}, {}, {}});
                } else if (task.kind == Kind::local_light) {
                    task.data->local_light = std::move(light);
                    task.data->lighting_ms = ms;
                    schedule_dirty_ = true;
                } else if (task.kind == Kind::connect_light) {
                    auto& column = *task.render;
                    column.result->light = std::move(light);
                    column.result->lighting_ms += ms;
                    // All neighbours are immutable and retained for this requested column.
                    for (int cy = 0; cy < chunks_per_column; ++cy) {
                        auto neighbours = neighbours_locked({column.key.x, cy, column.key.z});
                        if (!neighbours)
                            throw std::logic_error("Missing column mesh dependencies after lighting.");
                        enqueue_locked({Kind::mesh,
                                        {},
                                        task.render,
                                        cy,
                                        std::make_shared<ChunkNeighbours>(std::move(*neighbours)),
                                        {}});
                    }
                } else {
                    auto& column = *task.render;
                    column.result->meshes[task.cy] = std::move(mesh);
                    column.result->meshing_ms += ms;
                    column.done.set(task.cy);
                    if (column.done.all()) {
                        profiling::event(profiling::Stage::mesh_ready, column.key.x, column.key.z);
                        column.active = false;
                        --active_; // Completion frees computation capacity, independently of delivery.
                        if (!column.admitted) {
                            retired_result = std::move(column.result);
                            column.done.reset();
                        }
                        schedule_dirty_ = true;
                    }
                }
                wake_.notify_all();
            }
        }
    } catch (...) {
        std::lock_guard lock(mutex_);
        failure_ = std::current_exception();
        stopping_ = true;
        wake_.notify_all();
    }
}
} // namespace sandbox
