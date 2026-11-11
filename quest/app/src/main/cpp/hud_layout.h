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
    kHeadFault = 37,       // imago_head_track_fault
    kHeadInverted = 38,    // bit 0 pitch, bit 1 yaw: the rate sign the loop settled on
    kTriggerDown = 39,     // the right trigger is held
    kClickCount = 40,      // right trigger presses with the pointer on the interface, counted
    kClickX = 41,          // where the last one landed (picture coordinates, may be outside 0..1)
    kClickY = 42,
    kIrPalette = 43,
    kIrCameraType = 44,
    kHeadGain = 45,        // deg/s per degree in use
    kStorageValid = 46,
    kStorageFreeMib = 47,
    kCameraMode = 48,
    kGimbalProtocol = 49,   // imago_gimbal_protocol in use
    kHeadModeSetting = 50,  // imago_head_track_mode HEAD switches on
    kControlSystem = 51,    // who holds the gimbal manager
    kControlComponent = 52,
    kRoomDim = 53,            // how dark the room is around the screen, 0..1
    kPassthroughActive = 54,  // the room can be shown (the runtime's passthrough is running)
    kHeadScale = 55,          // degrees of gimbal per degree of head right now
    kHeadZoomComp = 56,       // head tracking's zoom compensation, 0..1
    kFollowHead = 57,         // the screen goes with the head while head tracking is on
    kClutch = 58,             // head tracking moves the camera only while the left grip is held
    kVignette = 59,           // the picture's edge darkens while the head turns fast
    kHeadClutched = 60,       // the clutch is holding the gimbal now
    kDetections = 61,
};

// The interface is larger than the picture: rails at both sides, a bar above and one below.
// Fractions of the picture's width (sides) and height (top, bottom); Kotlin lays out the
// overlay with the same figures, and pointer coordinates are in picture units, so a point
// on a rail has x < 0 or x > 1.
constexpr float kMarginSide = 0.16f;
constexpr float kMarginTop = 0.10f;
constexpr float kMarginBottom = 0.14f;

// Commands from the interface (Kotlin) to the operator, run on the XR thread.
enum Command : int {
    kCmdTrackAt = 1,        // a, b: picture point
    kCmdSetView = 2,        // a: imago_view_source
    kCmdPhoto = 3,
    kCmdRecordToggle = 4,
    kCmdZoom = 5,           // a: -1, 0, 1
    kCmdAutofocus = 6,
    kCmdHeadToggle = 7,
    kCmdHeadRecentre = 8,
    kCmdGimbalMode = 9,     // a: imago_gimbal_mode
    kCmdTrackStop = 10,
    kCmdTrackerFunction = 11, // a: imago_tracker_function
    kCmdPalette = 12,       // a: index
    kCmdFfc = 13,
    kCmdConnect = 14,       // a: profile index
    kCmdDisconnect = 15,
    kCmdHeadGain = 16,      // a: deg/s per degree
    kCmdPlacePanel = 17,
    kCmdGimbalProtocol = 18, // a: imago_gimbal_protocol
    kCmdTakeControl = 19,    // a: 1 take, 0 release the gimbal manager
    kCmdFollowHead = 22,     // a: 1 the screen goes with the head while head tracking is on
    kCmdHeadZoomComp = 23,   // a: 0..1
    kCmdClutch = 24,         // a: 1 on
    kCmdVignette = 25,       // a: 1 on
    kCmdRoomDim = 21,        // a: 0 the room as it is .. 1 black
    kCmdHeadMode = 20,       // a: imago_head_track_mode HEAD switches on (2 angle, 1 rate)
};
constexpr int kDetectionStride = 6; // x, y, w, h, class, confidence
constexpr int kMaxDetections = 20;
constexpr int kSize = kDetections + kMaxDetections * kDetectionStride;

} // namespace imago::quest::hud
