#pragma once

#include <filesystem>

namespace dolbuto
{
    std::filesystem::path executableDirectory();
    std::filesystem::path assetDirectory();
    std::filesystem::path configDirectory();
    std::filesystem::path shaderDirectory();
    std::filesystem::path worldDirectory();
    std::filesystem::path screenshotDirectory();
    std::filesystem::path logDirectory();
}
