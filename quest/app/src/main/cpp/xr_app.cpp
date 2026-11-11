#include "xr_app.h"

#include "hud_layout.h"
#include "jni_util.h"

#include <android/log.h>
#include <imago/imago_core.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

namespace imago::quest {

namespace {

constexpr const char* kTag = "imago-quest-xr";
constexpr float kRayLengthM = 6.0f;
// How quickly the screen catches up with the head while it follows it.
constexpr float kFollowTimeS = 0.10f;
// Eye buffers this much larger than recommended, each way.
constexpr float kSupersample = 1.25f;
// Moving and resizing the screen by hand: how fast the stick carries it along the ray, and
// the limits it is kept within.
constexpr float kGrabReachSpeed = 1.5f; // metres a second at full stick
constexpr float kPanelNearestM = 0.6f;
constexpr float kPanelFarthestM = 10.0f;
constexpr float kPanelNarrowestM = 0.6f;
constexpr float kPanelWidestM = 10.0f;
// Comfort vignette: from this head speed, full at that one, darkening the edge this much.
constexpr float kVignetteFromDps = 25.0f;
constexpr float kVignetteFullDps = 110.0f;
constexpr float kVignetteMax = 0.85f;
// Analog triggers and grips count as pressed past this.
constexpr float kPressThreshold = 0.7f;

void logError(const char* what, XrResult result) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s: %d", what, static_cast<int>(result));
}

bool ok(XrResult result, const char* what) {
    if (XR_FAILED(result)) {
        logError(what, result);
        return false;
    }
    return true;
}

} // namespace

XrApp::XrApp(JavaVM* vm, jobject activity, Operator& op, jobject videoTexture, jobject overlayTexture,
             size_t initialProfile)
    : vm_(vm), activity_(activity), operator_(op), videoTexture_(videoTexture),
      overlayTexture_(overlayTexture), initialProfile_(initialProfile) {}

XrApp::~XrApp() { stop(); }

void XrApp::start() {
    stopping_ = false;
    thread_ = std::thread(&XrApp::run, this);
}

void XrApp::stop() {
    stopping_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
}

// MARK: Setup

bool XrApp::createInstance() {
    // The Android loader must know the VM and the activity before anything else.
    PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
    if (!ok(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                  reinterpret_cast<PFN_xrVoidFunction*>(&initializeLoader)),
            "xrInitializeLoaderKHR lookup")) {
        return false;
    }
    XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loaderInfo.applicationVM = vm_;
    loaderInfo.applicationContext = activity_;
    if (!ok(initializeLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo)),
            "xrInitializeLoaderKHR")) {
        return false;
    }

    // Passthrough is optional: on a runtime without it the room stays dark.
    uint32_t available = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &available, nullptr);
    std::vector<XrExtensionProperties> properties(available, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, available, &available, properties.data());
    for (const XrExtensionProperties& e : properties) {
        if (std::strcmp(e.extensionName, XR_FB_PASSTHROUGH_EXTENSION_NAME) == 0) {
            passthroughSupported_ = true;
        }
    }
    std::vector<const char*> extensions = {XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
                                           XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME};
    if (passthroughSupported_) {
        extensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
    }
    XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidInfo.applicationVM = vm_;
    androidInfo.applicationActivity = activity_;
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    info.next = &androidInfo;
    std::strncpy(info.applicationInfo.applicationName, "Imago Payload VR", XR_MAX_APPLICATION_NAME_SIZE - 1);
    info.applicationInfo.applicationVersion = 1;
    std::strncpy(info.applicationInfo.engineName, "imago", XR_MAX_ENGINE_NAME_SIZE - 1);
    info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.enabledExtensionNames = extensions.data();
    if (!ok(xrCreateInstance(&info, &instance_), "xrCreateInstance")) {
        return false;
    }
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    return ok(xrGetSystem(instance_, &systemInfo, &system_), "xrGetSystem");
}

