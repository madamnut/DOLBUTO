#pragma once
#include <FastNoise/FastNoise.h>
#include <filesystem>
// Explicit measurement diagnostics: reference agreement, period continuity, image data export.
void inspect_psrd(const std::filesystem::path& folder, const FastNoise::Generator* perlin,
                  const FastNoise::Generator* psrd, int octaves);
