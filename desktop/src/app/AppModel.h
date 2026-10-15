#pragma once

#include "control/GamepadController.h"
#include "control/GamepadMapping.h"
#include "control/GimbalInputShaper.h"
#include "core/ConnectionProfile.h"
#include "core/PayloadSession.h"
#include "core/PayloadStatus.h"
#include "video/VideoDecoder.h"

#include <QElapsedTimer>
#include <QQmlEngine>
#include <QHash>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class QQuickWindow;

/// App state and actions, on the main thread, for QML (the App singleton). Each part of the
/// payload state is updated only when it changes, so only the bindings that read it re-evaluate.
/// Gimbal rates from the joystick or a gamepad go straight to the session, which sends them on
/// the calling thread.
class AppModel : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    // Connection
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString selectedProfileId READ selectedProfileId NOTIFY profilesChanged)
    Q_PROPERTY(QVariantMap selectedProfile READ selectedProfile NOTIFY profilesChanged)
    Q_PROPERTY(QVariantMap connectedProfile READ connectedProfile NOTIFY connectionChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(bool videoActive READ videoActive NOTIFY videoStateChanged)
    Q_PROPERTY(QString videoMessage READ videoMessage NOTIFY videoStateChanged)
    Q_PROPERTY(bool videoResyncing READ videoResyncing NOTIFY videoStateChanged)
    Q_PROPERTY(QString connectError READ connectError NOTIFY connectErrorChanged)

    // Payload state
    Q_PROPERTY(LinkStatus link READ link NOTIFY linkChanged)
    Q_PROPERTY(GimbalStatus gimbal READ gimbal NOTIFY gimbalChanged)
    Q_PROPERTY(CameraStatus camera READ camera NOTIFY cameraChanged)
    Q_PROPERTY(TrackingStatus tracking READ tracking NOTIFY trackingChanged)
    Q_PROPERTY(VideoMetrics metrics READ metrics NOTIFY metricsChanged)
    Q_PROPERTY(PayloadInfo info READ info NOTIFY infoChanged)
    /// Aircraft heading from North, when the gimbal or the autopilot reports it; -1 unknown.
    Q_PROPERTY(float vehicleHeading READ vehicleHeading NOTIFY vehicleHeadingChanged)
    /// How far behind live the picture is (a queue upstream), in 50 ms steps. 0 = live.
    Q_PROPERTY(int videoLagMs READ videoLagMs NOTIFY videoLagChanged)
    /// Picture size in pixels; 0 until the decoder reports it.
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY videoSizeChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoSizeChanged)

    // UI state
    Q_PROPERTY(bool showHud MEMBER showHud_ NOTIFY uiChanged)
    /// Gimbal attitude instrument at the bottom of the video. Remembered across launches.
    Q_PROPERTY(bool showAttitude READ showAttitude NOTIFY uiChanged)
    Q_PROPERTY(bool showConnections MEMBER showConnections_ NOTIFY uiChanged)
    Q_PROPERTY(bool showSettings MEMBER showSettings_ NOTIFY uiChanged)
    /// When on, a click on the video re-points the gimbal (Eagle Eye) instead of tracking.
    Q_PROPERTY(bool pointMode MEMBER pointMode_ NOTIFY uiChanged)
    Q_PROPERTY(bool detectionEnabled READ detectionEnabled NOTIFY uiChanged)
    /// Photo or video, as chosen on the camera rail. The payload confirms it in camera.mode.
    Q_PROPERTY(int captureMode READ captureMode NOTIFY uiChanged)
    Q_PROPERTY(QString gamepadName READ gamepadName NOTIFY gamepadChanged)
    /// {id, text, error} or empty.
    Q_PROPERTY(QVariantMap toast READ toast NOTIFY toastChanged)
    /// Zoom direction currently commanded, so the slider lets go when a continuous zoom takes over.
    Q_PROPERTY(int zoomDirection READ zoomDirection NOTIFY zoomDirectionChanged)
    Q_PROPERTY(bool usesGimbalManager READ usesGimbalManager NOTIFY connectionChanged)

    // Settings
    /// Camera and gimbal settings as the payload last reported them (param_id -> value).
    Q_PROPERTY(QVariantMap cameraValues READ cameraValues NOTIFY cameraValuesChanged)
    Q_PROPERTY(QVariantMap gimbalTuning READ gimbalTuning NOTIFY gimbalTuningChanged)
    /// [{key, title, label, bound, inverted}] per axis function and per button action.
    Q_PROPERTY(QVariantList gamepadAxes READ gamepadAxes NOTIFY gamepadMappingChanged)
    Q_PROPERTY(QVariantList gamepadButtons READ gamepadButtons NOTIFY gamepadMappingChanged)
    /// "axis:gimbalYaw", "action:shutter"... while waiting for a controller input; else empty.
    Q_PROPERTY(QString gamepadLearning READ gamepadLearning NOTIFY gamepadMappingChanged)

    // Diagnostics
    Q_PROPERTY(bool isRecordingLog READ isRecordingLog NOTIFY logChanged)
    Q_PROPERTY(QString lastLogFile READ lastLogFile NOTIFY logChanged)
    /// Commands the payload acknowledged to someone else (autopilot, GCS) this session.
    Q_PROPERTY(int foreignCommandCount READ foreignCommandCount NOTIFY logChanged)

    // Fixed lists for menus
    Q_PROPERTY(QVariantList viewSources READ viewSources CONSTANT)
    Q_PROPERTY(QVariantList gimbalModes READ gimbalModes CONSTANT)
    Q_PROPERTY(QStringList settingsPages READ settingsPages CONSTANT)
    Q_PROPERTY(QVariantList bitrates READ bitrates CONSTANT)
    Q_PROPERTY(QVariantList videoLagChoices READ videoLagChoices CONSTANT)
    Q_PROPERTY(QVariantList smoothingChoices READ smoothingChoices CONSTANT)
    Q_PROPERTY(float speedMin READ speedMin CONSTANT)
    Q_PROPERTY(float speedMax READ speedMax CONSTANT)
    Q_PROPERTY(float deadzoneMax READ deadzoneMax CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

  public:
    /// Not default-constructible, so QML takes the instance from create() instead of making one.
    explicit AppModel(QObject* parent);
    ~AppModel() override;

    /// QML gets the one instance main() creates.
    static AppModel* create(QQmlEngine* qml, QJSEngine* js);
    static void setInstance(AppModel* model);

    /// The picture goes to this window's VideoOutput; its swaps time the presentation.
    void attachWindow(QQuickWindow* window);

    /// The current picture, for a still or a check of the video path.
    QImage videoSnapshot() { return video_.snapshot(); }

    /// The picture size goes to QML, the frames to this sink.
    Q_INVOKABLE void setVideoSink(QObject* sink);

    // Connect / disconnect
    Q_INVOKABLE void connectToPayload();
    Q_INVOKABLE void disconnectFromPayload();

    // Gimbal
    /// Joystick deflection: x right, y up, both -1..1.
    Q_INVOKABLE void joystickMoved(double x, double y);
    Q_INVOKABLE void setGimbalMode(int mode);
    /// With the autopilot's gimbal manager: take control, or give it back to the pilot / GCS.
    Q_INVOKABLE void toggleGimbalControl();
    Q_INVOKABLE void recenterGimbal();

    // Camera
    Q_INVOKABLE void chooseCaptureMode(int mode);
    /// Shutter: a photo in photo mode, start/stop recording in video mode.
    Q_INVOKABLE void shutter();
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void zoom(int direction);
    /// Exact level from the zoom slider, on every change while it is dragged. The core paces it.
    Q_INVOKABLE void zoomTo(double level);
    /// The levels the camera can stop at in the current zoom range.
    Q_INVOKABLE QVariantList zoomLevels(bool superResolution) const;
    Q_INVOKABLE void setView(int source);
    Q_INVOKABLE void cycleView(int step);

    // Tracking
    /// Click on the video at a normalized point.
    Q_INVOKABLE void videoTapped(double x, double y);
    Q_INVOKABLE void stopTracking();
    Q_INVOKABLE void toggleDetection();

    // Profiles
    Q_INVOKABLE void selectProfile(const QString& id);
    Q_INVOKABLE QVariantMap newProfile() const;
    /// The problem with a profile being edited, or an empty string.
    Q_INVOKABLE QString validateProfile(const QVariantMap& profile) const;
    Q_INVOKABLE void saveProfile(const QVariantMap& profile);
    Q_INVOKABLE void deleteProfile(const QString& id);

    // Settings
    Q_INVOKABLE void toggleAttitude();
    /// Asks the payload for each setting of a settings page; values arrive in cameraValues.
    Q_INVOKABLE void readSettings(int page, bool onlyMissing);
    Q_INVOKABLE QVariantList settingsSections(int page) const;
    /// The payload confirms the new value, which then shows up in cameraValues.
    Q_INVOKABLE void setCameraSetting(const QString& id, const QString& title, int value);
    /// Lower bitrates survive a weak link with fewer lost packets (frozen picture).
    Q_INVOKABLE void setStreamBitrate(int bitsPerSecond);
    Q_INVOKABLE void setGimbalTuning(const QString& field, double value);
    Q_INVOKABLE void resetGimbalTuning();

    // Gamepad mapping
    /// Waits for the next button press or axis movement on the controller; empty cancels.
    Q_INVOKABLE void learnGamepadInput(const QString& target);
    Q_INVOKABLE void clearGamepadAxis(const QString& function);
    Q_INVOKABLE void clearGamepadButton(const QString& action);
    Q_INVOKABLE void setGamepadAxisInverted(const QString& function, bool inverted);
    Q_INVOKABLE void resetGamepadMapping();

    // Diagnostics
    /// Starts or stops recording all MAVLink to a .tlog (QGroundControl, MAVExplorer, pymavlink).
    Q_INVOKABLE void toggleLogRecording();
    /// Opens the folder with the logs in the file manager.
    Q_INVOKABLE void showLogFolder();

    // Property readers
    QVariantList profiles() const;
    QString selectedProfileId() const { return selectedProfileId_; }
    QVariantMap selectedProfile() const;
    QVariantMap connectedProfile() const;
    bool isConnected() const { return session_ != nullptr; }
    bool videoActive() const { return videoActive_; }
    QString videoMessage() const { return videoMessage_; }
    bool videoResyncing() const { return videoResyncing_; }
    QString connectError() const { return connectError_; }
    LinkStatus link() const { return link_; }
    GimbalStatus gimbal() const { return gimbal_; }
    CameraStatus camera() const { return camera_; }
    TrackingStatus tracking() const { return tracking_; }
    VideoMetrics metrics() const { return metrics_; }
    PayloadInfo info() const { return info_; }
    float vehicleHeading() const { return vehicleHeading_; }
    int videoLagMs() const { return videoLagMs_; }
    int videoWidth() const { return videoWidth_; }
    int videoHeight() const { return videoHeight_; }
    bool showAttitude() const { return showAttitude_; }
    bool detectionEnabled() const { return detectionEnabled_; }
    int captureMode() const { return captureMode_; }
    QString gamepadName() const { return gamepadName_; }
    QVariantMap toast() const { return toast_; }
    int zoomDirection() const { return zoomDirection_; }
    bool usesGimbalManager() const;
    QVariantMap cameraValues() const { return cameraValues_; }
    QVariantMap gimbalTuning() const { return gimbalTuning_.toVariant(); }
    QVariantList gamepadAxes() const;
    QVariantList gamepadButtons() const;
    QString gamepadLearning() const { return gamepadLearning_ ? gamepadLearning_->key() : QString(); }
    bool isRecordingLog() const { return isRecordingLog_; }
    QString lastLogFile() const { return lastLogFile_; }
    int foreignCommandCount() const { return foreignCommandCount_; }
    QVariantList viewSources() const;
    QVariantList gimbalModes() const;
    QStringList settingsPages() const;
    QVariantList bitrates() const;
    QVariantList videoLagChoices() const;
    QVariantList smoothingChoices() const;
    float speedMin() const { return GimbalInputShaper::kMinSpeed; }
    float speedMax() const { return GimbalInputShaper::kMaxSpeed; }
    float deadzoneMax() const { return GimbalInputShaper::kMaxDeadzone; }
    QString version() const;

    /// The picture is flagged as stale from this lag on.
    static constexpr int kLagWarningMs = 250;

  signals:
    void profilesChanged();
    void connectionChanged();
    void videoStateChanged();
    void connectErrorChanged();
    void linkChanged();
    void gimbalChanged();
    void cameraChanged();
    void trackingChanged();
    void metricsChanged();
    void infoChanged();
    void vehicleHeadingChanged();
    void videoLagChanged();
    void videoSizeChanged();
    void uiChanged();
    void gamepadChanged();
    void toastChanged();
    void zoomDirectionChanged();
    void cameraValuesChanged();
    void gimbalTuningChanged();
    void gamepadMappingChanged();
    void logChanged();

  private:
    void handle(const PayloadEvent& event);
    void poll();
    void takeGimbalControlOnce();
    void registerRequest(uint32_t requestId, const QString& label);
    void finish(uint32_t requestId, int32_t result, const QString& fallback);
    void show(const QString& text, bool error);
    void handleGamepadAction(GamepadAction action);
    void gamepadLearned(const GamepadLearnTarget& target, const QString& input, bool inverted);
    void updateGamepadMapping(const GamepadMapping& mapping);
    void updateGimbalTuning(const GimbalInputShaper& tuning);
    void foreignCommandSeen(int command, const QString& from);
    void resetPayloadState();
    void setConnectError(const QString& error);
    QString logsDirectory() const;

    QSettings settings_;
    ProfileStore store_;
    QList<ConnectionProfile> profiles_;
    QString selectedProfileId_;
    std::unique_ptr<PayloadSession> session_;
    VideoDecoder video_;
    GamepadController gamepad_;
    QTimer pollTimer_;
    QTimer toastTimer_;
    /// Incremented on every disconnect; events from an older session are ignored.
    int generation_ = 0;

    bool videoActive_ = false;
    QString videoMessage_;
    bool videoResyncing_ = false;
    QString connectError_;
    LinkStatus link_;
    GimbalStatus gimbal_;
    CameraStatus camera_;
    TrackingStatus tracking_;
    VideoMetrics metrics_;
    PayloadInfo info_;
    float vehicleHeading_ = -1.0f;
    int videoLagMs_ = 0;
    int videoWidth_ = 0;
    int videoHeight_ = 0;
    QElapsedTimer sinceResync_;

    bool showHud_ = false;
    bool showAttitude_ = true;
    bool showConnections_ = false;
    bool showSettings_ = false;
    bool pointMode_ = false;
    bool detectionEnabled_ = false;
    int captureMode_ = Payload::PhotoMode;
    QString gamepadName_;
    QVariantMap toast_;
    int toastCounter_ = 0;
    int zoomDirection_ = 0;

    QVariantMap cameraValues_;
    GimbalInputShaper gimbalTuning_;
    GamepadMapping gamepadMapping_;
    std::optional<GamepadLearnTarget> gamepadLearning_;
    QHash<uint32_t, QString> pendingLabels_;
    /// The gimbal manager was heard this connection and the automatic take is settled.
    bool gimbalControlSettled_ = false;

    bool isRecordingLog_ = false;
    QString lastLogFile_;
    int foreignCommandCount_ = 0;
    QElapsedTimer sinceForeignToast_;
};