bool XrApp::createSession() {
    // The runtime wants to be asked about GL before the session exists.
    PFN_xrGetOpenGLESGraphicsRequirementsKHR getRequirements = nullptr;
    if (!ok(xrGetInstanceProcAddr(instance_, "xrGetOpenGLESGraphicsRequirementsKHR",
                                  reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements)),
            "xrGetOpenGLESGraphicsRequirementsKHR lookup")) {
        return false;
    }
    XrGraphicsRequirementsOpenGLESKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (!ok(getRequirements(instance_, system_, &requirements), "xrGetOpenGLESGraphicsRequirementsKHR")) {
        return false;
    }
    if (!renderer_.initEgl()) {
        return false;
    }
    XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    binding.display = renderer_.display();
    binding.config = renderer_.config();
    binding.context = renderer_.context();
    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
    info.next = &binding;
    info.systemId = system_;
    if (!ok(xrCreateSession(instance_, &info, &session_), "xrCreateSession")) {
        return false;
    }

    // LOCAL: gravity-aligned, origin where the headset was at start; the screen is fixed in it.
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.poseInReferenceSpace.orientation = {0, 0, 0, 1};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!ok(xrCreateReferenceSpace(session_, &spaceInfo, &localSpace_), "local space")) {
        return false;
    }
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!ok(xrCreateReferenceSpace(session_, &spaceInfo, &viewSpace_), "view space")) {
        return false;
    }
    // The profile VR starts on decides it (the session is not connected yet).
    if (passthroughSupported_ && operator_.profileAt(initialProfile_).passthrough && !createPassthrough()) {
        __android_log_print(ANDROID_LOG_WARN, kTag, "passthrough unavailable: opaque background");
    }
    return true;
}

