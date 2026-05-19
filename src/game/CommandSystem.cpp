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
            result.messages.push_back("Commands: /help /pos /seed");
            result.messages.push_back("/tp <x> <y> <z>");
            result.messages.push_back("/time set <ticks>, /time add <ticks>");
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
