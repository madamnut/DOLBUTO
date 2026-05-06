#include "platform/RuntimePaths.h"

#include <array>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace dolbuto
{
    std::filesystem::path executableDirectory()
    {
#ifdef _WIN32
        std::array<wchar_t, 4096> buffer{};
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
        {
            throw std::runtime_error("Failed to resolve executable path.");
        }
        return std::filesystem::path(buffer.data()).parent_path();
#else
        return std::filesystem::current_path();
#endif
    }

    std::filesystem::path assetDirectory()
    {
#ifdef NDEBUG
        return executableDirectory() / "assets";
#else
        return std::filesystem::path(DOLBUTO_ASSET_DIR);
#endif
    }

    std::filesystem::path configDirectory()
    {
#ifdef NDEBUG
        return executableDirectory() / "config";
#else
        return std::filesystem::path(DOLBUTO_CONFIG_DIR);
#endif
    }

    std::filesystem::path shaderDirectory()
    {
#ifdef NDEBUG
        return executableDirectory() / "shaders";
#else
        return std::filesystem::path(DOLBUTO_SHADER_DIR);
#endif
    }

    std::filesystem::path worldDirectory()
    {
#ifdef NDEBUG
        return executableDirectory() / "saves" / "world";
#else
        return std::filesystem::path(DOLBUTO_WORLD_DIR);
#endif
    }

    std::filesystem::path screenshotDirectory()
    {
#ifdef NDEBUG
        return executableDirectory() / "screenshots";
#else
        return std::filesystem::path(DOLBUTO_ASSET_DIR).parent_path() / "screenshots";
#endif
    }
}
