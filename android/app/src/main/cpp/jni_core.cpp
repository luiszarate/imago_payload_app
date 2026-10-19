// JNI bindings of the core's C API for aero.imago.payloadcontrol.core.NativeCore.
//
// Handles are raw pointers passed to Kotlin as Long. Control commands go straight from the
// calling thread to the core, which sends them right away; video never reaches Java.

#include "jni_util.h"
#include "state_writer.h"
#include "video_decoder.h"

#include <imago/imago_core.h>

#include <android/native_window_jni.h>

#include <cstring>
#include <string>

using imago::android::currentEnv;
using imago::android::VideoDecoder;

#define JNI(ret, name) \
    extern "C" JNIEXPORT ret JNICALL Java_aero_imago_payloadcontrol_core_NativeCore_##name

namespace {

/// One session and the Kotlin object its events go to.
struct Session {
    imago_session* core = nullptr;
    jobject listener = nullptr; // global reference
    jmethodID onEvent = nullptr;
    VideoDecoder* decoder = nullptr; // owned by Kotlin's VideoRenderer, outlives the session
};

Session* session(jlong handle) { return reinterpret_cast<Session*>(handle); }
imago_session* core(jlong handle) { return reinterpret_cast<Session*>(handle)->core; }
VideoDecoder* decoder(jlong handle) { return reinterpret_cast<VideoDecoder*>(handle); }

/// Core threads. Text goes over as bytes: a payload's STATUSTEXT is not always valid UTF-8,
/// which NewStringUTF would abort on.
void onEvent(const imago_event* event, void* user) {
    auto* s = static_cast<Session*>(user);
    JNIEnv* env = currentEnv();
    if (env == nullptr || s->listener == nullptr) {
        return;
    }
    jbyteArray text = nullptr;
    if (event->text != nullptr) {
        const auto length = static_cast<jsize>(std::strlen(event->text));
        text = env->NewByteArray(length);
        if (text != nullptr) {
            env->SetByteArrayRegion(text, 0, length, reinterpret_cast<const jbyte*>(event->text));
        }
    }
    env->CallVoidMethod(s->listener, s->onEvent, static_cast<jint>(event->type),
                        static_cast<jint>(event->request_id), static_cast<jint>(event->code),
                        static_cast<jint>(event->result), text);
    imago::android::clearException(env);
    if (text != nullptr) {
        env->DeleteLocalRef(text);
    }
}

/// Core video thread.
void onVideoFrame(const imago_video_frame* frame, void* user) {
    auto* s = static_cast<Session*>(user);
    if (s->decoder != nullptr) {
        s->decoder->onFrame(*frame);
    }
}

} // namespace

// MARK: Video renderer

JNI(jlong, decoderCreate)(JNIEnv* env, jclass, jobject listener) {
    return reinterpret_cast<jlong>(new VideoDecoder(env->NewGlobalRef(listener)));
}

JNI(void, decoderDestroy)(JNIEnv*, jclass, jlong handle) { delete decoder(handle); }

JNI(void, decoderSetSurface)(JNIEnv* env, jclass, jlong handle, jobject surface) {
    decoder(handle)->setSurface(surface != nullptr ? ANativeWindow_fromSurface(env, surface) : nullptr);
}

JNI(void, decoderReset)(JNIEnv*, jclass, jlong handle) { decoder(handle)->reset(); }

/// decode failures, skipped, rendered
JNI(void, decoderCounters)(JNIEnv* env, jclass, jlong handle, jintArray out) {
    const VideoDecoder::Counters c = decoder(handle)->counters();
    const jint values[3] = {static_cast<jint>(c.decodeFailures), static_cast<jint>(c.skipped),
                            static_cast<jint>(c.rendered)};
    env->SetIntArrayRegion(out, 0, 3, values);
}

// MARK: Session

