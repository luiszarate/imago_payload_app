// JNI entry points of aero.imago.payloadcontrol.quest.NativeBridge. One native app per
// process: the Operator (core session, decoder) and the XrApp (OpenXR thread).

#include "hud_layout.h"
#include "jni_util.h"
#include "operator.h"
#include "xr_app.h"

#include <android/native_window_jni.h>

#include <memory>
#include <mutex>
#include <vector>

#define JNI(ret, name) \
    extern "C" JNIEXPORT ret JNICALL Java_aero_imago_payloadcontrol_quest_NativeBridge_##name

namespace {

using imago::quest::Operator;
using imago::quest::Profile;
using imago::quest::XrApp;

struct App {
    jobject activity = nullptr;       // global
    jobject videoTexture = nullptr;   // global
    jobject overlayTexture = nullptr; // global
    ANativeWindow* videoWindow = nullptr;
    std::unique_ptr<Operator> op;
    std::unique_ptr<XrApp> xr;
};

std::mutex gMutex;
std::unique_ptr<App> gApp; // guarded by gMutex for start/stop; read lock-free by the HUD

std::string stringField(JNIEnv* env, jclass cls, jobject obj, const char* name) {
    jfieldID id = env->GetFieldID(cls, name, "Ljava/lang/String;");
    auto text = static_cast<jstring>(env->GetObjectField(obj, id));
    std::string out = imago::android::toStdString(env, text);
    env->DeleteLocalRef(text);
    return out;
}

int intField(JNIEnv* env, jclass cls, jobject obj, const char* name) {
    return env->GetIntField(obj, env->GetFieldID(cls, name, "I"));
}

float floatField(JNIEnv* env, jclass cls, jobject obj, const char* name) {
    return env->GetFloatField(obj, env->GetFieldID(cls, name, "F"));
}

bool boolField(JNIEnv* env, jclass cls, jobject obj, const char* name) {
    return env->GetBooleanField(obj, env->GetFieldID(cls, name, "Z")) != JNI_FALSE;
}

Profile readProfile(JNIEnv* env, jobject obj) {
    jclass cls = env->GetObjectClass(obj);
    Profile p;
    p.name = stringField(env, cls, obj, "name");
    p.listen = boolField(env, cls, obj, "listen");
    p.payloadIp = stringField(env, cls, obj, "payloadIp");
    p.mavlinkPort = intField(env, cls, obj, "mavlinkPort");
    p.localPort = intField(env, cls, obj, "localPort");
    p.videoUrl = stringField(env, cls, obj, "videoUrl");
    p.transport = intField(env, cls, obj, "transport");
    p.gimbalProtocol = intField(env, cls, obj, "gimbalProtocol");
    p.maxVideoLagMs = intField(env, cls, obj, "maxVideoLagMs");
    p.videoSmoothingMs = intField(env, cls, obj, "videoSmoothingMs");
    p.headMode = intField(env, cls, obj, "headMode");
    p.headOnAtStart = boolField(env, cls, obj, "headOnAtStart");
    imago_head_track_config_init(&p.head);
    p.head.gain_dps_per_deg = floatField(env, cls, obj, "headGain");
    p.head.max_rate_dps = floatField(env, cls, obj, "headMaxRate");
    p.head.deadband_deg = floatField(env, cls, obj, "headDeadband");
    const int smoothing = intField(env, cls, obj, "headSmoothingMs");
    p.head.smoothing_ms = smoothing > 0 ? static_cast<uint32_t>(smoothing) : 0;
    p.head.yaw_range_deg = floatField(env, cls, obj, "headYawRange");
    p.maxYawDps = floatField(env, cls, obj, "maxYawDps");
    p.maxPitchDps = floatField(env, cls, obj, "maxPitchDps");
    p.deadzone = floatField(env, cls, obj, "deadzone");
    p.expo = floatField(env, cls, obj, "expo");
    p.zoomCompensation = floatField(env, cls, obj, "zoomCompensation");
    p.trimFactor = floatField(env, cls, obj, "trimFactor");
    p.panelDistance = floatField(env, cls, obj, "panelDistance");
    p.panelWidth = floatField(env, cls, obj, "panelWidth");
    p.panelPitchDown = floatField(env, cls, obj, "panelPitchDown");
    p.passthrough = boolField(env, cls, obj, "passthrough");
    p.keepWifiAwake = boolField(env, cls, obj, "keepWifiAwake");
    p.head.invert_pitch = boolField(env, cls, obj, "headInvertPitch");
    p.head.invert_yaw = boolField(env, cls, obj, "headInvertYaw");
    env->DeleteLocalRef(cls);
    return p;
}

} // namespace

