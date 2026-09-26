#include "world/lod.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <stdexcept>

namespace sandbox {
namespace {
void append(LodPillar& pillar, uint32_t bottom, uint32_t top, Block material) {
    if (top <= bottom || material == Block::air)
        return;
    if (!pillar.empty() && pillar.back().top == bottom && pillar.back().material == material)
        pillar.back().top = top;
    else
        pillar.push_back({bottom, top, material});
}
unsigned material_id(Block block, unsigned axis, bool positive) {
    if (is_snow(block))
        return 10;
    switch (block) {
    case Block::dirt:
        return 0;
    case Block::grass:
        return axis == 1 ? (positive ? 1 : 3) : 2;
    case Block::rock:
        return 4;
    case Block::water:
        return 5;
    case Block::sand:
        return 6;
    case Block::lava:
        return 7;
    case Block::glow:
        return 8;
    case Block::ice:
        return 9;
    default:
        return 4;
    }
}
LodKey parent(LodKey key) {
    ++key.level;
    const int mask = (1 << key.level) - 1;
    key.x &= ~mask;
    key.z &= ~mask;
    return key;
}
LodKey child(LodKey key, int i) {
    --key.level;
    key.x += (i & 1) << key.level;
    key.z += (i >> 1) << key.level;
    return key;
}
double distance(LodKey key, ColumnKey centre) {
    const double width = double(1 << key.level);
    const double dx = std::max(
        0.0, std::abs(world_delta((key.x + width * .5) * 16, (centre.x + .5) * 16)) / 16 - width * .5);
    const double dz = std::max(
        0.0, std::abs(world_delta((key.z + width * .5) * 16, (centre.z + .5) * 16)) / 16 - width * .5);
    return std::sqrt(dx * dx + dz * dz);
}
} // namespace
size_t LodData::bytes() const {
    size_t result = sizeof(*this);
    for (const auto& p : pillars)
        result += p.capacity() * sizeof(LodRun);
    return result;
}
LodData lod_extract(const Column& column) {
    LodData result;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            auto& pillar = result.pillars[x + z * 16];
            for (int y = 0; y < world_height; ++y) {
                const auto block = column.block_at(x, y, z);
                if (is_solid(block)) {
                    append(pillar, y * 256, y * 256 + uint32_t(std::lround(block_height(block) * 256)),
                           block);
                } else {
                    const auto fluid = column.fluid_at(x, y, z);
                    if (fluid.amount)
                        append(pillar, y * 256,
                               y * 256 + uint32_t(std::lround(
                                             fluid_height(fluid, column.fluid_at(x, y + 1, z)) * 256)),
                               fluid.kind == FluidKind::water ? Block::water : Block::lava);
                }
            }
            pillar.shrink_to_fit();
        }
    return result;
}
LodData lod_reduce(const std::array<const LodData*, 4>& children) {
    LodData result;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            const auto& source = *children[(x / 8) + 2 * (z / 8)];
            const int sx = (x % 8) * 2, sz = (z % 8) * 2;
            std::array<const LodPillar*, 4> pillars;
            std::vector<uint32_t> breaks;
            for (int i = 0; i < 4; ++i) {
                pillars[i] = &source.pillars[sx + (i & 1) + 16 * (sz + (i >> 1))];
                for (const auto& r : *pillars[i]) {
                    breaks.push_back(r.bottom);
                    breaks.push_back(r.top);
                }
            }
            std::sort(breaks.begin(), breaks.end());
            breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
            std::array<size_t, 4> cursor{};
            for (size_t j = 1; j < breaks.size(); ++j) {
                std::array<unsigned, 32> votes{};
                for (int i = 0; i < 4; ++i) {
                    const auto& p = *pillars[i];
                    auto& k = cursor[i];
                    while (k < p.size() && p[k].top <= breaks[j - 1])
                        ++k;
                    const auto m = k < p.size() && p[k].bottom <= breaks[j - 1] ? p[k].material : Block::air;
                    ++votes[unsigned(m)];
                }
                // 두 기둥 이상 비어 있으면 동률에서 고체를 우선, 세 기둥이 비면 빈 공간 유지.
                if (votes[0] > 2)
                    continue;
                unsigned best = 0, count = 0;
                for (unsigned m = 1; m < votes.size(); ++m)
                    if (votes[m] > count) {
                        best = m;
                        count = votes[m];
                    }
                append(result.pillars[x + z * 16], breaks[j - 1], breaks[j], Block(best));
            }
            result.pillars[x + z * 16].shrink_to_fit();
        }
    return result;
}
std::vector<LodFace> lod_mesh(const LodData& data) {
    std::vector<LodFace> faces;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            const auto& pillar = data.pillars[x + z * 16];
            for (size_t n = 0; n < pillar.size(); ++n) {
                const auto& r = pillar[n];
                const auto emit = [&](unsigned axis, bool positive, uint32_t bottom, uint32_t top) {
                    const unsigned mat = material_id(r.material, axis, positive);
                    faces.push_back({unsigned(x) | (unsigned(z) << 4) | (axis << 8) |
                                         (unsigned(positive) << 10) | (mat << 11),
                                     bottom, top, n + 1 == pillar.size() ? 15u : 6u});
                };
                if (n == 0 || pillar[n - 1].top < r.bottom ||
                    (is_fluid(pillar[n - 1].material) && !is_fluid(r.material)))
                    emit(1, false, r.bottom, r.top);
                if (n + 1 == pillar.size() || pillar[n + 1].bottom > r.top ||
                    (is_fluid(pillar[n + 1].material) && !is_fluid(r.material)))
                    emit(1, true, r.bottom, r.top);
                for (unsigned axis : {0u, 2u})
                    for (bool positive : {false, true}) {
                        const int nx = x + (axis == 0 ? (positive ? 1 : -1) : 0);
                        const int nz = z + (axis == 2 ? (positive ? 1 : -1) : 0);
                        if (nx < 0 || nx >= 16 || nz < 0 || nz >= 16) {
                            // 타일 가장자리를 닫는 수직 면은 다른 LOD 단계와의 높이 차도 덮는다.
                            emit(axis, positive, r.bottom, r.top);
                            continue;
                        }
                        uint32_t bottom = r.bottom;
                        for (const auto& adjacent : data.pillars[nx + nz * 16]) {
                            if (is_fluid(adjacent.material) && !is_fluid(r.material))
                                continue;
                            if (adjacent.top <= bottom)
                                continue;
                            if (adjacent.bottom >= r.top)
                                break;
                            if (adjacent.bottom > bottom)
                                emit(axis, positive, bottom, adjacent.bottom);
                            bottom = std::max(bottom, adjacent.top);
                            if (bottom >= r.top)
                                break;
                        }
                        if (bottom < r.top)
                            emit(axis, positive, bottom, r.top);
                    }
            }
        }
    faces.shrink_to_fit();
    return faces;
}
LodCache::LodCache(std::shared_ptr<const TerrainGenerator> generator)
    : generator_(std::move(generator)), worker_([this](std::stop_token stop) { work(stop); }) {}
