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

        void initWindow();
        void shutdownWindow();
        void handleMouse(double x, double y);
        void toggleFullscreen();
        void setMouseCaptured(bool captured);
        void cycleViewMode();
        void updatePlayer(double deltaSeconds);
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
        DVec3 playerPosition_{0.0, 150.0, 0.0};
        std::chrono::steady_clock::time_point lastFrameTime_{};
        int windowedX_ = 0;
        int windowedY_ = 0;
        int windowedWidth_ = 1280;
        int windowedHeight_ = 720;
        std::chrono::steady_clock::time_point fpsSampleStart_{};
        int fpsSampleFrames_ = 0;
        std::array<char, 192> debugText_{"FPS: 0000 [000.000MS]\nPOS: X 0.000 / Y 150.000 / Z 0.000\nLOOK: YAW 0.0 / PITCH 0.0 [EAST]"};
        bool firstMouse_ = true;
        double lastMouseX_ = 0.0;
        double lastMouseY_ = 0.0;
    };
}
