#include "game/PlayerMovementSystem.h"

#include <algorithm>
#include <cmath>

namespace dolbuto::game
{
    namespace
    {
        constexpr float Pi = 3.14159265359f;
        constexpr float TwoPi = Pi * 2.0f;
        constexpr double MaxCollisionStep = 0.25;
        constexpr double WalkCycleRadiansPerSecond = 8.0;
        constexpr double WalkAmountRiseResponse = 12.0;
        constexpr double WalkAmountFallResponse = 18.0;
        constexpr double BodyYawFollowResponse = 12.0;
        constexpr double SprintFovResponse = 10.0;
        constexpr double EyeHeightResponse = 12.0;
        constexpr double MaxWalkAmount = 1.35;
        constexpr float StrafeBodyYawOffset = Pi * 0.25f;

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
            float delta = normalizeAngle(to - from);
            return normalizeAngle(from + delta * static_cast<float>(alpha));
        }
    }

    PlayerMovementResult PlayerMovementSystem::tick(
        PlayerMovementInput input,
        PlayerMovementState state,
        const PlayerMovementConfig& config,
        const PlayerMovementCollision& collision,
        double fixedDeltaSeconds)
    {
        const DVec3 startPosition = state.position;

        const Vec3 forward{std::cos(input.yaw), 0.0f, std::sin(input.yaw)};
        const Vec3 right{std::sin(input.yaw), 0.0f, -std::cos(input.yaw)};

        Vec3 movement{};
        int strafeIntent = 0;
        if (!input.forwardHeld && !input.toggleSprint)
        {
            input.doubleTapSprintActive = false;
        }

        if (input.forwardHeld)
        {
            movement.x += forward.x;
            movement.z += forward.z;
        }
        if (input.backwardHeld)
        {
            movement.x -= forward.x;
            movement.z -= forward.z;
        }
        if (input.rightHeld)
        {
            movement.x += right.x;
            movement.z += right.z;
            ++strafeIntent;
        }
        if (input.leftHeld)
        {
            movement.x -= right.x;
            movement.z -= right.z;
            --strafeIntent;
        }

        const bool flyAccelerating = state.moveMode == PlayerMoveMode::Fly && input.ctrlHeld;
        const bool groundSneaking = state.moveMode == PlayerMoveMode::Ground &&
            (input.toggleSneak ? input.sneakToggled : input.shiftHeld);
        if (groundSneaking)
        {
            input.doubleTapSprintActive = false;
        }
        const bool holdSprintActive = !input.toggleSprint && (input.ctrlHeld || input.doubleTapSprintActive);
        const bool toggleSprintActive = input.toggleSprint && input.sprintToggled;
        const bool groundSprinting = state.moveMode == PlayerMoveMode::Ground && !groundSneaking && (holdSprintActive || toggleSprintActive);
        const double sprintFovBlend = 1.0 - std::exp(-SprintFovResponse * fixedDeltaSeconds);
        const double targetSprintFovAmount = groundSprinting ? 1.0 : 0.0;
        state.sprintFovAmount = static_cast<float>(std::clamp(
            static_cast<double>(state.sprintFovAmount) + (targetSprintFovAmount - static_cast<double>(state.sprintFovAmount)) * sprintFovBlend,
            0.0,
            1.0));
        const double playerHeightScale = groundSneaking ? config.sneakHeightScale : 1.0;
        const double eyeHeightBlend = 1.0 - std::exp(-EyeHeightResponse * fixedDeltaSeconds);
        state.eyeHeightScale = static_cast<float>(std::clamp(
            static_cast<double>(state.eyeHeightScale) + (playerHeightScale - static_cast<double>(state.eyeHeightScale)) * eyeHeightBlend,
            0.1,
            1.0));
        const bool flyDescending = state.moveMode == PlayerMoveMode::Fly && input.shiftHeld;

        if (state.moveMode == PlayerMoveMode::Fly)
        {
            if (input.jumpHeld)
            {
                movement.y += 1.0f;
            }
            if (flyDescending)
            {
                movement.y -= 1.0f;
            }
            state.verticalVelocity = 0.0;
            state.grounded = false;
        }
        else
        {
            movement.y = 0.0f;
            state.grounded = collision.playerColliderIntersectsTerrain &&
                collision.playerColliderIntersectsTerrain({state.position.x, state.position.y - 0.03, state.position.z}, playerHeightScale);
            if (state.grounded && state.verticalVelocity < 0.0)
            {
                state.verticalVelocity = 0.0;
            }
            if (state.grounded && state.verticalVelocity <= 0.0 && (input.jumpHeld || input.jumpPressed))
            {
                state.verticalVelocity = config.jumpSpeed;
                state.grounded = false;
                input.jumpPressed = false;
            }
            else if (!input.jumpHeld)
            {
                input.jumpPressed = false;
            }
        }

        movement = normalize(movement);
        double moveSpeed = state.moveMode == PlayerMoveMode::Fly ? config.flyMoveSpeed : config.groundMoveSpeed;
        if (flyAccelerating)
        {
            moveSpeed *= config.sprintSpeedScale;
        }
        else if (groundSneaking)
        {
            moveSpeed *= config.sneakSpeedScale;
        }
        else if (groundSprinting)
        {
            moveSpeed *= config.sprintSpeedScale;
        }
        const double distance = moveSpeed * fixedDeltaSeconds;
        const DVec3 delta{
            static_cast<double>(movement.x) * distance,
            state.moveMode == PlayerMoveMode::Fly ? static_cast<double>(movement.y) * distance : state.verticalVelocity * fixedDeltaSeconds,
            static_cast<double>(movement.z) * distance
        };
        const double maxDelta = std::max(std::abs(delta.x), std::max(std::abs(delta.y), std::abs(delta.z)));
        const int steps = std::max(1, static_cast<int>(std::ceil(maxDelta / MaxCollisionStep)));
        const DVec3 stepDelta{
            delta.x / static_cast<double>(steps),
            delta.y / static_cast<double>(steps),
            delta.z / static_cast<double>(steps)
        };

        auto isSneakEdgeGuardedMove = [&](double dx, double dy, double dz) -> bool
        {
            return groundSneaking && dy == 0.0 && (dx != 0.0 || dz != 0.0);
        };

        auto moveBlocked = [&](const DVec3& next, double dx, double dy, double dz) -> bool
        {
            if (collision.playerColliderIntersectsTerrain && collision.playerColliderIntersectsTerrain(next, playerHeightScale))
            {
                return true;
            }
            return isSneakEdgeGuardedMove(dx, dy, dz) &&
                collision.playerColliderHasSupportBelow &&
                !collision.playerColliderHasSupportBelow(next);
        };

        auto tryMoveAxis = [&](double dx, double dy, double dz) -> bool
        {
            DVec3 next = state.position;
            next.x += dx;
            next.y += dy;
            next.z += dz;
            if (!moveBlocked(next, dx, dy, dz))
            {
                state.position = next;
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
                DVec3 next = state.position;
                next.x += dx * mid;
                next.y += dy * mid;
                next.z += dz * mid;
                const bool blocked = moveBlocked(next, dx, dy, dz);
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
                state.position.x += dx * low;
                state.position.y += dy * low;
                state.position.z += dz * low;
            }
            return false;
        };

        bool blockedVertically = false;
        for (int i = 0; i < steps; ++i)
        {
            moveAxisWithContact(stepDelta.x, 0.0, 0.0);
            if (!moveAxisWithContact(0.0, stepDelta.y, 0.0) && state.moveMode == PlayerMoveMode::Ground)
            {
                blockedVertically = true;
                if (stepDelta.y < 0.0)
                {
                    state.grounded = true;
                }
                state.verticalVelocity = 0.0;
            }
            moveAxisWithContact(0.0, 0.0, stepDelta.z);
        }

        if (state.moveMode == PlayerMoveMode::Fly &&
            flyDescending &&
            collision.playerColliderIntersectsTerrain &&
            collision.playerColliderIntersectsTerrain({state.position.x, state.position.y - 0.03, state.position.z}, 1.0))
        {
            state.moveMode = PlayerMoveMode::Ground;
            state.verticalVelocity = 0.0;
            state.grounded = true;
            input.jumpHeld = false;
            input.jumpPressed = false;
        }

        if (state.moveMode == PlayerMoveMode::Ground && !blockedVertically && !state.grounded)
        {
            state.verticalVelocity -= config.gravity * fixedDeltaSeconds;
        }

        const double movedX = state.position.x - startPosition.x;
        const double movedZ = state.position.z - startPosition.z;
        const double horizontalDistance = std::sqrt(movedX * movedX + movedZ * movedZ);
        const double yawBlend = 1.0 - std::exp(-BodyYawFollowResponse * fixedDeltaSeconds);
        const float targetBodyYaw = input.yaw - static_cast<float>(std::clamp(strafeIntent, -1, 1)) * StrafeBodyYawOffset;
        state.bodyYaw = lerpAngle(state.bodyYaw, targetBodyYaw, yawBlend);
        const double referenceSpeed = state.moveMode == PlayerMoveMode::Fly ? config.flyMoveSpeed : config.groundMoveSpeed;
        const double targetWalkAmount = input.allowInput && referenceSpeed > 0.0
            ? std::clamp(horizontalDistance / (referenceSpeed * fixedDeltaSeconds), 0.0, MaxWalkAmount)
            : 0.0;
        const double response = targetWalkAmount > static_cast<double>(state.walkAmount) ? WalkAmountRiseResponse : WalkAmountFallResponse;
        const double blend = 1.0 - std::exp(-response * fixedDeltaSeconds);
        state.walkAmount = static_cast<float>(std::clamp(
            static_cast<double>(state.walkAmount) + (targetWalkAmount - static_cast<double>(state.walkAmount)) * blend,
            0.0,
            MaxWalkAmount));
        if (state.walkAmount > 0.001f)
        {
            const double phaseSpeed = WalkCycleRadiansPerSecond * (0.35 + 0.65 * static_cast<double>(state.walkAmount));
            state.walkPhase = static_cast<float>(std::fmod(static_cast<double>(state.walkPhase) + phaseSpeed * fixedDeltaSeconds, static_cast<double>(TwoPi)));
        }

        return PlayerMovementResult{input, state};
    }
}
