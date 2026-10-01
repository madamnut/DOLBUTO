#pragma once
#include <cstdint>

namespace sandbox {
// Explicit CPU diagnostics for the main solid-terrain pass, not GPU execution time.
struct DrawStats {
    double cpu_ms{}; // Traversal, culling, bindings and command recording; no queue submit/wait.
    uint32_t draws{}, descriptor_binds{}, vertex_binds{}, pushes{};
};
} // namespace sandbox
