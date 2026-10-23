#pragma once
// Rotations as the game keeps them: quaternions (x, y, z, w) and row-major matrices, y up, Link facing +z at yaw 0.
#include "lib.hpp"
#include "Game/actor.hpp"
#include <cmath>

namespace Game {
    constexpr float kPi = 3.14159265f;

    struct Quat { float x, y, z, w; };

    inline Quat Mul(const Quat& a, const Quat& b) {
        return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }

    // Degrees about one axis (0 x, 1 y, 2 z)
    inline Quat AxisQuat(int axis, float degrees) {
        float half = degrees * kPi / 360.0f;
        Quat q{0.0f, 0.0f, 0.0f, cosf(half)};
        (axis == 0 ? q.x : axis == 1 ? q.y : q.z) = sinf(half);
        return q;
    }

    // Degrees about each axis (x pitch, y yaw, z roll), applied roll first.
    inline Quat EulerQuat(const Vec3& degrees) {
        return Mul(AxisQuat(1, degrees.y), Mul(AxisQuat(0, degrees.x), AxisQuat(2, degrees.z)));
    }

    // The rotation of a rotation matrix, m(row, column).
    template<typename M>
    Quat QuatFromMatrix(M m) {
        float trace = m(0, 0) + m(1, 1) + m(2, 2);
        if (trace > 0.0f) {
            float s = sqrtf(trace + 1.0f) * 2.0f;
            return {(m(2, 1) - m(1, 2)) / s, (m(0, 2) - m(2, 0)) / s, (m(1, 0) - m(0, 1)) / s, 0.25f * s};
        }
        if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
            float s = sqrtf(1.0f + m(0, 0) - m(1, 1) - m(2, 2)) * 2.0f;
            return {0.25f * s, (m(0, 1) + m(1, 0)) / s, (m(0, 2) + m(2, 0)) / s, (m(2, 1) - m(1, 2)) / s};
        }
        if (m(1, 1) > m(2, 2)) {
            float s = sqrtf(1.0f + m(1, 1) - m(0, 0) - m(2, 2)) * 2.0f;
            return {(m(0, 1) + m(1, 0)) / s, 0.25f * s, (m(1, 2) + m(2, 1)) / s, (m(0, 2) - m(2, 0)) / s};
        }
        float s = sqrtf(1.0f + m(2, 2) - m(0, 0) - m(1, 1)) * 2.0f;
        return {(m(0, 2) + m(2, 0)) / s, (m(1, 2) + m(2, 1)) / s, 0.25f * s, (m(1, 0) - m(0, 1)) / s};
    }

    // Where a rotation faces about the vertical axis: degrees in (-180, 180], 0 = +z (the bottom of the screen),
    // 90 = +x (right).
    inline float YawDegrees(const Vec4& q) {
        return std::atan2(2 * (q.x * q.z + q.t * q.y), 1 - 2 * (q.x * q.x + q.y * q.y)) * 180.0f / kPi;
    }
}
