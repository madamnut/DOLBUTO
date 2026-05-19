#pragma once

#include "camera/Camera.h"

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
    };

    struct CommandResult
    {
        std::vector<std::string> messages;
        std::optional<DVec3> teleportPosition;
        std::optional<uint64_t> worldTicks;
    };

    CommandResult executeCommand(std::string_view input, const CommandContext& context);
}
