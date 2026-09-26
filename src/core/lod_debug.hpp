#pragma once
#include <array>
#include <glm/vec4.hpp>

namespace sandbox {
// Display-space colours, shared by the LOD coverage buffer and the F4 legend.
// Entry 0 is real chunks; entries 1..10 are the actually drawn LOD levels 0..9.
inline const std::array<glm::vec4, 11> lod_debug_palette{{{.55f, .55f, .55f, 1},
                                                          {.25f, .85f, .35f, 1},
                                                          {.95f, .85f, .20f, 1},
                                                          {1.f, .50f, .12f, 1},
                                                          {.95f, .20f, .22f, 1},
                                                          {.70f, .25f, .90f, 1},
                                                          {.30f, .40f, 1.f, 1},
                                                          {.15f, .80f, .95f, 1},
                                                          {.90f, .40f, .75f, 1},
                                                          {.55f, .70f, .95f, 1},
                                                          {.50f, .30f, .65f, 1}}};
} // namespace sandbox
