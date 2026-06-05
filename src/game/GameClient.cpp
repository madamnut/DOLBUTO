#include "game/GameClient.h"

#include "camera/CameraViewBob.h"
#include "game/CommandSystem.h"
#include "game/ClientUiTypes.h"
#include "game/ClientRuntimeState.h"
#include "gameplay/PlayerInventory.h"
#include "items/ItemData.h"
#include "platform/Log.h"
#include "platform/RuntimePaths.h"
#include "ui/UiSystem.h"
#include "world/TerrainBuilder.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <random>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace dolbuto
{
    namespace
    {
        constexpr float RadiansToDegrees = 57.2957795131f;
        constexpr float Pi = 3.14159265359f;
        constexpr float TwoPi = Pi * 2.0f;
        constexpr float MaxPlayerHeadYaw = Pi * 0.25f;
        constexpr float MaxPlayerHeadPitch = Pi * 70.0f / 180.0f;
        constexpr double SprintFovMultiplier = 1.15;
        constexpr double EyeHeight = 1.5625;
        constexpr double ThirdPersonDistance = 5.5;
        constexpr double FixedPhysicsTimestep = 1.0 / 20.0;
        constexpr double MaxPhysicsFrameTime = 0.25;
        constexpr double DefaultFlyMoveSpeed = 64.0;
        constexpr double DefaultGroundMoveSpeed = 4.317;
        constexpr double DefaultJumpSpeed = 8.4;
        constexpr double DefaultGravity = 32.0;
        constexpr double DefaultSprintSpeedScale = 1.3;
        constexpr double DefaultSneakSpeedScale = 0.3;
        constexpr double DefaultSneakHeightScale = 1.5 / 1.8;
        constexpr double DefaultProneHeight = 0.6;
        constexpr double DefaultProneEyeHeight = 0.5;
        constexpr double DefaultSwimSpeedScale = 0.55;
        constexpr double DefaultMovementDoubleTapWindow = 0.35;
        constexpr double DefaultFovDegrees = 60.0;
        constexpr double MinFovDegrees = 30.0;
        constexpr double MaxFovDegrees = 110.0;
        constexpr double WorldSizeBlocks = 65536.0;
        constexpr int ClimateOverlayModeCount = 7;
        constexpr size_t PlayerInventorySlotCount = gameplay::PlayerInventory::SlotCount;
        constexpr size_t PlayerStatsFileSize = sizeof(uint16_t) * 6u;
        constexpr size_t PlayerStateBaseFileSize = sizeof(double) * 4u + sizeof(float) * 2u + sizeof(uint8_t) * 2u + PlayerStatsFileSize;
        constexpr size_t PlayerInventoryLegacyFileSize = PlayerInventorySlotCount * sizeof(uint16_t) * 2u;
        constexpr size_t PlayerInventoryDurabilityFileSize = PlayerInventorySlotCount * sizeof(uint16_t) * 3u;
        constexpr size_t PlayerInventoryFileSize = PlayerInventorySlotCount * sizeof(uint16_t) * 4u;
        constexpr size_t PlayerOffhandDurabilityFileSize = sizeof(uint16_t) * 3u;
        constexpr size_t PlayerOffhandFileSize = sizeof(uint16_t) * 4u;
        constexpr size_t PlayerStateLegacyFileSize = PlayerStateBaseFileSize + PlayerInventoryLegacyFileSize;
        constexpr size_t PlayerStateDurabilityInventoryFileSize = PlayerStateBaseFileSize + PlayerInventoryDurabilityFileSize;
        constexpr size_t PlayerStateDurabilityFileSize = PlayerStateDurabilityInventoryFileSize + PlayerOffhandDurabilityFileSize;
        constexpr size_t PlayerStateInventoryFileSize = PlayerStateBaseFileSize + PlayerInventoryFileSize;
        constexpr size_t PlayerStateFileSize = PlayerStateInventoryFileSize + PlayerOffhandFileSize;
        constexpr size_t WorldStateFileSize = sizeof(uint64_t) * 4u;
        constexpr uint64_t TicksPerMinute = 20;
        constexpr uint64_t MinutesPerHour = 60;
        constexpr uint64_t HoursPerDay = 24;
        constexpr uint64_t TicksPerHour = TicksPerMinute * MinutesPerHour;
        constexpr uint64_t TicksPerDay = TicksPerHour * HoursPerDay;
        constexpr uint64_t DefaultWorldTicks = TicksPerHour * 6u;
        constexpr double SkyDebugTicksPerSecond = static_cast<double>(TicksPerHour);
        constexpr float MinSkyBrightness = 0.08f;
        constexpr uint16_t DebugFireBlockId = 15;
        constexpr float MaxSkyBrightness = 1.0f;
        constexpr uint16_t BlockGrass = 2;
        constexpr int InitialSpawnZ = 16384;
        constexpr int InitialSpawnMaxAttempts = 1024;
        constexpr float MenuButtonWidth = 240.0f;
        constexpr float MenuButtonHeight = 56.0f;
        constexpr float LobbyStartButtonY = 0.45f;
        constexpr float LobbyExitButtonY = 0.56f;
        constexpr float WorldBackButtonY = 0.72f;
        constexpr float PauseResumeButtonY = 0.46f;
        constexpr float PauseExitButtonY = 0.57f;
        constexpr size_t MaxChatMessages = 8;
        constexpr int ChatLineHeight = 20;

        double millisecondsSince(const std::chrono::steady_clock::time_point& start)
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }

        float smoothstep01(float value)
        {
            const float t = std::clamp(value, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float skyBrightnessForTicks(uint64_t worldTicks)
        {
            const double hour = static_cast<double>(worldTicks % TicksPerDay) / static_cast<double>(TicksPerHour);
            if (hour < 5.0)
            {
                return MinSkyBrightness;
            }
            if (hour < 7.0)
            {
                const float t = smoothstep01(static_cast<float>((hour - 5.0) / 2.0));
                return MinSkyBrightness + (MaxSkyBrightness - MinSkyBrightness) * t;
            }
            if (hour < 17.0)
            {
                return MaxSkyBrightness;
            }
            if (hour < 21.0)
            {
                const float t = smoothstep01(static_cast<float>((hour - 17.0) / 4.0));
                return MaxSkyBrightness + (MinSkyBrightness - MaxSkyBrightness) * t;
            }
            return MinSkyBrightness;
        }

        std::optional<double> jsonDoubleField(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t colonPos = object.find(':', keyPos + token.size());
            if (colonPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t valueStart = object.find_first_not_of(" \t\r\n", colonPos + 1);
            if (valueStart == std::string::npos)
            {
                return std::nullopt;
            }

            size_t valueEnd = valueStart;
            while (valueEnd < object.size())
            {
                const char c = object[valueEnd];
                if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
                {
                    ++valueEnd;
                    continue;
                }
                break;
            }

            if (valueEnd == valueStart)
            {
                return std::nullopt;
            }

            try
            {
                return std::stod(object.substr(valueStart, valueEnd - valueStart));
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        std::optional<std::string> jsonObjectField(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t openPos = object.find('{', keyPos + token.size());
            if (openPos == std::string::npos)
            {
                return std::nullopt;
            }

            int depth = 0;
            bool inString = false;
            bool escaped = false;
            for (size_t i = openPos; i < object.size(); ++i)
            {
                const char c = object[i];
                if (inString)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (c == '\\')
                    {
                        escaped = true;
                    }
                    else if (c == '"')
                    {
                        inString = false;
                    }
                    continue;
                }

                if (c == '"')
                {
                    inString = true;
                }
                else if (c == '{')
                {
                    ++depth;
                }
                else if (c == '}')
                {
                    --depth;
                    if (depth == 0)
                    {
                        return object.substr(openPos, i - openPos + 1);
                    }
                }
            }

            return std::nullopt;
        }

        std::optional<bool> jsonBoolField(const std::string& object, const std::string& key)
        {
            const std::string token = "\"" + key + "\"";
            const size_t keyPos = object.find(token);
            if (keyPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t colonPos = object.find(':', keyPos + token.size());
            if (colonPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t valueStart = object.find_first_not_of(" \t\r\n", colonPos + 1);
            if (valueStart == std::string::npos)
            {
                return std::nullopt;
            }

            if (object.compare(valueStart, 4, "true") == 0)
            {
                return true;
            }
            if (object.compare(valueStart, 5, "false") == 0)
            {
                return false;
            }
            return std::nullopt;
        }

        const char* facingName(float yaw)
        {
            const float x = std::cos(yaw);
            const float z = std::sin(yaw);

            if (std::abs(x) >= std::abs(z))
            {
                return x >= 0.0f ? "EAST" : "WEST";
            }

            return z >= 0.0f ? "NORTH" : "SOUTH";
        }

        double wrapWorldCoordinate(double value)
        {
            const double wrapped = value - std::floor(value / WorldSizeBlocks) * WorldSizeBlocks;
            return wrapped >= WorldSizeBlocks ? 0.0 : wrapped;
        }

        Vec3 renderViewDirection(const Camera& camera)
        {
            const Vec3 forward = camera.forward();
            return {forward.x, -forward.y, forward.z};
        }

        bool pointInButton(double x, double y, int width, int height, float centerYRatio)
        {
            const double centerX = static_cast<double>(width) * 0.5;
            const double centerY = static_cast<double>(height) * static_cast<double>(centerYRatio);
            return x >= centerX - MenuButtonWidth * 0.5 &&
                x <= centerX + MenuButtonWidth * 0.5 &&
                y >= centerY - MenuButtonHeight * 0.5 &&
                y <= centerY + MenuButtonHeight * 0.5;
        }

        float normalizeAngle(float angle)
        {
            while (angle > Pi)
            {
                angle -= TwoPi;
            }
            while (angle < -Pi)
            {
                angle += TwoPi;
            }
            return angle;
        }

        float lerpAngle(float from, float to, double alpha)
        {
            return normalizeAngle(from + normalizeAngle(to - from) * static_cast<float>(alpha));
        }

        float interpolateWalkPhase(float from, float to, double alpha)
        {
            float delta = to - from;
            if (delta < -Pi)
            {
                delta += TwoPi;
            }
            else if (delta > Pi)
            {
                delta -= TwoPi;
            }
            return normalizeAngle(from + delta * static_cast<float>(alpha));
        }

        int hotbarSlotFromKey(int key)
        {
            if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9)
            {
                return key - GLFW_KEY_1;
            }
            if (key == GLFW_KEY_0)
            {
                return 9;
            }
            return -1;
        }

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

        std::string sanitizeWorldName(std::string value)
        {
            value = trim(std::move(value));
            if (value.empty())
            {
                value = "New World";
            }

            for (char& c : value)
            {
                const unsigned char ch = static_cast<unsigned char>(c);
                if (ch < 32u || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
                {
                    c = '_';
                }
            }
            return value;
        }

        int volumePercent(double value)
        {
            return std::clamp(static_cast<int>(value * 100.0 + 0.5), 0, 100);
        }

        int roundedFovDegrees(double value)
        {
            return std::clamp(static_cast<int>(value + 0.5), static_cast<int>(MinFovDegrees), static_cast<int>(MaxFovDegrees));
        }

        uint64_t hashSeedString(std::string_view text)
        {
            uint64_t hash = 1469598103934665603ull;
            for (const unsigned char c : text)
            {
                hash ^= c;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        uint64_t parseWorldSeed(std::string value)
        {
            value = trim(std::move(value));
            if (value.empty())
            {
                return static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            }

            try
            {
                size_t parsed = 0;
                const uint64_t seed = std::stoull(value, &parsed, 10);
                if (parsed == value.size())
                {
                    return seed;
                }
            }
            catch (...)
            {
            }

            return hashSeedString(value);
        }

        uint64_t currentUnixSeconds()
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        }

        std::string formatUnixSeconds(uint64_t seconds)
        {
            if (seconds == 0)
            {
                return "----.--.-- --:--";
            }

            const std::time_t time = static_cast<std::time_t>(seconds);
            std::tm localTime{};
#if defined(_WIN32)
            localtime_s(&localTime, &time);
#else
            localtime_r(&time, &localTime);
#endif

            std::ostringstream text;
            text << std::put_time(&localTime, "%Y.%m.%d %H:%M");
            return text.str();
        }

        void writeU8(std::vector<uint8_t>& bytes, uint8_t value)
        {
            bytes.push_back(value);
        }

        void writeU16(std::vector<uint8_t>& bytes, uint16_t value)
        {
            for (int i = 0; i < 2; ++i)
            {
                bytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
            }
        }

        void writeU32(std::vector<uint8_t>& bytes, uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
            {
                bytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
            }
        }

        void writeU64(std::vector<uint8_t>& bytes, uint64_t value)
        {
            for (int i = 0; i < 8; ++i)
            {
                bytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
            }
        }

        void writeF32(std::vector<uint8_t>& bytes, float value)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            writeU32(bytes, bits);
        }

        void writeF64(std::vector<uint8_t>& bytes, double value)
        {
            uint64_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            writeU64(bytes, bits);
        }

        uint8_t readU8(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            if (offset >= bytes.size())
            {
                throw std::runtime_error("Player state read overflow.");
            }
            return bytes[offset++];
        }

        uint16_t readU16(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            if (offset + 2u > bytes.size())
            {
                throw std::runtime_error("Player state read overflow.");
            }
            uint16_t value = 0;
            for (int i = 0; i < 2; ++i)
            {
                value |= static_cast<uint16_t>(bytes[offset + static_cast<size_t>(i)] << (i * 8));
            }
            offset += 2u;
            return value;
        }

        uint32_t readU32(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            if (offset + 4u > bytes.size())
            {
                throw std::runtime_error("Player state read overflow.");
            }
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                value |= static_cast<uint32_t>(bytes[offset + static_cast<size_t>(i)]) << (i * 8);
            }
            offset += 4u;
            return value;
        }

        uint64_t readU64(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            if (offset + 8u > bytes.size())
            {
                throw std::runtime_error("Player state read overflow.");
            }
            uint64_t value = 0;
            for (int i = 0; i < 8; ++i)
            {
                value |= static_cast<uint64_t>(bytes[offset + static_cast<size_t>(i)]) << (i * 8);
            }
            offset += 8u;
            return value;
        }

        float readF32(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            const uint32_t bits = readU32(bytes, offset);
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        double readF64(const std::vector<uint8_t>& bytes, size_t& offset)
        {
            const uint64_t bits = readU64(bytes, offset);
            double value = 0.0;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }
    }

    GameClient::GameClient(GLFWwindow* window)
        : window_(window)
    {
        if (window_ == nullptr)
        {
            throw std::runtime_error("GameClient requires a valid GLFW window.");
        }

        log::info("DOLBUTO 0.0.0.3 start");
        log::info("Asset directory: " + assetDirectory().string());
        log::info("Config directory: " + configDirectory().string());
        log::info("Shader directory: " + shaderDirectory().string());
        log::info("Save root directory: " + saveRootDirectory().string());
        log::info("Log directory: " + logDirectory().string());
        loadMovementConfig();
        loadSettings();
        attachWindowCallbacks();
        runtime_ = std::make_unique<game::ClientRuntime>(window_);
        applyAudioSettings();
        updateOptionsUi();
        setScreen(AppScreen::Lobby);
        fpsSampleStart_ = std::chrono::steady_clock::now();
        lastFrameTime_ = fpsSampleStart_;
    }

    GameClient::~GameClient()
    {
        if (hasSelectedWorld_)
        {
            if (runtime_ != nullptr)
            {
                runtime_->ui().closeInventoryInteraction();
            }
            saveWorldState();
            savePlayerState();
        }
        runtime_.reset();
        if (window_ != nullptr)
        {
            glfwSetWindowUserPointer(window_, nullptr);
        }
    }

    void GameClient::run()
    {
        while (!glfwWindowShouldClose(window_))
        {
            const auto framePerfStart = std::chrono::steady_clock::now();
            auto sectionPerfStart = framePerfStart;
            glfwPollEvents();
            if (runtime_ != nullptr)
            {
                runtime_->diagnostics().recordPerformanceMax(game::ClientPerfCounter::Poll, millisecondsSince(sectionPerfStart));
            }
            if (glfwWindowShouldClose(window_))
            {
                break;
            }
            if (runtime_ != nullptr)
            {
                sectionPerfStart = std::chrono::steady_clock::now();
                while (std::optional<std::string> action = runtime_->ui().consumeAction())
                {
                    if (*action == "start")
                    {
                        setScreen(AppScreen::WorldSelect);
                    }
                    else if (action->rfind("world-open-", 0) == 0)
                    {
                        try
                        {
                            openWorldByIndex(static_cast<size_t>(std::stoull(action->substr(11))));
                        }
                        catch (...)
                        {
                            log::warn("Invalid world list action: " + *action);
                        }
                    }
                    else if (*action == "exit")
                    {
                        glfwSetWindowShouldClose(window_, GLFW_TRUE);
                    }
                    else if (*action == "new-world")
                    {
                        setScreen(AppScreen::WorldCreate);
                    }
                    else if (*action == "create-world")
                    {
                        createWorldFromUi();
                    }
                    else if (*action == "create-mode-toggle")
                    {
                        setCreateWorldGameMode(pendingCreateGameMode_ == game::GameMode::Sandbox
                            ? game::GameMode::Survival
                            : game::GameMode::Sandbox);
                    }
                    else if (*action == "back-to-lobby")
                    {
                        setScreen(AppScreen::Lobby);
                    }
                    else if (*action == "back-to-world-select")
                    {
                        setScreen(AppScreen::WorldSelect);
                    }
                    else if (*action == "resume")
                    {
                        setScreen(AppScreen::Game);
                    }
                    else if (*action == "exit-to-lobby")
                    {
                        returnToLobbyScene();
                    }
                    else if (*action == "options")
                    {
                        optionsReturnScreen_ = screen_;
                        setScreen(AppScreen::Options);
                    }
                    else if (*action == "options-back")
                    {
                        setScreen(optionsReturnScreen_);
                    }
                    else if (*action == "bgm-volume-slider" || *action == "sfx-volume-slider" || *action == "fov-slider")
                    {
                        applyOptionsSliderValues();
                    }
                    else if (*action == "toggle-sprint")
                    {
                        toggleSprintOption();
                    }
                    else if (*action == "toggle-sneak")
                    {
                        toggleSneakOption();
                    }
                    else if (*action == "toggle-prone")
                    {
                        toggleProneOption();
                    }
                    else if (*action == "toggle-view-bobbing")
                    {
                        toggleViewBobbingOption();
                    }
                }
                runtime_->diagnostics().recordPerformanceMax(game::ClientPerfCounter::UiActions, millisecondsSince(sectionPerfStart));
            }

            const auto now = std::chrono::steady_clock::now();
            const std::chrono::duration<double> delta = now - lastFrameTime_;
            lastFrameTime_ = now;

            if (screen_ == AppScreen::Game && !chatOpen_)
            {
                const bool rotateToSunrise = glfwGetKey(window_, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS;
                const bool rotateToSunset = glfwGetKey(window_, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS;
                if (rotateToSunrise != rotateToSunset)
                {
                    const uint64_t step = static_cast<uint64_t>(std::max(1.0, std::round(delta.count() * SkyDebugTicksPerSecond)));
                    const uint64_t dayStart = (worldTicks_ / TicksPerDay) * TicksPerDay;
                    const uint64_t tickOfDay = worldTicks_ % TicksPerDay;
                    const uint64_t wrappedStep = step % TicksPerDay;
                    const uint64_t nextTickOfDay = rotateToSunset
                        ? (tickOfDay + wrappedStep) % TicksPerDay
                        : (tickOfDay + TicksPerDay - wrappedStep) % TicksPerDay;
                    worldTicks_ = dayStart + nextTickOfDay;
                }

                const bool increaseClouds = glfwGetKey(window_, GLFW_KEY_KP_ADD) == GLFW_PRESS;
                const bool decreaseClouds = glfwGetKey(window_, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS;
                if (increaseClouds != decreaseClouds)
                {
                    constexpr double CloudCoverageDebugSpeed = 0.5;
                    const float deltaCoverage = static_cast<float>(delta.count() * CloudCoverageDebugSpeed);
                    cloudCoverage_ = std::clamp(
                        cloudCoverage_ + (increaseClouds ? deltaCoverage : -deltaCoverage),
                        0.0f,
                        1.0f);
                }
            }

            physicsAccumulator_ += std::min(delta.count(), MaxPhysicsFrameTime);
            const bool gameSimulationActive = screen_ == AppScreen::Game || screen_ == AppScreen::Inventory;
            sectionPerfStart = std::chrono::steady_clock::now();
            while (gameSimulationActive && physicsAccumulator_ >= FixedPhysicsTimestep)
            {
                previousPlayerPosition_ = playerPosition_;
                previousBodyYaw_ = bodyYaw_;
                previousPlayerWalkPhase_ = playerWalkPhase_;
                previousPlayerWalkAmount_ = playerWalkAmount_;
                previousSprintFovAmount_ = sprintFovAmount_;
                previousEyeHeightScale_ = eyeHeightScale_;
                updatePlayer(FixedPhysicsTimestep, screen_ == AppScreen::Game && !chatOpen_ && !radialActive_);
                if (runtime_ != nullptr)
                {
                    runtime_->gameplay().tickHeldBurningItems();
                    runtime_->gameplay().tickBlockUpdates();
                    const bool breaking = screen_ == AppScreen::Game && mouseCaptured_ && breakHeld_;
                    if (gameMode_ == game::GameMode::Sandbox)
                    {
                        if (breaking && worldTicks_ >= nextSandboxHeldBreakTick_)
                        {
                            runtime_->gameplay().updateBlockBreaking(
                                {playerPosition_.x, playerPosition_.y + currentEyeHeight(), playerPosition_.z},
                                renderViewDirection(camera_),
                                true,
                                playerPosition_,
                                static_cast<float>(FixedPhysicsTimestep),
                                true);
                            nextSandboxHeldBreakTick_ = worldTicks_ + 10u;
                        }
                        else if (!breaking)
                        {
                            runtime_->gameplay().updateBlockBreaking(
                                {playerPosition_.x, playerPosition_.y + currentEyeHeight(), playerPosition_.z},
                                renderViewDirection(camera_),
                                false,
                                playerPosition_,
                                static_cast<float>(FixedPhysicsTimestep),
                                true);
                        }
                    }
                    else
                    {
                        runtime_->gameplay().updateBlockBreaking(
                            {playerPosition_.x, playerPosition_.y + currentEyeHeight(), playerPosition_.z},
                            renderViewDirection(camera_),
                            breaking,
                            playerPosition_,
                            static_cast<float>(FixedPhysicsTimestep),
                            false);
                    }
                    if (worldTicks_ % 5u == 0u)
                    {
                        runtime_->gameplay().tickFluidSimulation();
                    }
                }
                ++worldTicks_;
                physicsAccumulator_ -= FixedPhysicsTimestep;
            }
            if (!gameSimulationActive)
            {
                if (runtime_ != nullptr)
                {
                    runtime_->gameplay().updateBlockBreaking(
                        {playerPosition_.x, playerPosition_.y + currentEyeHeight(), playerPosition_.z},
                        renderViewDirection(camera_),
                        false,
                        playerPosition_,
                        static_cast<float>(FixedPhysicsTimestep),
                        gameMode_ == game::GameMode::Sandbox);
                }
                physicsAccumulator_ = 0.0;
                previousPlayerPosition_ = playerPosition_;
                previousBodyYaw_ = bodyYaw_;
                previousPlayerWalkPhase_ = playerWalkPhase_;
                previousPlayerWalkAmount_ = playerWalkAmount_;
                previousSprintFovAmount_ = sprintFovAmount_;
                previousEyeHeightScale_ = eyeHeightScale_;
            }
            if (runtime_ != nullptr)
            {
                runtime_->diagnostics().recordPerformanceMax(game::ClientPerfCounter::Physics, millisecondsSince(sectionPerfStart));
            }

            const double physicsAlpha = std::clamp(physicsAccumulator_ / FixedPhysicsTimestep, 0.0, 1.0);
            const DVec3 renderPlayerPosition = interpolatedPlayerPosition(physicsAlpha);
            const float renderBodyYaw = lerpAngle(previousBodyYaw_, bodyYaw_, physicsAlpha);
            const float renderWalkPhase = interpolateWalkPhase(previousPlayerWalkPhase_, playerWalkPhase_, physicsAlpha);
            const float renderWalkAmount = std::clamp(
                static_cast<float>(static_cast<double>(previousPlayerWalkAmount_) + (static_cast<double>(playerWalkAmount_) - static_cast<double>(previousPlayerWalkAmount_)) * physicsAlpha),
                0.0f,
                1.35f);
            const double walkDeltaX = playerPosition_.x - previousPlayerPosition_.x;
            const double walkDeltaZ = playerPosition_.z - previousPlayerPosition_.z;
            const double walkForwardX = std::cos(static_cast<double>(renderBodyYaw));
            const double walkForwardZ = std::sin(static_cast<double>(renderBodyYaw));
            const bool renderWalkReverse =
                renderWalkAmount > 0.01f &&
                walkDeltaX * walkDeltaX + walkDeltaZ * walkDeltaZ > 0.000001 &&
                walkDeltaX * walkForwardX + walkDeltaZ * walkForwardZ < -0.000001;
            const double renderSprintFovAmount = std::clamp(
                static_cast<double>(previousSprintFovAmount_) + (static_cast<double>(sprintFovAmount_) - static_cast<double>(previousSprintFovAmount_)) * physicsAlpha,
                0.0,
                1.0);
            const double worldFovDegrees = fovDegrees_ * (1.0 + (SprintFovMultiplier - 1.0) * renderSprintFovAmount);
            const DVec3 eyePosition{renderPlayerPosition.x, renderPlayerPosition.y + interpolatedEyeHeight(physicsAlpha), renderPlayerPosition.z};
            if (screen_ == AppScreen::Game || screen_ == AppScreen::Inventory)
            {
                runtime_->gameplay().updateBlockSelection(
                    {playerPosition_.x, playerPosition_.y + currentEyeHeight(), playerPosition_.z},
                    renderViewDirection(camera_));
            }
            sectionPerfStart = std::chrono::steady_clock::now();
            updateDebugText();
            updatePlayerStatsUi();
            if (runtime_ != nullptr)
            {
                runtime_->diagnostics().recordPerformanceMax(game::ClientPerfCounter::DebugText, millisecondsSince(sectionPerfStart));
            }
            Camera renderCamera = camera_;
            DVec3 renderCameraPosition = eyePosition;
            bool showPlayer = false;
            if (viewMode_ == ViewMode::ThirdPersonRear)
            {
                const Vec3 forward = renderViewDirection(camera_);
                renderCameraPosition = {
                    eyePosition.x - forward.x * ThirdPersonDistance,
                    eyePosition.y - forward.y * ThirdPersonDistance,
                    eyePosition.z - forward.z * ThirdPersonDistance
                };
                showPlayer = true;
            }
            else if (viewMode_ == ViewMode::ThirdPersonFront)
            {
                const Vec3 forward = renderViewDirection(camera_);
                renderCameraPosition = {
                    eyePosition.x + forward.x * ThirdPersonDistance,
                    eyePosition.y + forward.y * ThirdPersonDistance,
                    eyePosition.z + forward.z * ThirdPersonDistance
                };
                renderCamera.setAngles(camera_.yaw() + Pi, -camera_.pitch());
                showPlayer = true;
            }
            const bool renderPlayerInWater = runtime_ != nullptr &&
                runtime_->gameplay().playerColliderIntersectsWater(renderPlayerPosition, currentPlayerHeightScale());
            const bool viewBobEnabled = viewBobbing_ &&
                moveMode_ == MoveMode::Ground &&
                !renderPlayerInWater &&
                !waterClimbActive_ &&
                (screen_ == AppScreen::Game || screen_ == AppScreen::Inventory);
            const Vec3 viewBobOffset = CameraViewBob::offset(CameraViewBobInput{
                viewBobEnabled,
                renderCamera.yaw(),
                renderWalkPhase,
                renderWalkAmount
            });
            renderCameraPosition.x += static_cast<double>(viewBobOffset.x);
            renderCameraPosition.y += static_cast<double>(viewBobOffset.y);
            renderCameraPosition.z += static_cast<double>(viewBobOffset.z);

            const int menuOverlayMode = screen_ == AppScreen::Lobby ? 1 : (screen_ == AppScreen::Pause ? 2 : (screen_ == AppScreen::WorldSelect ? 3 : (screen_ == AppScreen::WorldCreate ? 4 : (screen_ == AppScreen::Inventory ? 5 : (screen_ == AppScreen::Options ? 6 : 0)))));
            const bool optionsOverGame = screen_ == AppScreen::Options && optionsReturnScreen_ == AppScreen::Pause;
            const bool worldUpdateEnabled = screen_ == AppScreen::Game || screen_ == AppScreen::Pause || screen_ == AppScreen::Inventory || optionsOverGame;
            const bool gameSceneRenderEnabled = screen_ == AppScreen::Game || screen_ == AppScreen::Pause || screen_ == AppScreen::Inventory || optionsOverGame;
            const bool renderDebugText = (screen_ == AppScreen::Game || screen_ == AppScreen::Inventory) && debugTextVisible_;
            const bool frameHudVisible = hudVisible_ || chatOpen_ || radialActive_;
            float playerHeadYaw = 0.0f;
            float playerHeadPitch = 0.0f;
            updatePlayerLookPose(renderBodyYaw, playerHeadYaw, playerHeadPitch);
            uint16_t heldItemId = 0;
            uint16_t heldPortableLightEmission = 0;
            if (runtime_ != nullptr)
            {
                const std::array<ItemStack, PlayerInventorySlotCount> inventorySlots = runtime_->gameplay().inventorySnapshot();
                const ItemStack& heldStack = inventorySlots[static_cast<std::size_t>(std::clamp(hotbarSelectedSlot_, 0, 9))];
                if (heldStack.count > 0)
                {
                    heldItemId = heldStack.itemId;
                }
                heldPortableLightEmission = runtime_->gameplay().heldPortableLightEmission();
            }
            const bool showFirstPersonHand = gameSceneRenderEnabled && menuOverlayMode == 0 && viewMode_ == ViewMode::FirstPerson;
            game::RadialMenuRenderFrame radialMenuRenderFrame{};
            radialMenuRenderFrame.visible = radialActive_;
            radialMenuRenderFrame.actionCount = static_cast<uint32_t>(radialActions_.size());
            if (radialSelectedActionIndex_.has_value() && *radialSelectedActionIndex_ < radialActions_.size())
            {
                radialMenuRenderFrame.selectedActionIndex = static_cast<uint32_t>(*radialSelectedActionIndex_);
                const std::vector<ItemInteractionCandidate>& candidates = radialActions_[*radialSelectedActionIndex_].candidates;
                radialMenuRenderFrame.candidateCount = static_cast<uint32_t>(candidates.size());
                radialMenuRenderFrame.candidateEnabled.reserve(candidates.size());
                for (const ItemInteractionCandidate& candidate : candidates)
                {
                    radialMenuRenderFrame.candidateEnabled.push_back(static_cast<uint8_t>(candidate.enabled ? 1u : 0u));
                }
            }
            if (radialSelectedCandidateIndex_.has_value())
            {
                radialMenuRenderFrame.selectedCandidateIndex = static_cast<uint32_t>(*radialSelectedCandidateIndex_);
            }
            const bool playerProne = moveMode_ == MoveMode::Ground &&
                static_cast<double>(playerHeightScale_) <= proneHeight_ / 1.75 + 0.001;
            const bool playerCrouching = moveMode_ == MoveMode::Ground &&
                !playerProne &&
                static_cast<double>(playerHeightScale_) <= sneakHeightScale_ + 0.001;
            const bool playerSprinting = !playerCrouching &&
                !playerProne &&
                renderSprintFovAmount > 0.01;
            sectionPerfStart = std::chrono::steady_clock::now();
            runtime_->render().frame(game::ClientFrame{
                renderCamera,
                renderCameraPosition,
                static_cast<float>(worldFovDegrees * Pi / 180.0),
                skyBrightnessForTicks(worldTicks_),
                cloudCoverage_,
                debugText_.data(),
                "",
                renderDebugText,
                screenshotRequested_,
                showPlayer,
                renderPlayerPosition,
                renderBodyYaw,
                playerHeadYaw,
                playerHeadPitch,
                renderWalkPhase,
                renderWalkAmount,
                renderWalkReverse,
                playerCrouching,
                playerSprinting,
                playerProne,
                showFirstPersonHand,
                heldItemId,
                heldPortableLightEmission,
                terrainWireframe_,
                climateOverlayMode_,
                menuOverlayMode,
                frameHudVisible,
                worldUpdateEnabled,
                gameSceneRenderEnabled,
                worldTicks_,
                radialMenuRenderFrame
            });
            runtime_->diagnostics().recordPerformanceMax(game::ClientPerfCounter::RenderCall, millisecondsSince(sectionPerfStart));
            runtime_->diagnostics().recordPerformanceMax(game::ClientPerfCounter::Frame, millisecondsSince(framePerfStart));
            screenshotRequested_ = false;
        }
    }

    void GameClient::attachWindowCallbacks()
    {
        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* window, int, int)
        {
            auto* app = static_cast<GameClient*>(glfwGetWindowUserPointer(window));
            if (app != nullptr && app->runtime_ != nullptr)
            {
                app->runtime_->render().notifyFramebufferResized();
            }
        });

        glfwSetCursorPosCallback(window_, [](GLFWwindow* window, double x, double y)
        {
            auto* app = static_cast<GameClient*>(glfwGetWindowUserPointer(window));
            if (app != nullptr)
            {
                app->handleMouse(x, y);
            }
        });

        glfwSetKeyCallback(window_, [](GLFWwindow* window, int key, int, int action, int mods)
        {
            auto* app = static_cast<GameClient*>(glfwGetWindowUserPointer(window));
            if (app != nullptr && app->screen_ == GameClient::AppScreen::Game && app->chatOpen_ &&
                (action == GLFW_PRESS || action == GLFW_REPEAT || action == GLFW_RELEASE))
            {
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
                {
                    app->closeChatInput();
                    return;
                }
                if (action == GLFW_PRESS && key == GLFW_KEY_ENTER)
                {
                    app->submitChatInput();
                    return;
                }
                if (app->runtime_ != nullptr)
                {
                    app->runtime_->ui().key(key, action != GLFW_RELEASE, mods);
                }
                return;
            }
            if (app != nullptr && app->radialActive_ &&
                (action == GLFW_PRESS || action == GLFW_REPEAT || action == GLFW_RELEASE))
            {
                if (action == GLFW_PRESS && key == GLFW_KEY_F2)
                {
                    app->screenshotRequested_ = true;
                }
                else if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
                {
                    app->closeRadialInteraction(false);
                }
                return;
            }
            if (app != nullptr && app->screen_ == GameClient::AppScreen::Game &&
                !app->chatOpen_ && key == GLFW_KEY_ENTER && action == GLFW_PRESS)
            {
                app->openChatInput();
                return;
            }
            if (app != nullptr && app->screen_ != GameClient::AppScreen::Game && app->runtime_ != nullptr &&
                (action == GLFW_PRESS || action == GLFW_REPEAT || action == GLFW_RELEASE))
            {
                app->runtime_->ui().key(key, action != GLFW_RELEASE, mods);
            }
            if (key == GLFW_KEY_SPACE && app != nullptr && app->screen_ == GameClient::AppScreen::Game)
            {
                if (action == GLFW_PRESS)
                {
                    const double now = glfwGetTime();
                    bool toggledMoveMode = false;
                    const bool canToggleMoveMode = app->gameMode_ == game::GameMode::Sandbox;
                    if (canToggleMoveMode && now - app->lastJumpTapTime_ <= app->movementDoubleTapWindow_)
                    {
                        if (app->moveMode_ == MoveMode::Ground)
                        {
                            app->moveMode_ = MoveMode::Fly;
                            app->verticalVelocity_ = 0.0;
                            app->grounded_ = false;
                            app->sprintToggled_ = false;
                            app->sneakToggled_ = false;
                            app->proneToggled_ = false;
                            app->playerHeightScale_ = 1.0f;
                            app->proneClimbActive_ = false;
                            app->proneClimbProgress_ = 0.0;
                            app->waterClimbActive_ = false;
                            app->waterClimbProgress_ = 0.0;
                        }
                        else
                        {
                            app->moveMode_ = MoveMode::Ground;
                            app->verticalVelocity_ = 0.0;
                            app->grounded_ = false;
                            app->doubleTapSprintActive_ = false;
                        }
                        app->jumpPressed_ = false;
                        app->jumpHeld_ = false;
                        app->lastJumpTapTime_ = -1000.0;
                        toggledMoveMode = true;
                    }
                    else
                    {
                        if (app->moveMode_ == MoveMode::Ground)
                        {
                            app->jumpPressed_ = true;
                        }
                        app->lastJumpTapTime_ = now;
                    }
                    if (!toggledMoveMode)
                    {
                        app->jumpHeld_ = true;
                    }
                }
                else if (action == GLFW_RELEASE)
                {
                    app->jumpHeld_ = false;
                }
            }
            else if (key == GLFW_KEY_W && app != nullptr && app->screen_ == GameClient::AppScreen::Game)
            {
                if (action == GLFW_PRESS)
                {
                    const double now = glfwGetTime();
                    if (now - app->lastForwardTapTime_ <= app->movementDoubleTapWindow_)
                    {
                        if (app->toggleSprint_)
                        {
                            app->sprintToggled_ = !app->sprintToggled_;
                        }
                        else
                        {
                            app->doubleTapSprintActive_ = true;
                        }
                    }
                    app->lastForwardTapTime_ = now;
                }
                else if (action == GLFW_RELEASE && !app->toggleSprint_)
                {
                    app->doubleTapSprintActive_ = false;
                }
            }
            else if ((key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL) &&
                app != nullptr && app->screen_ == GameClient::AppScreen::Game && action == GLFW_PRESS &&
                app->toggleSprint_ && app->moveMode_ == MoveMode::Ground)
            {
                app->sprintToggled_ = !app->sprintToggled_;
            }
            else if ((key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) &&
                app != nullptr && app->screen_ == GameClient::AppScreen::Game && action == GLFW_PRESS &&
                app->toggleSneak_ && app->moveMode_ == MoveMode::Ground)
            {
                app->sneakToggled_ = !app->sneakToggled_;
            }
            else if (key == GLFW_KEY_Z &&
                app != nullptr && app->screen_ == GameClient::AppScreen::Game && action == GLFW_PRESS &&
                app->toggleProne_ && app->moveMode_ == MoveMode::Ground)
            {
                app->proneToggled_ = !app->proneToggled_;
            }

            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            {
                if (app != nullptr && app->screen_ == GameClient::AppScreen::Game)
                {
                    app->setScreen(GameClient::AppScreen::Pause);
                }
                else if (app != nullptr && app->screen_ == GameClient::AppScreen::Inventory)
                {
                    app->setScreen(GameClient::AppScreen::Game);
                }
                else if (app != nullptr && app->screen_ == GameClient::AppScreen::Pause)
                {
                    app->setScreen(GameClient::AppScreen::Game);
                }
                else if (app != nullptr && app->screen_ == GameClient::AppScreen::Options)
                {
                    app->setScreen(app->optionsReturnScreen_);
                }
                else if (app != nullptr && (app->screen_ == GameClient::AppScreen::WorldSelect || app->screen_ == GameClient::AppScreen::WorldCreate))
                {
                    app->setScreen(app->screen_ == GameClient::AppScreen::WorldCreate ? GameClient::AppScreen::WorldSelect : GameClient::AppScreen::Lobby);
                }
            }
            else if (key == GLFW_KEY_F11 && action == GLFW_PRESS && app != nullptr)
            {
                app->toggleFullscreen();
            }
            else if (key == GLFW_KEY_F3 && action == GLFW_PRESS && app != nullptr)
            {
                app->debugTextVisible_ = !app->debugTextVisible_;
            }
            else if (key == GLFW_KEY_R && action == GLFW_PRESS && app != nullptr && app->runtime_ != nullptr &&
                !app->chatOpen_ && (app->screen_ == GameClient::AppScreen::Game || app->screen_ == GameClient::AppScreen::Inventory))
            {
                app->runtime_->gameplay().swapSelectedHotbarWithOffhand();
            }
            else if (key == GLFW_KEY_F1 && action == GLFW_PRESS && app != nullptr)
            {
                app->hudVisible_ = !app->hudVisible_;
            }
            else if (key == GLFW_KEY_F2 && action == GLFW_PRESS && app != nullptr)
            {
                app->screenshotRequested_ = true;
            }
            else if (key == GLFW_KEY_F4 && action == GLFW_PRESS && app != nullptr)
            {
                app->terrainWireframe_ = !app->terrainWireframe_;
            }
            else if (key == GLFW_KEY_F5 && action == GLFW_PRESS && app != nullptr)
            {
                app->cycleViewMode();
            }
            else if (key == GLFW_KEY_F6 && action == GLFW_PRESS && app != nullptr)
            {
                app->climateOverlayMode_ = (app->climateOverlayMode_ + 1) % ClimateOverlayModeCount;
            }
            else if (key == GLFW_KEY_E && action == GLFW_PRESS && app != nullptr)
            {
                if (app->screen_ == GameClient::AppScreen::Game)
                {
                    app->setScreen(GameClient::AppScreen::Inventory);
                }
                else if (app->screen_ == GameClient::AppScreen::Inventory)
                {
                    app->setScreen(GameClient::AppScreen::Game);
                }
            }
            else if (const int slot = hotbarSlotFromKey(key); action == GLFW_PRESS && app != nullptr && app->screen_ == GameClient::AppScreen::Game && slot >= 0)
            {
                app->setHotbarSelectedSlot(slot);
            }
            else if (key == GLFW_KEY_F && action == GLFW_PRESS && app != nullptr)
            {
                if (app->screen_ == GameClient::AppScreen::Game && app->runtime_ != nullptr)
                {
                    app->runtime_->gameplay().pickupDroppedItemInView(
                        {app->playerPosition_.x, app->playerPosition_.y + app->currentEyeHeight(), app->playerPosition_.z},
                        renderViewDirection(app->camera_));
                }
            }
            else if (key == GLFW_KEY_T && action == GLFW_PRESS && app != nullptr)
            {
                if (app->screen_ == GameClient::AppScreen::Game && !app->chatOpen_ && app->runtime_ != nullptr)
                {
                    app->runtime_->gameplay().editBlockInView(
                        {app->playerPosition_.x, app->playerPosition_.y + app->currentEyeHeight(), app->playerPosition_.z},
                        renderViewDirection(app->camera_),
                        true,
                        DebugFireBlockId,
                        app->playerPosition_,
                        app->currentPlayerHeightScale());
                }
            }
            else if (key == GLFW_KEY_Q && action == GLFW_PRESS && app != nullptr)
            {
                if (app->screen_ == GameClient::AppScreen::Game && app->runtime_ != nullptr)
                {
                    const bool wholeStack = (mods & GLFW_MOD_CONTROL) != 0;
                    app->runtime_->gameplay().dropSelectedHotbarItem(
                        wholeStack,
                        {app->playerPosition_.x, app->playerPosition_.y + app->currentEyeHeight(), app->playerPosition_.z},
                        renderViewDirection(app->camera_));
                }
            }
        });

        glfwSetCharCallback(window_, [](GLFWwindow* window, unsigned int codepoint)
        {
            auto* app = static_cast<GameClient*>(glfwGetWindowUserPointer(window));
            if (app != nullptr && (app->screen_ != GameClient::AppScreen::Game || app->chatOpen_) && app->runtime_ != nullptr)
            {
                app->runtime_->ui().textInput(codepoint);
            }
        });

        glfwSetMouseButtonCallback(window_, [](GLFWwindow* window, int button, int action, int mods)
        {
            auto* app = static_cast<GameClient*>(glfwGetWindowUserPointer(window));
            if (app == nullptr)
            {
                return;
            }

            if (app->screen_ == GameClient::AppScreen::Game && app->chatOpen_)
            {
                double x = 0.0;
                double y = 0.0;
                glfwGetCursorPos(window, &x, &y);
                if (app->runtime_ != nullptr && (action == GLFW_PRESS || action == GLFW_RELEASE))
                {
                    app->runtime_->ui().mouseMove(x, y);
                    app->runtime_->ui().mouseButton(button, action == GLFW_PRESS, mods);
                }
                return;
            }

            if (app->screen_ != GameClient::AppScreen::Game)
            {
                double x = 0.0;
                double y = 0.0;
                glfwGetCursorPos(window, &x, &y);
                if (app->runtime_ != nullptr && (action == GLFW_PRESS || action == GLFW_RELEASE))
                {
                    app->runtime_->ui().mouseMove(x, y);
                    app->runtime_->ui().mouseButton(button, action == GLFW_PRESS, mods);
                }
                if (action == GLFW_PRESS && (app->runtime_ == nullptr || !app->runtime_->ui().available()))
                {
                    app->handleMenuClick(x, y);
                }
                return;
            }

            if (app->radialActive_)
            {
                if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE)
                {
                    app->closeRadialInteraction(true);
                }
                return;
            }

            if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
            {
                app->breakHeld_ = false;
                if (app->runtime_ != nullptr)
                {
                    app->runtime_->gameplay().updateBlockBreaking(
                        {app->playerPosition_.x, app->playerPosition_.y + app->currentEyeHeight(), app->playerPosition_.z},
                        renderViewDirection(app->camera_),
                        false,
                        app->playerPosition_,
                        static_cast<float>(FixedPhysicsTimestep),
                        app->gameMode_ == game::GameMode::Sandbox);
                }
                return;
            }

            if (action != GLFW_PRESS)
            {
                return;
            }

            if (button == GLFW_MOUSE_BUTTON_MIDDLE)
            {
                app->setMouseCaptured(false);
            }
            else if (button == GLFW_MOUSE_BUTTON_LEFT)
            {
                if (app->mouseCaptured_)
                {
                    app->breakHeld_ = true;
                    if (app->gameMode_ == game::GameMode::Sandbox && app->runtime_ != nullptr)
                    {
                        app->runtime_->gameplay().updateBlockBreaking(
                            {app->playerPosition_.x, app->playerPosition_.y + app->currentEyeHeight(), app->playerPosition_.z},
                            renderViewDirection(app->camera_),
                            true,
                            app->playerPosition_,
                            static_cast<float>(FixedPhysicsTimestep),
                            true);
                        app->nextSandboxHeldBreakTick_ = app->worldTicks_ + 10u;
                    }
                }
                app->setMouseCaptured(true);
            }
            else if (button == GLFW_MOUSE_BUTTON_RIGHT)
            {
                if (app->mouseCaptured_ && app->runtime_ != nullptr)
                {
                    const bool preferHeldItemBlockActions = (mods & GLFW_MOD_SHIFT) != 0;
                    if (app->openRadialInteraction(preferHeldItemBlockActions))
                    {
                        return;
                    }
                    app->runtime_->gameplay().placeSelectedItemBlockInView(
                        {app->playerPosition_.x, app->playerPosition_.y + app->currentEyeHeight(), app->playerPosition_.z},
                        renderViewDirection(app->camera_),
                        app->playerPosition_,
                        app->currentPlayerHeightScale());
                }
                else
                {
                    app->setMouseCaptured(true);
                }
            }
        });

        glfwSetScrollCallback(window_, [](GLFWwindow* window, double, double yOffset)
        {
            auto* app = static_cast<GameClient*>(glfwGetWindowUserPointer(window));
            if (app == nullptr)
            {
                return;
            }

            if (app->screen_ == GameClient::AppScreen::Game)
            {
                if (app->radialActive_)
                {
                    return;
                }
                if (app->chatOpen_)
                {
                    if (app->runtime_ != nullptr)
                    {
                        double x = 0.0;
                        double y = 0.0;
                        glfwGetCursorPos(window, &x, &y);
                        app->runtime_->ui().mouseMove(x, y);
                        app->runtime_->ui().mouseWheel(yOffset);
                    }
                    return;
                }
                if (yOffset > 0.0)
                {
                    app->cycleHotbarSelectedSlot(-1);
                }
                else if (yOffset < 0.0)
                {
                    app->cycleHotbarSelectedSlot(1);
                }
                return;
            }

            if (app->runtime_ != nullptr)
            {
                double x = 0.0;
                double y = 0.0;
                glfwGetCursorPos(window, &x, &y);
                app->runtime_->ui().mouseMove(x, y);
                app->runtime_->ui().mouseWheel(yOffset);
            }
        });

        setMouseCaptured(false);
    }

    void GameClient::handleMouse(double x, double y)
    {
        if (radialActive_)
        {
            updateRadialSelection(x, y);
            return;
        }

        if (screen_ != AppScreen::Game)
        {
            if (runtime_ != nullptr)
            {
                runtime_->ui().mouseMove(x, y);
            }
            return;
        }

        if (chatOpen_)
        {
            if (runtime_ != nullptr)
            {
                runtime_->ui().mouseMove(x, y);
            }
            return;
        }

        if (!mouseCaptured_ || screen_ != AppScreen::Game)
        {
            return;
        }

        if (firstMouse_)
        {
            lastMouseX_ = x;
            lastMouseY_ = y;
            firstMouse_ = false;
            return;
        }

        camera_.rotate(static_cast<float>(x - lastMouseX_), static_cast<float>(y - lastMouseY_));
        lastMouseX_ = x;
        lastMouseY_ = y;
    }

    bool GameClient::openRadialInteraction(bool preferHeldItemBlockActions)
    {
        if (runtime_ == nullptr || screen_ != AppScreen::Game || chatOpen_)
        {
            return false;
        }

        const gameplay::ItemInteractionMenu menu = runtime_->gameplay().beginItemInteractionInView(
            {playerPosition_.x, playerPosition_.y + currentEyeHeight(), playerPosition_.z},
            renderViewDirection(camera_),
            preferHeldItemBlockActions);
        if (!menu.available || menu.actions.empty())
        {
            return menu.hasUseTarget;
        }

        int width = 0;
        int height = 0;
        glfwGetWindowSize(window_, &width, &height);
        if (width <= 0 || height <= 0)
        {
            return false;
        }

        radialActive_ = true;
        radialRestoreMouseCaptured_ = mouseCaptured_;
        radialActions_ = menu.actions;
        radialSelectedActionIndex_.reset();
        radialSelectedCandidateIndex_.reset();
        radialCenterX_ = static_cast<double>(width) * 0.5;
        radialCenterY_ = static_cast<double>(height) * 0.5;
        breakHeld_ = false;

        runtime_->ui().setRadialMenu(radialActions_, radialSelectedActionIndex_, radialSelectedCandidateIndex_);
        setMouseCaptured(false);
        glfwSetCursorPos(window_, radialCenterX_, radialCenterY_);
        runtime_->ui().mouseMove(radialCenterX_, radialCenterY_);
        return true;
    }

    void GameClient::updateRadialSelection(double x, double y)
    {
        if (!radialActive_ || runtime_ == nullptr || radialActions_.empty())
        {
            return;
        }

        constexpr double DeadZoneRadius = 56.0;
        constexpr double ActionOuterRadius = 132.0;
        constexpr double TwoPi = 2.0 * static_cast<double>(Pi);
        const double startAngle = -0.5 * static_cast<double>(Pi);
        const double dx = x - radialCenterX_;
        const double dy = y - radialCenterY_;
        const double distanceSquared = dx * dx + dy * dy;
        double relativeAngle = std::atan2(dy, dx) - startAngle;
        while (relativeAngle < 0.0)
        {
            relativeAngle += TwoPi;
        }
        while (relativeAngle >= TwoPi)
        {
            relativeAngle -= TwoPi;
        }

        const auto sectionIndex = [&](std::size_t count) -> std::size_t
        {
            const double step = TwoPi / static_cast<double>(count);
            std::size_t index = static_cast<std::size_t>(std::floor(relativeAngle / step));
            if (index >= count)
            {
                index = 0;
            }
            return index;
        };

        std::optional<std::size_t> selectedAction;
        std::optional<std::size_t> selectedCandidate;
        if (distanceSquared >= DeadZoneRadius * DeadZoneRadius)
        {
            if (distanceSquared < ActionOuterRadius * ActionOuterRadius)
            {
                selectedAction = sectionIndex(radialActions_.size());
            }
            else
            {
                selectedAction = radialSelectedActionIndex_;
                if (!selectedAction.has_value())
                {
                    selectedAction = sectionIndex(radialActions_.size());
                }
                if (selectedAction.has_value() && *selectedAction < radialActions_.size())
                {
                    const std::size_t candidateCount = radialActions_[*selectedAction].candidates.size();
                    if (candidateCount > 0)
                    {
                        const double actionStep = TwoPi / static_cast<double>(radialActions_.size());
                        const double actionStart = actionStep * static_cast<double>(*selectedAction);
                        const double actionEnd = actionStart + actionStep;
                        if (relativeAngle >= actionStart && relativeAngle < actionEnd)
                        {
                            const double candidateStep = actionStep / static_cast<double>(candidateCount);
                            selectedCandidate = static_cast<std::size_t>(std::floor((relativeAngle - actionStart) / candidateStep));
                            if (*selectedCandidate >= candidateCount)
                            {
                                selectedCandidate = candidateCount - 1;
                            }
                        }
                    }
                }
            }
        }

        if (selectedAction == radialSelectedActionIndex_ && selectedCandidate == radialSelectedCandidateIndex_)
        {
            return;
        }

        radialSelectedActionIndex_ = selectedAction;
        radialSelectedCandidateIndex_ = selectedCandidate;
        runtime_->ui().setRadialMenu(radialActions_, radialSelectedActionIndex_, radialSelectedCandidateIndex_);
    }

    void GameClient::closeRadialInteraction(bool execute)
    {
        if (!radialActive_)
        {
            return;
        }

        if (runtime_ != nullptr)
        {
            if (execute && radialSelectedActionIndex_.has_value() && radialSelectedCandidateIndex_.has_value())
            {
                const bool repeat = window_ != nullptr &&
                    (glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                        glfwGetKey(window_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
                runtime_->gameplay().executePendingItemInteraction(*radialSelectedActionIndex_, *radialSelectedCandidateIndex_, repeat);
            }
            else
            {
                runtime_->gameplay().cancelPendingItemInteraction();
            }
            runtime_->ui().hideRadialMenu();
        }

        radialActive_ = false;
        radialActions_.clear();
        radialSelectedActionIndex_.reset();
        radialSelectedCandidateIndex_.reset();
        if (screen_ == AppScreen::Game)
        {
            setMouseCaptured(radialRestoreMouseCaptured_);
        }
    }

    void GameClient::toggleFullscreen()
    {
        fullscreen_ = !fullscreen_;

        if (fullscreen_)
        {
            glfwGetWindowPos(window_, &windowedX_, &windowedY_);
            glfwGetWindowSize(window_, &windowedWidth_, &windowedHeight_);

            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
        else
        {
            glfwSetWindowMonitor(window_, nullptr, windowedX_, windowedY_, windowedWidth_, windowedHeight_, GLFW_DONT_CARE);
        }

        if (runtime_ != nullptr)
        {
            runtime_->render().notifyFramebufferResized();
        }
    }

    void GameClient::setHotbarSelectedSlot(int slot)
    {
        hotbarSelectedSlot_ = std::clamp(slot, 0, 9);
        if (runtime_ != nullptr)
        {
            runtime_->ui().setHotbarSelectedSlot(hotbarSelectedSlot_);
        }
    }

    void GameClient::cycleHotbarSelectedSlot(int delta)
    {
        setHotbarSelectedSlot((hotbarSelectedSlot_ + delta + 10) % 10);
    }

    void GameClient::setMouseCaptured(bool captured)
    {
        mouseCaptured_ = captured;
        firstMouse_ = true;
        glfwSetInputMode(window_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }

    void GameClient::openChatInput()
    {
        if (screen_ != AppScreen::Game || runtime_ == nullptr || chatOpen_)
        {
            return;
        }

        chatOpen_ = true;
        chatRestoreMouseCaptured_ = mouseCaptured_;
        breakHeld_ = false;
        runtime_->ui().setChatVisible(true, !chatMessages_.empty());
        runtime_->ui().clearChatInput();
        updateChatUi();
        runtime_->ui().focusChatInput();
        setMouseCaptured(false);
    }

    void GameClient::closeChatInput()
    {
        if (!chatOpen_)
        {
            return;
        }

        chatOpen_ = false;
        breakHeld_ = false;
        if (runtime_ != nullptr)
        {
            runtime_->ui().clearChatInput();
            runtime_->ui().setChatVisible(false, !chatMessages_.empty());
        }
        if (screen_ == AppScreen::Game && chatRestoreMouseCaptured_)
        {
            setMouseCaptured(true);
        }
    }

    void GameClient::submitChatInput()
    {
        if (!chatOpen_ || runtime_ == nullptr)
        {
            closeChatInput();
            return;
        }

        const std::string text = trim(runtime_->ui().chatInputValue());
        if (!text.empty())
        {
            if (text.front() == '/')
            {
                game::CommandResult commandResult = game::executeCommand(text, game::CommandContext{
                    playerPosition_,
                    worldSeed_,
                    worldTicks_,
                    playerStats_.hp,
                    playerStats_.maxHp,
                    playerStats_.hunger,
                    playerStats_.maxHunger,
                    playerStats_.thirst,
                    playerStats_.maxThirst,
                    gameMode_
                });

                if (commandResult.teleportPosition)
                {
                    playerPosition_ = *commandResult.teleportPosition;
                    previousPlayerPosition_ = playerPosition_;
                    verticalVelocity_ = 0.0;
                    grounded_ = false;
                    jumpHeld_ = false;
                    jumpPressed_ = false;
                    proneClimbActive_ = false;
                    proneClimbProgress_ = 0.0;
                    waterClimbActive_ = false;
                    waterClimbProgress_ = 0.0;
                    physicsAccumulator_ = 0.0;
                }
                if (commandResult.worldTicks)
                {
                    worldTicks_ = *commandResult.worldTicks;
                }
                if (commandResult.playerHp)
                {
                    playerStats_.hp = *commandResult.playerHp;
                }
                if (commandResult.playerHunger)
                {
                    playerStats_.hunger = *commandResult.playerHunger;
                }
                if (commandResult.playerThirst)
                {
                    playerStats_.thirst = *commandResult.playerThirst;
                }
                if (commandResult.playerHp || commandResult.playerHunger || commandResult.playerThirst)
                {
                    playerStats_.clamp();
                    updatePlayerStatsUi();
                }
                if (commandResult.gameMode)
                {
                    applyGameMode(*commandResult.gameMode);
                }
                for (const std::string& message : commandResult.messages)
                {
                    appendChatSystemMessage(message);
                }
            }
            else
            {
                appendChatMessage(text);
            }
        }
        closeChatInput();
    }

    void GameClient::appendChatMessage(std::string_view text)
    {
        if (text.empty())
        {
            return;
        }

        chatMessages_.push_back("You: " + std::string(text));
        while (chatMessages_.size() > MaxChatMessages)
        {
            chatMessages_.erase(chatMessages_.begin());
        }
        updateChatUi();
    }

    void GameClient::appendChatSystemMessage(std::string_view text)
    {
        if (text.empty())
        {
            return;
        }

        chatMessages_.push_back("System: " + std::string(text));
        while (chatMessages_.size() > MaxChatMessages)
        {
            chatMessages_.erase(chatMessages_.begin());
        }
        updateChatUi();
    }

    void GameClient::updateChatUi()
    {
        if (runtime_ == nullptr)
        {
            return;
        }

        std::string rml;
        if (chatMessages_.size() < MaxChatMessages)
        {
            const size_t emptyLineCount = MaxChatMessages - chatMessages_.size();
            rml += "<div class=\"chat-spacer\" style=\"height: " + std::to_string(emptyLineCount * ChatLineHeight) + "px;\"></div>";
        }
        for (const std::string& message : chatMessages_)
        {
            rml += "<div class=\"chat-line\">" + ui::escapeRml(message) + "</div>";
        }
        runtime_->ui().setChatMessages(rml);
    }

    void GameClient::handleMenuClick(double x, double y)
    {
        int width = windowedWidth_;
        int height = windowedHeight_;
        glfwGetWindowSize(window_, &width, &height);

        if (screen_ == AppScreen::Lobby)
        {
            if (pointInButton(x, y, width, height, LobbyStartButtonY))
            {
                setScreen(AppScreen::WorldSelect);
            }
            else if (pointInButton(x, y, width, height, LobbyExitButtonY))
            {
                glfwSetWindowShouldClose(window_, GLFW_TRUE);
            }
        }
        else if (screen_ == AppScreen::WorldSelect)
        {
            if (pointInButton(x, y, width, height, WorldBackButtonY))
            {
                setScreen(AppScreen::Lobby);
            }
        }
        else if (screen_ == AppScreen::WorldCreate)
        {
            if (pointInButton(x, y, width, height, WorldBackButtonY))
            {
                setScreen(AppScreen::WorldSelect);
            }
        }
        else if (screen_ == AppScreen::Pause)
        {
            if (pointInButton(x, y, width, height, PauseResumeButtonY))
            {
                setScreen(AppScreen::Game);
            }
            else if (pointInButton(x, y, width, height, PauseExitButtonY))
            {
                returnToLobbyScene();
            }
        }
    }

    void GameClient::setScreen(AppScreen screen)
    {
        if (radialActive_)
        {
            closeRadialInteraction(false);
        }
        if (chatOpen_ && screen != AppScreen::Game)
        {
            closeChatInput();
        }
        screen_ = screen;
        if (screen_ == AppScreen::WorldSelect)
        {
            refreshWorldList();
        }
        if (screen_ == AppScreen::WorldCreate && runtime_ != nullptr)
        {
            runtime_->ui().setWorldCreateGameMode(pendingCreateGameMode_ == game::GameMode::Sandbox);
        }
        if (screen_ == AppScreen::Options)
        {
            if (runtime_ != nullptr)
            {
                runtime_->ui().setOptionsLobbyBackground(optionsReturnScreen_ == AppScreen::Lobby);
            }
            updateOptionsUi();
        }
        jumpHeld_ = false;
        jumpPressed_ = false;
        doubleTapSprintActive_ = false;
        breakHeld_ = false;
        if (runtime_ != nullptr)
        {
            runtime_->gameplay().updateBlockBreaking(
                {playerPosition_.x, playerPosition_.y + currentEyeHeight(), playerPosition_.z},
                renderViewDirection(camera_),
                false,
                playerPosition_,
                static_cast<float>(FixedPhysicsTimestep),
                gameMode_ == game::GameMode::Sandbox);
        }
        if (screen_ == AppScreen::Game)
        {
            setMouseCaptured(true);
        }
        else
        {
            setMouseCaptured(false);
        }
    }

    void GameClient::enterGameScene()
    {
        if (!hasSelectedWorld_)
        {
            log::warn("Cannot enter game scene without a selected world.");
            return;
        }

        loadWorldState();
        saveWorldState();
        resetPlayerRuntimeState();
        if (runtime_ != nullptr)
        {
            runtime_->scene().loadGameScene(selectedWorldDirectory_, worldSeed_);
        }
        loadPlayerState();
        setScreen(AppScreen::Game);
    }

    std::filesystem::path GameClient::playerStatePath() const
    {
        return selectedWorldDirectory_ / "player.dat";
    }

    std::filesystem::path GameClient::worldStatePath() const
    {
        return selectedWorldDirectory_ / "world.dat";
    }

    void GameClient::resetPlayerRuntimeState()
    {
        playerPosition_ = {0.0, DefaultPlayerSpawnHeight, 0.0};
        previousPlayerPosition_ = playerPosition_;
        camera_.setAngles(0.0f, 0.0f);
        bodyYaw_ = camera_.yaw();
        previousBodyYaw_ = bodyYaw_;
        playerWalkPhase_ = 0.0f;
        previousPlayerWalkPhase_ = 0.0f;
        playerWalkAmount_ = 0.0f;
        previousPlayerWalkAmount_ = 0.0f;
        sprintFovAmount_ = 0.0f;
        previousSprintFovAmount_ = 0.0f;
        eyeHeightScale_ = 1.0f;
        previousEyeHeightScale_ = 1.0f;
        playerHeightScale_ = 1.0f;
        proneClimbActive_ = false;
        proneClimbProgress_ = 0.0;
        proneClimbStart_ = {};
        proneClimbTarget_ = {};
        waterClimbActive_ = false;
        waterClimbProgress_ = 0.0;
        waterClimbStart_ = {};
        waterClimbTarget_ = {};
        moveMode_ = MoveMode::Ground;
        verticalVelocity_ = 0.0;
        grounded_ = false;
        jumpHeld_ = false;
        jumpPressed_ = false;
        sprintToggled_ = false;
        sneakToggled_ = false;
        proneToggled_ = false;
        doubleTapSprintActive_ = false;
        lastForwardTapTime_ = -1000.0;
        lastJumpTapTime_ = -1000.0;
        physicsAccumulator_ = 0.0;
        playerStats_ = game::PlayerStats{};
        updatePlayerStatsUi();
    }

    void GameClient::setCreateWorldGameMode(game::GameMode mode)
    {
        pendingCreateGameMode_ = mode;
        if (runtime_ != nullptr)
        {
            runtime_->ui().setWorldCreateGameMode(pendingCreateGameMode_ == game::GameMode::Sandbox);
        }
    }

    DVec3 GameClient::findInitialSpawnPosition(uint64_t worldSeed) const
    {
        if (runtime_ == nullptr)
        {
            return {0.0, DefaultPlayerSpawnHeight, static_cast<double>(InitialSpawnZ)};
        }

        const world::TerrainBuilder builder(runtime_->terrainConfigForWorldSeed(worldSeed));
        std::mt19937_64 random(worldSeed ^ 0xD01B0705A5A5A5A5ull);
        std::uniform_int_distribution<int> xDistribution(0, static_cast<int>(WorldSizeBlocks) - 1);

        for (int attempt = 0; attempt < InitialSpawnMaxAttempts; ++attempt)
        {
            const int worldX = xDistribution(random);
            int surfaceY = -1;
            const uint16_t surfaceBlock = builder.surfaceBlockAtWorld(worldX, InitialSpawnZ, &surfaceY);
            if (surfaceY < 0)
            {
                continue;
            }

            if (surfaceBlock == BlockGrass)
            {
                log::info("Initial grass spawn found at X " + std::to_string(worldX) +
                    " / Y " + std::to_string(surfaceY + 1) +
                    " / Z " + std::to_string(InitialSpawnZ) + ".");
                return {
                    static_cast<double>(worldX),
                    static_cast<double>(surfaceY + 1),
                    static_cast<double>(InitialSpawnZ)
                };
            }
        }

        log::warn("Grass spawn search failed, using fallback spawn.");
        return {0.0, DefaultPlayerSpawnHeight, static_cast<double>(InitialSpawnZ)};
    }

    void GameClient::applyGameMode(game::GameMode mode)
    {
        gameMode_ = mode;
        if (gameMode_ == game::GameMode::Survival && moveMode_ == MoveMode::Fly)
        {
            moveMode_ = MoveMode::Ground;
            verticalVelocity_ = 0.0;
            grounded_ = false;
            doubleTapSprintActive_ = false;
            lastJumpTapTime_ = -1000.0;
        }
    }

    void GameClient::updatePlayerStatsUi()
    {
        playerStats_.clamp();
        if (runtime_ != nullptr)
        {
            runtime_->ui().setPlayerStats(
                playerStats_.hp,
                playerStats_.maxHp,
                playerStats_.hunger,
                playerStats_.maxHunger,
                playerStats_.thirst,
                playerStats_.maxThirst);
        }
    }

    void GameClient::refreshWorldList()
    {
        availableWorlds_.clear();
        const std::filesystem::path root = saveRootDirectory();

        try
        {
            std::filesystem::create_directories(root);
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root))
            {
                if (!entry.is_directory())
                {
                    continue;
                }

                const std::filesystem::path worldPath = entry.path();
                const std::filesystem::path statePath = worldPath / "world.dat";
                if (!std::filesystem::exists(statePath))
                {
                    continue;
                }

                WorldInfo info{};
                info.name = worldPath.filename().string();
                info.path = worldPath;
                info.totalTicks = DefaultWorldTicks;
                info.seed = 0;
                info.createdUnixSeconds = 0;
                info.lastPlayedUnixSeconds = 0;

                std::ifstream file(statePath, std::ios::binary);
                if (file.is_open())
                {
                    std::vector<uint8_t> bytes(WorldStateFileSize);
                    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                    if (file)
                    {
                        size_t offset = 0;
                        info.totalTicks = readU64(bytes, offset);
                        info.seed = readU64(bytes, offset);
                        info.createdUnixSeconds = readU64(bytes, offset);
                        info.lastPlayedUnixSeconds = readU64(bytes, offset);
                    }
                }

                availableWorlds_.push_back(std::move(info));
            }

            std::sort(availableWorlds_.begin(), availableWorlds_.end(), [](const WorldInfo& a, const WorldInfo& b)
            {
                return a.name < b.name;
            });
        }
        catch (...)
        {
            log::warn("World list refresh failed.");
        }

        if (runtime_ != nullptr)
        {
            std::vector<game::WorldListItem> items;
            items.reserve(availableWorlds_.size());
            for (const WorldInfo& world : availableWorlds_)
            {
                items.push_back(game::WorldListItem{
                    world.name,
                    formatUnixSeconds(world.createdUnixSeconds),
                    formatUnixSeconds(world.lastPlayedUnixSeconds)
                });
            }
            runtime_->ui().setWorldList(items);
        }
    }

    void GameClient::openWorldByIndex(size_t index)
    {
        if (index >= availableWorlds_.size())
        {
            log::warn("World index out of range.");
            return;
        }

        const WorldInfo& world = availableWorlds_[index];
        selectedWorldName_ = world.name;
        selectedWorldDirectory_ = world.path;
        worldSeed_ = world.seed;
        worldCreatedUnixSeconds_ = world.createdUnixSeconds;
        worldLastPlayedUnixSeconds_ = world.lastPlayedUnixSeconds;
        gameMode_ = game::GameMode::Sandbox;
        playerStats_ = game::PlayerStats{};
        hasSelectedWorld_ = true;
        log::info("World selected: " + selectedWorldName_);
        enterGameScene();
    }

    void GameClient::createWorldFromUi()
    {
        const std::string rawName = runtime_ != nullptr ? runtime_->ui().inputValue("new-world-name") : "New World";
        const std::string rawSeed = runtime_ != nullptr ? runtime_->ui().inputValue("new-world-seed") : "";
        const std::string baseName = sanitizeWorldName(rawName);
        uint64_t seed = parseWorldSeed(rawSeed);

        std::filesystem::path worldPath = saveRootDirectory() / baseName;
        std::string finalName = baseName;
        int suffix = 2;
        while (std::filesystem::exists(worldPath))
        {
            finalName = baseName + " " + std::to_string(suffix++);
            worldPath = saveRootDirectory() / finalName;
        }

        try
        {
            selectedWorldName_ = finalName;
            selectedWorldDirectory_ = worldPath;
            worldTicks_ = DefaultWorldTicks;
            worldSeed_ = seed;
            gameMode_ = pendingCreateGameMode_;
            worldCreatedUnixSeconds_ = currentUnixSeconds();
            worldLastPlayedUnixSeconds_ = worldCreatedUnixSeconds_;
            hasSelectedWorld_ = true;
            std::filesystem::create_directories(selectedWorldDirectory_ / "regions");
            saveWorldState();
            resetPlayerRuntimeState();
            playerPosition_ = findInitialSpawnPosition(worldSeed_);
            previousPlayerPosition_ = playerPosition_;
            savePlayerState();
            log::info("World created: " + selectedWorldName_);
            enterGameScene();
        }
        catch (...)
        {
            hasSelectedWorld_ = false;
            log::warn("World creation failed.");
        }
    }

    void GameClient::returnToLobbyScene()
    {
        if (runtime_ != nullptr)
        {
            runtime_->ui().closeInventoryInteraction();
        }
        saveWorldState();
        savePlayerState();
        if (runtime_ != nullptr)
        {
            runtime_->scene().unloadGameScene();
        }
        previousPlayerPosition_ = playerPosition_;
        physicsAccumulator_ = 0.0;
        verticalVelocity_ = 0.0;
        grounded_ = false;
        hasSelectedWorld_ = false;
        setScreen(AppScreen::Lobby);
    }

    void GameClient::cycleViewMode()
    {
        if (viewMode_ == ViewMode::FirstPerson)
        {
            viewMode_ = ViewMode::ThirdPersonRear;
        }
        else if (viewMode_ == ViewMode::ThirdPersonRear)
        {
            viewMode_ = ViewMode::ThirdPersonFront;
        }
        else
        {
            viewMode_ = ViewMode::FirstPerson;
        }
    }

    void GameClient::loadMovementConfig()
    {
        flyMoveSpeed_ = DefaultFlyMoveSpeed;
        groundMoveSpeed_ = DefaultGroundMoveSpeed;
        jumpSpeed_ = DefaultJumpSpeed;
        gravity_ = DefaultGravity;
        sprintSpeedScale_ = DefaultSprintSpeedScale;
        sneakSpeedScale_ = DefaultSneakSpeedScale;
        sneakHeightScale_ = DefaultSneakHeightScale;
        proneHeight_ = DefaultProneHeight;
        proneEyeHeight_ = DefaultProneEyeHeight;
        swimSpeedScale_ = DefaultSwimSpeedScale;
        movementDoubleTapWindow_ = DefaultMovementDoubleTapWindow;

        const std::filesystem::path path = configDirectory() / "world.json";
        std::ifstream file(path);
        if (!file.is_open())
        {
            return;
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        const std::string text = contents.str();
        const std::string player = jsonObjectField(text, "player").value_or("{}");

        if (const std::optional<double> value = jsonDoubleField(player, "flyMoveSpeed"); value.has_value() && *value > 0.0)
        {
            flyMoveSpeed_ = *value;
        }
        if (const std::optional<double> value = jsonDoubleField(player, "groundMoveSpeed"); value.has_value() && *value > 0.0)
        {
            groundMoveSpeed_ = *value;
        }
        if (const std::optional<double> value = jsonDoubleField(player, "jumpSpeed"); value.has_value() && *value > 0.0)
        {
            jumpSpeed_ = *value;
        }
        if (const std::optional<double> value = jsonDoubleField(player, "gravity"); value.has_value() && *value > 0.0)
        {
            gravity_ = *value;
        }
        if (const std::optional<double> value = jsonDoubleField(player, "sprintSpeedScale"); value.has_value() && *value > 0.0)
        {
            sprintSpeedScale_ = *value;
        }
        if (const std::optional<double> value = jsonDoubleField(player, "sneakSpeedScale"); value.has_value() && *value > 0.0)
        {
            sneakSpeedScale_ = *value;
        }
        if (const std::optional<double> value = jsonDoubleField(player, "sneakHeightScale"); value.has_value() && *value > 0.0)
        {
            sneakHeightScale_ = std::clamp(*value, 0.1, 1.0);
        }
        if (const std::optional<double> value = jsonDoubleField(player, "swimSpeedScale"); value.has_value() && *value > 0.0)
        {
            swimSpeedScale_ = std::clamp(*value, 0.1, 2.0);
        }
        if (const std::optional<double> value = jsonDoubleField(player, "movementDoubleTapWindow"); value.has_value() && *value > 0.0)
        {
            movementDoubleTapWindow_ = *value;
        }
    }

    void GameClient::loadSettings()
    {
        bgmVolume_ = 1.0;
        sfxVolume_ = 1.0;
        fovDegrees_ = DefaultFovDegrees;
        viewBobbing_ = true;
        toggleSprint_ = false;
        toggleSneak_ = false;
        toggleProne_ = false;

        const std::filesystem::path path = configDirectory() / "settings.json";
        std::ifstream file(path);
        if (!file.is_open())
        {
            return;
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        const std::string text = contents.str();
        const std::string audio = jsonObjectField(text, "audio").value_or(text);
        const std::string video = jsonObjectField(text, "video").value_or("{}");
        const std::string controls = jsonObjectField(text, "controls").value_or("{}");

        if (const std::optional<double> value = jsonDoubleField(audio, "bgmVolume"); value.has_value())
        {
            bgmVolume_ = std::clamp(*value, 0.0, 1.0);
        }
        if (const std::optional<double> value = jsonDoubleField(audio, "sfxVolume"); value.has_value())
        {
            sfxVolume_ = std::clamp(*value, 0.0, 1.0);
        }
        if (const std::optional<double> value = jsonDoubleField(video, "fovDegrees"); value.has_value())
        {
            fovDegrees_ = std::clamp(*value, MinFovDegrees, MaxFovDegrees);
        }
        if (const std::optional<bool> value = jsonBoolField(video, "viewBobbing"); value.has_value())
        {
            viewBobbing_ = *value;
        }
        if (const std::optional<bool> value = jsonBoolField(controls, "toggleSprint"); value.has_value())
        {
            toggleSprint_ = *value;
        }
        if (const std::optional<bool> value = jsonBoolField(controls, "toggleSneak"); value.has_value())
        {
            toggleSneak_ = *value;
        }
        if (const std::optional<bool> value = jsonBoolField(controls, "toggleProne"); value.has_value())
        {
            toggleProne_ = *value;
        }
    }

    void GameClient::saveSettings() const
    {
        try
        {
            std::filesystem::create_directories(configDirectory());
            std::ofstream file(configDirectory() / "settings.json", std::ios::trunc);
            if (!file.is_open())
            {
                log::warn("Settings save file could not be opened.");
                return;
            }

            file << "{\n";
            file << "  \"audio\": {\n";
            file << "    \"bgmVolume\": " << std::fixed << std::setprecision(2) << bgmVolume_ << ",\n";
            file << "    \"sfxVolume\": " << std::fixed << std::setprecision(2) << sfxVolume_ << "\n";
            file << "  },\n";
            file << "  \"video\": {\n";
            file << "    \"fovDegrees\": " << roundedFovDegrees(fovDegrees_) << ",\n";
            file << "    \"viewBobbing\": " << (viewBobbing_ ? "true" : "false") << "\n";
            file << "  },\n";
            file << "  \"controls\": {\n";
            file << "    \"toggleSprint\": " << (toggleSprint_ ? "true" : "false") << ",\n";
            file << "    \"toggleSneak\": " << (toggleSneak_ ? "true" : "false") << ",\n";
            file << "    \"toggleProne\": " << (toggleProne_ ? "true" : "false") << "\n";
            file << "  }\n";
            file << "}\n";
        }
        catch (...)
        {
            log::warn("Settings save failed.");
        }
    }

    void GameClient::applyAudioSettings()
    {
        if (runtime_ != nullptr)
        {
            runtime_->audio().setVolumes(static_cast<float>(bgmVolume_), static_cast<float>(sfxVolume_));
        }
    }

    void GameClient::updateOptionsUi()
    {
        if (runtime_ != nullptr)
        {
            runtime_->ui().setOptionsVolumes(volumePercent(bgmVolume_), volumePercent(sfxVolume_));
            runtime_->ui().setOptionsFov(roundedFovDegrees(fovDegrees_));
            runtime_->ui().setOptionsViewBobbing(viewBobbing_);
            runtime_->ui().setOptionsControls(toggleSprint_, toggleSneak_, toggleProne_);
        }
    }

    void GameClient::applyOptionsSliderValues()
    {
        if (runtime_ == nullptr)
        {
            return;
        }

        try
        {
            bgmVolume_ = std::clamp(std::stod(runtime_->ui().inputValue("bgm-volume-slider")) / 100.0, 0.0, 1.0);
            sfxVolume_ = std::clamp(std::stod(runtime_->ui().inputValue("sfx-volume-slider")) / 100.0, 0.0, 1.0);
            fovDegrees_ = std::clamp(std::stod(runtime_->ui().inputValue("fov-slider")), MinFovDegrees, MaxFovDegrees);
        }
        catch (...)
        {
            updateOptionsUi();
            return;
        }
        applyAudioSettings();
        updateOptionsUi();
        saveSettings();
    }

    void GameClient::toggleViewBobbingOption()
    {
        viewBobbing_ = !viewBobbing_;
        updateOptionsUi();
        saveSettings();
    }

    void GameClient::toggleSprintOption()
    {
        toggleSprint_ = !toggleSprint_;
        sprintToggled_ = false;
        doubleTapSprintActive_ = false;
        updateOptionsUi();
        saveSettings();
    }

    void GameClient::toggleSneakOption()
    {
        toggleSneak_ = !toggleSneak_;
        sneakToggled_ = false;
        updateOptionsUi();
        saveSettings();
    }

    void GameClient::toggleProneOption()
    {
        toggleProne_ = !toggleProne_;
        proneToggled_ = false;
        updateOptionsUi();
        saveSettings();
    }

    void GameClient::loadWorldState()
    {
        worldTicks_ = DefaultWorldTicks;
        if (worldCreatedUnixSeconds_ == 0)
        {
            worldCreatedUnixSeconds_ = currentUnixSeconds();
        }

        std::ifstream file(worldStatePath(), std::ios::binary);
        if (!file.is_open())
        {
            log::warn("World state not found, using default world time and selected seed.");
            return;
        }

        std::vector<uint8_t> bytes(WorldStateFileSize);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file)
        {
            log::warn("World state file is incomplete, using default world time.");
            worldTicks_ = DefaultWorldTicks;
            return;
        }

        try
        {
            size_t offset = 0;
            worldTicks_ = readU64(bytes, offset);
            worldSeed_ = readU64(bytes, offset);
            worldCreatedUnixSeconds_ = readU64(bytes, offset);
            worldLastPlayedUnixSeconds_ = readU64(bytes, offset);
            log::info("World state loaded.");
        }
        catch (...)
        {
            log::warn("World state load failed, using default world time.");
            worldTicks_ = DefaultWorldTicks;
        }
    }

    void GameClient::saveWorldState()
    {
        if (!hasSelectedWorld_)
        {
            return;
        }

        try
        {
            std::filesystem::create_directories(selectedWorldDirectory_);
            if (worldCreatedUnixSeconds_ == 0)
            {
                worldCreatedUnixSeconds_ = currentUnixSeconds();
            }
            worldLastPlayedUnixSeconds_ = currentUnixSeconds();

            std::vector<uint8_t> bytes;
            bytes.reserve(WorldStateFileSize);
            writeU64(bytes, worldTicks_);
            writeU64(bytes, worldSeed_);
            writeU64(bytes, worldCreatedUnixSeconds_);
            writeU64(bytes, worldLastPlayedUnixSeconds_);

            std::ofstream file(worldStatePath(), std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                log::warn("World state save file could not be opened.");
                return;
            }
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file)
            {
                log::warn("World state save write failed.");
                return;
            }
            log::info("World state saved.");
        }
        catch (...)
        {
            log::warn("World state save failed.");
        }
    }

    void GameClient::loadPlayerState()
    {
        std::ifstream file(playerStatePath(), std::ios::binary);
        if (!file.is_open())
        {
            log::warn("Player state not found, using default player state.");
            previousPlayerPosition_ = playerPosition_;
            return;
        }

        file.seekg(0, std::ios::end);
        const std::streamoff fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        if (fileSize != static_cast<std::streamoff>(PlayerStateFileSize) &&
            fileSize != static_cast<std::streamoff>(PlayerStateInventoryFileSize) &&
            fileSize != static_cast<std::streamoff>(PlayerStateDurabilityFileSize) &&
            fileSize != static_cast<std::streamoff>(PlayerStateDurabilityInventoryFileSize) &&
            fileSize != static_cast<std::streamoff>(PlayerStateLegacyFileSize))
        {
            log::warn("Player state file has unsupported size, using default player state.");
            previousPlayerPosition_ = playerPosition_;
            return;
        }

        std::vector<uint8_t> bytes(static_cast<std::size_t>(fileSize));
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file)
        {
            log::warn("Player state file is incomplete, using default player state.");
            previousPlayerPosition_ = playerPosition_;
            return;
        }

        try
        {
            size_t offset = 0;
            const double x = readF64(bytes, offset);
            const double y = readF64(bytes, offset);
            const double z = readF64(bytes, offset);
            const float yaw = readF32(bytes, offset);
            const float pitch = readF32(bytes, offset);
            const uint8_t moveMode = readU8(bytes, offset);
            const uint8_t gameMode = readU8(bytes, offset);
            const double verticalVelocity = readF64(bytes, offset);
            game::PlayerStats stats{};
            stats.hp = readU16(bytes, offset);
            stats.maxHp = readU16(bytes, offset);
            stats.hunger = readU16(bytes, offset);
            stats.maxHunger = readU16(bytes, offset);
            stats.thirst = readU16(bytes, offset);
            stats.maxThirst = readU16(bytes, offset);
            stats.clamp();
            std::array<ItemStack, PlayerInventorySlotCount> inventorySlots{};
            const bool hasBurnTicks = bytes.size() == PlayerStateFileSize || bytes.size() == PlayerStateInventoryFileSize;
            const bool hasDurability = hasBurnTicks ||
                bytes.size() == PlayerStateDurabilityFileSize ||
                bytes.size() == PlayerStateDurabilityInventoryFileSize;
            const bool hasOffhandSlot = bytes.size() == PlayerStateFileSize || bytes.size() == PlayerStateDurabilityFileSize;
            const bool offhandHasBurnTicks = bytes.size() == PlayerStateFileSize;
            for (ItemStack& slot : inventorySlots)
            {
                slot.itemId = readU16(bytes, offset);
                slot.count = readU16(bytes, offset);
                slot.durability = hasDurability ? readU16(bytes, offset) : 0;
                slot.burnTicksRemaining = hasBurnTicks ? readU16(bytes, offset) : 0;
            }
            ItemStack offhandSlot{};
            if (hasOffhandSlot)
            {
                offhandSlot.itemId = readU16(bytes, offset);
                offhandSlot.count = readU16(bytes, offset);
                offhandSlot.durability = readU16(bytes, offset);
                offhandSlot.burnTicksRemaining = offhandHasBurnTicks ? readU16(bytes, offset) : 0;
            }

            if (!std::isfinite(x) ||
                !std::isfinite(y) ||
                !std::isfinite(z) ||
                !std::isfinite(yaw) ||
                !std::isfinite(pitch) ||
                !std::isfinite(verticalVelocity) ||
                moveMode > 1u ||
                gameMode > 1u)
            {
                log::warn("Player state file contains invalid values, using default player state.");
                previousPlayerPosition_ = playerPosition_;
                return;
            }

            playerPosition_ = {wrapWorldCoordinate(x), y, wrapWorldCoordinate(z)};
            previousPlayerPosition_ = playerPosition_;
            camera_.setAngles(yaw, pitch);
            bodyYaw_ = camera_.yaw();
            previousBodyYaw_ = bodyYaw_;
            playerWalkPhase_ = 0.0f;
            previousPlayerWalkPhase_ = 0.0f;
            playerWalkAmount_ = 0.0f;
            previousPlayerWalkAmount_ = 0.0f;
            sprintFovAmount_ = 0.0f;
            previousSprintFovAmount_ = 0.0f;
            eyeHeightScale_ = 1.0f;
            previousEyeHeightScale_ = 1.0f;
            playerHeightScale_ = 1.0f;
            proneClimbActive_ = false;
            proneClimbProgress_ = 0.0;
            proneClimbStart_ = {};
            proneClimbTarget_ = {};
            waterClimbActive_ = false;
            waterClimbProgress_ = 0.0;
            waterClimbStart_ = {};
            waterClimbTarget_ = {};
            gameMode_ = gameMode == 0u ? game::GameMode::Survival : game::GameMode::Sandbox;
            moveMode_ = moveMode == 0u ? MoveMode::Fly : MoveMode::Ground;
            if (gameMode_ == game::GameMode::Survival && moveMode_ == MoveMode::Fly)
            {
                moveMode_ = MoveMode::Ground;
            }
            playerStats_ = stats;
            updatePlayerStatsUi();
            verticalVelocity_ = verticalVelocity;
            grounded_ = false;
            jumpHeld_ = false;
            jumpPressed_ = false;
            sprintToggled_ = false;
            sneakToggled_ = false;
            doubleTapSprintActive_ = false;
            lastForwardTapTime_ = -1000.0;
            lastJumpTapTime_ = -1000.0;
            physicsAccumulator_ = 0.0;
            if (runtime_ != nullptr)
            {
                runtime_->gameplay().setInventorySnapshot(inventorySlots);
                runtime_->gameplay().setOffhandSlot(offhandSlot);
            }
            log::info("Player state loaded.");
        }
        catch (...)
        {
            log::warn("Player state load failed, using default player state.");
            previousPlayerPosition_ = playerPosition_;
        }
    }

    void GameClient::savePlayerState() const
    {
        if (!hasSelectedWorld_)
        {
            return;
        }

        try
        {
            std::filesystem::create_directories(selectedWorldDirectory_);

            std::vector<uint8_t> bytes;
            bytes.reserve(PlayerStateFileSize);
            writeF64(bytes, wrapWorldCoordinate(playerPosition_.x));
            writeF64(bytes, playerPosition_.y);
            writeF64(bytes, wrapWorldCoordinate(playerPosition_.z));
            writeF32(bytes, camera_.yaw());
            writeF32(bytes, camera_.pitch());
            writeU8(bytes, static_cast<uint8_t>(moveMode_ == MoveMode::Fly ? 0u : 1u));
            writeU8(bytes, static_cast<uint8_t>(gameMode_ == game::GameMode::Survival ? 0u : 1u));
            writeF64(bytes, verticalVelocity_);
            game::PlayerStats stats = playerStats_;
            stats.clamp();
            writeU16(bytes, static_cast<uint16_t>(stats.hp));
            writeU16(bytes, static_cast<uint16_t>(stats.maxHp));
            writeU16(bytes, static_cast<uint16_t>(stats.hunger));
            writeU16(bytes, static_cast<uint16_t>(stats.maxHunger));
            writeU16(bytes, static_cast<uint16_t>(stats.thirst));
            writeU16(bytes, static_cast<uint16_t>(stats.maxThirst));
            const std::array<ItemStack, PlayerInventorySlotCount> inventorySlots = runtime_ != nullptr
                ? runtime_->gameplay().inventorySnapshot()
                : std::array<ItemStack, PlayerInventorySlotCount>{};
            for (const ItemStack& slot : inventorySlots)
            {
                writeU16(bytes, slot.itemId);
                writeU16(bytes, slot.count);
                writeU16(bytes, slot.durability);
                writeU16(bytes, slot.burnTicksRemaining);
            }
            const ItemStack offhandSlot = runtime_ != nullptr ? runtime_->gameplay().offhandSlot() : ItemStack{};
            writeU16(bytes, offhandSlot.itemId);
            writeU16(bytes, offhandSlot.count);
            writeU16(bytes, offhandSlot.durability);
            writeU16(bytes, offhandSlot.burnTicksRemaining);

            std::ofstream file(playerStatePath(), std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                log::warn("Player state save file could not be opened.");
                return;
            }
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file)
            {
                log::warn("Player state save write failed.");
                return;
            }
            log::info("Player state saved.");
        }
        catch (...)
        {
            log::warn("Player state save failed.");
            return;
        }
    }

    DVec3 GameClient::interpolatedPlayerPosition(double alpha) const
    {
        return {
            previousPlayerPosition_.x + (playerPosition_.x - previousPlayerPosition_.x) * alpha,
            previousPlayerPosition_.y + (playerPosition_.y - previousPlayerPosition_.y) * alpha,
            previousPlayerPosition_.z + (playerPosition_.z - previousPlayerPosition_.z) * alpha
        };
    }

    double GameClient::currentPlayerHeightScale() const
    {
        return static_cast<double>(playerHeightScale_);
    }

    double GameClient::currentEyeHeight() const
    {
        return EyeHeight * static_cast<double>(eyeHeightScale_);
    }

    double GameClient::interpolatedEyeHeight(double alpha) const
    {
        const double scale = static_cast<double>(previousEyeHeightScale_) +
            (static_cast<double>(eyeHeightScale_) - static_cast<double>(previousEyeHeightScale_)) * alpha;
        return EyeHeight * scale;
    }

    void GameClient::updatePlayerLookPose(float bodyYaw, float& headYaw, float& headPitch) const
    {
        const float cameraYaw = normalizeAngle(camera_.yaw());
        const float relativeHeadYaw = std::clamp(normalizeAngle(cameraYaw - normalizeAngle(bodyYaw)), -MaxPlayerHeadYaw, MaxPlayerHeadYaw);

        headYaw = relativeHeadYaw;
        headPitch = std::clamp(camera_.pitch(), -MaxPlayerHeadPitch, MaxPlayerHeadPitch);
    }

    void GameClient::updatePlayer(double fixedDeltaSeconds, bool allowInput)
    {
        const game::PlayerMoveMode previousMoveMode = moveMode_;
        const game::PlayerMovementResult result = game::PlayerMovementSystem::tick(
            game::PlayerMovementInput{
                allowInput,
                allowInput && glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS,
                allowInput && glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS,
                allowInput && glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS,
                allowInput && glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS,
                allowInput && (glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS),
                allowInput && (glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS),
                allowInput && glfwGetKey(window_, GLFW_KEY_Z) == GLFW_PRESS,
                jumpHeld_,
                jumpPressed_,
                toggleSprint_,
                toggleSneak_,
                toggleProne_,
                sprintToggled_,
                sneakToggled_,
                proneToggled_,
                doubleTapSprintActive_,
                camera_.yaw()
            },
            game::PlayerMovementState{
                playerPosition_,
                moveMode_,
                verticalVelocity_,
                grounded_,
                bodyYaw_,
                playerWalkPhase_,
                playerWalkAmount_,
                sprintFovAmount_,
                eyeHeightScale_,
                playerHeightScale_,
                proneClimbActive_,
                proneClimbProgress_,
                proneClimbStart_,
                proneClimbTarget_,
                waterClimbActive_,
                waterClimbProgress_,
                waterClimbStart_,
                waterClimbTarget_
            },
            game::PlayerMovementConfig{
                flyMoveSpeed_,
                groundMoveSpeed_,
                jumpSpeed_,
                gravity_,
                sprintSpeedScale_,
                sneakSpeedScale_,
                sneakHeightScale_,
                proneHeight_,
                proneEyeHeight_,
                swimSpeedScale_
            },
            game::PlayerMovementCollision{
                [this](DVec3 position, double heightScale)
                {
                    return runtime_ != nullptr && runtime_->gameplay().playerColliderIntersectsTerrain(position, heightScale);
                },
                [this](DVec3 position)
                {
                    return runtime_ == nullptr || runtime_->gameplay().playerColliderHasSupportBelow(position);
                },
                [this](DVec3 position, double heightScale)
                {
                    return runtime_ != nullptr && runtime_->gameplay().playerColliderIntersectsWater(position, heightScale);
                }
            },
            fixedDeltaSeconds);

        jumpHeld_ = result.input.jumpHeld;
        jumpPressed_ = result.input.jumpPressed;
        doubleTapSprintActive_ = result.input.doubleTapSprintActive;
        proneToggled_ = result.input.proneToggled;
        sneakToggled_ = result.input.sneakToggled;
        playerPosition_ = result.state.position;
        moveMode_ = result.state.moveMode;
        verticalVelocity_ = result.state.verticalVelocity;
        grounded_ = result.state.grounded;
        bodyYaw_ = result.state.bodyYaw;
        playerWalkPhase_ = result.state.walkPhase;
        playerWalkAmount_ = result.state.walkAmount;
        sprintFovAmount_ = result.state.sprintFovAmount;
        eyeHeightScale_ = result.state.eyeHeightScale;
        playerHeightScale_ = result.state.playerHeightScale;
        proneClimbActive_ = result.state.proneClimbActive;
        proneClimbProgress_ = result.state.proneClimbProgress;
        proneClimbStart_ = result.state.proneClimbStart;
        proneClimbTarget_ = result.state.proneClimbTarget;
        waterClimbActive_ = result.state.waterClimbActive;
        waterClimbProgress_ = result.state.waterClimbProgress;
        waterClimbStart_ = result.state.waterClimbStart;
        waterClimbTarget_ = result.state.waterClimbTarget;

        if (previousMoveMode == MoveMode::Fly && moveMode_ == MoveMode::Ground)
        {
            lastJumpTapTime_ = -1000.0;
        }
    }

    void GameClient::updateDebugText()
    {
        ++fpsSampleFrames_;

        const auto now = std::chrono::steady_clock::now();
        const std::chrono::duration<double> elapsed = now - fpsSampleStart_;
        if (elapsed.count() < 0.05)
        {
            return;
        }

        const double fps = static_cast<double>(fpsSampleFrames_) / elapsed.count();
        const double milliseconds = std::clamp(fps > 0.0 ? 1000.0 / fps : 0.0, 0.0, 999.999);
        const int clampedFps = std::clamp(static_cast<int>(fps + 0.5), 0, 9999);
        const float yawDegrees = camera_.yaw() * RadiansToDegrees;
        const float pitchDegrees = camera_.pitch() * RadiansToDegrees;
        const char* facing = facingName(camera_.yaw());
        const double wrappedPlayerX = wrapWorldCoordinate(playerPosition_.x);
        const double wrappedPlayerZ = wrapWorldCoordinate(playerPosition_.z);
        const std::string lookAtText = runtime_ != nullptr ? runtime_->diagnostics().selectedBlockText() : "LOOKAT: none";
        const std::string climateText = runtime_ != nullptr ? runtime_->diagnostics().climateText(playerPosition_) : "CLIMATE: T[0.000] P[0.000]";
        const std::string biomeText = runtime_ != nullptr ? runtime_->diagnostics().biomeText(playerPosition_) : "BIOME: T[0] P[0] GND[0] - FrozenOcean";
        const std::string terrainText = runtime_ != nullptr ? runtime_->diagnostics().terrainText(playerPosition_) : "TERRAIN: GND[0.000] SMTH[0.000] W[0.000] PV[0.000]\nVALUE: RAW[0.000] NORM[0.000] PVW[0.000] PVMUL[0.000] BASE[0.000] INF[0.000] VAL[0.000] H[0]";
        const uint64_t day = worldTicks_ / TicksPerDay;
        const uint64_t minuteOfDay = (worldTicks_ % TicksPerDay) / TicksPerMinute;
        const uint64_t hour = minuteOfDay / MinutesPerHour;
        const uint64_t minute = minuteOfDay % MinutesPerHour;
        const float skyBrightness = skyBrightnessForTicks(worldTicks_);

        std::snprintf(
            debugText_.data(),
            debugText_.size(),
            "FPS: %04d [%07.3fMS]\nPOS: X %.3f [%.3f] / Y %.3f / Z %.3f [%.3f]\nVIEW: YAW %.1f / PITCH %.1f [%s]\n%s\n%s\n%s\n%s\nLIGHT: SKY[%.2f]\nTIME: %lluD %02lluH %02lluM\nSEED: %llu",
            clampedFps,
            milliseconds,
            wrappedPlayerX,
            playerPosition_.x,
            playerPosition_.y,
            wrappedPlayerZ,
            playerPosition_.z,
            yawDegrees,
            pitchDegrees,
            facing,
            lookAtText.c_str(),
            climateText.c_str(),
            biomeText.c_str(),
            terrainText.c_str(),
            skyBrightness,
            static_cast<unsigned long long>(day),
            static_cast<unsigned long long>(hour),
            static_cast<unsigned long long>(minute),
            static_cast<unsigned long long>(worldSeed_));

        fpsSampleFrames_ = 0;
        fpsSampleStart_ = now;
    }
}
