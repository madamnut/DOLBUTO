#pragma once
#include "world/edit.hpp"
#include <bitset>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>

namespace sandbox {
// One byte per voxel: low nibble sky, high nibble block light. Halos include edge samples.
struct LightChunk {
    // Conservative summary. Unknown/external snapshots default to possibly containing block light.
    bool has_block_light{true};
    uint8_t uniform{};
    std::shared_ptr<const std::array<uint8_t, 18 * 18 * 18>> values;
    // Occupancy is separate from brightness: unlit air remains a valid zero sample.
    bool uniform_opaque{};
    std::shared_ptr<const std::bitset<18 * 18 * 18>> occluders;
    bool opaque_at(int x, int y, int z) const {
        return occluders ? occluders->test(x + 1 + 18 * (y + 1 + 18 * (z + 1))) : uniform_opaque;
    }
    uint8_t at(int x, int y, int z) const {
        return values ? (*values)[x + 1 + 18 * (y + 1 + 18 * (z + 1))] : uniform;
    }
};
struct ColumnLight {
    ColumnKey key;
    uint64_t revision{};
    std::array<LightChunk, chunks_per_column> chunks;
};
struct LightChange {
    BlockPos position;
    Block before{}, after{};
};
struct LightInput {
    Column blocks;
    std::shared_ptr<const ColumnLight> light;
};
struct LightRebuild {
    ColumnKey key;
    std::array<Column, 9> columns;
    WorldEdits edits;
    std::shared_ptr<const ColumnLight> previous;
};
struct LightResult {
    std::vector<std::shared_ptr<const ColumnLight>> columns;
};
// Base terrain only. Local halos are placeholders until connect_column_light completes.
std::shared_ptr<const ColumnLight> local_column_light(const Column& column, std::stop_token stop);
// Reuse nine local interiors; seed flood fill only where their shared faces disagree.
// A 16-cell margin contains the complete nonzero horizontal propagation reach.
std::shared_ptr<const ColumnLight> connect_column_light(ColumnKey key, std::array<LightInput, 9> columns,
                                                        std::stop_token stop);
uint32_t face_light(PackedFace face, const LightChunk& light);
// Shared synchronous CPU entry point; workers and the explicit comparison tool use the same code.
std::unique_ptr<LightResult> update_column_lights(uint64_t revision, std::vector<LightInput> inputs,
                                                  const WorldEdits& edits,
                                                  const std::vector<LightChange>& changes,
                                                  std::stop_token stop);
// CPU-only preparation for both initial geometry uploads and light-only replacements.
struct PackedMesh {
    ChunkKey key;
    uint64_t ticket{};
    std::shared_ptr<const std::vector<PackedFace>> geometry;
    std::shared_ptr<const std::vector<uint32_t>> values;
    LightChunk light;
    uint32_t solid_count{};
    uint32_t ice_count{};
    bool light_only{}, unchanged{};
};
class MeshLightWorker {
  public:
    MeshLightWorker();
    ~MeshLightWorker();
    void submit(PackedMesh request, ChunkMesh source = {},
                std::shared_ptr<const std::vector<uint32_t>> previous = {}, bool urgent = false);
    std::deque<PackedMesh> take();

  private:
    struct Job {
        PackedMesh request;
        ChunkMesh source;
        std::shared_ptr<const std::vector<uint32_t>> previous;
        bool urgent{};
    };
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<Job> jobs_;
    std::deque<PackedMesh> ready_;
    std::exception_ptr failure_;
    std::jthread thread_;
    void run(std::stop_token stop);
};
class LightWorker {
  public:
    LightWorker();
    ~LightWorker();
    void submit(ColumnKey key, uint64_t revision, std::array<Column, 9> columns, WorldEdits edits,
                std::array<std::shared_ptr<const ColumnLight>, 9> known);
    void submit_update(uint64_t revision, std::vector<LightInput> inputs, WorldEdits edits,
                       std::vector<LightChange> changes, std::vector<LightRebuild> rebuilds);
    std::unique_ptr<LightResult> take();

  private:
    struct Job {
        ColumnKey key;
        uint64_t revision;
        std::array<Column, 9> columns;
        WorldEdits edits;
        std::vector<LightInput> inputs;
        std::vector<LightChange> changes;
        std::vector<LightRebuild> rebuilds;
        bool update{};
        std::array<std::shared_ptr<const ColumnLight>, 9> known;
    };
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::optional<Job> pending_;
    std::unique_ptr<LightResult> ready_;
    std::exception_ptr failure_;
    std::jthread thread_;
    void run(std::stop_token stop);
};
} // namespace sandbox
