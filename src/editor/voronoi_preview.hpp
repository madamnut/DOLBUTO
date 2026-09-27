#pragma once
#include <nlohmann/json_fwd.hpp>
#include <vector>
namespace sandbox::editor {
// Editor-only periodic Voronoi graph and seeded continent growth.
// Binary v10: header40/site16/pixel5/edge2/boundary20.
// Sites append altitude/compression tendencies; pixels append sampled tendencies.
// Shared boundaries contain two endpoints and a midpoint with separate sea/land constraints.
// See docs/cell-tendencies-2026-09-28.md. No final block heights are generated.
nlohmann::ordered_json voronoi_settings(const nlohmann::ordered_json& input);
std::vector<float> voronoi_preview(const nlohmann::ordered_json& request);
} // namespace sandbox::editor
