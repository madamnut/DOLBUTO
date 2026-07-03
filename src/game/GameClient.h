#pragma once

#include "camera/Camera.h"
#include "game/ClientRuntime.h"
#include "game/GameMode.h"
#include "game/PlayerMovementSystem.h"
#include "game/PlayerStats.h"
#include "game/ViewmodelMotion.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace dolbuto
{
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

        using MoveMode = game::PlayerMoveMode;

        enum class AppScreen
        {
            Lobby,
            WorldSelect,
            WorldCreate,
            Game,
            Inventory,
            Guide,
            Pause,
            Options
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

        enum class GuideNotificationPhase
        {
            Entering,
            Stacked,
            Exiting
        };

        struct GuideNotification
        {
            std::string title;
            GuideNotificationPhase phase = GuideNotificationPhase::Entering;
            double x = 0.0;
            double y = 0.0;
            double enterTime = 0.0;
            double holdTime = 0.0;
            double exitTime = 0.0;
        };

        struct GuideProgress
        {
            std::string key;
            std::vector<std::string> obtainedItemKeys;
        };

        struct GuideInventoryCount
        {
            std::string key;
            uint32_t count = 0;
        };

        static constexpr double DefaultPlayerSpawnHeight = 512.0;

        void attachWindowCallbacks();
        void handleMouse(double x, double y);
        bool openRadialInteraction(bool preferHeldItemBlockActions);
        void updateRadialSelection(double x, double y);
        void closeRadialInteraction(bool execute);
        void handleMenuClick(double x, double y);
        void toggleFullscreen();
        void setMouseCaptured(bool captured);
        void openChatInput();
        void closeChatInput();
        void submitChatInput();
        void appendChatMessage(std::string_view text);
        void appendChatSystemMessage(std::string_view text);
        void updateChatUi();
        void setScreen(AppScreen screen);
        void completeGuideStep(std::string_view key);
        bool guideStepCompleted(std::string_view key) const;
        bool guideStepAvailable(std::string_view key) const;
        void updateGuideInventoryCriteria();
        void processGuideObtainedItem(std::string_view itemKey);
        void refreshGuideInventoryCountBaseline();
        void updateGuideUi();
        void enqueueGuideNotification(std::string_view title);
        void updateGuideNotifications(double deltaSeconds);
        void enterGameScene();
        void refreshWorldList();
        void openWorldByIndex(size_t index);
        void createWorldFromUi();
        DVec3 findInitialSpawnPosition(uint64_t worldSeed) const;
        void resetPlayerRuntimeState();
        void setCreateWorldGameMode(game::GameMode mode);
        void applyGameMode(game::GameMode mode);
        std::filesystem::path playerStatePath() const;
        std::filesystem::path worldStatePath() const;
        void returnToLobbyScene();
        void cycleViewMode();
        void setHotbarSelectedSlot(int slot);
        void cycleHotbarSelectedSlot(int delta);
        void loadMovementConfig();
        void loadSettings();
        void saveSettings() const;
        void applyAudioSettings();
        void updateOptionsUi();
        void applyOptionsSliderValues();
        void toggleViewBobbingOption();
        void toggleSprintOption();
        void toggleSneakOption();
        void toggleProneOption();
        void loadWorldState();
        void saveWorldState();
        void loadPlayerState();
        void savePlayerState() const;
        void updatePlayerStatsUi();
        DVec3 interpolatedPlayerPosition(double alpha) const;
        double currentPlayerHeightScale() const;
        double currentEyeHeight() const;
        double interpolatedEyeHeight(double alpha) const;
        DVec3 thirdPersonCameraPosition(DVec3 pivot, Vec3 offsetDirection) const;
        void updatePlayerLookPose(float bodyYaw, float& headYaw, float& headPitch) const;
        void startViewmodelSwing();
        ViewmodelMotion updateViewmodelMotion(const Camera& camera, bool active, double deltaSeconds, float walkPhase, float walkAmount, double verticalVelocity, bool grounded, bool fallMotionEnabled);
        void updatePlayer(double fixedDeltaSeconds, bool allowInput);
        void updateDebugText();

        GLFWwindow* window_ = nullptr;
        std::unique_ptr<game::ClientRuntime> runtime_;
        Camera camera_;
        bool fullscreen_ = false;
        bool debugTextVisible_ = true;
        bool hudVisible_ = true;
        bool terrainWireframe_ = false;
        int climateOverlayMode_ = 0;
        int hotbarSelectedSlot_ = 0;
        bool screenshotRequested_ = false;
        bool mouseCaptured_ = true;
        bool chatOpen_ = false;
        bool chatRestoreMouseCaptured_ = true;
        AppScreen screen_ = AppScreen::Lobby;
        AppScreen optionsReturnScreen_ = AppScreen::Lobby;
        ViewMode viewMode_ = ViewMode::FirstPerson;
        game::GameMode gameMode_ = game::GameMode::Sandbox;
        game::GameMode pendingCreateGameMode_ = game::GameMode::Sandbox;
        MoveMode moveMode_ = MoveMode::Ground;
        std::vector<WorldInfo> availableWorlds_;
        std::vector<std::string> chatMessages_;
        std::vector<std::string> completedGuideKeys_;
        std::vector<GuideProgress> guideProgress_;
        std::vector<GuideInventoryCount> guideInventoryCounts_;
        std::vector<GuideNotification> guideNotifications_;
        std::string selectedWorldName_;
        std::filesystem::path selectedWorldDirectory_;
        uint64_t worldSeed_ = 0;
        uint64_t worldCreatedUnixSeconds_ = 0;
        uint64_t worldLastPlayedUnixSeconds_ = 0;
        bool hasSelectedWorld_ = false;
        game::PlayerStats playerStats_{};
        DVec3 playerPosition_{0.0, DefaultPlayerSpawnHeight, 0.0};
        DVec3 previousPlayerPosition_{0.0, DefaultPlayerSpawnHeight, 0.0};
        double flyMoveSpeed_ = 64.0;
        double groundMoveSpeed_ = 4.317;
        double jumpSpeed_ = 8.4;
        double gravity_ = 32.0;
        double sprintSpeedScale_ = 1.3;
        double sneakSpeedScale_ = 0.3;
        double sneakHeightScale_ = 1.5 / 1.8;
        double proneHeight_ = 0.6;
        double proneEyeHeight_ = 0.5;
        double swimSpeedScale_ = 0.55;
        double movementDoubleTapWindow_ = 0.35;
        float bodyYaw_ = 0.0f;
        float previousBodyYaw_ = 0.0f;
        float playerWalkPhase_ = 0.0f;
        float previousPlayerWalkPhase_ = 0.0f;
        float playerWalkAmount_ = 0.0f;
        float previousPlayerWalkAmount_ = 0.0f;
        float sprintFovAmount_ = 0.0f;
        float previousSprintFovAmount_ = 0.0f;
        float eyeHeightScale_ = 1.0f;
        float previousEyeHeightScale_ = 1.0f;
        float playerHeightScale_ = 1.0f;
        bool proneClimbActive_ = false;
        double proneClimbProgress_ = 0.0;
        DVec3 proneClimbStart_{};
        DVec3 proneClimbTarget_{};
        bool waterClimbActive_ = false;
        double waterClimbProgress_ = 0.0;
        DVec3 waterClimbStart_{};
        DVec3 waterClimbTarget_{};
        double footstepDistanceAccumulator_ = 0.0;
        ViewmodelMotion viewmodelMotionState_{};
        double viewmodelMotionTime_ = 0.0;
        double viewmodelSwingTime_ = 0.0;
        float viewmodelFallLift_ = 0.0f;
        float viewmodelLandingKick_ = 0.0f;
        float viewmodelLandingKickTarget_ = 0.0f;
        float viewmodelMotionLastYaw_ = 0.0f;
        float viewmodelMotionLastPitch_ = 0.0f;
        bool viewmodelSwingActive_ = false;
        bool viewmodelSwingRepeat_ = false;
        bool viewmodelMotionInitialized_ = false;
        double bgmVolume_ = 1.0;
        double sfxVolume_ = 1.0;
        double fovDegrees_ = 60.0;
        bool viewBobbing_ = true;
        double verticalVelocity_ = 0.0;
        bool grounded_ = false;
        bool jumpHeld_ = false;
        bool jumpPressed_ = false;
        bool toggleSprint_ = false;
        bool toggleSneak_ = false;
        bool toggleProne_ = false;
        bool sprintToggled_ = false;
        bool sneakToggled_ = false;
        bool proneToggled_ = false;
        bool doubleTapSprintActive_ = false;
        double lastForwardTapTime_ = -1000.0;
        double lastJumpTapTime_ = -1000.0;
        bool breakHeld_ = false;
        uint64_t nextSandboxHeldBreakTick_ = 0;
        bool radialActive_ = false;
        bool radialRestoreMouseCaptured_ = true;
        std::vector<gameplay::ItemInteractionActionMenu> radialActions_;
        std::optional<std::size_t> radialSelectedActionIndex_;
        std::optional<std::size_t> radialSelectedCandidateIndex_;
        double radialCenterX_ = 0.0;
        double radialCenterY_ = 0.0;
        double physicsAccumulator_ = 0.0;
        std::chrono::steady_clock::time_point lastFrameTime_{};
        int windowedX_ = 0;
        int windowedY_ = 0;
        int windowedWidth_ = 1280;
        int windowedHeight_ = 720;
        std::chrono::steady_clock::time_point fpsSampleStart_{};
        int fpsSampleFrames_ = 0;
        uint64_t worldTicks_ = 7200;
        std::array<char, 768> debugText_{"FPS: 0000 [000.000MS]\nPOS: X 0.000 [0.000] / Y 512.000 / Z 0.000 [0.000]\nVIEW: YAW 0.0 / PITCH 0.0 [EAST]\nLOOKAT: none\nCLIMATE: T[0.000] P[0.000]\nBIOME: T[0] P[0] GND[0] - FrozenOcean\nTERRAIN: GND[0.000] SMTH[0.000] W[0.000] PV[0.000]\nVALUE: RAW[0.000] NORM[0.000] PVW[0.000] PVMUL[0.000] BASE[0.000] INF[0.000] VAL[0.000] H[0]\nLIGHT: SKY[1.00]\nTIME: 0D 06H 00M\nSEED: 0"};
        bool firstMouse_ = true;
        double lastMouseX_ = 0.0;
        double lastMouseY_ = 0.0;
        bool guideDragging_ = false;
        double guideMouseX_ = 0.0;
        double guideMouseY_ = 0.0;
        double guideLastMouseX_ = 0.0;
        double guideLastMouseY_ = 0.0;
        double guidePanX_ = 0.0;
        double guidePanY_ = 0.0;
        double guideZoom_ = 1.0;
    };
}