LodCache::~LodCache() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    worker_.request_stop();
    wake_.notify_all();
    worker_.join();
}
void LodCache::request(ColumnKey centre, int radius, int near_radius, bool enabled, bool busy) {
    std::lock_guard lock(mutex_);
    Request next{canonical(centre), std::clamp(radius, 16, 256), near_radius, enabled, busy};
    if (next != request_) {
        request_ = next;
        wake_.notify_one();
    }
}
void LodCache::submit(Column column) {
    std::lock_guard lock(mutex_);
    if (!request_.enabled)
        return;
    if (submitted_.size() < 32 || submitted_.contains(column.key))
        submitted_.insert_or_assign(column.key, std::move(column));
    wake_.notify_one();
}
void LodCache::edit(int x, int y, int z, Block block, Fluid fluid) {
    std::lock_guard lock(mutex_);
    changes_[{wrap_block(x) / 16, wrap_block(z) / 16}]
            [local_coordinate(x) + 16 * (local_coordinate(z) + 16 * y)] = {block, fluid};
    wake_.notify_one();
}
std::shared_ptr<const LodScene> LodCache::scene() const {
    std::lock_guard lock(mutex_);
    if (failure_)
        std::rethrow_exception(failure_);
    return scene_;
}
LodStats LodCache::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}
void LodCache::work(std::stop_token stop) {
    try {
        std::map<LodKey, Node> nodes;
        std::unordered_map<ColumnKey, Changes, ColumnHash> edits;
        std::set<LodKey> dirty;
        std::vector<ColumnKey> order;
        size_t cursor = 0, generated = 0, data_bytes = 0, evicted = 0;
        uint64_t serial = 0;
        Request previous{};
        previous.radius = 0;
        auto last_publish = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        double last_ms = 0;
        bool changed = true, limited = false;
        auto mesh_account = std::make_shared<std::atomic_size_t>(0);
        const auto replace = [&](LodKey key, LodData data) {
            auto& node = nodes[key];
            if (node.data)
                data_bytes -= node.data->bytes();
            node.data = std::make_shared<LodData>(std::move(data));
            node.mesh.reset();
            node.revision = ++serial;
            data_bytes += node.data->bytes();
            changed = true;
        };
        const auto propagate = [&](LodKey key) {
            while (key.level < lod_max_level) {
                const auto updated = key;
                key = parent(key);
                std::array<const LodData*, 4> inputs{};
                for (int i = 0; i < 4; ++i) {
                    auto it = nodes.find(child(key, i));
                    if (it != nodes.end())
                        inputs[i] = it->second.data.get();
                }
                if (std::any_of(inputs.begin(), inputs.end(), [](auto p) { return p == nullptr; })) {
                    auto existing = nodes.find(key);
                    auto changed_child = nodes.find(updated);
                    if (existing == nodes.end() || changed_child == nodes.end())
                        break;
                    // 일부 형제의 세밀한 캐시가 퇴거돼도 바뀐 사분면만 축약하여 상위 편집을 보존한다.
                    const auto* data = changed_child->second.data.get();
                    auto reduced = lod_reduce({data, data, data, data});
                    auto next = *existing->second.data;
                    const int qx = updated.x == key.x ? 0 : 8, qz = updated.z == key.z ? 0 : 8;
                    for (int z = 0; z < 8; ++z)
                        for (int x = 0; x < 8; ++x)
                            next.pillars[qx + x + 16 * (qz + z)] = std::move(reduced.pillars[x + 16 * z]);
                    replace(key, std::move(next));
                } else
                    replace(key, lod_reduce(inputs));
            }
        };
        while (!stop.stop_requested()) {
            Request req;
            std::unordered_map<ColumnKey, Column, ColumnHash> submitted;
            std::unordered_map<ColumnKey, Changes, ColumnHash> updates;
            {
                std::lock_guard lock(mutex_);
                if (stopping_)
                    return;
                req = request_;
                submitted.swap(submitted_);
                updates.swap(changes_);
            }
            if (req.centre != previous.centre || req.radius != previous.radius ||
                req.near_radius != previous.near_radius) {
                order.clear();
                cursor = 0;
                for (int z = -req.radius; z <= req.radius; ++z)
                    for (int x = -req.radius; x <= req.radius; ++x)
                        if (x * x + z * z <= req.radius * req.radius)
                            order.push_back(canonical(ColumnKey{req.centre.x + x, req.centre.z + z}));
                std::sort(order.begin(), order.end(), [&](ColumnKey a, ColumnKey b) {
                    const int ax = column_delta(a.x, req.centre.x), az = column_delta(a.z, req.centre.z);
                    const int bx = column_delta(b.x, req.centre.x), bz = column_delta(b.z, req.centre.z);
                    return std::tuple{ax * ax + az * az, a.x, a.z} < std::tuple{bx * bx + bz * bz, b.x, b.z};
                });
                changed = true;
            }
            if (req.enabled != previous.enabled)
                changed = true;
            previous = req;
            for (auto& [key, delta] : updates) {
                auto& stored = edits[key];
                for (auto [index, value] : delta)
                    stored[index] = value;
                dirty.insert({key.x, key.z, 0});
            }
            const auto install = [&](Column& column) {
                if (auto it = edits.find(column.key); it != edits.end()) {
                    std::array<std::shared_ptr<std::array<Block, 4096>>, chunks_per_column> blocks;
                    std::array<std::shared_ptr<std::array<Fluid, 4096>>, chunks_per_column> fluids;
                    for (auto [index, c] : it->second) {
                        const int y = index / 256, x = index % 16, z = (index / 16) % 16, cy = y / 16;
                        const auto& chunk = column.chunks[cy];
                        if (!blocks[cy]) {
                            blocks[cy] = std::make_shared<std::array<Block, 4096>>();
                            fluids[cy] = std::make_shared<std::array<Fluid, 4096>>();
                            if (chunk.blocks)
                                *blocks[cy] = *chunk.blocks;
                            else
                                blocks[cy]->fill(chunk.uniform);
                            if (chunk.fluids)
                                *fluids[cy] = *chunk.fluids;
                            else
                                fluids[cy]->fill(chunk.uniform_fluid);
                        }
                        const int local = x + 16 * (y % 16 + 16 * z);
                        (*blocks[cy])[local] = c.block;
                        (*fluids[cy])[local] = c.fluid;
                    }
                    for (int cy = 0; cy < chunks_per_column; ++cy)
                        if (blocks[cy]) {
                            column.chunks[cy].blocks = std::move(blocks[cy]);
                            column.chunks[cy].fluids = std::move(fluids[cy]);
                        }
                }
                LodKey key{column.key.x, column.key.z, 0};
                replace(key, lod_extract(column));
                propagate(key);
                dirty.erase(key);
            };
            if (req.enabled)
                for (auto& [key, column] : submitted) {
                    if (stop.stop_requested())
                        return;
                    install(column);
                }
            const auto suitable = [&](LodKey key) {
                if (distance(key, req.centre) <= req.near_radius + 3)
                    return nodes.contains(key);
                for (;;) {
                    if (nodes.contains(key))
                        return true;
                    if (key.level == lod_max_level)
                        return false;
                    key = parent(key);
                    if (distance(key, req.centre) < (1 << key.level) * 4.0)
                        return false;
                }
            };
            while (cursor < order.size() && suitable({order[cursor].x, order[cursor].z, 0}))
                ++cursor;
            bool did_work = false;
            if (req.enabled && !req.busy && (!dirty.empty() || cursor < order.size())) {
                ColumnKey key;
                if (!dirty.empty())
                    key = {dirty.begin()->x, dirty.begin()->z};
                else
                    key = order[cursor++];
                const auto begin = std::chrono::steady_clock::now();
                Column column;
                column.key = key;
                const auto tile = generate_tile(key, *generator_);
                bool cancelled = false;
                for (int cy = 0; cy < chunks_per_column; ++cy) {
                    if (stop.stop_requested())
                        return;
                    {
                        std::lock_guard lock(mutex_);
                        cancelled = request_.busy || !request_.enabled || request_.centre != req.centre;
                    }
                    if (cancelled)
                        break;
                    column.chunks[cy] = generate_chunk({key.x, cy, key.z}, *generator_, &tile);
                }
                if (!cancelled) {
                    install(column);
                    ++generated;
                    did_work = true;
                } else if (cursor)
                    --cursor;
                last_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                              .count();
            }
            const auto now = std::chrono::steady_clock::now();
            if (changed && now - last_publish >= std::chrono::milliseconds(100)) {
                // 데이터 캐시는 256MiB, 메시/진행중 사본에는 나머지 공간을 예약한다.
                limited = false;
                if (data_bytes > 256ull * 1024 * 1024) {
                    std::vector<LodKey> victims;
                    for (const auto& [key, node] : nodes)
                        if (key.level < lod_max_level && nodes.contains(parent(key)) &&
                            distance(key, req.centre) > req.near_radius + 4)
                            victims.push_back(key);
                    std::sort(victims.begin(), victims.end(), [&](auto a, auto b) {
                        return std::tuple{a.level, -distance(a, req.centre)} <
                               std::tuple{b.level, -distance(b, req.centre)};
                    });
                    for (auto key : victims) {
                        if (data_bytes <= 240ull * 1024 * 1024)
                            break;
                        auto it = nodes.find(key);
                        if (it == nodes.end())
                            continue;
                        data_bytes -= it->second.data->bytes();
                        nodes.erase(it);
                        ++evicted;
                        limited = true;
                    }
                    // 완성된 부모가 없는 외곽/오래된 지역도 무한 보관하지 않는다.
                    for (auto it = nodes.begin(); data_bytes > 256ull * 1024 * 1024 && it != nodes.end();) {
                        if (distance(it->first, req.centre) > req.radius + 2) {
                            data_bytes -= it->second.data->bytes();
                            it = nodes.erase(it);
                            ++evicted;
                            limited = true;
                        } else
                            ++it;
                    }
                }
                if (data_bytes + nodes.size() * 128 > 272ull * 1024 * 1024) {
                    // 복잡한 지형/최대 근거리에서도 예산은 우선한다. 거친 부모를 남기는 퇴거를 먼저 시도한다.
                    std::vector<LodKey> victims;
                    for (const auto& [key, node] : nodes)
                        victims.push_back(key);
                    std::sort(victims.begin(), victims.end(), [&](auto a, auto b) {
                        return std::tuple{nodes.contains(parent(a)) ? 0 : 1, -distance(a, req.centre),
                                          a.level} < std::tuple{nodes.contains(parent(b)) ? 0 : 1,
                                                                -distance(b, req.centre), b.level};
                    });
                    for (auto key : victims) {
                        if (data_bytes + nodes.size() * 128 <= 256ull * 1024 * 1024)
                            break;
                        auto it = nodes.find(key);
                        if (it == nodes.end())
                            continue;
                        data_bytes -= it->second.data->bytes();
                        nodes.erase(it);
                        ++evicted;
                        limited = true;
                    }
                }
                auto scene = std::make_shared<LodScene>();
                scene->revision = ++serial;
                std::set<LodKey> roots, selected, branches;
                for (const auto& [key, node] : nodes) {
                    auto root = key;
                    branches.insert(root);
                    while (root.level < lod_max_level) {
                        root = parent(root);
                        branches.insert(root);
                    }
                    roots.insert(root);
                }
                const auto visit = [&](auto&& self, LodKey key) -> void {
                    if (!branches.contains(key))
                        return;
                    const double d = distance(key, req.centre);
                    if (d > req.radius + 1)
                        return;
                    auto it = nodes.find(key);
                    const bool want_fine =
                        key.level > 0 && (d <= req.near_radius + 3 || d < (1 << key.level) * 4.0);
                    bool children = true;
                    if (want_fine)
                        for (int i = 0; i < 4; ++i)
                            children &= nodes.contains(child(key, i));
                    if (it != nodes.end() && (!want_fine || !children)) {
                        selected.insert(key);
                        return;
                    }
                    if (key.level > 0)
                        for (int i = 0; i < 4; ++i)
                            self(self, child(key, i));
                };
                if (req.enabled)
                    for (auto root : roots)
                        visit(visit, root);
                size_t mesh_bytes = 0;
                std::vector<LodKey> ordered(selected.begin(), selected.end());
                std::sort(ordered.begin(), ordered.end(),
                          [&](auto a, auto b) { return distance(a, req.centre) < distance(b, req.centre); });
                for (auto key : ordered) {
                    if (stop.stop_requested())
                        return;
                    auto& node = nodes.at(key);
                    if (!node.mesh) {
                        auto faces = lod_mesh(*node.data);
                        const size_t bytes = faces.capacity() * sizeof(LodFace);
                        if (data_bytes + mesh_account->load() + bytes + nodes.size() * 128 >
                            lod_cpu_budget - 32ull * 1024 * 1024) {
                            limited = true;
                            continue;
                        }
                        auto* raw = new LodMesh{key, node.revision, std::move(faces)};
                        mesh_account->fetch_add(bytes);
                        node.mesh =
                            std::shared_ptr<LodMesh>(raw, [account = mesh_account, bytes](LodMesh* mesh) {
                                delete mesh;
                                account->fetch_sub(bytes);
                            });
                    }
                    const size_t bytes = node.mesh->faces.capacity() * sizeof(LodFace);
                    if (mesh_bytes + bytes > 48ull * 1024 * 1024) {
                        limited = true;
                        node.mesh.reset();
                        continue;
                    }
                    mesh_bytes += bytes;
                    scene->meshes.push_back(node.mesh);
                }
                for (auto& [key, node] : nodes)
                    if (!selected.contains(key))
                        node.mesh.reset();
                size_t edit_bytes = 0;
                for (const auto& [key, c] : edits)
                    edit_bytes += c.size() * (sizeof(Cell) + sizeof(int) + 32);
                std::shared_ptr<const LodScene> outgoing = std::move(scene);
                {
                    std::lock_guard lock(mutex_);
                    scene_.swap(outgoing);
                    stats_ = {data_bytes + mesh_account->load() + nodes.size() * 128 +
                                  order.capacity() * sizeof(ColumnKey),
                              nodes.size(),
                              req.enabled ? order.size() - cursor + dirty.size() : 0,
                              generated,
                              edit_bytes,
                              evicted,
                              last_ms,
                              req.busy,
                              limited};
                }
                last_publish = now;
                changed = false;
            } else {
                std::lock_guard lock(mutex_);
                stats_.cpu_bytes = data_bytes + mesh_account->load() + nodes.size() * 128 +
                                   order.capacity() * sizeof(ColumnKey);
                stats_.pending = req.enabled ? order.size() - cursor + dirty.size() : 0;
                stats_.paused = req.busy;
            }
            if (!did_work && submitted.empty()) {
                std::unique_lock lock(mutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(50), [&] {
                    return stopping_ || !submitted_.empty() || !changes_.empty() || request_ != req;
                });
            }
        }
    } catch (...) {
        std::lock_guard lock(mutex_);
        failure_ = std::current_exception();
    }
}
} // namespace sandbox
