#pragma once

#include "camera/Camera.h"

#include <array>
#include <chrono>
#include <memory>

struct GLFWwindow;

namespace dolbuto
{
    class Renderer;

    class Application
    {
    public:
        Application();
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        void run();

    private:
        enum class ViewMode
        {
            FirstPerson,
            ThirdPersonRear,
            ThirdPersonFront
        };

        enum class MoveMode
        {
            Fly,
            Ground
        };

        void initWindow();
        void shutdownWindow();
        void handleMouse(double x, double y);
        void toggleFullscreen();
        void setMouseCaptured(bool captured);
        void cycleViewMode();
        void loadMovementConfig();
        DVec3 interpolatedPlayerPosition(double alpha) const;
        void updatePlayer(double fixedDeltaSeconds);
        void updateDebugText();

        GLFWwindow* window_ = nullptr;
        std::unique_ptr<Renderer> renderer_;
        Camera camera_;
        bool fullscreen_ = false;
        bool debugTextVisible_ = true;
        bool terrainWireframe_ = false;
        bool screenshotRequested_ = false;
        bool mouseCaptured_ = true;
        ViewMode viewMode_ = ViewMode::FirstPerson;
        MoveMode moveMode_ = MoveMode::Fly;
        DVec3 playerPosition_{0.0, 300.0, 0.0};
        DVec3 previousPlayerPosition_{0.0, 300.0, 0.0};
        double flyMoveSpeed_ = 64.0;
        double groundMoveSpeed_ = 4.317;
        double jumpSpeed_ = 8.4;
        double gravity_ = 32.0;
        double verticalVelocity_ = 0.0;
        bool grounded_ = false;
        bool jumpHeld_ = false;
        bool jumpPressed_ = false;
        double physicsAccumulator_ = 0.0;
        std::chrono::steady_clock::time_point lastFrameTime_{};
        int windowedX_ = 0;
        int windowedY_ = 0;
        int windowedWidth_ = 1280;
        int windowedHeight_ = 720;
        std::chrono::steady_clock::time_point fpsSampleStart_{};
        int fpsSampleFrames_ = 0;
        std::array<char, 192> debugText_{"FPS: 0000 [000.000MS]\nPOS: X 0.000 / Y 300.000 / Z 0.000\nLOOK: YAW 0.0 / PITCH 0.0 [EAST]"};
        bool firstMouse_ = true;
        double lastMouseX_ = 0.0;
        double lastMouseY_ = 0.0;
    };
}
