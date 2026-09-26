#pragma once
#include "world/lighting.hpp"
namespace sandbox {
std::shared_ptr<const ColumnLight> reference_local_column_light(const Column&, std::stop_token);
std::shared_ptr<const ColumnLight> reference_connect_column_light(ColumnKey, std::array<LightInput, 9>,
                                                                  std::stop_token);
std::unique_ptr<LightResult> reference_update_column_lights(uint64_t, std::vector<LightInput>,
                                                            const WorldEdits&,
                                                            const std::vector<LightChange>&, std::stop_token);
} // namespace sandbox
