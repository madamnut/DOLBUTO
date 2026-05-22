#pragma once

#include <optional>
#include <string_view>

namespace dolbuto::game
{
    enum class GameMode : unsigned char
    {
        Survival = 0,
        Sandbox = 1
    };

    inline const char* gameModeName(GameMode mode)
    {
        switch (mode)
        {
        case GameMode::Survival: return "Survival";
        case GameMode::Sandbox: return "Sandbox";
        }
        return "Sandbox";
    }

    inline const char* gameModeToken(GameMode mode)
    {
        switch (mode)
        {
        case GameMode::Survival: return "survival";
        case GameMode::Sandbox: return "sandbox";
        }
        return "sandbox";
    }

    inline std::optional<GameMode> parseGameMode(std::string_view value)
    {
        if (value == "survival")
        {
            return GameMode::Survival;
        }
        if (value == "sandbox")
        {
            return GameMode::Sandbox;
        }
        return std::nullopt;
    }
}
