#pragma once

// The little vector algebra the renderer and the pointer need, over OpenXR's own types
// (right-handed, +X right, +Y up, -Z forward; column-major 4x4 matrices as GL wants them).

#include <openxr/openxr.h>

#include <cmath>

namespace imago::quest {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;

inline XrVector3f operator+(XrVector3f a, XrVector3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline XrVector3f operator-(XrVector3f a, XrVector3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline XrVector3f operator*(XrVector3f a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(XrVector3f a, XrVector3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline XrVector3f cross(XrVector3f a, XrVector3f b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline XrVector3f normalized(XrVector3f v) {
    const float n = std::sqrt(dot(v, v));
    return n > 1e-6f ? v * (1.0f / n) : XrVector3f{0, 0, -1};
}

/// q * v * q^-1 for a unit quaternion.
inline XrVector3f rotate(const XrQuaternionf& q, XrVector3f v) {
    const XrVector3f u{q.x, q.y, q.z};
    const XrVector3f t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

inline XrQuaternionf multiply(const XrQuaternionf& a, const XrQuaternionf& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline XrQuaternionf conjugate(const XrQuaternionf& q) { return {-q.x, -q.y, -q.z, q.w}; }

/// Rotation of `deg` about the +Y axis (positive turns to the left, as OpenXR has it).
inline XrQuaternionf yawRotation(float deg) {
    const float h = deg * kDegToRad / 2;
    return {0, std::sin(h), 0, std::cos(h)};
}

/// Rotation of `deg` about the +X axis (positive tilts up).
inline XrQuaternionf pitchRotation(float deg) {
    const float h = deg * kDegToRad / 2;
    return {std::sin(h), 0, 0, std::cos(h)};
}

struct Mat4 {
    float m[16]; // column-major

    static Mat4 identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
        return r;
    }

    static Mat4 fromPose(const XrPosef& p) {
        const XrQuaternionf& q = p.orientation;
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        Mat4 r{};
        r.m[0] = 1 - 2 * (yy + zz);
        r.m[1] = 2 * (xy + wz);
        r.m[2] = 2 * (xz - wy);
        r.m[4] = 2 * (xy - wz);
        r.m[5] = 1 - 2 * (xx + zz);
        r.m[6] = 2 * (yz + wx);
        r.m[8] = 2 * (xz + wy);
        r.m[9] = 2 * (yz - wx);
        r.m[10] = 1 - 2 * (xx + yy);
        r.m[12] = p.position.x;
        r.m[13] = p.position.y;
        r.m[14] = p.position.z;
        r.m[15] = 1;
        return r;
    }

    /// The view matrix of a camera at this pose (its inverse, for a rigid transform).
    static Mat4 viewFromPose(const XrPosef& p) {
        XrPosef inv;
        inv.orientation = conjugate(p.orientation);
        inv.position = rotate(inv.orientation, p.position) * -1.0f;
        return fromPose(inv);
    }

    /// Projection from an OpenXR field of view (tangent angles), GL clip space.
    static Mat4 projection(const XrFovf& fov, float nearZ, float farZ) {
        const float l = std::tan(fov.angleLeft), r = std::tan(fov.angleRight);
        const float d = std::tan(fov.angleDown), u = std::tan(fov.angleUp);
        Mat4 p{};
        p.m[0] = 2 / (r - l);
        p.m[5] = 2 / (u - d);
        p.m[8] = (r + l) / (r - l);
        p.m[9] = (u + d) / (u - d);
        p.m[10] = -(farZ + nearZ) / (farZ - nearZ);
        p.m[11] = -1;
        p.m[14] = -(2 * farZ * nearZ) / (farZ - nearZ);
        return p;
    }

    static Mat4 scale(float x, float y, float z) {
        Mat4 s = identity();
        s.m[0] = x;
        s.m[5] = y;
        s.m[10] = z;
        return s;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r{};
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0;
                for (int k = 0; k < 4; ++k) {
                    sum += m[k * 4 + row] * o.m[c * 4 + k];
                }
                r.m[c * 4 + row] = sum;
            }
        }
        return r;
    }
};

} // namespace imago::quest
