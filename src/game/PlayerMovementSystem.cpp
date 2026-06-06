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
        constexpr double PlayerStandingHeight = 1.75;
        constexpr double PlayerStandingEyeHeight = 1.5625;
        constexpr double GroundStepUpHeight = 0.5;
        constexpr double ProneClimbHeight = 1.0;
        constexpr double ProneClimbStepUpSpeed = 2.0;
        constexpr double WaterClimbHeight = 1.0;
        constexpr double SwimIdleSinkSpeedScale = 0.25;
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
        const double horizontalIntentLength = std::sqrt(
            static_cast<double>(movement.x) * static_cast<double>(movement.x) +
            static_cast<double>(movement.z) * static_cast<double>(movement.z));

        const bool flyAccelerating = state.moveMode == PlayerMoveMode::Fly && input.ctrlHeld;
        const double proneHeightScale = std::clamp(config.proneHeight / PlayerStandingHeight, 0.1, 1.0);
        const bool waterContact = state.moveMode == PlayerMoveMode::Ground &&
            collision.playerColliderIntersectsWater &&
            collision.playerColliderIntersectsWater(state.position, state.playerHeightScale);
        if (waterContact || state.waterClimbActive)
        {
            input.doubleTapSprintActive = false;
            input.sneakToggled = false;
            input.proneToggled = false;
        }
        const bool proneIntent = state.moveMode == PlayerMoveMode::Ground &&
            !waterContact &&
            (input.toggleProne ? input.proneToggled : input.proneHeld);
        const bool sneakIntent = state.moveMode == PlayerMoveMode::Ground &&
            !waterContact &&
            !proneIntent &&
            (input.toggleSneak ? input.sneakToggled : input.shiftHeld);
        const double previousPlayerHeightScale = std::clamp(static_cast<double>(state.playerHeightScale), 0.1, 1.0);
        double playerHeightScale = proneIntent ? proneHeightScale : (sneakIntent ? config.sneakHeightScale : 1.0);
        playerHeightScale = std::clamp(playerHeightScale, 0.1, 1.0);
        if (playerHeightScale > previousPlayerHeightScale &&
            collision.playerColliderIntersectsTerrain &&
            collision.playerColliderIntersectsTerrain(state.position, playerHeightScale))
        {
            playerHeightScale = previousPlayerHeightScale;
            if (input.toggleProne && previousPlayerHeightScale <= proneHeightScale + 0.001)
            {
                input.proneToggled = true;
            }
            if (input.toggleSneak && previousPlayerHeightScale <= config.sneakHeightScale + 0.001)
            {
                input.sneakToggled = true;
            }
        }
        if (state.moveMode != PlayerMoveMode::Ground || waterContact || state.waterClimbActive)
        {
            playerHeightScale = 1.0;
        }
        state.playerHeightScale = static_cast<float>(playerHeightScale);

        const bool groundProne = state.moveMode == PlayerMoveMode::Ground && playerHeightScale <= proneHeightScale + 0.001;
        const bool groundSneaking = state.moveMode == PlayerMoveMode::Ground &&
            !groundProne &&
            playerHeightScale <= config.sneakHeightScale + 0.001;
        if (groundProne || groundSneaking)
        {
            input.doubleTapSprintActive = false;
        }
        const bool holdSprintActive = !input.toggleSprint && (input.ctrlHeld || input.doubleTapSprintActive);
        const bool toggleSprintActive = input.toggleSprint && input.sprintToggled;
        const bool groundSprinting = state.moveMode == PlayerMoveMode::Ground && !groundProne && !groundSneaking && (holdSprintActive || toggleSprintActive);
        const double targetEyeHeightScale = groundProne
            ? std::clamp(config.proneEyeHeight / PlayerStandingEyeHeight, 0.1, 1.0)
            : (groundSneaking ? config.sneakHeightScale : 1.0);
        const double eyeHeightBlend = 1.0 - std::exp(-EyeHeightResponse * fixedDeltaSeconds);
        state.eyeHeightScale = static_cast<float>(std::clamp(
            static_cast<double>(state.eyeHeightScale) + (targetEyeHeightScale - static_cast<double>(state.eyeHeightScale)) * eyeHeightBlend,
            0.1,
            1.0));
        const bool flyDescending = state.moveMode == PlayerMoveMode::Fly && input.shiftHeld;
        bool proneClimbHandledThisTick = false;
        bool waterClimbHandledThisTick = false;
        const auto colliderBlocked = [&](DVec3 position) -> bool
        {
            return collision.playerColliderIntersectsTerrain &&
                collision.playerColliderIntersectsTerrain(position, playerHeightScale);
        };
        const auto terrainClimbHeight = [&](DVec3 blockedPosition, double maxHeight) -> double
        {
            if (collision.playerColliderTerrainClimbHeight)
            {
                return std::clamp(
                    collision.playerColliderTerrainClimbHeight(blockedPosition, playerHeightScale, maxHeight),
                    0.0,
                    maxHeight);
            }
            return maxHeight;
        };
        if (state.proneClimbActive)
        {
            const double maxClimbHeight = state.proneClimbTarget.y > 0.0 ? state.proneClimbTarget.y : ProneClimbHeight;
            const bool keepClimbing = input.allowInput &&
                state.moveMode == PlayerMoveMode::Ground &&
                groundProne &&
                horizontalIntentLength > 0.001 &&
                state.proneClimbProgress < maxClimbHeight;
            if (keepClimbing)
            {
                const double lift = std::min(ProneClimbStepUpSpeed * fixedDeltaSeconds, maxClimbHeight - state.proneClimbProgress);
                DVec3 lifted = state.position;
                lifted.y += lift;
                if (!colliderBlocked(lifted))
                {
                    state.position = lifted;
                    state.proneClimbProgress += lift;
                    state.verticalVelocity = 0.0;
                    state.grounded = true;
                    proneClimbHandledThisTick = true;
                    if (state.proneClimbProgress >= maxClimbHeight)
                    {
                        state.proneClimbActive = false;
                        state.proneClimbProgress = 0.0;
                    }
                }
                else
                {
                    state.proneClimbActive = false;
                    state.proneClimbProgress = 0.0;
                }
            }
            else
            {
                state.proneClimbActive = false;
                state.proneClimbProgress = 0.0;
            }
        }

        if (state.waterClimbActive)
        {
            const double maxClimbHeight = state.waterClimbTarget.y > 0.0 ? state.waterClimbTarget.y : WaterClimbHeight;
            const bool keepClimbing = input.allowInput &&
                state.moveMode == PlayerMoveMode::Ground &&
                input.jumpHeld &&
                horizontalIntentLength > 0.001 &&
                state.waterClimbProgress < maxClimbHeight;
            if (keepClimbing)
            {
                const double horizontalDistance = config.groundMoveSpeed * config.swimSpeedScale * fixedDeltaSeconds;
                DVec3 horizontalNext = state.position;
                horizontalNext.x += (static_cast<double>(movement.x) / horizontalIntentLength) * horizontalDistance;
                horizontalNext.z += (static_cast<double>(movement.z) / horizontalIntentLength) * horizontalDistance;
                if (!colliderBlocked(horizontalNext))
                {
                    state.position = horizontalNext;
                    state.waterClimbActive = false;
                    state.waterClimbProgress = 0.0;
                    state.verticalVelocity = 0.0;
                    state.grounded = false;
                    waterClimbHandledThisTick = true;
                }
                else
                {
                    const double lift = std::min(ProneClimbStepUpSpeed * fixedDeltaSeconds, maxClimbHeight - state.waterClimbProgress);
                    DVec3 lifted = state.position;
                    lifted.y += lift;
                    if (!colliderBlocked(lifted))
                    {
                        DVec3 liftedNext = lifted;
                        liftedNext.x += (static_cast<double>(movement.x) / horizontalIntentLength) * horizontalDistance;
                        liftedNext.z += (static_cast<double>(movement.z) / horizontalIntentLength) * horizontalDistance;
                        state.position = colliderBlocked(liftedNext) ? lifted : liftedNext;
                        state.waterClimbProgress += lift;
                        state.verticalVelocity = 0.0;
                        state.grounded = false;
                        waterClimbHandledThisTick = true;
                        if (state.waterClimbProgress >= maxClimbHeight)
                        {
                            state.waterClimbActive = false;
                            state.waterClimbProgress = 0.0;
                        }
                    }
                    else
                    {
                        state.waterClimbActive = false;
                        state.waterClimbProgress = 0.0;
                    }
                }
            }
            else
            {
                state.waterClimbActive = false;
                state.waterClimbProgress = 0.0;
                state.verticalVelocity = 0.0;
                state.grounded = false;
            }
        }

        const bool swimming = state.moveMode == PlayerMoveMode::Ground && waterContact && !waterClimbHandledThisTick;
        double swimVerticalSpeedScale = 0.0;
        if (state.moveMode == PlayerMoveMode::Fly)
        {
            state.waterClimbActive = false;
            state.waterClimbProgress = 0.0;
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
        else if (swimming)
        {
            if (input.jumpHeld)
            {
                swimVerticalSpeedScale = 1.0;
            }
            else if (input.shiftHeld)
            {
                swimVerticalSpeedScale = -1.0;
            }
            else
            {
                swimVerticalSpeedScale = -SwimIdleSinkSpeedScale;
            }
            state.verticalVelocity = 0.0;
            state.grounded = false;
            input.jumpPressed = false;
        }
        else if (!proneClimbHandledThisTick && !waterClimbHandledThisTick)
        {
            movement.y = 0.0f;
            state.grounded = collision.playerColliderIntersectsTerrain &&
                collision.playerColliderIntersectsTerrain({state.position.x, state.position.y - 0.03, state.position.z}, playerHeightScale);
            if (state.grounded && state.verticalVelocity < 0.0)
            {
                state.verticalVelocity = 0.0;
            }
            if (!groundProne && state.grounded && state.verticalVelocity <= 0.0 && (input.jumpHeld || input.jumpPressed))
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

        if (swimming)
        {
            const double swimHorizontalLength = std::sqrt(
                static_cast<double>(movement.x) * static_cast<double>(movement.x) +
                static_cast<double>(movement.z) * static_cast<double>(movement.z));
            if (swimHorizontalLength > 0.000001)
            {
                movement.x = static_cast<float>(static_cast<double>(movement.x) / swimHorizontalLength);
                movement.z = static_cast<float>(static_cast<double>(movement.z) / swimHorizontalLength);
            }
            movement.y = 0.0f;
        }
        else
        {
            movement = normalize(movement);
        }
        if (waterClimbHandledThisTick)
        {
            movement = {};
        }
        double moveSpeed = state.moveMode == PlayerMoveMode::Fly ? config.flyMoveSpeed : config.groundMoveSpeed;
        if (flyAccelerating)
        {
            moveSpeed *= config.sprintSpeedScale;
        }
        else if (swimming || state.waterClimbActive || waterClimbHandledThisTick)
        {
            moveSpeed *= config.swimSpeedScale;
        }
        else if (groundProne || groundSneaking)
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
            swimming ? swimVerticalSpeedScale * distance : (state.moveMode == PlayerMoveMode::Fly ? static_cast<double>(movement.y) * distance : state.verticalVelocity * fixedDeltaSeconds),
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
            if (dy == 0.0 &&
                state.moveMode == PlayerMoveMode::Ground &&
                state.grounded &&
                !groundProne &&
                !swimming &&
                !state.waterClimbActive &&
                !waterClimbHandledThisTick &&
                input.allowInput &&
                horizontalIntentLength > 0.001 &&
                (dx != 0.0 || dz != 0.0))
            {
                DVec3 stepped = state.position;
                stepped.y += GroundStepUpHeight;
                DVec3 steppedNext = stepped;
                steppedNext.x += dx;
                steppedNext.z += dz;
                if (!moveBlocked(stepped, 0.0, GroundStepUpHeight, 0.0) &&
                    !moveBlocked(steppedNext, dx, 0.0, dz) &&
                    collision.playerColliderHasSupportBelow &&
                    collision.playerColliderHasSupportBelow(steppedNext))
                {
                    state.position = steppedNext;
                    state.verticalVelocity = 0.0;
                    state.grounded = true;
                    return true;
                }
            }
            if (dy == 0.0 &&
                groundProne &&
                !state.proneClimbActive &&
                state.moveMode == PlayerMoveMode::Ground &&
                state.grounded &&
                input.allowInput &&
                horizontalIntentLength > 0.001 &&
                (dx != 0.0 || dz != 0.0))
            {
                DVec3 blockedNext = state.position;
                blockedNext.x += dx;
                blockedNext.z += dz;
                const double climbHeight = terrainClimbHeight(blockedNext, ProneClimbHeight);
                if (climbHeight <= 0.000001)
                {
                    return false;
                }

                DVec3 climbTop = state.position;
                climbTop.y += climbHeight;
                DVec3 climbTopNext = climbTop;
                climbTopNext.x += dx;
                climbTopNext.z += dz;
                if (!moveBlocked(climbTop, 0.0, climbHeight, 0.0) &&
                    !moveBlocked(climbTopNext, dx, 0.0, dz))
                {
                    state.proneClimbActive = true;
                    state.proneClimbProgress = 0.0;
                    state.proneClimbStart = state.position;
                    state.proneClimbTarget = {0.0, climbHeight, 0.0};
                    const double appliedLift = std::min(climbHeight, ProneClimbStepUpSpeed * fixedDeltaSeconds);
                    DVec3 applied = state.position;
                    applied.y += appliedLift;
                    if (moveBlocked(applied, 0.0, appliedLift, 0.0))
                    {
                        state.proneClimbActive = false;
                        state.proneClimbProgress = 0.0;
                        return false;
                    }

                    DVec3 appliedNext = applied;
                    appliedNext.x += dx;
                    appliedNext.z += dz;
                    if (!moveBlocked(appliedNext, dx, 0.0, dz))
                    {
                        state.position = appliedNext;
                    }
                    else
                    {
                        state.position = applied;
                    }
                    state.proneClimbProgress = appliedLift;
                    if (state.proneClimbProgress >= climbHeight)
                    {
                        state.proneClimbActive = false;
                        state.proneClimbProgress = 0.0;
                    }
                    state.verticalVelocity = 0.0;
                    state.grounded = true;
                    proneClimbHandledThisTick = true;
                    return true;
                }
            }
            if (dy == 0.0 &&
                swimming &&
                input.jumpHeld &&
                input.allowInput &&
                horizontalIntentLength > 0.001 &&
                (dx != 0.0 || dz != 0.0))
            {
                DVec3 climbTop = state.position;
                climbTop.y += WaterClimbHeight;
                DVec3 climbTopNext = climbTop;
                climbTopNext.x += dx;
                climbTopNext.z += dz;
                if (!moveBlocked(climbTop, 0.0, WaterClimbHeight, 0.0) &&
                    !moveBlocked(climbTopNext, dx, 0.0, dz))
                {
                    state.waterClimbActive = true;
                    state.waterClimbProgress = 0.0;
                    state.waterClimbStart = state.position;
                    state.waterClimbTarget = {0.0, WaterClimbHeight, 0.0};
                    const double appliedLift = std::min(WaterClimbHeight, ProneClimbStepUpSpeed * fixedDeltaSeconds);
                    DVec3 applied = state.position;
                    applied.y += appliedLift;
                    if (moveBlocked(applied, 0.0, appliedLift, 0.0))
                    {
                        state.waterClimbActive = false;
                        state.waterClimbProgress = 0.0;
                        return false;
                    }

                    DVec3 appliedNext = applied;
                    appliedNext.x += dx;
                    appliedNext.z += dz;
                    if (!moveBlocked(appliedNext, dx, 0.0, dz))
                    {
                        state.position = appliedNext;
                    }
                    else
                    {
                        state.position = applied;
                    }
                    state.waterClimbProgress = appliedLift;
                    if (state.waterClimbProgress >= WaterClimbHeight)
                    {
                        state.waterClimbActive = false;
                        state.waterClimbProgress = 0.0;
                    }
                    state.verticalVelocity = 0.0;
                    state.grounded = false;
                    waterClimbHandledThisTick = true;
                    return true;
                }
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
            if (!moveAxisWithContact(0.0, stepDelta.y, 0.0) && state.moveMode == PlayerMoveMode::Ground && !swimming)
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

        if (state.moveMode == PlayerMoveMode::Ground &&
            !state.proneClimbActive &&
            !proneClimbHandledThisTick &&
            !state.waterClimbActive &&
            !waterClimbHandledThisTick &&
            !swimming &&
            !blockedVertically &&
            !state.grounded)
        {
            state.verticalVelocity -= config.gravity * fixedDeltaSeconds;
        }

        const double movedX = state.position.x - startPosition.x;
        const double movedZ = state.position.z - startPosition.z;
        const double horizontalDistance = std::sqrt(movedX * movedX + movedZ * movedZ);
        const double yawBlend = 1.0 - std::exp(-BodyYawFollowResponse * fixedDeltaSeconds);
        const float targetBodyYaw = input.yaw - static_cast<float>(std::clamp(strafeIntent, -1, 1)) * StrafeBodyYawOffset;
        state.bodyYaw = lerpAngle(state.bodyYaw, targetBodyYaw, yawBlend);
        const double referenceSpeed = state.moveMode == PlayerMoveMode::Fly ? config.flyMoveSpeed : (swimming ? config.groundMoveSpeed * config.swimSpeedScale : config.groundMoveSpeed);
        const double targetWalkAmount = input.allowInput && referenceSpeed > 0.0
            ? std::clamp(horizontalDistance / (referenceSpeed * fixedDeltaSeconds), 0.0, MaxWalkAmount)
            : 0.0;
        const double sprintFovBlend = 1.0 - std::exp(-SprintFovResponse * fixedDeltaSeconds);
        const double targetSprintFovAmount = groundSprinting && targetWalkAmount > 0.01 ? 1.0 : 0.0;
        state.sprintFovAmount = static_cast<float>(std::clamp(
            static_cast<double>(state.sprintFovAmount) + (targetSprintFovAmount - static_cast<double>(state.sprintFovAmount)) * sprintFovBlend,
            0.0,
            1.0));
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
