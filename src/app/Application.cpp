#include "app/Application.h"

#include "platform/Log.h"
#include "platform/RuntimePaths.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "renderer/Renderer.h"

#include <stb_image.h>

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
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace dolbuto
{
    namespace
    {
        constexpr int WindowWidth = 1280;
        constexpr int WindowHeight = 720;
        constexpr const char* WindowTitle = "DOLBUTO";
        constexpr float RadiansToDegrees = 57.2957795131f;
        constexpr float Pi = 3.14159265359f;
        constexpr double EyeHeight = 1.5625;
        constexpr double ThirdPersonDistance = 5.5;
        constexpr double FixedPhysicsTimestep = 1.0 / 20.0;
        constexpr double MaxPhysicsFrameTime = 0.25;
        constexpr double DefaultFlyMoveSpeed = 64.0;
        constexpr double DefaultGroundMoveSpeed = 4.317;
        constexpr double DefaultJumpSpeed = 8.4;
        constexpr double DefaultGravity = 32.0;
        constexpr double WorldSizeBlocks = 65536.0;
        constexpr size_t PlayerInventorySlotCount = 50;
        constexpr size_t PlayerStateBaseFileSize = sizeof(double) * 4u + sizeof(float) * 2u + sizeof(uint8_t);
        constexpr size_t PlayerInventoryFileSize = PlayerInventorySlotCount * sizeof(uint16_t) * 2u;
        constexpr size_t PlayerStateFileSize = PlayerStateBaseFileSize + PlayerInventoryFileSize;
        constexpr size_t WorldStateFileSize = sizeof(uint64_t) * 4u;
        constexpr uint64_t TicksPerMinute = 20;
        constexpr uint64_t MinutesPerHour = 60;
        constexpr uint64_t HoursPerDay = 24;
        constexpr uint64_t TicksPerHour = TicksPerMinute * MinutesPerHour;
        constexpr uint64_t TicksPerDay = TicksPerHour * HoursPerDay;
        constexpr uint64_t DefaultWorldTicks = TicksPerHour * 6u;
        constexpr float MenuButtonWidth = 240.0f;
        constexpr float MenuButtonHeight = 56.0f;
        constexpr float LobbyStartButtonY = 0.45f;
        constexpr float LobbyExitButtonY = 0.56f;
        constexpr float WorldBackButtonY = 0.72f;
        constexpr float PauseResumeButtonY = 0.46f;
        constexpr float PauseExitButtonY = 0.57f;

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

    Application::Application()
    {
        log::initialize();
        log::info("DOLBUTO 0.0.0.1 start");
        log::info("Asset directory: " + assetDirectory().string());
        log::info("Config directory: " + configDirectory().string());
        log::info("Shader directory: " + shaderDirectory().string());
        log::info("Save root directory: " + saveRootDirectory().string());
        log::info("Log directory: " + logDirectory().string());
        loadMovementConfig();
        initWindow();
        renderer_ = std::make_unique<Renderer>(window_);
        setScreen(AppScreen::Lobby);
        fpsSampleStart_ = std::chrono::steady_clock::now();
        lastFrameTime_ = fpsSampleStart_;
    }

    Application::~Application()
    {
        if (hasSelectedWorld_)
        {
            if (renderer_ != nullptr)
            {
                renderer_->closeInventoryInteraction();
            }
            saveWorldState();
            savePlayerState();
        }
        renderer_.reset();
        shutdownWindow();
    }

    void Application::run()
    {
        while (!glfwWindowShouldClose(window_))
        {
            glfwPollEvents();
            if (glfwWindowShouldClose(window_))
            {
                break;
            }
            if (renderer_ != nullptr)
            {
                while (std::optional<std::string> action = renderer_->consumeUiAction())
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
                }
            }

            const auto now = std::chrono::steady_clock::now();
            const std::chrono::duration<double> delta = now - lastFrameTime_;
            lastFrameTime_ = now;

            physicsAccumulator_ += std::min(delta.count(), MaxPhysicsFrameTime);
            const bool gameSimulationActive = screen_ == AppScreen::Game || screen_ == AppScreen::Inventory;
            while (gameSimulationActive && physicsAccumulator_ >= FixedPhysicsTimestep)
            {
                previousPlayerPosition_ = playerPosition_;
                updatePlayer(FixedPhysicsTimestep, screen_ == AppScreen::Game);
                if (renderer_ != nullptr)
                {
                    renderer_->updateBlockBreaking(
                        {playerPosition_.x, playerPosition_.y + EyeHeight, playerPosition_.z},
                        renderViewDirection(camera_),
                        screen_ == AppScreen::Game && mouseCaptured_ && breakHeld_,
                        playerPosition_,
                        static_cast<float>(FixedPhysicsTimestep));
                }
                ++worldTicks_;
                physicsAccumulator_ -= FixedPhysicsTimestep;
            }
            if (!gameSimulationActive)
            {
                if (renderer_ != nullptr)
                {
                    renderer_->updateBlockBreaking(
                        {playerPosition_.x, playerPosition_.y + EyeHeight, playerPosition_.z},
                        renderViewDirection(camera_),
                        false,
                        playerPosition_,
                        static_cast<float>(FixedPhysicsTimestep));
                }
                physicsAccumulator_ = 0.0;
                previousPlayerPosition_ = playerPosition_;
            }

            const double physicsAlpha = std::clamp(physicsAccumulator_ / FixedPhysicsTimestep, 0.0, 1.0);
            const DVec3 renderPlayerPosition = interpolatedPlayerPosition(physicsAlpha);
            const DVec3 eyePosition{renderPlayerPosition.x, renderPlayerPosition.y + EyeHeight, renderPlayerPosition.z};
            if (screen_ == AppScreen::Game || screen_ == AppScreen::Inventory)
            {
                renderer_->updateBlockSelection(
                    {playerPosition_.x, playerPosition_.y + EyeHeight, playerPosition_.z},
                    renderViewDirection(camera_));
            }
            updateDebugText();
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

            const int menuOverlayMode = screen_ == AppScreen::Lobby ? 1 : (screen_ == AppScreen::Pause ? 2 : (screen_ == AppScreen::WorldSelect ? 3 : (screen_ == AppScreen::WorldCreate ? 4 : (screen_ == AppScreen::Inventory ? 5 : 0))));
            const bool worldUpdateEnabled = screen_ == AppScreen::Game || screen_ == AppScreen::Pause || screen_ == AppScreen::Inventory;
            const bool gameSceneRenderEnabled = screen_ == AppScreen::Game || screen_ == AppScreen::Pause || screen_ == AppScreen::Inventory;
            const bool renderDebugText = (screen_ == AppScreen::Game || screen_ == AppScreen::Inventory) && debugTextVisible_;
            renderer_->drawFrame(renderCamera, renderCameraPosition, debugText_.data(), renderDebugText, screenshotRequested_, showPlayer, renderPlayerPosition, camera_.yaw(), terrainWireframe_, climateOverlayMode_, menuOverlayMode, hudVisible_, worldUpdateEnabled, gameSceneRenderEnabled, worldTicks_);
            screenshotRequested_ = false;
        }
    }

    void Application::initWindow()
    {
        if (glfwInit() != GLFW_TRUE)
        {
            throw std::runtime_error("Failed to initialize GLFW.");
        }

        if (glfwVulkanSupported() != GLFW_TRUE)
        {
            glfwTerminate();
            throw std::runtime_error("GLFW could not find Vulkan support.");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window_ = glfwCreateWindow(WindowWidth, WindowHeight, WindowTitle, nullptr, nullptr);
        if (window_ == nullptr)
        {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window.");
        }

        setWindowIcon();

        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* window, int, int)
        {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
            if (app != nullptr && app->renderer_ != nullptr)
            {
                app->renderer_->setFramebufferResized();
            }
        });

        glfwSetCursorPosCallback(window_, [](GLFWwindow* window, double x, double y)
        {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
            if (app != nullptr)
            {
                app->handleMouse(x, y);
            }
        });

        glfwSetKeyCallback(window_, [](GLFWwindow* window, int key, int, int action, int mods)
        {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
            if (app != nullptr && app->screen_ != Application::AppScreen::Game && app->renderer_ != nullptr &&
                (action == GLFW_PRESS || action == GLFW_REPEAT || action == GLFW_RELEASE))
            {
                app->renderer_->uiKey(key, action != GLFW_RELEASE, mods);
            }
            if (key == GLFW_KEY_SPACE && app != nullptr && app->screen_ == Application::AppScreen::Game)
            {
                if (action == GLFW_PRESS)
                {
                    app->jumpHeld_ = true;
                    app->jumpPressed_ = true;
                }
                else if (action == GLFW_RELEASE)
                {
                    app->jumpHeld_ = false;
                }
            }

            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            {
                if (app != nullptr && app->screen_ == Application::AppScreen::Game)
                {
                    app->setScreen(Application::AppScreen::Pause);
                }
                else if (app != nullptr && app->screen_ == Application::AppScreen::Inventory)
                {
                    app->setScreen(Application::AppScreen::Game);
                }
                else if (app != nullptr && app->screen_ == Application::AppScreen::Pause)
                {
                    app->setScreen(Application::AppScreen::Game);
                }
                else if (app != nullptr && (app->screen_ == Application::AppScreen::WorldSelect || app->screen_ == Application::AppScreen::WorldCreate))
                {
                    app->setScreen(app->screen_ == Application::AppScreen::WorldCreate ? Application::AppScreen::WorldSelect : Application::AppScreen::Lobby);
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
                app->climateOverlayMode_ = (app->climateOverlayMode_ + 1) % 3;
            }
            else if (key == GLFW_KEY_E && action == GLFW_PRESS && app != nullptr)
            {
                if (app->screen_ == Application::AppScreen::Game)
                {
                    app->setScreen(Application::AppScreen::Inventory);
                }
                else if (app->screen_ == Application::AppScreen::Inventory)
                {
                    app->setScreen(Application::AppScreen::Game);
                }
            }
            else if (const int slot = hotbarSlotFromKey(key); action == GLFW_PRESS && app != nullptr && app->screen_ == Application::AppScreen::Game && slot >= 0)
            {
                app->setHotbarSelectedSlot(slot);
            }
            else if (key == GLFW_KEY_F && action == GLFW_PRESS && app != nullptr)
            {
                if (app->screen_ == Application::AppScreen::Game && app->renderer_ != nullptr)
                {
                    app->renderer_->pickupDroppedItemInView(
                        {app->playerPosition_.x, app->playerPosition_.y + EyeHeight, app->playerPosition_.z},
                        renderViewDirection(app->camera_));
                }
            }
            else if (key == GLFW_KEY_V && action == GLFW_PRESS && app != nullptr)
            {
                if (app->screen_ == Application::AppScreen::Game)
                {
                    app->moveMode_ = app->moveMode_ == MoveMode::Fly ? MoveMode::Ground : MoveMode::Fly;
                    app->verticalVelocity_ = 0.0;
                    app->grounded_ = false;
                    app->jumpPressed_ = false;
                }
            }
            else if (key == GLFW_KEY_R && action == GLFW_PRESS && app != nullptr && app->renderer_ != nullptr)
            {
                app->renderer_->resetPeakProfiler();
            }
        });

        glfwSetCharCallback(window_, [](GLFWwindow* window, unsigned int codepoint)
        {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
            if (app != nullptr && app->screen_ != Application::AppScreen::Game && app->renderer_ != nullptr)
            {
                app->renderer_->uiTextInput(codepoint);
            }
        });

        glfwSetMouseButtonCallback(window_, [](GLFWwindow* window, int button, int action, int mods)
        {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
            if (app == nullptr)
            {
                return;
            }

            if (app->screen_ != Application::AppScreen::Game)
            {
                double x = 0.0;
                double y = 0.0;
                glfwGetCursorPos(window, &x, &y);
                if (app->renderer_ != nullptr && (action == GLFW_PRESS || action == GLFW_RELEASE))
                {
                    app->renderer_->uiMouseMove(x, y);
                    app->renderer_->uiMouseButton(button, action == GLFW_PRESS, mods);
                }
                if (action == GLFW_PRESS && (app->renderer_ == nullptr || !app->renderer_->rmlUiAvailable()))
                {
                    app->handleMenuClick(x, y);
                }
                return;
            }

            if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
            {
                app->breakHeld_ = false;
                if (app->renderer_ != nullptr)
                {
                    app->renderer_->updateBlockBreaking(
                        {app->playerPosition_.x, app->playerPosition_.y + EyeHeight, app->playerPosition_.z},
                        renderViewDirection(app->camera_),
                        false,
                        app->playerPosition_,
                        static_cast<float>(FixedPhysicsTimestep));
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
                }
                app->setMouseCaptured(true);
            }
            else if (button == GLFW_MOUSE_BUTTON_RIGHT)
            {
                if (app->mouseCaptured_ && app->renderer_ != nullptr)
                {
                    app->renderer_->editBlockInView(
                        {app->playerPosition_.x, app->playerPosition_.y + EyeHeight, app->playerPosition_.z},
                        renderViewDirection(app->camera_),
                        true,
                        app->playerPosition_);
                }
                else
                {
                    app->setMouseCaptured(true);
                }
            }
        });

        glfwSetScrollCallback(window_, [](GLFWwindow* window, double, double yOffset)
        {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
            if (app == nullptr)
            {
                return;
            }

            if (app->screen_ == Application::AppScreen::Game)
            {
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

            if (app->renderer_ != nullptr)
            {
                double x = 0.0;
                double y = 0.0;
                glfwGetCursorPos(window, &x, &y);
                app->renderer_->uiMouseMove(x, y);
                app->renderer_->uiMouseWheel(yOffset);
            }
        });

        setMouseCaptured(false);
    }

    void Application::setWindowIcon()
    {
        const std::filesystem::path path = assetDirectory() / "textures" / "icon" / "icon.png";

        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            return;
        }

        GLFWimage icon{};
        icon.width = width;
        icon.height = height;
        icon.pixels = pixels;
        glfwSetWindowIcon(window_, 1, &icon);

        stbi_image_free(pixels);
    }

    void Application::shutdownWindow()
    {
        if (window_ != nullptr)
        {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }

        glfwTerminate();
    }

    void Application::handleMouse(double x, double y)
    {
        if (screen_ != AppScreen::Game)
        {
            if (renderer_ != nullptr)
            {
                renderer_->uiMouseMove(x, y);
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

    void Application::toggleFullscreen()
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

        if (renderer_ != nullptr)
        {
            renderer_->setFramebufferResized();
        }
    }

    void Application::setHotbarSelectedSlot(int slot)
    {
        hotbarSelectedSlot_ = std::clamp(slot, 0, 9);
        if (renderer_ != nullptr)
        {
            renderer_->setHotbarSelectedSlot(hotbarSelectedSlot_);
        }
    }

    void Application::cycleHotbarSelectedSlot(int delta)
    {
        setHotbarSelectedSlot((hotbarSelectedSlot_ + delta + 10) % 10);
    }

    void Application::setMouseCaptured(bool captured)
    {
        mouseCaptured_ = captured;
        firstMouse_ = true;
        glfwSetInputMode(window_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }

    void Application::handleMenuClick(double x, double y)
    {
        int width = WindowWidth;
        int height = WindowHeight;
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

    void Application::setScreen(AppScreen screen)
    {
        screen_ = screen;
        if (screen_ == AppScreen::WorldSelect)
        {
            refreshWorldList();
        }
        jumpHeld_ = false;
        jumpPressed_ = false;
        breakHeld_ = false;
        if (renderer_ != nullptr)
        {
            renderer_->updateBlockBreaking(
                {playerPosition_.x, playerPosition_.y + EyeHeight, playerPosition_.z},
                renderViewDirection(camera_),
                false,
                playerPosition_,
                static_cast<float>(FixedPhysicsTimestep));
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

    void Application::enterGameScene()
    {
        if (!hasSelectedWorld_)
        {
            log::warn("Cannot enter game scene without a selected world.");
            return;
        }

        loadWorldState();
        saveWorldState();
        resetPlayerRuntimeState();
        if (renderer_ != nullptr)
        {
            renderer_->loadGameScene(selectedWorldDirectory_, worldSeed_);
        }
        loadPlayerState();
        setScreen(AppScreen::Game);
    }

    std::filesystem::path Application::playerStatePath() const
    {
        return selectedWorldDirectory_ / "player.dat";
    }

    std::filesystem::path Application::worldStatePath() const
    {
        return selectedWorldDirectory_ / "world.dat";
    }

    void Application::resetPlayerRuntimeState()
    {
        playerPosition_ = {0.0, 300.0, 0.0};
        previousPlayerPosition_ = playerPosition_;
        camera_.setAngles(0.0f, 0.0f);
        moveMode_ = MoveMode::Fly;
        verticalVelocity_ = 0.0;
        grounded_ = false;
        jumpHeld_ = false;
        jumpPressed_ = false;
        physicsAccumulator_ = 0.0;
    }

    void Application::refreshWorldList()
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

        if (renderer_ != nullptr)
        {
            std::vector<Renderer::WorldListItem> items;
            items.reserve(availableWorlds_.size());
            for (const WorldInfo& world : availableWorlds_)
            {
                items.push_back(Renderer::WorldListItem{
                    world.name,
                    formatUnixSeconds(world.createdUnixSeconds),
                    formatUnixSeconds(world.lastPlayedUnixSeconds)
                });
            }
            renderer_->setWorldList(items);
        }
    }

    void Application::openWorldByIndex(size_t index)
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
        hasSelectedWorld_ = true;
        log::info("World selected: " + selectedWorldName_);
        enterGameScene();
    }

    void Application::createWorldFromUi()
    {
        const std::string rawName = renderer_ != nullptr ? renderer_->uiInputValue("new-world-name") : "New World";
        const std::string rawSeed = renderer_ != nullptr ? renderer_->uiInputValue("new-world-seed") : "";
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
            worldCreatedUnixSeconds_ = currentUnixSeconds();
            worldLastPlayedUnixSeconds_ = worldCreatedUnixSeconds_;
            hasSelectedWorld_ = true;
            std::filesystem::create_directories(selectedWorldDirectory_ / "regions");
            saveWorldState();
            resetPlayerRuntimeState();
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

    void Application::returnToLobbyScene()
    {
        if (renderer_ != nullptr)
        {
            renderer_->closeInventoryInteraction();
        }
        saveWorldState();
        savePlayerState();
        if (renderer_ != nullptr)
        {
            renderer_->unloadGameScene();
        }
        previousPlayerPosition_ = playerPosition_;
        physicsAccumulator_ = 0.0;
        verticalVelocity_ = 0.0;
        grounded_ = false;
        hasSelectedWorld_ = false;
        setScreen(AppScreen::Lobby);
    }

    void Application::cycleViewMode()
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

    void Application::loadMovementConfig()
    {
        flyMoveSpeed_ = DefaultFlyMoveSpeed;
        groundMoveSpeed_ = DefaultGroundMoveSpeed;
        jumpSpeed_ = DefaultJumpSpeed;
        gravity_ = DefaultGravity;

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
    }

    void Application::loadWorldState()
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

    void Application::saveWorldState()
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

    void Application::loadPlayerState()
    {
        std::ifstream file(playerStatePath(), std::ios::binary);
        if (!file.is_open())
        {
            log::warn("Player state not found, using default player state.");
            previousPlayerPosition_ = playerPosition_;
            return;
        }

        std::vector<uint8_t> bytes(PlayerStateFileSize);
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
            const double verticalVelocity = readF64(bytes, offset);
            std::array<Renderer::InventorySlot, PlayerInventorySlotCount> inventorySlots{};
            for (Renderer::InventorySlot& slot : inventorySlots)
            {
                slot.itemId = readU16(bytes, offset);
                slot.count = readU16(bytes, offset);
            }

            if (!std::isfinite(x) ||
                !std::isfinite(y) ||
                !std::isfinite(z) ||
                !std::isfinite(yaw) ||
                !std::isfinite(pitch) ||
                !std::isfinite(verticalVelocity) ||
                moveMode > 1u)
            {
                log::warn("Player state file contains invalid values, using default player state.");
                previousPlayerPosition_ = playerPosition_;
                return;
            }

            playerPosition_ = {wrapWorldCoordinate(x), y, wrapWorldCoordinate(z)};
            previousPlayerPosition_ = playerPosition_;
            camera_.setAngles(yaw, pitch);
            moveMode_ = moveMode == 0u ? MoveMode::Fly : MoveMode::Ground;
            verticalVelocity_ = verticalVelocity;
            grounded_ = false;
            jumpHeld_ = false;
            jumpPressed_ = false;
            physicsAccumulator_ = 0.0;
            if (renderer_ != nullptr)
            {
                renderer_->setInventorySnapshot(inventorySlots);
            }
            log::info("Player state loaded.");
        }
        catch (...)
        {
            log::warn("Player state load failed, using default player state.");
            previousPlayerPosition_ = playerPosition_;
        }
    }

    void Application::savePlayerState() const
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
            writeF64(bytes, verticalVelocity_);
            const std::array<Renderer::InventorySlot, PlayerInventorySlotCount> inventorySlots = renderer_ != nullptr
                ? renderer_->inventorySnapshot()
                : std::array<Renderer::InventorySlot, PlayerInventorySlotCount>{};
            for (const Renderer::InventorySlot& slot : inventorySlots)
            {
                writeU16(bytes, slot.itemId);
                writeU16(bytes, slot.count);
            }

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

    DVec3 Application::interpolatedPlayerPosition(double alpha) const
    {
        return {
            previousPlayerPosition_.x + (playerPosition_.x - previousPlayerPosition_.x) * alpha,
            previousPlayerPosition_.y + (playerPosition_.y - previousPlayerPosition_.y) * alpha,
            previousPlayerPosition_.z + (playerPosition_.z - previousPlayerPosition_.z) * alpha
        };
    }

    void Application::updatePlayer(double fixedDeltaSeconds, bool allowInput)
    {
        constexpr double MaxCollisionStep = 0.25;

        const float yaw = camera_.yaw();
        const Vec3 forward{std::cos(yaw), 0.0f, std::sin(yaw)};
        const Vec3 right{std::sin(yaw), 0.0f, -std::cos(yaw)};

        Vec3 movement{};
        if (allowInput && glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS)
        {
            movement.x += forward.x;
            movement.z += forward.z;
        }
        if (allowInput && glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS)
        {
            movement.x -= forward.x;
            movement.z -= forward.z;
        }
        if (allowInput && glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS)
        {
            movement.x += right.x;
            movement.z += right.z;
        }
        if (allowInput && glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS)
        {
            movement.x -= right.x;
            movement.z -= right.z;
        }

        if (moveMode_ == MoveMode::Fly)
        {
            if (allowInput && glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS)
            {
                movement.y += 1.0f;
            }
            if (allowInput && (glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS))
            {
                movement.y -= 1.0f;
            }
            verticalVelocity_ = 0.0;
            grounded_ = false;
        }
        else
        {
            movement.y = 0.0f;
            grounded_ = renderer_ != nullptr && renderer_->playerColliderIntersectsTerrain({playerPosition_.x, playerPosition_.y - 0.03, playerPosition_.z});
            if (grounded_ && verticalVelocity_ < 0.0)
            {
                verticalVelocity_ = 0.0;
            }
            if (grounded_ && verticalVelocity_ <= 0.0 && (jumpHeld_ || jumpPressed_))
            {
                verticalVelocity_ = jumpSpeed_;
                grounded_ = false;
                jumpPressed_ = false;
            }
            else if (!jumpHeld_)
            {
                jumpPressed_ = false;
            }
        }

        movement = normalize(movement);
        const double moveSpeed = moveMode_ == MoveMode::Fly ? flyMoveSpeed_ : groundMoveSpeed_;
        const double distance = moveSpeed * fixedDeltaSeconds;
        const DVec3 delta{
            static_cast<double>(movement.x) * distance,
            moveMode_ == MoveMode::Fly ? static_cast<double>(movement.y) * distance : verticalVelocity_ * fixedDeltaSeconds,
            static_cast<double>(movement.z) * distance
        };
        const double maxDelta = std::max(std::abs(delta.x), std::max(std::abs(delta.y), std::abs(delta.z)));
        const int steps = std::max(1, static_cast<int>(std::ceil(maxDelta / MaxCollisionStep)));
        const DVec3 stepDelta{
            delta.x / static_cast<double>(steps),
            delta.y / static_cast<double>(steps),
            delta.z / static_cast<double>(steps)
        };

        auto tryMoveAxis = [&](double dx, double dy, double dz) -> bool
        {
            DVec3 next = playerPosition_;
            next.x += dx;
            next.y += dy;
            next.z += dz;
            if (renderer_ == nullptr || !renderer_->playerColliderIntersectsTerrain(next))
            {
                playerPosition_ = next;
                return true;
            }
            return false;
        };

        auto moveAxisWithContact = [&](double dx, double dy, double dz) -> bool
        {
            if (tryMoveAxis(dx, dy, dz))
            {
                return true;
            }

            double low = 0.0;
            double high = 1.0;
            for (int i = 0; i < 8; ++i)
            {
                const double mid = (low + high) * 0.5;
                DVec3 next = playerPosition_;
                next.x += dx * mid;
                next.y += dy * mid;
                next.z += dz * mid;
                const bool blocked = renderer_ != nullptr && renderer_->playerColliderIntersectsTerrain(next);
                if (blocked)
                {
                    high = mid;
                }
                else
                {
                    low = mid;
                }
            }

            if (low > 0.000001)
            {
                playerPosition_.x += dx * low;
                playerPosition_.y += dy * low;
                playerPosition_.z += dz * low;
            }
            return false;
        };

        bool blockedVertically = false;
        for (int i = 0; i < steps; ++i)
        {
            moveAxisWithContact(stepDelta.x, 0.0, 0.0);
            if (!moveAxisWithContact(0.0, stepDelta.y, 0.0) && moveMode_ == MoveMode::Ground)
            {
                blockedVertically = true;
                if (stepDelta.y < 0.0)
                {
                    grounded_ = true;
                }
                verticalVelocity_ = 0.0;
            }
            moveAxisWithContact(0.0, 0.0, stepDelta.z);
        }

        if (moveMode_ == MoveMode::Ground && !blockedVertically && !grounded_)
        {
            verticalVelocity_ -= gravity_ * fixedDeltaSeconds;
        }
    }

    void Application::updateDebugText()
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
        const std::string lookAtText = renderer_ != nullptr ? renderer_->selectedBlockText() : "LOOKAT: none";
        const std::string climateText = renderer_ != nullptr ? renderer_->climateText(playerPosition_) : "CLIMATE: T[0.000] P[0.000]";
        const uint64_t day = worldTicks_ / TicksPerDay;
        const uint64_t minuteOfDay = (worldTicks_ % TicksPerDay) / TicksPerMinute;
        const uint64_t hour = minuteOfDay / MinutesPerHour;
        const uint64_t minute = minuteOfDay % MinutesPerHour;

        std::snprintf(
            debugText_.data(),
            debugText_.size(),
            "FPS: %04d [%07.3fMS]\nPOS: X %.3f [%.3f] / Y %.3f / Z %.3f [%.3f]\nVIEW: YAW %.1f / PITCH %.1f [%s]\n%s\n%s\nTIME: %lluD %02lluH %02lluM\nSEED: %llu",
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
            static_cast<unsigned long long>(day),
            static_cast<unsigned long long>(hour),
            static_cast<unsigned long long>(minute),
            static_cast<unsigned long long>(worldSeed_));

        fpsSampleFrames_ = 0;
        fpsSampleStart_ = now;
    }
}