bool XrApp::createPassthrough() {
    const auto load = [&](const char* name, auto& fn) {
        return ok(xrGetInstanceProcAddr(instance_, name, reinterpret_cast<PFN_xrVoidFunction*>(&fn)), name);
    };
    if (!load("xrCreatePassthroughFB", createPassthrough_) ||
        !load("xrDestroyPassthroughFB", destroyPassthrough_) ||
        !load("xrCreatePassthroughLayerFB", createPassthroughLayer_) ||
        !load("xrDestroyPassthroughLayerFB", destroyPassthroughLayer_)) {
        return false;
    }
    XrPassthroughCreateInfoFB info{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
    info.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    if (!ok(createPassthrough_(session_, &info, &passthrough_), "xrCreatePassthroughFB")) {
        passthrough_ = XR_NULL_HANDLE;
        return false;
    }
    XrPassthroughLayerCreateInfoFB layerInfo{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
    layerInfo.passthrough = passthrough_;
    layerInfo.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    layerInfo.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
    if (!ok(createPassthroughLayer_(session_, &layerInfo, &passthroughLayer_), "xrCreatePassthroughLayerFB")) {
        passthroughLayer_ = XR_NULL_HANDLE;
        destroyPassthrough_(passthrough_);
        passthrough_ = XR_NULL_HANDLE;
        return false;
    }
    return true;
}

void XrApp::applyHaptics(int hands) {
    if (hands == 0 || haptic_ == XR_NULL_HANDLE) {
        return;
    }
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.duration = 40'000'000; // 40 ms
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    vibration.amplitude = 0.6f;
    for (int bit = 0; bit < 2; ++bit) {
        if ((hands & (1 << bit)) == 0) {
            continue;
        }
        XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
        info.action = haptic_;
        info.subactionPath = bit == 0 ? leftHand_ : rightHand_;
        xrApplyHapticFeedback(session_, &info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
    }
}

bool XrApp::createSwapchains() {
    uint32_t count = 0;
    xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                      &count, nullptr);
    viewConfigs_.assign(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (!ok(xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                              count, &count, viewConfigs_.data()),
            "xrEnumerateViewConfigurationViews")) {
        return false;
    }
    uint32_t formatCount = 0;
    xrEnumerateSwapchainFormats(session_, 0, &formatCount, nullptr);
    std::vector<int64_t> formats(formatCount);
    xrEnumerateSwapchainFormats(session_, formatCount, &formatCount, formats.data());
    // sRGB first: the compositor then treats the picture as sRGB-encoded, which it is.
    colorFormat_ = formats.empty() ? GL_RGBA8 : formats.front();
    for (const int64_t preferred : {int64_t{GL_SRGB8_ALPHA8}, int64_t{GL_RGBA8}}) {
        if (std::find(formats.begin(), formats.end(), preferred) != formats.end()) {
            colorFormat_ = preferred;
            break;
        }
    }
    views_.assign(count, {XR_TYPE_VIEW});
    swapchains_.clear();
    for (const XrViewConfigurationView& view : viewConfigs_) {
        Swapchain sc;
        // A little more than the runtime recommends: this scene is a few quads, and text is
        // what it shows; the extra pixels go to its edges.
        const auto larger = [](uint32_t recommended, uint32_t most) {
            return std::min(static_cast<uint32_t>(static_cast<float>(recommended) * kSupersample), most);
        };
        const uint32_t imageWidth = larger(view.recommendedImageRectWidth, view.maxImageRectWidth);
        const uint32_t imageHeight = larger(view.recommendedImageRectHeight, view.maxImageRectHeight);
        sc.width = static_cast<int>(imageWidth);
        sc.height = static_cast<int>(imageHeight);
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        info.format = colorFormat_;
        info.sampleCount = 1;
        info.width = imageWidth;
        info.height = imageHeight;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;
        if (!ok(xrCreateSwapchain(session_, &info, &sc.handle), "xrCreateSwapchain")) {
            return false;
        }
        uint32_t imageCount = 0;
        xrEnumerateSwapchainImages(sc.handle, 0, &imageCount, nullptr);
        sc.images.assign(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        if (!ok(xrEnumerateSwapchainImages(sc.handle, imageCount, &imageCount,
                                           reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.images.data())),
                "xrEnumerateSwapchainImages")) {
            return false;
        }
        swapchains_.push_back(std::move(sc));
    }
    return true;
}

bool XrApp::createActions() {
    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(setInfo.actionSetName, "operator");
    std::strcpy(setInfo.localizedActionSetName, "Operator");
    if (!ok(xrCreateActionSet(instance_, &setInfo, &actionSet_), "xrCreateActionSet")) {
        return false;
    }
    xrStringToPath(instance_, "/user/hand/left", &leftHand_);
    xrStringToPath(instance_, "/user/hand/right", &rightHand_);
    const XrPath leftHand = leftHand_;

    const auto action = [&](const char* name, XrActionType type, XrAction& out, bool bothHands = false) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
        std::strncpy(info.localizedActionName, name, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        info.actionType = type;
        XrPath hands[] = {leftHand, rightHand_};
        if (bothHands) {
            info.countSubactionPaths = 2;
            info.subactionPaths = hands;
        }
        return ok(xrCreateAction(actionSet_, &info, &out), name);
    };
    if (!action("right_stick", XR_ACTION_TYPE_VECTOR2F_INPUT, rightStick_) ||
        !action("left_stick", XR_ACTION_TYPE_VECTOR2F_INPUT, leftStick_) ||
        !action("a", XR_ACTION_TYPE_BOOLEAN_INPUT, aButton_) ||
        !action("b", XR_ACTION_TYPE_BOOLEAN_INPUT, bButton_) ||
        !action("x", XR_ACTION_TYPE_BOOLEAN_INPUT, xButton_) ||
        !action("y", XR_ACTION_TYPE_BOOLEAN_INPUT, yButton_) ||
        !action("menu", XR_ACTION_TYPE_BOOLEAN_INPUT, menuButton_) ||
        !action("left_trigger", XR_ACTION_TYPE_FLOAT_INPUT, leftTrigger_) ||
        !action("right_trigger", XR_ACTION_TYPE_FLOAT_INPUT, rightTrigger_) ||
        !action("left_grip", XR_ACTION_TYPE_FLOAT_INPUT, leftGrip_) ||
        !action("right_grip", XR_ACTION_TYPE_FLOAT_INPUT, rightGrip_) ||
        !action("left_stick_click", XR_ACTION_TYPE_BOOLEAN_INPUT, leftStickClick_) ||
        !action("right_stick_click", XR_ACTION_TYPE_BOOLEAN_INPUT, rightStickClick_) ||
        !action("aim", XR_ACTION_TYPE_POSE_INPUT, aimPose_, true) ||
        !action("haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT, haptic_, true)) {
        return false;
    }

    // Meta Touch controllers. Another profile would need its own list here.
    struct Binding {
        XrAction action;
        const char* path;
    };
    const Binding bindings[] = {
        {rightStick_, "/user/hand/right/input/thumbstick"},
        {leftStick_, "/user/hand/left/input/thumbstick"},
        {aButton_, "/user/hand/right/input/a/click"},
        {bButton_, "/user/hand/right/input/b/click"},
        {xButton_, "/user/hand/left/input/x/click"},
        {yButton_, "/user/hand/left/input/y/click"},
        {menuButton_, "/user/hand/left/input/menu/click"},
        {leftTrigger_, "/user/hand/left/input/trigger/value"},
        {rightTrigger_, "/user/hand/right/input/trigger/value"},
        {leftGrip_, "/user/hand/left/input/squeeze/value"},
        {rightGrip_, "/user/hand/right/input/squeeze/value"},
        {leftStickClick_, "/user/hand/left/input/thumbstick/click"},
        {rightStickClick_, "/user/hand/right/input/thumbstick/click"},
        {aimPose_, "/user/hand/left/input/aim/pose"},
        {aimPose_, "/user/hand/right/input/aim/pose"},
        {haptic_, "/user/hand/left/output/haptic"},
        {haptic_, "/user/hand/right/output/haptic"},
    };
    std::vector<XrActionSuggestedBinding> suggested;
    for (const Binding& b : bindings) {
        XrPath path = XR_NULL_PATH;
        xrStringToPath(instance_, b.path, &path);
        suggested.push_back({b.action, path});
    }
    XrPath profile = XR_NULL_PATH;
    xrStringToPath(instance_, "/interaction_profiles/oculus/touch_controller", &profile);
    XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggestion.interactionProfile = profile;
    suggestion.countSuggestedBindings = static_cast<uint32_t>(suggested.size());
    suggestion.suggestedBindings = suggested.data();
    if (!ok(xrSuggestInteractionProfileBindings(instance_, &suggestion), "xrSuggestInteractionProfileBindings")) {
        return false;
    }

    XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    spaceInfo.action = aimPose_;
    spaceInfo.subactionPath = rightHand_;
    spaceInfo.poseInActionSpace.orientation = {0, 0, 0, 1};
    if (!ok(xrCreateActionSpace(session_, &spaceInfo, &aimSpace_), "aim space")) {
        return false;
    }
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &actionSet_;
    return ok(xrAttachSessionActionSets(session_, &attach), "xrAttachSessionActionSets");
}

void XrApp::destroy() {
    operator_.disconnect();
    for (Swapchain& sc : swapchains_) {
        if (sc.handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(sc.handle);
        }
    }
    swapchains_.clear();
    if (passthroughLayer_ != XR_NULL_HANDLE) destroyPassthroughLayer_(passthroughLayer_);
    if (passthrough_ != XR_NULL_HANDLE) destroyPassthrough_(passthrough_);
    passthroughLayer_ = XR_NULL_HANDLE;
    passthrough_ = XR_NULL_HANDLE;
    if (aimSpace_ != XR_NULL_HANDLE) xrDestroySpace(aimSpace_);
    if (actionSet_ != XR_NULL_HANDLE) xrDestroyActionSet(actionSet_);
    if (viewSpace_ != XR_NULL_HANDLE) xrDestroySpace(viewSpace_);
    if (localSpace_ != XR_NULL_HANDLE) xrDestroySpace(localSpace_);
    if (session_ != XR_NULL_HANDLE) xrDestroySession(session_);
    if (instance_ != XR_NULL_HANDLE) xrDestroyInstance(instance_);
    aimSpace_ = XR_NULL_HANDLE;
    actionSet_ = XR_NULL_HANDLE;
    viewSpace_ = XR_NULL_HANDLE;
    localSpace_ = XR_NULL_HANDLE;
    session_ = XR_NULL_HANDLE;
    instance_ = XR_NULL_HANDLE;
}

// MARK: Loop

void XrApp::run() {
    JNIEnv* env = android::currentEnv(); // attached for the thread's life
    if (env == nullptr || !createInstance() || !createSession() || !createSwapchains() ||
        !createActions() || !renderer_.attachTextures(env, videoTexture_, overlayTexture_)) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "OpenXR setup failed");
        destroy();
        return;
    }
    operator_.connect(initialProfile_);
    while (!stopping_) {
        if (!pollEvents()) {
            break;
        }
        if (sessionRunning_) {
            frame();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    if (sessionRunning_) {
        xrEndSession(session_);
        sessionRunning_ = false;
    }
    destroy();
}

bool XrApp::pollEvents() {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(instance_, &event) == XR_SUCCESS) {
        switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const auto& changed = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            state_ = changed.state;
            switch (state_) {
            case XR_SESSION_STATE_READY: {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (ok(xrBeginSession(session_, &begin), "xrBeginSession")) {
                    sessionRunning_ = true;
                }
                break;
            }
            case XR_SESSION_STATE_STOPPING:
                xrEndSession(session_);
                sessionRunning_ = false;
                break;
            case XR_SESSION_STATE_EXITING:
            case XR_SESSION_STATE_LOSS_PENDING:
                return false;
            default:
                break;
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            return false;
        default:
            break;
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    return true;
}

void XrApp::placePanel(const XrPosef& head) {
    // In front of the operator, at eye height minus a little, turned to face them. Yaw only:
    // the screen stays upright whatever the head's tilt.
    const XrVector3f forward = rotate(head.orientation, XrVector3f{0, 0, -1});
    const float yawDeg = std::atan2(-forward.x, -forward.z) / kDegToRad; // OpenXR: left positive
    const Profile& p = operator_.profile();
    const XrQuaternionf orientation = multiply(yawRotation(yawDeg), pitchRotation(-p.panelPitchDown));
    panel_.pose.orientation = orientation;
    panel_.pose.position = head.position + rotate(orientation, XrVector3f{0, 0, -p.panelDistance});
    panel_.width = p.panelWidth;
    panelPlaced_ = true;
}

void XrApp::followHead(const XrPosef& head, float dtS) {
    // With head tracking on, the picture shows where the head points: a screen left behind in
    // the room would be out of sight after the first turn (first flight-like test with the
    // headset, 2026-11-11). It goes with the head, upright; when head tracking goes off, or
    // the clutch holds the camera, it stays where it is.
    const Profile& p = operator_.profile();
    float headYaw = 0;
    float headPitch = 0;
    imago_head_track_angles_from_quaternion(head.orientation.w, head.orientation.x, head.orientation.y,
                                            head.orientation.z, &headYaw, &headPitch);
    const float wantPitch = headPitch - p.panelPitchDown;
    const auto wrap = [](float deg) { return deg - 360.0f * std::round(deg / 360.0f); };
    if (!following_) {
        // Start from where the screen is now, as seen from the head, and glide to the front.
        const XrVector3f to = panel_.pose.position - head.position;
        glideYaw_ = wrap(std::atan2(to.x, -to.z) / kDegToRad - headYaw);
        glidePitch_ = std::atan2(to.y, std::sqrt(to.x * to.x + to.z * to.z)) / kDegToRad - wantPitch;
        following_ = true;
    }
    const float glide = std::exp(-dtS / kFollowTimeS);
    glideYaw_ *= glide;
    glidePitch_ *= glide;
    // Placing the screen where the gimbal reports it points (so the picture sat where it was
    // taken from) was tried and taken out: on the headset it came in jumps, the reports being
    // a few a second, and felt worse than the plain follow (2026-11-11).
    followYaw_ = headYaw + glideYaw_;
    followPitch_ = std::clamp(wantPitch + glidePitch_, -85.0f, 85.0f);
    const XrQuaternionf orientation = multiply(yawRotation(-followYaw_), pitchRotation(followPitch_));
    panel_.pose.orientation = orientation;
    panel_.pose.position = head.position + rotate(orientation, XrVector3f{0, 0, -p.panelDistance});
    panel_.width = p.panelWidth;
}

void XrApp::updateVignette(const XrPosef& head, float dtS) {
    // How fast the head turns, in degrees a second: past a brisk look around, the picture's
    // edge goes dark (quickly) and comes back (slowly) once the head is still again.
    float yaw = 0;
    float pitch = 0;
    imago_head_track_angles_from_quaternion(head.orientation.w, head.orientation.x, head.orientation.y,
                                            head.orientation.z, &yaw, &pitch);
    float speed = 0;
    if (headAnglesKnown_ && dtS > 0.001f) {
        float turn = yaw - lastHeadYaw_;
        turn -= 360.0f * std::round(turn / 360.0f);
        speed = std::sqrt(turn * turn + (pitch - lastHeadPitch_) * (pitch - lastHeadPitch_)) / dtS;
    }
    headAnglesKnown_ = true;
    lastHeadYaw_ = yaw;
    lastHeadPitch_ = pitch;
    const float want = operator_.vignetteWanted()
                           ? std::clamp((speed - kVignetteFromDps) / (kVignetteFullDps - kVignetteFromDps), 0.0f, 1.0f) * kVignetteMax
                           : 0.0f;
    const float tau = want > vignette_ ? 0.08f : 0.45f;
    vignette_ += (want - vignette_) * (1.0f - std::exp(-dtS / tau));
}

void XrApp::updateGrab(bool aimValid, const XrPosef& aim, const XrPosef& head, float stickY, float dtS) {
    // The screen taken by a handle, as the headset's own windows are: the move handle carries
    // it along the pointer's ray (the right stick pushes it away or pulls it in), a corner
    // resizes it about its centre. What it is left with is what following and SCREEN use
    // from then on.
    const int wanted = aimValid ? operator_.grab() : 0;
    const XrVector3f direction = rotate(aim.orientation, XrVector3f{0, 0, -1});
    const XrVector3f normal = rotate(panel_.pose.orientation, XrVector3f{0, 0, 1});
    const XrVector3f right = rotate(panel_.pose.orientation, XrVector3f{1, 0, 0});
    const XrVector3f up = rotate(panel_.pose.orientation, XrVector3f{0, 1, 0});
    // Where the ray meets the screen's plane (anywhere on it, also past its edge).
    const auto onPlane = [&](XrVector3f& local, float& t) {
        const float facing = dot(direction, normal);
        if (std::fabs(facing) < 1e-4f) {
            return false;
        }
        t = dot(panel_.pose.position - aim.position, normal) / facing;
        if (t <= 0) {
            return false;
        }
        const XrVector3f hit = aim.position + direction * t - panel_.pose.position;
        local = XrVector3f{dot(hit, right), dot(hit, up), 0};
        return true;
    };
    if (wanted != grabbing_) {
        if (grabbing_ != 0) {
            // Let go: keep the size, the distance and the height against the line of sight.
            const XrVector3f to = panel_.pose.position - head.position;
            const float distance = std::sqrt(dot(to, to));
            float headYaw = 0;
            float headPitch = 0;
            imago_head_track_angles_from_quaternion(head.orientation.w, head.orientation.x, head.orientation.y,
                                                    head.orientation.z, &headYaw, &headPitch);
            const float elevation = std::atan2(to.y, std::sqrt(to.x * to.x + to.z * to.z)) / kDegToRad;
            operator_.setPanelGeometry(distance, panel_.width, std::clamp(headPitch - elevation, -40.0f, 40.0f));
        }
        grabbing_ = 0;
        XrVector3f local{0, 0, 0};
        float t = 0;
        if (wanted != 0 && onPlane(local, t)) {
            grabbing_ = wanted;
            grabLocal_ = local;
            grabDistance_ = t;
            grabStartWidth_ = panel_.width;
            grabStartRadius_ = std::max(0.05f, std::sqrt(local.x * local.x + local.y * local.y));
        }
        return;
    }
    if (grabbing_ == 1) {
        grabDistance_ = std::clamp(grabDistance_ + stickY * kGrabReachSpeed * dtS, kPanelNearestM, kPanelFarthestM);
        const XrVector3f held = aim.position + direction * grabDistance_;
        // Facing the operator, upright, wherever it is carried: the held point stays under
        // the pointer. The orientation depends on the centre and the centre on it; one more
        // pass settles it.
        XrVector3f centre = held - (right * grabLocal_.x + up * grabLocal_.y);
        XrQuaternionf orientation = panel_.pose.orientation;
        for (int pass = 0; pass < 2; ++pass) {
            const XrVector3f to = centre - head.position;
            const float yaw = std::atan2(to.x, -to.z) / kDegToRad;
            const float pitch = std::atan2(to.y, std::sqrt(to.x * to.x + to.z * to.z)) / kDegToRad;
            orientation = multiply(yawRotation(-yaw), pitchRotation(pitch));
            centre = held - (rotate(orientation, XrVector3f{1, 0, 0}) * grabLocal_.x +
                             rotate(orientation, XrVector3f{0, 1, 0}) * grabLocal_.y);
        }
        panel_.pose.orientation = orientation;
        panel_.pose.position = centre;
    } else if (grabbing_ == 2) {
        XrVector3f local{0, 0, 0};
        float t = 0;
        if (onPlane(local, t)) {
            const float radius = std::sqrt(local.x * local.x + local.y * local.y);
            panel_.width = std::clamp(grabStartWidth_ * radius / grabStartRadius_, kPanelNarrowestM, kPanelWidestM);
        }
    }
}

Pointer XrApp::pointerOn(const XrPosef& aim, GlRenderer::Ray& ray) const {
    Pointer pointer;
    const XrVector3f direction = rotate(aim.orientation, XrVector3f{0, 0, -1});
    ray.valid = true;
    ray.origin = aim.position;
    ray.end = aim.position + direction * kRayLengthM;
    // The nearest interface layer the ray meets: what is drawn in front is what is hit.
    float best = 1e9f;
    for (const Layer& layer : layers_) {
        float t = 0, x = 0, y = 0;
        if (intersect(layer, aim.position, direction, t, x, y) && t < best) {
            best = t;
            pointer.valid = true;
            pointer.x = x;
            pointer.y = y;
        }
    }
    if (pointer.valid) {
        ray.end = aim.position + direction * best;
        pointer.onVideo = pointer.x >= 0 && pointer.x <= 1 && pointer.y >= 0 && pointer.y <= 1;
    }
    return pointer;
}

void XrApp::readInput(XrTime time, Input& input) {
    input = Input{};
    input.focused = state_ == XR_SESSION_STATE_FOCUSED;
    if (!input.focused) {
        return;
    }
    XrActiveActionSet active{actionSet_, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (XR_FAILED(xrSyncActions(session_, &sync))) {
        return;
    }
    const auto vector2 = [&](XrAction a) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = a;
        XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
        xrGetActionStateVector2f(session_, &get, &st);
        return st.isActive ? st.currentState : XrVector2f{0, 0};
    };
    const auto boolean = [&](XrAction a) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = a;
        XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
        xrGetActionStateBoolean(session_, &get, &st);
        return st.isActive && st.currentState;
    };
    const auto pressed = [&](XrAction a) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = a;
        XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
        xrGetActionStateFloat(session_, &get, &st);
        return st.isActive && st.currentState >= kPressThreshold;
    };
    input.rightStick = vector2(rightStick_);
    input.leftStick = vector2(leftStick_);
    input.a = boolean(aButton_);
    input.b = boolean(bButton_);
    input.x = boolean(xButton_);
    input.y = boolean(yButton_);
    input.menu = boolean(menuButton_);
    input.leftTrigger = pressed(leftTrigger_);
    input.rightTrigger = pressed(rightTrigger_);
    input.leftGrip = pressed(leftGrip_);
    input.rightGrip = pressed(rightGrip_);
    input.leftStickClick = boolean(leftStickClick_);
    input.rightStickClick = boolean(rightStickClick_);
    (void)time;
}

void XrApp::frame() {
    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    if (!ok(xrWaitFrame(session_, &waitInfo, &frameState), "xrWaitFrame")) {
        return;
    }
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    xrBeginFrame(session_, &beginInfo);

    // With passthrough the room is the first layer and the scene is blended over it by its
    // alpha (clear to 0, the screen and the ray opaque).
    const bool passthrough = passthroughLayer_ != XR_NULL_HANDLE;
    operator_.setPassthroughActive(passthrough);
    XrCompositionLayerPassthroughFB roomLayer{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
    roomLayer.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    roomLayer.space = XR_NULL_HANDLE;
    roomLayer.layerHandle = passthroughLayer_;
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    if (passthrough) {
        layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    }
    std::vector<XrCompositionLayerProjectionView> projectionViews;
    const XrCompositionLayerBaseHeader* layers[2] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(passthrough ? static_cast<const void*>(&roomLayer)
                                                                           : static_cast<const void*>(&layer)),
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
    uint32_t layerCount = 0;

    if (frameState.shouldRender) {
        const XrTime time = frameState.predictedDisplayTime;
        // Head: the VIEW space in the LOCAL space, for the panel and the head tracking.
        XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
        xrLocateSpace(viewSpace_, localSpace_, time, &head);
        const bool headValid = (head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
        const float dtS = lastFrameTime_ != 0
                              ? std::clamp(static_cast<float>(time - lastFrameTime_) / 1e9f, 0.0f, 0.1f)
                              : 0.0f;
        lastFrameTime_ = time;
        if (headValid && (!panelPlaced_ || operator_.takePlacePanelRequest())) {
            placePanel(head.pose);
            following_ = false;
        }
        if (headValid && operator_.screenFollowsHead()) {
            followHead(head.pose, dtS);
        } else {
            following_ = false;
        }
        if (headValid) {
            updateVignette(head.pose, dtS);
        }
        panel_.height = panel_.width / operator_.videoAspect();
        layers_ = buildLayers(panel_, operator_.sheetOpen());
        if (headValid) {
            // Stamped "now" on the core's clock: the pose is a prediction a few ms ahead,
            // which is as fresh as it gets.
            operator_.headPose(head.pose.orientation, 0);
        }

        Input input;
        readInput(time, input);
        GlRenderer::Ray ray;
        Pointer pointer;
        XrSpaceLocation aim{XR_TYPE_SPACE_LOCATION};
        bool aimValid = false;
        if (input.focused) {
            xrLocateSpace(aimSpace_, localSpace_, time, &aim);
            aimValid = (aim.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0 &&
                       (aim.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        }
        if (headValid) {
            // The screen in the operator's hand moves before the pointer is read against it.
            updateGrab(aimValid, aim.pose, head.pose, input.rightStick.y, dtS);
            if (grabbing_ != 0) {
                panel_.height = panel_.width / operator_.videoAspect();
                layers_ = buildLayers(panel_, operator_.sheetOpen());
            }
        }
        if (aimValid) {
            pointer = pointerOn(aim.pose, ray);
        }
        operator_.tick(input, pointer, imago_now_ns());
        applyHaptics(operator_.takeHapticRequests());

        XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = time;
        locateInfo.space = localSpace_;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t viewCount = 0;
        xrLocateViews(session_, &locateInfo, &viewState, static_cast<uint32_t>(views_.size()), &viewCount,
                      views_.data());

        renderer_.updateTextures();
        projectionViews.resize(viewCount, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});
        for (uint32_t k = 0; k < viewCount; ++k) {
            Swapchain& sc = swapchains_[k];
            uint32_t imageIndex = 0;
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            xrAcquireSwapchainImage(sc.handle, &acquire, &imageIndex);
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout = XR_INFINITE_DURATION;
            xrWaitSwapchainImage(sc.handle, &wait);

            renderer_.render(sc.images[imageIndex].image, sc.width, sc.height, views_[k].pose,
                             views_[k].fov, panel_, layers_.data(), layers_.size(), ray, passthrough,
                             operator_.profile().roomDim, vignette_);

            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            xrReleaseSwapchainImage(sc.handle, &release);

            XrCompositionLayerProjectionView& pv = projectionViews[k];
            pv.pose = views_[k].pose;
            pv.fov = views_[k].fov;
            pv.subImage.swapchain = sc.handle;
            pv.subImage.imageRect.offset = {0, 0};
            pv.subImage.imageRect.extent = {sc.width, sc.height};
            pv.subImage.imageArrayIndex = 0;
        }
        layer.space = localSpace_;
        layer.viewCount = viewCount;
        layer.views = projectionViews.data();
        layerCount = passthrough ? 2 : 1;
    }

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = layerCount;
    endInfo.layers = layers;
    xrEndFrame(session_, &endInfo);
}

} // namespace imago::quest
