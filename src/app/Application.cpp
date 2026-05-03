#include "app/Application.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "renderer/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>

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

        Vec3 renderViewDirection(const Camera& camera)
        {
            const Vec3 forward = camera.forward();
            return {forward.x, -forward.y, forward.z};
        }
    }

    Application::Application()
    {
        loadMovementConfig();
        initWindow();
        renderer_ = std::make_unique<Renderer>(window_);
        fpsSampleStart_ = std::chrono::steady_clock::now();
        lastFrameTime_ = fpsSampleStart_;
    }

    Application::~Application()
    {
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

            const auto now = std::chrono::steady_clock::now();
            const std::chrono::duration<double> delta = now - lastFrameTime_;
            lastFrameTime_ = now;

            physicsAccumulator_ += std::min(delta.count(), MaxPhysicsFrameTime);
            while (physicsAccumulator_ >= FixedPhysicsTimestep)
            {
                previousPlayerPosition_ = playerPosition_;
                updatePlayer(FixedPhysicsTimestep);
                physicsAccumulator_ -= FixedPhysicsTimestep;
            }

            updateDebugText();

            const double physicsAlpha = std::clamp(physicsAccumulator_ / FixedPhysicsTimestep, 0.0, 1.0);
            const DVec3 renderPlayerPosition = interpolatedPlayerPosition(physicsAlpha);
            const DVec3 eyePosition{renderPlayerPosition.x, renderPlayerPosition.y + EyeHeight, renderPlayerPosition.z};
            renderer_->updateBlockSelection(
                {playerPosition_.x, playerPosition_.y + EyeHeight, playerPosition_.z},
                renderViewDirection(camera_));
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

            renderer_->drawFrame(renderCamera, renderCameraPosition, debugText_.data(), debugTextVisible_, screenshotRequested_, showPlayer, renderPlayerPosition, camera_.yaw(), terrainWireframe_);
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

        glfwSetKeyCallback(window_, [](GLFWwindow* window, int key, int, int action, int)
        {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
            if (key == GLFW_KEY_SPACE && app != nullptr)
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
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            else if (key == GLFW_KEY_F11 && action == GLFW_PRESS && app != nullptr)
            {
                app->toggleFullscreen();
            }
            else if (key == GLFW_KEY_F3 && action == GLFW_PRESS && app != nullptr)
            {
                app->debugTextVisible_ = !app->debugTextVisible_;
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
            else if (key == GLFW_KEY_F && action == GLFW_PRESS && app != nullptr)
            {
                app->moveMode_ = app->moveMode_ == MoveMode::Fly ? MoveMode::Ground : MoveMode::Fly;
                app->verticalVelocity_ = 0.0;
                app->grounded_ = false;
                app->jumpPressed_ = false;
            }
        });

        glfwSetMouseButtonCallback(window_, [](GLFWwindow* window, int button, int action, int)
        {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
            if (app == nullptr || action != GLFW_PRESS)
            {
                return;
            }

            if (button == GLFW_MOUSE_BUTTON_MIDDLE)
            {
                app->setMouseCaptured(false);
            }
            else if (button == GLFW_MOUSE_BUTTON_LEFT)
            {
                if (app->mouseCaptured_ && app->renderer_ != nullptr)
                {
                    app->renderer_->editBlockInView(
                        {app->playerPosition_.x, app->playerPosition_.y + EyeHeight, app->playerPosition_.z},
                        renderViewDirection(app->camera_),
                        false);
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
                        true);
                }
                else
                {
                    app->setMouseCaptured(true);
                }
            }
        });

        setMouseCaptured(true);
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
        if (!mouseCaptured_)
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

    void Application::setMouseCaptured(bool captured)
    {
        mouseCaptured_ = captured;
        firstMouse_ = true;
        glfwSetInputMode(window_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
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

        const std::filesystem::path path = std::filesystem::path(DOLBUTO_CONFIG_DIR) / "world.json";
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

    DVec3 Application::interpolatedPlayerPosition(double alpha) const
    {
        return {
            previousPlayerPosition_.x + (playerPosition_.x - previousPlayerPosition_.x) * alpha,
            previousPlayerPosition_.y + (playerPosition_.y - previousPlayerPosition_.y) * alpha,
            previousPlayerPosition_.z + (playerPosition_.z - previousPlayerPosition_.z) * alpha
        };
    }

    void Application::updatePlayer(double fixedDeltaSeconds)
    {
        constexpr double MaxCollisionStep = 0.25;

        const float yaw = camera_.yaw();
        const Vec3 forward{std::cos(yaw), 0.0f, std::sin(yaw)};
        const Vec3 right{std::sin(yaw), 0.0f, -std::cos(yaw)};

        Vec3 movement{};
        if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS)
        {
            movement.x += forward.x;
            movement.z += forward.z;
        }
        if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS)
        {
            movement.x -= forward.x;
            movement.z -= forward.z;
        }
        if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS)
        {
            movement.x += right.x;
            movement.z += right.z;
        }
        if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS)
        {
            movement.x -= right.x;
            movement.z -= right.z;
        }

        if (moveMode_ == MoveMode::Fly)
        {
            if (glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS)
            {
                movement.y += 1.0f;
            }
            if (glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
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

        std::snprintf(
            debugText_.data(),
            debugText_.size(),
            "FPS: %04d [%07.3fMS]\nPOS: X %.3f / Y %.3f / Z %.3f\nLOOK: YAW %.1f / PITCH %.1f [%s]",
            clampedFps,
            milliseconds,
            playerPosition_.x,
            playerPosition_.y,
            playerPosition_.z,
            yawDegrees,
            pitchDegrees,
            facing);

        fpsSampleFrames_ = 0;
        fpsSampleStart_ = now;
    }
}
