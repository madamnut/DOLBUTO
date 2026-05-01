#include "app/Application.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "renderer/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace dolbuto
{
    namespace
    {
        constexpr int WindowWidth = 1280;
        constexpr int WindowHeight = 720;
        constexpr const char* WindowTitle = "DOLBUTO";
        constexpr float RadiansToDegrees = 57.2957795131f;
        constexpr float Pi = 3.14159265359f;
        constexpr float EyeHeight = 1.5625f;
        constexpr float ThirdPersonDistance = 5.5f;

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

        Vec3 thirdPersonViewDirection(const Camera& camera)
        {
            const Vec3 forward = camera.forward();
            return {forward.x, -forward.y, forward.z};
        }
    }

    Application::Application()
    {
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

            updatePlayer(delta.count());
            updateDebugText();

            const Vec3 eyePosition{playerPosition_.x, playerPosition_.y + EyeHeight, playerPosition_.z};
            Camera renderCamera = camera_;
            Vec3 renderCameraPosition = eyePosition;
            bool showPlayer = false;
            if (viewMode_ == ViewMode::ThirdPersonRear)
            {
                const Vec3 forward = thirdPersonViewDirection(camera_);
                renderCameraPosition = {
                    eyePosition.x - forward.x * ThirdPersonDistance,
                    eyePosition.y - forward.y * ThirdPersonDistance,
                    eyePosition.z - forward.z * ThirdPersonDistance
                };
                showPlayer = true;
            }
            else if (viewMode_ == ViewMode::ThirdPersonFront)
            {
                const Vec3 forward = thirdPersonViewDirection(camera_);
                renderCameraPosition = {
                    eyePosition.x + forward.x * ThirdPersonDistance,
                    eyePosition.y + forward.y * ThirdPersonDistance,
                    eyePosition.z + forward.z * ThirdPersonDistance
                };
                renderCamera.setAngles(camera_.yaw() + Pi, -camera_.pitch());
                showPlayer = true;
            }

            renderer_->drawFrame(renderCamera, renderCameraPosition, debugText_.data(), debugTextVisible_, screenshotRequested_, showPlayer, playerPosition_, camera_.yaw());
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
                app->setMouseCaptured(true);
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

    void Application::updatePlayer(double deltaSeconds)
    {
        constexpr float MoveSpeed = 64.0f;

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
        if (glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS)
        {
            movement.y += 1.0f;
        }
        if (glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
        {
            movement.y -= 1.0f;
        }

        movement = normalize(movement);
        const float distance = MoveSpeed * static_cast<float>(deltaSeconds);
        playerPosition_.x += movement.x * distance;
        playerPosition_.y += movement.y * distance;
        playerPosition_.z += movement.z * distance;
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
