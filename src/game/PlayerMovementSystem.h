#pragma once

#include "camera/Camera.h"

#include <functional>

namespace dolbuto::game
{
    enum class PlayerMoveMode
    {
        Fly,
        Ground
    };

    struct PlayerMovementConfig
    {
        double flyMoveSpeed = 64.0;
        double groundMoveSpeed = 4.317;
        double jumpSpeed = 8.4;
        double gravity = 32.0;
        double sprintSpeedScale = 1.3;
        double sneakSpeedScale = 0.3;
        double sneakHeightScale = 1.5 / 1.8;
        double proneHeight = 0.6;
        double proneEyeHeight = 0.5;
        double swimSpeedScale = 0.55;
    };

    struct PlayerMovementInput
    {
        bool allowInput = false;
        bool forwardHeld = false;
        bool backwardHeld = false;
        bool rightHeld = false;
        bool leftHeld = false;
        bool ctrlHeld = false;
        bool shiftHeld = false;
        bool proneHeld = false;
        bool jumpHeld = false;
        bool jumpPressed = false;
        bool toggleSprint = false;
        bool toggleSneak = false;
        bool toggleProne = false;
        bool sprintToggled = false;
        bool sneakToggled = false;
        bool proneToggled = false;
        bool doubleTapSprintActive = false;
        float yaw = 0.0f;
    };

    struct PlayerMovementState
    {
        DVec3 position{};
        PlayerMoveMode moveMode = PlayerMoveMode::Fly;
        double verticalVelocity = 0.0;
        bool grounded = false;
        float bodyYaw = 0.0f;
        float walkPhase = 0.0f;
        float walkAmount = 0.0f;
        float sprintFovAmount = 0.0f;
        float eyeHeightScale = 1.0f;
        float playerHeightScale = 1.0f;
        bool proneClimbActive = false;
        double proneClimbProgress = 0.0;
        DVec3 proneClimbStart{};
        DVec3 proneClimbTarget{};
        bool waterClimbActive = false;
        double waterClimbProgress = 0.0;
        DVec3 waterClimbStart{};
        DVec3 waterClimbTarget{};
    };

    struct PlayerMovementResult
    {
        PlayerMovementInput input{};
        PlayerMovementState state{};
    };

    struct PlayerMovementCollision
    {
        std::function<bool(DVec3, double)> playerColliderIntersectsTerrain;
        std::function<bool(DVec3)> playerColliderHasSupportBelow;
        std::function<bool(DVec3, double)> playerColliderIntersectsWater;
    };

    class PlayerMovementSystem
    {
    public:
        static PlayerMovementResult tick(
            PlayerMovementInput input,
            PlayerMovementState state,
            const PlayerMovementConfig& config,
            const PlayerMovementCollision& collision,
            double fixedDeltaSeconds);
    };
}
