#pragma once
#include "world/generation_config.hpp"

namespace sandbox {
// Shared by the generation editor and the independent preview window.
void noise_controls(const char* id, NoiseSettings& settings, bool individual = false, bool periodic_z = true,
                    int maximum_octaves = 16, const GenerationConfig* config = nullptr);
void warp_controls(GenerationConfig& config);
void temperature_controls(TemperatureSettings& settings);
} // namespace sandbox
