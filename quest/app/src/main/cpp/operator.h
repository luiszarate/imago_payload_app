#pragma once

#include "video_decoder.h"

#include <imago/imago_core.h>

#include <jni.h>
#include <openxr/openxr.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace imago::quest {

/// One connection profile, as Kotlin's ProfileArgs flattened it.
struct Profile {
    std::string name;
    bool listen = false;
    std::string payloadIp;
    int mavlinkPort = 14566;
    int localPort = 0;
    std::string videoUrl;
    int transport = 0;
    int gimbalProtocol = 0;
    int maxVideoLagMs = 500;
    int videoSmoothingMs = 0;
    int headMode = IMAGO_HEAD_TRACK_RATE;
    bool headOnAtStart = false;
    imago_head_track_config head{};
    float maxYawDps = 60, maxPitchDps = 60, deadzone = 0.08f, expo = 0.55f;
    float zoomCompensation = 0.5f, trimFactor = 0.25f;
    float panelDistance = 2.2f, panelWidth = 2.6f, panelPitchDown = 8;
    bool passthrough = true;
    float roomDim = 0.35f; // how much the room is darkened around the screen, 0..1
    bool followHead = true; // the screen goes with the head while head tracking is on
    bool fineAim = true;    // slow stick moves become angles in the core (the Orus L's rate floor)
    // Comfort with head tracking on.
    bool headClutch = false;   // the camera follows the head only while the left grip is held
    bool vignette = true;      // the picture's edge darkens while the head turns fast
    bool stabilize = true;     // the screen sits where the gimbal points, not where the head does
    int stabilizeDelayMs = 150; // how old the picture on screen is against the gimbal's reports
    bool keepWifiAwake = true;
};

/// The controllers, once per headset frame. Clicks are levels; the operator finds the edges.
struct Input {
    bool focused = false; // actions are live (the app has the headset's focus)
    XrVector2f rightStick{0, 0};
    XrVector2f leftStick{0, 0};
    bool a = false, b = false, x = false, y = false, menu = false;
    bool leftTrigger = false, rightTrigger = false, leftGrip = false, rightGrip = false;
    bool leftStickClick = false, rightStickClick = false;
};

struct Pointer {
    bool valid = false;   // the right controller points at the interface (picture or rails)
    bool onVideo = false; // ... at the picture itself
    float x = 0, y = 0;   // picture coordinates, (0, 0) top-left; outside 0..1 on the rails
};

/// Ties the headset to the payload: owns the core session and the decoder, turns controller
/// input into commands, feeds the head pose to the core's head tracking, and makes the HUD
/// snapshot. Lives as long as the native app; the session comes and goes with the profile.
class Operator {
  public:
    /// activity: global reference; it gets onVideoSize(II)V and onNotice([B)V.
    Operator(jobject activity, std::vector<Profile> profiles);
    ~Operator();

    Operator(const Operator&) = delete;
    Operator& operator=(const Operator&) = delete;

    /// Where each session's MAVLink is recorded (.tlog), for looking at a test afterwards.
    void setLogDir(std::string dir) { logDir_ = std::move(dir); }
    /// The decoder's output: a native window over the video SurfaceTexture.
    void setVideoWindow(ANativeWindow* window);
    void connect(size_t profileIndex);
    void disconnect();
    const Profile& profile() const { return profiles_[profileIndex_]; }
    const Profile& profileAt(size_t index) const { return profiles_[index % profiles_.size()]; }
    size_t profileIndex() const { return profileIndex_; }

    /// XR thread, once per frame, with the actions already synced.
    void tick(const Input& input, const Pointer& pointer, int64_t nowNs);
    /// XR thread, once per frame: the headset's orientation in the panel's reference space.
    void headPose(const XrQuaternionf& orientation, int64_t timeNs);
    /// The right stick was clicked: the XR app puts the screen in front of the operator.
    bool takePlacePanelRequest() { return placePanel_.exchange(false); }
    /// A sheet (Connections) is open over the picture: its layer comes forward.
    void setSheetOpen(bool open) { sheetOpen_ = open; }
    bool sheetOpen() const { return sheetOpen_.load(); }
    /// The runtime's passthrough is running: the room can be shown and dimmed.
    void setPassthroughActive(bool active) { passthroughActive_ = active; }
    /// The screen should go with the head now: head tracking is on and the profile says so.
    bool screenFollowsHead() const { return headOn_ && !headHeld_ && profile().followHead; }
    /// How far the picture on screen is from where the head points now, in degrees of head
    /// (right and up positive): the gimbal still on its way. Zero when it has arrived.
    void pictureLag(float& yawDeg, float& pitchDeg) const {
        yawDeg = lagYaw_;
        pitchDeg = lagPitch_;
    }
    /// Head tracking is moving the camera now and the profile wants the comfort vignette.
    bool vignetteWanted() const { return headFollowing_ && profile().vignette; }
    /// A short buzz asked for the left (bit 0) and right (bit 1) controller since the last call.
    int takeHapticRequests() { return haptics_.exchange(0); }

