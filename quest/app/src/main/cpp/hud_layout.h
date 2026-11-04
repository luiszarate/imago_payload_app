#pragma once

// Layout of the HUD snapshot handed to Kotlin (floats, booleans as 0/1): the contract with
// `Hud` in NativeBridge.kt. Change both together.
namespace imago::quest::hud {

enum Index : int {
    kLinkUp = 0,
    kConnected = 1,
    kProfileIndex = 2,
    kVideoActive = 3,
    kFps = 4,
    kBitrateKbps = 5,
    kLagMs = 6,
    kLagStale = 7,
    kGimbalValid = 8,
    kPitch = 9,
    kYaw = 10,
    kPanNearLimit = 11,
    kGimbalMode = 12,
    kZoom = 13,
    kViewSource = 14,
    kRecording = 15,
    kRecordingTimeMs = 16,
    kTrackingMode = 17,
    kTrackingState = 18,
    kBoxValid = 19,
    kBoxX = 20,
    kBoxY = 21,
    kBoxW = 22,
    kBoxH = 23,
    kHeadMode = 24,
    kHeadFollowing = 25,
    kHeadHeld = 26,
    kHeadPoseFresh = 27,
    kHeadError = 28,
    kPointerValid = 29,
    kPointerX = 30,
    kPointerY = 31,
    kLatencyPresentMs = 32,
    kLrfValid = 33,
    kLrfRangeM = 34,
    kGimbalControl = 35,
    kDetectionCount = 36,
    kDetections = 37,
};
constexpr int kDetectionStride = 6; // x, y, w, h, class, confidence
constexpr int kMaxDetections = 20;
constexpr int kSize = kDetections + kMaxDetections * kDetectionStride;

} // namespace imago::quest::hud
