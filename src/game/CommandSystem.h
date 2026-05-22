#pragma once

#include "camera/Camera.h"
#include "game/GameMode.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dolbuto::game
{
    struct CommandContext
    {
        DVec3 playerPosition{};
        uint64_t worldSeed = 0;
        uint64_t worldTicks = 0;
        int playerHp = 100;
        int playerMaxHp = 100;
        int playerHunger = 100;
        int playerMaxHunger = 100;
        int playerThirst = 100;
        int playerMaxThirst = 100;
        GameMode gameMode = GameMode::Sandbox;
    };

    struct CommandResult
    {
        std::vector<std::string> messages;
        std::optional<DVec3> teleportPosition;
        std::optional<uint64_t> worldTicks;
        std::optional<int> playerHp;
        std::optional<int> playerHunger;
        std::optional<int> playerThirst;
        std::optional<GameMode> gameMode;
    };

    CommandResult executeCommand(std::string_view input, const CommandContext& context);
}