    /// Fills hud::kSize floats (hud_layout.h). Any thread.
    void hudSnapshot(float* out) const;
    /// A command from the interface (hud::Command), from any thread; runs at the next tick.
    void post(int command, float a, float b);
    void setVideoSize(int width, int height);
    float videoAspect() const { return videoAspect_.load(); }

  private:
    static void onEvent(const imago_event* event, void* user);
    static void onVideoFrame(const imago_video_frame* frame, void* user);
    void handleEvent(const imago_event& event);
    void notice(const std::string& text);
    void label(uint32_t requestId, const char* what);
    void keepGimbalControl(const imago_state& state, int64_t nowNs);
    float shape(float value) const;
    void setHeadTracking(bool on);
    void run(int command, float a, float b, const imago_state& state);
    void applyZoom();

    jobject activity_;
    jmethodID onVideoSize_ = nullptr;
    jmethodID onNotice_ = nullptr;
    std::vector<Profile> profiles_;
    std::string logDir_;
    size_t profileIndex_ = 0;

    std::unique_ptr<android::VideoDecoder> decoder_;
    imago_session* session_ = nullptr; // XR thread creates and destroys it
    std::atomic<bool> connected_{false};
    std::atomic<bool> linkUp_{false};

    Input last_;
    int zoomDirection_ = 0; // what the camera was last told
    int stickZoom_ = 0;
    int uiZoom_ = 0;
    struct Posted {
        int command;
        float a, b;
    };
    std::mutex postedMutex_;
    std::vector<Posted> posted_;
    bool headOn_ = false;
    bool headStarted_ = false; // headOnAtStart applied for this session
    bool headHeld_ = false;    // the clutch holds the gimbal (left grip not pressed)
    bool headFollowing_ = false;
    // Where the gimbal was, by time, to place the picture where it was taken from.
    struct Attitude {
        int64_t ns = 0;
        float yaw = 0, pitch = 0;
    };
    std::array<Attitude, 64> attitudes_{};
    size_t attitudeNext_ = 0;
    float lagYaw_ = 0, lagPitch_ = 0;
    float lagBiasYaw_ = 0, lagBiasPitch_ = 0; // the steady part of the gimbal's error
    int64_t lastTickNs_ = 0;
    void updatePictureLag(const imago_state& state, int64_t nowNs);
    int32_t lastHeadFault_ = 0; // to notice a fault once
    std::atomic<bool> placePanel_{false};
    std::atomic<int> haptics_{0};
    std::atomic<bool> sheetOpen_{false};
    std::atomic<bool> passthroughActive_{false};
    // Gimbal manager: when control is nobody's and we did not give it up, take it again.
    bool controlReleasedByUs_ = false;
    int64_t nextControlNs_ = 0;

    mutable std::mutex labelsMutex_;
    std::unordered_map<uint32_t, std::string> labels_; // request id -> what it was

    std::atomic<float> videoAspect_{16.0f / 9.0f};
    mutable std::mutex stateMutex_;
    imago_state state_{}; // the latest snapshot, for the HUD thread
    // What else the HUD shows, written by the XR thread under stateMutex_.
    struct HudExtra {
        Pointer pointer;
        bool triggerDown = false;
        uint32_t clicks = 0;
        float clickX = 0, clickY = 0;
        size_t profileIndex = 0;
        float headGain = 0.6f;
        int gimbalProtocol = 0;
        float roomDim = 0.35f;
        float headZoomComp = 0;
        bool followHead = true;
        bool clutch = false;
        bool vignette = true;
        bool held = false;
        int headMode = IMAGO_HEAD_TRACK_ANGLE;
        int maxVideoLagMs = 500;
    } extra_;
};

} // namespace imago::quest
