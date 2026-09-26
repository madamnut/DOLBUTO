#pragma once
#include "world/terrain.hpp"
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace sandbox {
// 한 타일은 16x16 세로 기둥. level마다 기둥의 X/Z 폭만 두 배가 된다.
struct LodKey {
    int x{}, z{}, level{}; // 원점의 청크 컬럼 좌표, 2^level 정렬.
    auto operator<=>(const LodKey&) const = default;
};
inline constexpr int lod_max_level = 9;
inline constexpr size_t lod_cpu_budget = 512ull * 1024 * 1024;
inline constexpr size_t lod_gpu_budget = 256ull * 1024 * 1024;
struct LodRun {
    uint32_t bottom{}, top{}; // 1/256 블록. 물 7/8·눈 1/16 포함.
    Block material{Block::air};
};
using LodPillar = std::vector<LodRun>;
struct LodData {
    std::array<LodPillar, 256> pillars;
    size_t bytes() const;
};
struct LodFace {
    uint32_t cell_axis_material{}, bottom{}, top{}, light{};
};
static_assert(sizeof(LodFace) == 16);
struct LodMesh {
    LodKey key;
    uint64_t revision{};
    std::vector<LodFace> faces;
};
struct LodScene {
    uint64_t revision{};
    std::vector<std::shared_ptr<const LodMesh>> meshes;
};
struct LodStats {
    size_t cpu_bytes{}, nodes{}, pending{}, generated{}, session_edit_bytes{}, evicted{};
    double last_generation_ms{};
    bool paused{}, memory_limited{};
};
LodData lod_extract(const Column& column);
LodData lod_reduce(const std::array<const LodData*, 4>& children);
std::vector<LodFace> lod_mesh(const LodData& data);

class LodCache {
  public:
    explicit LodCache(std::shared_ptr<const TerrainGenerator> generator);
    ~LodCache();
    // 동일 생성기를 쓰되 원거리 작업은 가까운 로딩/편집이 끝날 때까지 대기한다.
    void request(ColumnKey centre, int radius, int near_radius, bool enabled, bool busy);
    void submit(Column column); // 일반 청크 생성 결과 공유. 큐가 가득 차면 원거리에서 나중에 재생성.
    void edit(int x, int y, int z, Block block, Fluid fluid);
    std::shared_ptr<const LodScene> scene() const;
    LodStats stats() const;

  private:
    struct Request {
        ColumnKey centre;
        int radius{128}, near_radius{12};
        bool enabled{true}, busy{true};
        bool operator==(const Request&) const = default;
    };
    struct Cell {
        Block block;
        Fluid fluid;
    };
    using Changes = std::unordered_map<int, Cell>;
    struct Node {
        std::shared_ptr<LodData> data;
        std::shared_ptr<LodMesh> mesh;
        uint64_t revision{};
    };
    std::shared_ptr<const TerrainGenerator> generator_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_{};
    Request request_;
    std::unordered_map<ColumnKey, Column, ColumnHash> submitted_;
    std::unordered_map<ColumnKey, Changes, ColumnHash> changes_;
    std::shared_ptr<const LodScene> scene_;
    LodStats stats_;
    std::exception_ptr failure_;
    std::jthread worker_;
    void work(std::stop_token stop);
};
} // namespace sandbox