JNI(jlong, sessionCreate)(JNIEnv* env, jclass, jstring payloadIp, jint payloadPort,
                          jint localPort, jstring rtspUrl, jint transport, jint gimbalProtocol,
                          jboolean keepWifiAwake, jint maxLagMs, jint smoothingMs, jstring webHost,
                          jlong decoderHandle, jobject listener) {
    const std::string ip = imago::android::toStdString(env, payloadIp);
    const std::string url = imago::android::toStdString(env, rtspUrl);
    const std::string web = imago::android::toStdString(env, webHost);

    imago_config config;
    imago_config_init(&config);
    config.payload_ip = ip.empty() ? nullptr : ip.c_str();
    config.payload_port = static_cast<uint16_t>(payloadPort);
    config.local_port = static_cast<uint16_t>(localPort);
    config.rtsp_url = url.empty() ? nullptr : url.c_str();
    config.rtsp_use_payload_host = true;
    config.auto_start_video = true;
    config.rtsp_transport = static_cast<imago_rtsp_transport>(transport);
    config.nal_format = IMAGO_NAL_ANNEXB;
    config.gimbal_protocol = static_cast<imago_gimbal_protocol>(gimbalProtocol);
    config.wifi_keep_awake = keepWifiAwake;
    config.video_smoothing_ms = static_cast<uint32_t>(smoothingMs);
    config.web_host = web.empty() ? nullptr : web.c_str();
    config.video_resync_on_lag = maxLagMs > 0;
    if (maxLagMs > 0) {
        config.video_max_lag_ms = static_cast<uint32_t>(maxLagMs);
    }

    auto* s = new Session();
    s->listener = env->NewGlobalRef(listener);
    jclass cls = env->GetObjectClass(listener);
    s->onEvent = env->GetMethodID(cls, "onEvent", "(IIII[B)V");
    env->DeleteLocalRef(cls);
    s->decoder = decoderHandle != 0 ? decoder(decoderHandle) : nullptr;

    // The core copies the strings.
    s->core = imago_session_create(&config, onEvent, onVideoFrame, s);
    if (s->core == nullptr) {
        env->DeleteGlobalRef(s->listener);
        delete s;
        return 0;
    }
    if (s->decoder != nullptr) {
        s->decoder->setSession(s->core);
    }
    return reinterpret_cast<jlong>(s);
}

JNI(jint, sessionStart)(JNIEnv*, jclass, jlong h) { return imago_session_start(core(h)); }

/// Joins the core threads: no callback runs after this returns.
JNI(void, sessionStop)(JNIEnv*, jclass, jlong h) {
    Session* s = session(h);
    if (s->decoder != nullptr) {
        s->decoder->setSession(nullptr);
    }
    imago_session_stop(s->core);
}

JNI(void, sessionDestroy)(JNIEnv* env, jclass, jlong h) {
    Session* s = session(h);
    if (s->decoder != nullptr) {
        s->decoder->setSession(nullptr);
    }
    imago_session_destroy(s->core); // stops first
    env->DeleteGlobalRef(s->listener);
    delete s;
}

/// Writes the snapshot into a direct ByteBuffer (see state_writer.h). Returns the byte count.
JNI(jint, sessionState)(JNIEnv* env, jclass, jlong h, jobject buffer) {
    auto* out = static_cast<uint8_t*>(env->GetDirectBufferAddress(buffer));
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (out == nullptr || capacity <= 0) {
        return 0;
    }
    imago_state state;
    imago_session_get_state(core(h), &state);
    return static_cast<jint>(imago::android::writeState(state, out, static_cast<size_t>(capacity)));
}

JNI(jint, stateBufferSize)(JNIEnv*, jclass) {
    return static_cast<jint>(imago::android::kStateBufferSize);
}

JNI(jint, startRecording)(JNIEnv* env, jclass, jlong h, jstring path) {
    const std::string p = imago::android::toStdString(env, path);
    return imago_session_start_recording(core(h), p.c_str());
}

JNI(jint, stopRecording)(JNIEnv*, jclass, jlong h) { return imago_session_stop_recording(core(h)); }

JNI(void, setRecordingDetail)(JNIEnv*, jclass, jlong h, jboolean compact) {
    imago_session_set_recording_detail(core(h), compact ? IMAGO_RECORDING_COMPACT : IMAGO_RECORDING_FULL);
}

JNI(jlong, nowNs)(JNIEnv*, jclass) { return imago_now_ns(); }

// MARK: Gimbal

JNI(void, gimbalSetRate)(JNIEnv*, jclass, jlong h, jfloat pitch, jfloat yaw) {
    imago_gimbal_set_rate(core(h), pitch, yaw);
}

JNI(jint, gimbalSetAngle)(JNIEnv*, jclass, jlong h, jfloat pitch, jfloat yaw) {
    return static_cast<jint>(imago_gimbal_set_angle(core(h), pitch, yaw));
}

JNI(jint, gimbalSetMode)(JNIEnv*, jclass, jlong h, jint mode) {
    return static_cast<jint>(imago_gimbal_set_mode(core(h), static_cast<imago_gimbal_mode>(mode)));
}

JNI(jint, gimbalTakeControl)(JNIEnv*, jclass, jlong h, jboolean take) {
    return static_cast<jint>(imago_gimbal_take_control(core(h), take));
}

// MARK: Camera

JNI(jint, cameraSetMode)(JNIEnv*, jclass, jlong h, jint mode) {
    return static_cast<jint>(imago_camera_set_mode(core(h), static_cast<imago_camera_mode>(mode)));
}

JNI(jint, cameraTakePhoto)(JNIEnv*, jclass, jlong h) {
    return static_cast<jint>(imago_camera_take_photo(core(h)));
}

JNI(jint, cameraSetRecording)(JNIEnv*, jclass, jlong h, jboolean on) {
    return static_cast<jint>(imago_camera_set_recording(core(h), on));
}

