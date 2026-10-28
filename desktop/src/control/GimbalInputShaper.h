#pragma once

#include <QSettings>
#include <QString>
#include <QVariantMap>

/// Turns a stick deflection into gimbal rates. Shared by the on-screen joystick and gamepads so
/// both feel the same. Every field is a user setting (Settings › Gimbal), saved across launches.
struct GimbalInputShaper {
    /// Rate at full deflection with no zoom, degrees per second, for pan (yaw) and tilt (pitch).
    float maxYawDps = 60.0f;
    float maxPitchDps = 60.0f;
    /// Ignore deflections below this, so a resting thumb does not drift the gimbal.
    float deadzone = 0.08f;
    /// 0 = linear, 1 = fully cubic. Higher values give finer control near the centre.
    float expo = 0.55f;
    /// Rate of a D-pad or button: always fully "pressed", so it gets its own, usually lower, speed.
    float digitalYawDps = 20.0f;
    float digitalPitchDps = 20.0f;
    /// How much the rate shrinks as the camera zooms in: rate × zoom^-compensation. 0 keeps the
    /// same °/s at any zoom, 1 keeps the picture moving at the same pace on screen, 0.5 (√zoom)
    /// is in between. Operators found 0.5 too slow at full zoom to follow a target (2026-10-28).
    float zoomCompensation = 0.5f;

    struct Rates {
        float pitch = 0.0f;
        float yaw = 0.0f;
        bool operator==(const Rates&) const = default;
    };

    /// x: right positive, y: up positive, both in -1..1. zoom: current EO zoom (x).
    /// digital: the input is a D-pad or button (on or off) rather than a stick.
    Rates rates(float x, float y, float zoom, bool digitalX = false, bool digitalY = false) const;

    float shape(float value) const;

    static constexpr float kMinSpeed = 2.0f;
    static constexpr float kMaxSpeed = 120.0f;
    static constexpr float kMaxDeadzone = 0.3f;

    /// Field names as in the mobile apps' JSON.
    QVariantMap toVariant() const;
    /// Changes one field by name; values are clamped to their ranges. false if unknown.
    bool set(const QString& field, double value);

    void save(QSettings& settings) const;
    static GimbalInputShaper load(const QSettings& settings);

    bool operator==(const GimbalInputShaper&) const = default;
};
