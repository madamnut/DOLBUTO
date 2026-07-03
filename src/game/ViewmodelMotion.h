#pragma once

#include "camera/Camera.h"

#include <cmath>

namespace dolbuto
{
    struct ViewmodelMotion
    {
        Vec3 shoulderOffset{};
        Vec3 shoulderPivot{};
        Vec3 shoulderRotation{};
        Vec3 localRotation{};
    };

    inline Vec3 rotateViewmodelX(Vec3 value, float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return {value.x, c * value.y - s * value.z, s * value.y + c * value.z};
    }

    inline Vec3 rotateViewmodelY(Vec3 value, float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return {c * value.x + s * value.z, value.y, -s * value.x + c * value.z};
    }

    inline Vec3 rotateViewmodelZ(Vec3 value, float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return {c * value.x - s * value.y, s * value.x + c * value.y, value.z};
    }

    inline Vec3 rotateViewmodelXzy(Vec3 value, Vec3 rotation)
    {
        value = rotateViewmodelX(value, rotation.x);
        value = rotateViewmodelZ(value, rotation.z);
        return rotateViewmodelY(value, rotation.y);
    }

    inline Vec3 applyViewmodelMotionToPosition(Vec3 basePosition, const ViewmodelMotion& motion)
    {
        const Vec3 relative{
            basePosition.x - motion.shoulderPivot.x,
            basePosition.y - motion.shoulderPivot.y,
            basePosition.z - motion.shoulderPivot.z};
        const Vec3 rotated = rotateViewmodelXzy(relative, motion.shoulderRotation);
        return {
            motion.shoulderPivot.x + motion.shoulderOffset.x + rotated.x,
            motion.shoulderPivot.y + motion.shoulderOffset.y + rotated.y,
            motion.shoulderPivot.z + motion.shoulderOffset.z + rotated.z};
    }

    inline Vec3 applyViewmodelMotionToRotation(Vec3 baseRotation, const ViewmodelMotion& motion)
    {
        return {
            baseRotation.x + motion.localRotation.x,
            baseRotation.y + motion.localRotation.y,
            baseRotation.z + motion.localRotation.z};
    }

    inline ViewmodelMotion mirroredViewmodelMotion(const ViewmodelMotion& motion)
    {
        ViewmodelMotion mirrored = motion;
        mirrored.shoulderPivot.x = -mirrored.shoulderPivot.x;
        mirrored.shoulderRotation.y = -mirrored.shoulderRotation.y;
        mirrored.shoulderRotation.z = -mirrored.shoulderRotation.z;
        mirrored.localRotation.y = -mirrored.localRotation.y;
        mirrored.localRotation.z = -mirrored.localRotation.z;
        return mirrored;
    }
}
