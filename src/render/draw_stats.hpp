#pragma once
#include <cstdint>

namespace sandbox {
// Explicit CPU diagnostics for the main solid-terrain pass, not GPU execution time.
struct DrawStats {
    double cpu_ms{}; // Traversal, culling, bindings and command recording; no queue submit/wait.
    uint32_t draws{}, descriptor_binds{}, vertex_binds{}, pushes{};
    double list_ms{}, sort_ms{}, record_ms{}, batch_prepare_ms{};
    uint32_t indirect_calls{}, indirect_draws{};
};
// Opt-in sparse timings: raw sums for 1/32 of published columns, rotated every frame.
// These include timer overhead; they are not a precise decomposition of normal frame time.
struct NearDrawDetail {
    uint32_t stride{}, phase{}, columns{}, chunk_slots{}, nonempty{}, culled{}, visible{};
    uint32_t sampled_columns{}, sampled_nonempty{}, sampled_visible{};
    uint32_t rejected_columns{}, rejected_column_nonempty{}, rejection_conflicts{};
    double scan_ms{}, cull_ms{}, record_ms{};
};
} // namespace sandbox