JNI(jint, cameraZoom)(JNIEnv*, jclass, jlong h, jint direction) {
    return static_cast<jint>(imago_camera_zoom(core(h), direction));
}

JNI(void, cameraZoomTarget)(JNIEnv*, jclass, jlong h, jfloat level) {
    imago_camera_zoom_target(core(h), level);
}

JNI(jint, cameraSetView)(JNIEnv*, jclass, jlong h, jint source) {
    return static_cast<jint>(imago_camera_set_view(core(h), static_cast<imago_view_source>(source)));
}

JNI(jint, cameraSetParam)(JNIEnv* env, jclass, jlong h, jstring id, jint value) {
    const std::string param = imago::android::toStdString(env, id);
    return static_cast<jint>(imago_camera_set_param(core(h), param.c_str(), static_cast<uint32_t>(value)));
}

JNI(jint, cameraReadParam)(JNIEnv* env, jclass, jlong h, jstring id) {
    const std::string param = imago::android::toStdString(env, id);
    return imago_camera_read_param(core(h), param.c_str());
}

JNI(jfloatArray, zoomLevels)(JNIEnv* env, jclass, jint mode) {
    float levels[64];
    size_t count = imago_zoom_levels(mode, levels, 64);
    count = count < 64 ? count : 64;
    jfloatArray out = env->NewFloatArray(static_cast<jsize>(count));
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(count), levels);
    return out;
}

JNI(jint, streamSetBitrate)(JNIEnv*, jclass, jlong h, jint camera, jint bitsPerSecond) {
    return static_cast<jint>(imago_stream_set_bitrate(core(h), static_cast<uint32_t>(camera),
                                                      static_cast<uint32_t>(bitsPerSecond)));
}

// MARK: IR camera

JNI(jstring, irPaletteName)(JNIEnv* env, jclass, jint cameraType, jint index) {
    return env->NewStringUTF(imago_ir_palette_name(cameraType, static_cast<uint32_t>(index)));
}

JNI(jintArray, irPaletteColors)(JNIEnv* env, jclass, jint cameraType, jint index) {
    uint32_t colors[8];
    size_t count = imago_ir_palette_colors(cameraType, static_cast<uint32_t>(index), colors, 8);
    count = count < 8 ? count : 8;
    jint values[8];
    for (size_t k = 0; k < count; ++k) {
        values[k] = static_cast<jint>(colors[k]);
    }
    jintArray out = env->NewIntArray(static_cast<jsize>(count));
    env->SetIntArrayRegion(out, 0, static_cast<jsize>(count), values);
    return out;
}

JNI(jint, irSetIsotherm)(JNIEnv*, jclass, jlong h, jboolean enabled, jfloat lowC, jfloat highC) {
    return static_cast<jint>(imago_ir_set_isotherm(core(h), enabled, lowC, highC));
}

// MARK: Web settings

JNI(jint, webReadIRSettings)(JNIEnv*, jclass, jlong h) {
    return static_cast<jint>(imago_web_read_ir_settings(core(h)));
}

JNI(jint, webSetIsotherm)(JNIEnv*, jclass, jlong h, jboolean enabled, jint thresholdC, jint measurementRange) {
    return static_cast<jint>(imago_web_set_isotherm(core(h), enabled, thresholdC, measurementRange));
}

JNI(jint, webRestart)(JNIEnv*, jclass, jlong h, jboolean fullReboot) {
    return static_cast<jint>(imago_web_restart(core(h), fullReboot));
}

/// valid, isotherm enabled, threshold °C, measurement range
JNI(void, webIRSettings)(JNIEnv* env, jclass, jlong h, jintArray out) {
    imago_web_ir_settings settings;
    imago_web_get_ir_settings(core(h), &settings);
    const jint values[4] = {settings.valid ? 1 : 0, settings.isotherm_enabled ? 1 : 0,
                            settings.isotherm_threshold_c, settings.measurement_range};
    env->SetIntArrayRegion(out, 0, 4, values);
}

// MARK: Tracking

JNI(jint, trackerSetFunction)(JNIEnv*, jclass, jlong h, jint function) {
    return static_cast<jint>(
        imago_tracker_set_function(core(h), static_cast<imago_tracker_function>(function)));
}

JNI(jint, trackingStartAt)(JNIEnv*, jclass, jlong h, jfloat x, jfloat y, jfloat w, jfloat hgt) {
    return static_cast<jint>(imago_tracking_start_at(core(h), x, y, w, hgt));
}

JNI(jint, trackingPointAt)(JNIEnv*, jclass, jlong h, jfloat x, jfloat y) {
    return static_cast<jint>(imago_tracking_point_at(core(h), x, y));
}

JNI(jint, trackingStop)(JNIEnv*, jclass, jlong h) {
    return static_cast<jint>(imago_tracking_stop(core(h)));
}
