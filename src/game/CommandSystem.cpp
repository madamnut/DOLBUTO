#include "game/CommandSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace dolbuto::game
{
    namespace
    {
        std::string trim(std::string value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
            {
                value.erase(value.begin());
            }
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
            {
                value.pop_back();
            }
            return value;
        }

        std::vector<std::string> splitWhitespace(std::string_view text)
        {
            std::istringstream stream{std::string(text)};
            std::vector<std::string> tokens;
            std::string token;
            while (stream >> token)
            {
                tokens.push_back(token);
            }
            return tokens;
        }

        std::string lowercase(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::optional<double> parseDoubleStrict(const std::string& text)
        {
            try
            {
                size_t parsed = 0;
                const double value = std::stod(text, &parsed);
                if (parsed == text.size() && std::isfinite(value))
                {
                    return value;
                }
            }
            catch (...)
            {
            }
            return std::nullopt;
        }

        std::optional<uint64_t> parseUnsignedStrict(const std::string& text)
        {
            if (text.empty() || text.front() == '-')
            {
                return std::nullopt;
            }

            try
            {
                size_t parsed = 0;
                const uint64_t value = std::stoull(text, &parsed, 10);
                if (parsed == text.size())
                {
                    return value;
                }
            }
            catch (...)
            {
            }
            return std::nullopt;
        }

        std::optional<int64_t> parseSignedStrict(const std::string& text)
        {
            if (text.empty())
            {
                return std::nullopt;
            }

            try
            {
                size_t parsed = 0;
                const int64_t value = std::stoll(text, &parsed, 10);
                if (parsed == text.size())
                {
                    return value;
                }
            }
            catch (...)
            {
            }
            return std::nullopt;
        }

        std::optional<double> parseCoordinate(std::string_view token, double current)
        {
            if (token.empty())
            {
                return std::nullopt;
            }

            if (token.front() != '~')
            {
                return parseDoubleStrict(std::string(token));
            }

            if (token.size() == 1)
            {
                return current;
            }

            std::optional<double> offset = parseDoubleStrict(std::string(token.substr(1)));
            if (!offset)
            {
                return std::nullopt;
            }
            return current + *offset;
        }

        std::string formatDouble(double value)
        {
            std::ostringstream text;
            text << std::fixed << std::setprecision(3) << value;
            return text.str();
        }

        struct StatRef
        {
            const char* name = "";
            int current = 0;
            int max = 1;
            std::optional<int> CommandResult::* resultMember = nullptr;
        };

        int clampedMax(int value)
        {
            return std::max(1, value);
        }

        int clampedStatValue(int value, int maxValue)
        {
            return std::clamp(value, 0, clampedMax(maxValue));
        }

        std::string formatStat(const StatRef& stat)
        {
            return std::string("STAT: ") + stat.name + " " +
                std::to_string(clampedStatValue(stat.current, stat.max)) + "/" +
                std::to_string(clampedMax(stat.max));
        }

        std::string formatAllStats(const CommandContext& context)
        {
            return "STAT: HP " + std::to_string(clampedStatValue(context.playerHp, context.playerMaxHp)) + "/" + std::to_string(clampedMax(context.playerMaxHp)) +
                " / HUNGER " + std::to_string(clampedStatValue(context.playerHunger, context.playerMaxHunger)) + "/" + std::to_string(clampedMax(context.playerMaxHunger)) +
                " / THIRST " + std::to_string(clampedStatValue(context.playerThirst, context.playerMaxThirst)) + "/" + std::to_string(clampedMax(context.playerMaxThirst)) +
                " / OXYGEN " + std::to_string(clampedStatValue(context.playerOxygen, context.playerMaxOxygen)) + "/" + std::to_string(clampedMax(context.playerMaxOxygen));
        }

        std::optional<StatRef> statRef(std::string_view token, const CommandContext& context)
        {
            const std::string stat = lowercase(std::string(token));
            if (stat == "hp" || stat == "health")
            {
                return StatRef{"HP", context.playerHp, context.playerMaxHp, &CommandResult::playerHp};
            }
            if (stat == "hunger" || stat == "food")
            {
                return StatRef{"HUNGER", context.playerHunger, context.playerMaxHunger, &CommandResult::playerHunger};
            }
            if (stat == "thirst" || stat == "water")
            {
                return StatRef{"THIRST", context.playerThirst, context.playerMaxThirst, &CommandResult::playerThirst};
            }
            if (stat == "oxygen" || stat == "air" || stat == "breath")
            {
                return StatRef{"OXYGEN", context.playerOxygen, context.playerMaxOxygen, &CommandResult::playerOxygen};
            }
            return std::nullopt;
        }

        std::optional<GameMode> parseCommandGameMode(const std::string& token)
        {
            return parseGameMode(lowercase(token));
        }

        bool applyStatCommand(
            CommandResult& result,
            const StatRef& stat,
            std::string_view modeToken,
            const std::string& valueToken)
        {
            const std::string mode = lowercase(std::string(modeToken));
            const int maxValue = clampedMax(stat.max);
            const int current = clampedStatValue(stat.current, maxValue);
            const std::optional<int64_t> value = parseSignedStrict(valueToken);
            if (!value)
            {
                result.messages.push_back("Invalid stat value.");
                return true;
            }

            int64_t next = current;
            if (mode == "add")
            {
                next = static_cast<int64_t>(current) + *value;
            }
            else if (mode == "set")
            {
                next = *value;
            }
            else
            {
                result.messages.push_back("Usage: /stat <hp|hunger|thirst|oxygen> add <value> or set <value>");
                return true;
            }

            const int clamped = static_cast<int>(std::clamp<int64_t>(next, 0, maxValue));
            (result.*(stat.resultMember)) = clamped;
            result.messages.push_back(std::string("STAT: ") + stat.name + " " + std::to_string(clamped) + "/" + std::to_string(maxValue));
            return true;
        }
    }

    CommandResult executeCommand(std::string_view input, const CommandContext& context)
    {
        CommandResult result;
        std::string commandLine = trim(std::string(input));
        if (!commandLine.empty() && commandLine.front() == '/')
        {
            commandLine.erase(commandLine.begin());
        }

        const std::vector<std::string> tokens = splitWhitespace(commandLine);
        if (tokens.empty())
        {
            result.messages.push_back("Empty command. Use /help.");
            return result;
        }

        const std::string command = lowercase(tokens.front());
        if (command == "help" || command == "?")
        {
            result.messages.push_back("Commands: /help /pos /seed /stat /gamemode");
            result.messages.push_back("/tp <x> <y> <z>");
            result.messages.push_back("/time set <ticks>, /time add <ticks>");
            result.messages.push_back("/stat <hp|hunger|thirst|oxygen> add <value>, set <value>");
            result.messages.push_back("/gamemode survival, /gamemode sandbox");
            result.messages.push_back("Coords: ~, ~10, ~-5");
            return result;
        }

        if (command == "pos")
        {
            result.messages.push_back(
                "POS: X " + formatDouble(context.playerPosition.x) +
                " / Y " + formatDouble(context.playerPosition.y) +
                " / Z " + formatDouble(context.playerPosition.z));
            return result;
        }

        if (command == "seed")
        {
            result.messages.push_back("SEED: " + std::to_string(context.worldSeed));
            return result;
        }

        if (command == "gamemode")
        {
            if (tokens.size() == 1)
            {
                result.messages.push_back(std::string("GAMEMODE: ") + gameModeName(context.gameMode));
                return result;
            }
            if (tokens.size() != 2)
            {
                result.messages.push_back("Usage: /gamemode survival or /gamemode sandbox");
                return result;
            }

            const std::optional<GameMode> mode = parseCommandGameMode(tokens[1]);
            if (!mode)
            {
                result.messages.push_back("Usage: /gamemode survival or /gamemode sandbox");
                return result;
            }

            result.gameMode = *mode;
            result.messages.push_back(std::string("GAMEMODE: ") + gameModeName(*mode));
            return result;
        }

        if (command == "stat")
        {
            if (tokens.size() == 1)
            {
                result.messages.push_back(formatAllStats(context));
                return result;
            }

            const std::optional<StatRef> stat = statRef(tokens[1], context);
            if (!stat)
            {
                result.messages.push_back("Usage: /stat <hp|hunger|thirst|oxygen> [add <value>|set <value>]");
                return result;
            }
            if (tokens.size() == 2)
            {
                result.messages.push_back(formatStat(*stat));
                return result;
            }
            if (tokens.size() != 4)
            {
                result.messages.push_back("Usage: /stat <hp|hunger|thirst|oxygen> add <value> or set <value>");
                return result;
            }

            applyStatCommand(result, *stat, tokens[2], tokens[3]);
            return result;
        }

        if (command == "tp")
        {
            if (tokens.size() != 4)
            {
                result.messages.push_back("Usage: /tp <x> <y> <z>");
                return result;
            }

            const std::optional<double> x = parseCoordinate(tokens[1], context.playerPosition.x);
            const std::optional<double> y = parseCoordinate(tokens[2], context.playerPosition.y);
            const std::optional<double> z = parseCoordinate(tokens[3], context.playerPosition.z);
            if (!x || !y || !z)
            {
                result.messages.push_back("Invalid coordinate. Use numbers, ~, ~10, or ~-5.");
                return result;
            }

            result.teleportPosition = DVec3{*x, *y, *z};
            result.messages.push_back(
                "Teleported to X " + formatDouble(*x) +
                " / Y " + formatDouble(*y) +
                " / Z " + formatDouble(*z));
            return result;
        }

        if (command == "time")
        {
            if (tokens.size() != 3)
            {
                result.messages.push_back("Usage: /time set <ticks> or /time add <ticks>");
                return result;
            }

            const std::string mode = lowercase(tokens[1]);
            const std::optional<uint64_t> ticks = parseUnsignedStrict(tokens[2]);
            if (!ticks)
            {
                result.messages.push_back("Invalid ticks value.");
                return result;
            }

            if (mode == "set")
            {
                result.worldTicks = *ticks;
                result.messages.push_back("Time set to " + std::to_string(*ticks) + " ticks.");
                return result;
            }
            if (mode == "add")
            {
                if (*ticks > std::numeric_limits<uint64_t>::max() - context.worldTicks)
                {
                    result.messages.push_back("Time value is too large.");
                    return result;
                }
                result.worldTicks = context.worldTicks + *ticks;
                result.messages.push_back("Time advanced to " + std::to_string(*result.worldTicks) + " ticks.");
                return result;
            }

            result.messages.push_back("Usage: /time set <ticks> or /time add <ticks>");
            return result;
        }

        result.messages.push_back("Unknown command: /" + tokens.front() + ". Use /help.");
        return result;
    }
}
