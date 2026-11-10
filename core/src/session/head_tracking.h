#pragma once

#include "imago/imago_core.h"

#include <cstdint>
#include <mutex>

namespace imago {

// Aims the gimbal where the operator's head points (imago_head_track_* in imago_core.h).
//
// Pure: it never talks to the network. Any thread feeds poses, switches and trims; the
// session's network thread calls update() every tick with the gimbal's reported attitude and
// gets back what to send, so the protocol stays in Session. Every method locks, briefly.
class HeadTracking {
  public:
    struct Gimbal {
        bool valid = false;    // attitude seen (pitch)
        float pitchDeg = 0;    // to the horizon
        bool yawValid = false; // yaw relative to the nose known
        float yawDeg = 0;      // -180..180, positive to the right
        // What the gimbal adds to the yaw of an angle setpoint (yaw_absolute - yaw from
        // MOUNT_ORIENTATION): the Orus L takes it in its own absolute frame (2026-11-09).
        bool yawOffsetValid = false;
        float yawOffsetDeg = 0;
        // Angle setpoints can be sent (not with DO_MOUNT_CONTROL, which the Orus L refuses).
        bool anglesSupported = true;
    };

    struct Command {
        enum class Kind { None, Stop, Rate, Angle } kind = Kind::None;
        float pitch = 0; // deg/s for Rate, deg for Angle
        float yaw = 0;
    };

    struct Status {
        int32_t mode = IMAGO_HEAD_TRACK_OFF;
        bool poseFresh = false;
        bool following = false;
        bool heldByTracker = false;
        float targetPitchDeg = 0;
        float targetYawDeg = 0;
        float errorDeg = 0;
        int32_t fault = IMAGO_HEAD_TRACK_FAULT_NONE;
        bool pitchInverted = false;
        bool yawInverted = false;
    };

    HeadTracking();

    // nullptr restores the defaults. Takes effect at the next update.
    void configure(const imago_head_track_config* cfg);
    // Returns Stop when the gimbal was being driven and must be stopped now (switching off or
    // changing mode), so the caller can send it from its own thread at once.
    Command setMode(int32_t mode);
    int32_t mode() const;
    void recentre();
    void setPose(float yawDeg, float pitchDeg, int64_t timeNs);
    // A joystick while on: shifts the head-to-gimbal offset at this rate.
    void setTrim(float pitchDps, float yawDps);
    // One control tick. trackerActive: the payload's tracker holds a target. gimbalPeriodNs
    // paces the ANGLE mode's setpoints.
    Command update(int64_t nowNs, const Gimbal& gimbal, bool trackerActive, int64_t gimbalPeriodNs);
    Status status(int64_t nowNs) const;

    // Yaw (right positive) and pitch (up positive) of the look direction of an OpenXR
    // orientation (+X right, +Y up, -Z forward), degrees.
    static void anglesFromQuaternion(float w, float x, float y, float z, float& yawDeg,
                                     float& pitchDeg);
    static float wrap180(float deg);

  private:
    // One axis of the RATE loop: the sign the rate is sent with, learnt from the attitude
    // the gimbal reports under a steady command, and the running command burst that the
    // learning and the fault guards watch.
    struct Axis {
        float sign = 1;
        bool learned = false;
        int direction = 0; // of the rate asked, before the sign: -1, 0, 1
        float startDeg = 0; // attitude when this direction started
        int64_t startNs = 0;
    };
    bool poseFreshLocked(int64_t nowNs) const;
    Command stopLocked();
    void resetLoopLocked();
    // Learns the axis sign and checks it for motion; true if a fault was raised.
    bool watchAxis(Axis& axis, float rateAsked, float measuredDeg, bool wrap, int64_t nowNs);

    mutable std::mutex mu_;
    imago_head_track_config cfg_{}; // resolved: no zeros left
    int32_t mode_ = IMAGO_HEAD_TRACK_OFF;

    bool havePose_ = false;
    float poseYaw_ = 0, posePitch_ = 0;
    int64_t poseNs_ = 0;

    // target = pose + offset (gimbal frame). Captured when switching on, moved by recentre
    // and the trim.
    bool offsetKnown_ = false;
    float offsetYaw_ = 0, offsetPitch_ = 0;
    bool recentrePending_ = false;
    float trimPitchDps_ = 0, trimYawDps_ = 0;

    // Smoothed target.
    bool filterInit_ = false;
    float filteredYaw_ = 0, filteredPitch_ = 0;
    int64_t lastUpdateNs_ = 0;

    bool commanding_ = false; // a rate other than zero, or angles, are being streamed
    // ANGLE mode: the setpoint (yaw to the nose) that trails the smoothed head, and what was
    // last sent.
    bool angleInit_ = false;
    float anglePitch_ = 0, angleYaw_ = 0;
    float angleYawOffset_ = 0; // the latest reported, kept while reports lack it
    int64_t lastAngleNs_ = 0;
    float lastAnglePitch_ = 0, lastAngleYaw_ = 0;
    int64_t angleBehindNs_ = 0; // since when the gimbal has been far from the setpoint

    Axis pitchAxis_;
    Axis yawAxis_;
    int32_t fault_ = IMAGO_HEAD_TRACK_FAULT_NONE;

    // For status().
    bool following_ = false;
    bool heldByTracker_ = false;
    float errorDeg_ = 0;
};

} // namespace imago