JNI(jboolean, start)(JNIEnv* env, jclass, jobject activity, jobjectArray profiles, jint initialProfile,
                     jobject videoSurface,
                     jobject videoTexture, jobject overlayTexture) {
    std::lock_guard lock(gMutex);
    if (gApp) {
        return JNI_TRUE;
    }
    std::vector<Profile> list;
    const jsize count = env->GetArrayLength(profiles);
    for (jsize k = 0; k < count; ++k) {
        jobject obj = env->GetObjectArrayElement(profiles, k);
        list.push_back(readProfile(env, obj));
        env->DeleteLocalRef(obj);
    }
    auto app = std::make_unique<App>();
    app->activity = env->NewGlobalRef(activity);
    app->videoTexture = env->NewGlobalRef(videoTexture);
    app->overlayTexture = env->NewGlobalRef(overlayTexture);
    app->videoWindow = ANativeWindow_fromSurface(env, videoSurface);
    app->op = std::make_unique<Operator>(app->activity, std::move(list));
    ANativeWindow_acquire(app->videoWindow);
    app->op->setVideoWindow(app->videoWindow); // the decoder takes that reference
    app->xr = std::make_unique<XrApp>(imago::android::javaVM(), app->activity, *app->op, app->videoTexture,
                                      app->overlayTexture, static_cast<size_t>(initialProfile < 0 ? 0 : initialProfile));
    app->xr->start();
    gApp = std::move(app);
    return JNI_TRUE;
}

JNI(void, stop)(JNIEnv* env, jclass) {
    std::unique_ptr<App> app;
    {
        std::lock_guard lock(gMutex);
        app = std::move(gApp);
    }
    if (!app) {
        return;
    }
    app->xr->stop(); // joins the XR thread, which disconnects the session
    app->xr.reset();
    app->op->setVideoWindow(nullptr);
    app->op.reset();
    ANativeWindow_release(app->videoWindow);
    env->DeleteGlobalRef(app->overlayTexture);
    env->DeleteGlobalRef(app->videoTexture);
    env->DeleteGlobalRef(app->activity);
}

JNI(jboolean, hudSnapshot)(JNIEnv* env, jclass, jfloatArray out) {
    std::lock_guard lock(gMutex);
    if (!gApp || env->GetArrayLength(out) < imago::quest::hud::kSize) {
        return JNI_FALSE;
    }
    float values[imago::quest::hud::kSize];
    gApp->op->hudSnapshot(values);
    env->SetFloatArrayRegion(out, 0, imago::quest::hud::kSize, values);
    return JNI_TRUE;
}

JNI(void, setVideoSize)(JNIEnv*, jclass, jint width, jint height) {
    std::lock_guard lock(gMutex);
    if (gApp) {
        gApp->op->setVideoSize(width, height);
    }
}

JNI(void, command)(JNIEnv*, jclass, jint command, jfloat a, jfloat b) {
    std::lock_guard lock(gMutex);
    if (gApp) {
        gApp->op->post(command, a, b);
    }
}

JNI(jstring, irPaletteName)(JNIEnv* env, jclass, jint cameraType, jint index) {
    return env->NewStringUTF(imago_ir_palette_name(cameraType, static_cast<uint32_t>(index)));
}
