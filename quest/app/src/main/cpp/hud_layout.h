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
    kGrab = 61,               // the screen is being moved (1) or resized (2) with the pointer
    // The camera against the aircraft, for the attitude dial (imago_state's gimbal fields).
    kYawValid = 62,           // kYaw is the camera's direction from the nose
    kYawNorthValid = 63,
    kYawNorth = 64,           // 0..360 from North
    kPanValid = 65,
    kPan = 66,                // unwrapped, to the mechanical stops
    kHeadingValid = 67,
    kHeading = 68,            // the aircraft's, 0..360 from North
    kHfov = 69,               // the EO camera's horizontal field of view, 0 unknown
    // The aircraft's telemetry (imago_state.vehicle_*), when the autopilot shares the link.
    kVehicleAltitudeValid = 70,
    kVehicleAltitude = 71,    // metres above home
    kVehicleSpeedValid = 72,
    kVehicleGroundspeed = 73, // m/s
    kVehicleAirspeed = 74,    // m/s, 0 without the sensor
    kVehicleBatteryValid = 75,
    kVehicleBatteryV = 76,
    kVehicleBatteryPercent = 77, // -1 unknown
    kPictureOnly = 78,        // the interface is hidden: picture, attitude dial and warnings only
    kLogRecording = 79,       // the session's MAVLink is being recorded to a .tlog
    kLogSeconds = 80,         // for how long
    kLogDetailed = 81,        // every message (1) or the compact log (0)
    // The right stick as a gimbal joystick (the Controls sheet's sliders), and the AI switch.
    kCtlMaxSpeed = 82,        // deg/s at full stick, 1x
    kCtlZoomComp = 83,        // 0..1: how much slower with zoom
    kCtlExpo = 84,            // 0 linear .. 1 fine around the centre
    kCtlFineAim = 85,         // deg/s under which the stick aims by angles; 0 off
    kAiOn = 86,               // AI detection was asked for (the payload gives no state back)
    // Where the aircraft is, for the map. Degrees x 1e7 split in two so each float is exact:
    // value = hi * 65536 + lo.
    kVehiclePositionValid = 87,
    kVehicleLatHi = 88,
    kVehicleLatLo = 89,
    kVehicleLonHi = 90,
    kVehicleLonLo = 91,
    // The point the camera looks at (the payload's TARGET), metres east and north of the aircraft.
    kTargetValid = 92,
    kTargetEastM = 93,
    kTargetNorthM = 94,
    kControllerLabels = 95,   // the cards over the controllers are on
    kDetections = 96,
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
    kCmdGrab = 26,           // a: 1 move the screen with the pointer, 2 resize it, 0 let go
    kCmdPictureOnly = 27,    // a: 1 hide the interface (picture, dial and warnings stay), 0 show it
    kCmdLog = 28,            // a: 1 record the MAVLink to a new .tlog, 0 stop
    kCmdLogDetail = 29,      // a: 1 every message, 0 compact
    kCmdControl = 30,        // a: 0 max speed, 1 zoom compensation, 2 expo, 3 fine aim floor, 4 controller labels; b: value
    kCmdRoomDim = 21,        // a: 0 the room as it is .. 1 black
    kCmdHeadMode = 20,       // a: imago_head_track_mode HEAD switches on (2 angle, 1 rate)
};
constexpr int kDetectionStride = 6; // x, y, w, h, class, confidence
constexpr int kMaxDetections = 20;
constexpr int kSize = kDetections + kMaxDetections * kDetectionStride;

} // namespace imago::quest::hud
