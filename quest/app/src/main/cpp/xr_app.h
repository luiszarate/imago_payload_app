#pragma once

#include "gl_renderer.h"
#include "layers.h"
#include "operator.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace imago::quest {

/// The OpenXR session on its own thread: instance, swapchains, controller actions and the
/// frame loop. Each frame it reads the controllers and the head pose, hands them to the
/// Operator, and draws the virtual screen for both eyes.
class XrApp {
  public:
    /// activity, videoTexture, overlayTexture: global references, released by the caller
    /// after stop().
    XrApp(JavaVM* vm, jobject activity, Operator& op, jobject videoTexture, jobject overlayTexture,
          size_t initialProfile);
    ~XrApp();

    XrApp(const XrApp&) = delete;
    XrApp& operator=(const XrApp&) = delete;

    void start();
    void stop();

  private:
    void run();
    bool createInstance();
    bool createSession();
    bool createSwapchains();
    bool createActions();
    void destroy();
    bool pollEvents();
    void frame();
    void readInput(XrTime time, Input& input);
    void placePanel(const XrPosef& head);
    void followHead(const XrPosef& head, float dtS);
    Pointer pointerOn(const XrPosef& aim, GlRenderer::Ray& ray) const;

    struct Swapchain {
        XrSwapchain handle = XR_NULL_HANDLE;
        int width = 0, height = 0;
        std::vector<XrSwapchainImageOpenGLESKHR> images;
    };

    JavaVM* vm_;
    jobject activity_;
    Operator& operator_;
    jobject videoTexture_;
    jobject overlayTexture_;
    size_t initialProfile_;

    std::thread thread_;
    std::atomic<bool> stopping_{false};

    GlRenderer renderer_;
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId system_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace localSpace_ = XR_NULL_HANDLE;
    XrSpace viewSpace_ = XR_NULL_HANDLE;
    XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
    bool sessionRunning_ = false;
    std::vector<XrViewConfigurationView> viewConfigs_;
    std::vector<Swapchain> swapchains_;
    std::vector<XrView> views_;
    int64_t colorFormat_ = 0;

    XrActionSet actionSet_ = XR_NULL_HANDLE;
    XrAction rightStick_ = XR_NULL_HANDLE, leftStick_ = XR_NULL_HANDLE;
    XrAction aButton_ = XR_NULL_HANDLE, bButton_ = XR_NULL_HANDLE, xButton_ = XR_NULL_HANDLE,
             yButton_ = XR_NULL_HANDLE, menuButton_ = XR_NULL_HANDLE;
    XrAction leftTrigger_ = XR_NULL_HANDLE, rightTrigger_ = XR_NULL_HANDLE;
    XrAction leftGrip_ = XR_NULL_HANDLE, rightGrip_ = XR_NULL_HANDLE;
    XrAction leftStickClick_ = XR_NULL_HANDLE, rightStickClick_ = XR_NULL_HANDLE;
    XrAction aimPose_ = XR_NULL_HANDLE;
    XrPath rightHand_ = XR_NULL_PATH;
    XrSpace aimSpace_ = XR_NULL_HANDLE;

    XrAction haptic_ = XR_NULL_HANDLE;
    XrPath leftHand_ = XR_NULL_PATH;

    // XR_FB_passthrough: the room around the screen, when the runtime has it and the profile
    // asks for it. Function pointers come from the instance.
    bool passthroughSupported_ = false;
    XrPassthroughFB passthrough_ = XR_NULL_HANDLE;
    XrPassthroughLayerFB passthroughLayer_ = XR_NULL_HANDLE;
    PFN_xrCreatePassthroughFB createPassthrough_ = nullptr;
    PFN_xrDestroyPassthroughFB destroyPassthrough_ = nullptr;
    PFN_xrCreatePassthroughLayerFB createPassthroughLayer_ = nullptr;
    PFN_xrDestroyPassthroughLayerFB destroyPassthroughLayer_ = nullptr;
    bool createPassthrough();
    void applyHaptics(int hands);

    Panel panel_;
    bool panelPlaced_ = false;
    // The screen going with the head (head tracking on): its direction from the head, eased.
    bool following_ = false;
    float followYaw_ = 0;   // degrees, right positive
    float followPitch_ = 0; // degrees, up positive
    // What is left of the glide from where the screen was when it started following.
    float glideYaw_ = 0, glidePitch_ = 0;
    // Head turn speed, for the comfort vignette.
    bool headAnglesKnown_ = false;
    float lastHeadYaw_ = 0, lastHeadPitch_ = 0;
    float vignette_ = 0;
    void updateVignette(const XrPosef& head, float dtS);
    // Moving or resizing the screen with the pointer (Operator::grab).
    void updateGrab(bool aimValid, const XrPosef& aim, const XrPosef& head, float stickY, float dtS);
    int grabbing_ = 0;
    XrVector3f grabLocal_{0, 0, 0}; // the point held, in the screen's own axes (metres)
    float grabDistance_ = 0;        // along the ray, from the controller
    float grabStartWidth_ = 0;
    float grabStartRadius_ = 0;
    XrTime lastFrameTime_ = 0;
    std::array<Layer, 5> layers_{};
};

} // namespace imago::quest
