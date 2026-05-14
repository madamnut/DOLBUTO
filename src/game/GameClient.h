#pragma once

#include "camera/Camera.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace dolbuto
{
    class Renderer;

    class GameClient
    {
    public:
        explicit GameClient(GLFWwindow* window);
        ~GameClient();

        GameClient(const GameClient&) = delete;
        GameClient& operator=(const GameClient&) = delete;

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

        enum class AppScreen
        {
            Lobby,
            WorldSelect,
            WorldCreate,
            Game,
            Inventory,
            Pause
        };

        struct WorldInfo
        {
            std::string name;
            std::filesystem::path path;
            uint64_t totalTicks = 0;
            uint64_t seed = 0;
            uint64_t createdUnixSeconds = 0;
            uint64_t lastPlayedUnixSeconds = 0;
        };

        void attachWindowCallbacks();
        void handleMouse(double x, double y);
        void handleMenuClick(double x, double y);
        void toggleFullscreen();
        void setMouseCaptured(bool captured);
        void setScreen(AppScreen screen);
        void enterGameScene();
        void refreshWorldList();
        void openWorldByIndex(size_t index);
        void createWorldFromUi();
        void resetPlayerRuntimeState();
        std::filesystem::path playerStatePath() const;
        std::filesystem::path worldStatePath() const;
        void returnToLobbyScene();
        void cycleViewMode();
        void setHotbarSelectedSlot(int slot);
        void cycleHotbarSelectedSlot(int delta);
        void loadMovementConfig();
        void loadWorldState();
        void saveWorldState();
        void loadPlayerState();
        void savePlayerState() const;
        DVec3 interpolatedPlayerPosition(double alpha) const;
        void updatePlayer(double fixedDeltaSeconds, bool allowInput);
        void updateDebugText();

        GLFWwindow* window_ = nullptr;
        std::unique_ptr<Renderer> renderer_;
        Camera camera_;
        bool fullscreen_ = false;
        bool debugTextVisible_ = true;
        bool hudVisible_ = true;
        bool terrainWireframe_ = false;
        int climateOverlayMode_ = 0;
        int hotbarSelectedSlot_ = 0;
        bool screenshotRequested_ = false;
        bool mouseCaptured_ = true;
        AppScreen screen_ = AppScreen::Lobby;
        ViewMode viewMode_ = ViewMode::FirstPerson;
        MoveMode moveMode_ = MoveMode::Fly;
        std::vector<WorldInfo> availableWorlds_;
        std::string selectedWorldName_;
        std::filesystem::path selectedWorldDirectory_;
        uint64_t worldSeed_ = 0;
        uint64_t worldCreatedUnixSeconds_ = 0;
        uint64_t worldLastPlayedUnixSeconds_ = 0;
        bool hasSelectedWorld_ = false;
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
        bool breakHeld_ = false;
        double physicsAccumulator_ = 0.0;
        std::chrono::steady_clock::time_point lastFrameTime_{};
        int windowedX_ = 0;
        int windowedY_ = 0;
        int windowedWidth_ = 1280;
        int windowedHeight_ = 720;
        std::chrono::steady_clock::time_point fpsSampleStart_{};
        int fpsSampleFrames_ = 0;
        uint64_t worldTicks_ = 7200;
        std::array<char, 512> debugText_{"FPS: 0000 [000.000MS]\nPOS: X 0.000 [0.000] / Y 300.000 / Z 0.000 [0.000]\nVIEW: YAW 0.0 / PITCH 0.0 [EAST]\nLOOKAT: none\nCLIMATE: T[0.000] P[0.000]\nTIME: 0D 06H 00M\nSEED: 0"};
        bool firstMouse_ = true;
        double lastMouseX_ = 0.0;
        double lastMouseY_ = 0.0;
    };
}
