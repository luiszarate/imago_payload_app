#pragma once

#include "video_decoder.h"

#include <imago/imago_core.h>

#include <jni.h>
#include <openxr/openxr.h>

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
    bool valid = false; // the right controller points at the virtual screen
    float x = 0, y = 0; // normalized picture coordinates, (0, 0) top-left
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

    /// The decoder's output: a native window over the video SurfaceTexture.
    void setVideoWindow(ANativeWindow* window);
    void connect(size_t profileIndex);
    void disconnect();
    const Profile& profile() const { return profiles_[profileIndex_]; }
    size_t profileIndex() const { return profileIndex_; }

    /// XR thread, once per frame, with the actions already synced.
    void tick(const Input& input, const Pointer& pointer, int64_t nowNs);
    /// XR thread, once per frame: the headset's orientation in the panel's reference space.
    void headPose(const XrQuaternionf& orientation, int64_t timeNs);
    /// The right stick was clicked: the XR app puts the screen in front of the operator.
    bool takePlacePanelRequest() { return placePanel_.exchange(false); }
    /// A short buzz asked for the left (bit 0) and right (bit 1) controller since the last call.
    int takeHapticRequests() { return haptics_.exchange(0); }

    /// Fills hud::kSize floats (hud_layout.h). Any thread.
    void hudSnapshot(float* out) const;
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

    jobject activity_;
    jmethodID onVideoSize_ = nullptr;
    jmethodID onNotice_ = nullptr;
    std::vector<Profile> profiles_;
    size_t profileIndex_ = 0;

    std::unique_ptr<android::VideoDecoder> decoder_;
    imago_session* session_ = nullptr; // XR thread creates and destroys it
    std::atomic<bool> connected_{false};
    std::atomic<bool> linkUp_{false};

    Input last_;
    Pointer pointer_;
    int zoomDirection_ = 0;
    bool headOn_ = false;
    bool headStarted_ = false; // headOnAtStart applied for this session
    int32_t lastHeadFault_ = 0; // to notice a fault once
    std::atomic<bool> placePanel_{false};
    std::atomic<int> haptics_{0};
    // Gimbal manager: when control is nobody's and we did not give it up, take it again.
    bool controlReleasedByUs_ = false;
    int64_t nextControlNs_ = 0;

    mutable std::mutex labelsMutex_;
    std::unordered_map<uint32_t, std::string> labels_; // request id -> what it was

    std::atomic<float> videoAspect_{16.0f / 9.0f};
    mutable std::mutex stateMutex_;
    imago_state state_{}; // the latest snapshot, for the HUD thread
};

} // namespace imago::quest
