#pragma once

#include "core/ConnectionProfile.h"

#include <imago/imago_core.h>

#include <QRectF>
#include <QString>

#include <functional>
#include <memory>

class VideoDecoder;

/// Something the payload or the video link reported (imago_event), on a core thread.
struct PayloadEvent {
    imago_event_type type = IMAGO_EVENT_STATUS_TEXT;
    uint32_t requestId = 0;
    uint32_t code = 0;
    int32_t result = 0;
    QString text;
};

/// One connection to a payload, wrapping an imago_session.
///
/// Every command may be called from any thread. The core encodes and sends it on the calling
/// thread immediately, so the joystick and the gamepad never wait for anything. Commands return
/// the request ID reported later in a result event, or 0 if nothing was sent.
class PayloadSession {
  public:
    using EventHandler = std::function<void(const PayloadEvent&)>;

    /// nullptr if the profile cannot make a session (invalid address). Events arrive on core
    /// threads; video frames go to the decoder, which must outlive the session.
    static std::unique_ptr<PayloadSession> create(const ConnectionProfile& profile, VideoDecoder* video,
                                                  EventHandler onEvent);
    ~PayloadSession();

    PayloadSession(const PayloadSession&) = delete;
    PayloadSession& operator=(const PayloadSession&) = delete;

    const ConnectionProfile& profile() const { return profile_; }

    /// Opens the sockets and starts the core. false if the local port is taken.
    bool start();
    /// Stops the core threads. No callback runs after this returns.
    void stop();

    imago_state state() const;

    // Gimbal
    void setGimbalRate(float pitch, float yaw);
    uint32_t setGimbalMode(int mode);
    /// Takes or gives back primary control of the autopilot's gimbal manager.
    uint32_t takeGimbalControl(bool take);

    // Camera
    uint32_t setCameraMode(int mode);
    uint32_t takePhoto();
    uint32_t setRecording(bool on);
    /// -1 zoom out, 0 stop, 1 zoom in.
    uint32_t zoom(int direction);
    /// Exact level from a dragged control: paced and not tracked (imago_camera_zoom_target).
    void zoomTarget(float level);
    uint32_t setView(int source);
    uint32_t setCameraParam(const QString& id, int value);
    /// Asks for a setting's current value; it arrives as a CAMERA_PARAM event.
    void readCameraParam(const QString& id);
    // IR camera
    /// Colours the band low..high °C over the palette; disabled turns the isotherm off. The ID
    /// is the isotherm switch's, sent last (see imago_ir_set_isotherm).
    uint32_t setIsotherm(bool enabled, int lowC = 0, int highC = 0);

    /// Bitrate of a camera's stream, bit/s. (Unverified on the Orus L: which camera is EO.)
    uint32_t setStreamBitrate(int cameraId, int bitsPerSecond);

    /// Records all MAVLink in and out to a .tlog until stopRecording() or the session ends.
    bool startRecording(const QString& path);
    /// false if part of the log could not be written (full disk).
    bool stopRecording();
    /// Compact: the payload's side only, streams once a second, no app measurements (see
    /// imago_recording_detail). Takes effect at once.
    void setRecordingDetail(bool compact);

    // Web settings
    /// Reads the payload's web IR page; the values arrive with a WEB_RESULT event, then
    /// webIRSettings().
    uint32_t readWebIRSettings();
    /// Saves the isotherm on the web page (it shows at once), then reads back.
    uint32_t setWebIsotherm(bool enabled, int thresholdC, int measurementRange);
    /// Restarts the payload's camera software (its VIO service) through the web page: video,
    /// tracking and control drop for a while and come back by themselves.
    uint32_t restartPayloadService();
    imago_web_ir_settings webIRSettings() const;

    // Tracking (normalized image coordinates, (0, 0) top-left)
    uint32_t track(float x, float y);
    /// Locks onto an object with its own box, e.g. an AI detection.
    uint32_t track(const QRectF& box);
    uint32_t pointGimbal(float x, float y);
    uint32_t stopTracking();
    uint32_t setDetection(bool enabled);

    /// The levels the camera steps through in a zoom mode (C_V_ZM_CB_LV / C_V_ZM_SR_LV).
    static QList<float> zoomLevels(bool superResolution);

  private:
    PayloadSession(const ConnectionProfile& profile, VideoDecoder* video, EventHandler onEvent);
    static void eventCallback(const imago_event* event, void* user);
    static void frameCallback(const imago_video_frame* frame, void* user);

    ConnectionProfile profile_;
    VideoDecoder* video_;
    EventHandler onEvent_;
    imago_session* session_ = nullptr;
};
