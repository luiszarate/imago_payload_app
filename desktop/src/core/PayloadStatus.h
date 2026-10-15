#pragma once

#include <imago/imago_core.h>

#include <QList>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Views of the core's state snapshot, split by topic so each part of the UI only updates when
// its own part changes. QML reads them as value types: App.gimbal.pitch.

/// The core's enumerations, for QML: Payload.GimbalLock, Payload.TrackingLost...
namespace Payload {
Q_NAMESPACE
QML_ELEMENT

enum GimbalMode {
    GimbalModeUnknown = IMAGO_GIMBAL_MODE_UNKNOWN,
    GimbalOff = IMAGO_GIMBAL_MODE_OFF,
    GimbalLock = IMAGO_GIMBAL_MODE_LOCK,
    GimbalFollow = IMAGO_GIMBAL_MODE_FOLLOW,
    GimbalMapping = IMAGO_GIMBAL_MODE_MAPPING,
    GimbalReset = IMAGO_GIMBAL_MODE_RESET,
};
Q_ENUM_NS(GimbalMode)

enum CameraMode { CameraModeUnknown = -1, PhotoMode = IMAGO_CAMERA_MODE_PHOTO, VideoMode = IMAGO_CAMERA_MODE_VIDEO };
Q_ENUM_NS(CameraMode)

enum TrackingState {
    TrackingIdle = IMAGO_TRACKING_IDLE,
    TrackingTracked = IMAGO_TRACKING_TRACKED,
    TrackingLost = IMAGO_TRACKING_LOST,
};
Q_ENUM_NS(TrackingState)

enum GimbalControl {
    ControlUnknown = IMAGO_GIMBAL_CONTROL_UNKNOWN,
    ControlThisApp = IMAGO_GIMBAL_CONTROL_THIS_APP,
    ControlOther = IMAGO_GIMBAL_CONTROL_OTHER,
    ControlNobody = IMAGO_GIMBAL_CONTROL_NOBODY,
};
Q_ENUM_NS(GimbalControl)

/// "Lock", "EO + IR"... empty for unknown values.
QString gimbalModeTitle(int mode);
QString viewSourceTitle(int source);
/// The view sources in menu order.
QList<int> viewSources();
} // namespace Payload

struct LinkStatus {
    Q_GADGET
    QML_VALUE_TYPE(linkStatus)
    Q_PROPERTY(bool up MEMBER up)
    Q_PROPERTY(QString peer MEMBER peer)
    Q_PROPERTY(int roundTripMs MEMBER roundTripMs)

  public:
    bool up = false;
    QString peer;
    int roundTripMs = 0;
    int cameraComponent = 0;
    int gimbalComponent = 0;
    bool operator==(const LinkStatus&) const = default;
};

struct GimbalStatus {
    Q_GADGET
    QML_VALUE_TYPE(gimbalStatus)
    Q_PROPERTY(bool valid MEMBER valid)
    Q_PROPERTY(float pitch MEMBER pitch)
    Q_PROPERTY(bool hasYaw MEMBER hasYaw)
    Q_PROPERTY(float yaw MEMBER yaw)
    Q_PROPERTY(bool hasYawNorth MEMBER hasYawNorth)
    Q_PROPERTY(float yawNorth MEMBER yawNorth)
    Q_PROPERTY(int mode MEMBER mode)
    Q_PROPERTY(QString modeTitle MEMBER modeTitle)
    Q_PROPERTY(int control MEMBER control)
    Q_PROPERTY(QString controlHolder MEMBER controlHolder)
    Q_PROPERTY(bool hasPan MEMBER hasPan)
    Q_PROPERTY(float pan MEMBER pan)
    Q_PROPERTY(bool panNearLimit MEMBER panNearLimit)

  public:
    bool valid = false;
    /// Degrees, rounded to 0.1. Relative to the horizon, positive up.
    float pitch = 0.0f;
    /// Relative to the aircraft's nose, positive right.
    bool hasYaw = false;
    float yaw = 0.0f;
    /// Relative to North, 0..<360.
    bool hasYawNorth = false;
    float yawNorth = 0.0f;
    int mode = Payload::GimbalModeUnknown;
    QString modeTitle;
    /// Who holds the autopilot's gimbal manager (only with that gimbal protocol).
    int control = Payload::ControlUnknown;
    int controlSystem = 0;
    int controlComponent = 0;
    /// "Autopilot", "Ground station"... for the rail.
    QString controlHolder;
    /// Yaw followed across ±180: how far along its ±320° travel the gimbal is. Not valid until
    /// the core has seen the gimbal turn.
    bool hasPan = false;
    float pan = 0.0f;
    bool panNearLimit = false;
    bool operator==(const GimbalStatus&) const = default;
};

struct CameraStatus {
    Q_GADGET
    QML_VALUE_TYPE(cameraStatus)
    Q_PROPERTY(int view MEMBER view)
    Q_PROPERTY(QString viewTitle MEMBER viewTitle)
    Q_PROPERTY(int mode MEMBER mode)
    Q_PROPERTY(float zoom MEMBER zoom)
    Q_PROPERTY(float zoomMax MEMBER zoomMax)
    Q_PROPERTY(bool recording MEMBER recording)
    Q_PROPERTY(int recordingSeconds MEMBER recordingSeconds)
    Q_PROPERTY(bool hasStorage MEMBER hasStorage)
    Q_PROPERTY(float storageAvailableGB MEMBER storageAvailableGB)
    Q_PROPERTY(bool hasLaserRange MEMBER hasLaserRange)
    Q_PROPERTY(float laserRangeMeters MEMBER laserRangeMeters)
    Q_PROPERTY(float horizontalFov MEMBER horizontalFov)

  public:
    int view = -1;
    QString viewTitle;
    int mode = Payload::CameraModeUnknown;
    /// EO zoom, rounded to 0.1; 0 unknown.
    float zoom = 0.0f;
    /// Top of the EO zoom range in the camera's zoom mode; 0 until reported.
    float zoomMax = 0.0f;
    bool recording = false;
    int recordingSeconds = 0;
    bool hasStorage = false;
    float storageAvailableGB = 0.0f;
    bool hasLaserRange = false;
    float laserRangeMeters = 0.0f;
    /// Whole degrees; 0 unknown.
    float horizontalFov = 0.0f;
    bool operator==(const CameraStatus&) const = default;
};

/// One AI detection, normalized image coordinates.
struct Detection {
    QRectF rect;
    int classId = 0;
    int trackId = 0;
    float confidence = 0.0f;
    QString label() const;
    bool operator==(const Detection&) const = default;
};

struct TrackingStatus {
    Q_GADGET
    QML_VALUE_TYPE(trackingStatus)
    Q_PROPERTY(int state MEMBER state)
    Q_PROPERTY(bool hasBox MEMBER hasBox)
    Q_PROPERTY(QRectF box MEMBER box)
    /// For QML: {x, y, w, h, classId, label, confidence} per detection.
    Q_PROPERTY(QVariantList detections READ detectionList)

  public:
    int state = Payload::TrackingIdle;
    bool hasBox = false;
    QRectF box;
    QList<Detection> detections;

    QVariantList detectionList() const;
    /// The detection a click at a normalized point means: the smallest box around it, with a
    /// little slack for boxes that trail a moving object. nullptr when none is near.
    const Detection* hit(double x, double y, double slack = 0.02) const;
    bool operator==(const TrackingStatus& other) const {
        return state == other.state && hasBox == other.hasBox && box == other.box &&
               detections == other.detections;
    }
};

struct VideoMetrics {
    Q_GADGET
    QML_VALUE_TYPE(videoMetrics)
    Q_PROPERTY(bool active MEMBER active)
    Q_PROPERTY(QString codec MEMBER codec)
    Q_PROPERTY(QString transport MEMBER transport)
    Q_PROPERTY(QString decoder MEMBER decoder)
    Q_PROPERTY(float fps MEMBER fps)
    Q_PROPERTY(float kbps MEMBER kbps)
    Q_PROPERTY(int packetsLost MEMBER packetsLost)
    Q_PROPERTY(int packetsLate MEMBER packetsLate)
    Q_PROPERTY(float jitterMs MEMBER jitterMs)
    Q_PROPERTY(float transferMs MEMBER transferMs)
    Q_PROPERTY(float assemblyMs MEMBER assemblyMs)
    Q_PROPERTY(float decodeMs MEMBER decodeMs)
    Q_PROPERTY(float presentMs MEMBER presentMs)
    Q_PROPERTY(int mavlinkGaps MEMBER mavlinkGaps)
    Q_PROPERTY(int mavlinkReceived MEMBER mavlinkReceived)
    Q_PROPERTY(float roundTripLastMs MEMBER roundTripLastMs)
    Q_PROPERTY(float lagMs MEMBER lagMs)
    Q_PROPERTY(int resyncs MEMBER resyncs)
    Q_PROPERTY(int framesCorrupt MEMBER framesCorrupt)
    Q_PROPERTY(int decodeFailures MEMBER decodeFailures)
    Q_PROPERTY(int framesSkipped MEMBER framesSkipped)
    Q_PROPERTY(float frameGapP95Ms MEMBER frameGapP95Ms)
    Q_PROPERTY(float frameGapMaxMs MEMBER frameGapMaxMs)

  public:
    bool active = false;
    QString codec = QStringLiteral("—");
    QString transport = QStringLiteral("—");
    /// "D3D11VA", "VAAPI", "Software"...
    QString decoder = QStringLiteral("—");
    float fps = 0.0f;
    float kbps = 0.0f;
    int packetsLost = 0;
    int packetsLate = 0;
    float jitterMs = 0.0f;
    float transferMs = 0.0f;
    float assemblyMs = 0.0f;
    float decodeMs = 0.0f;
    float presentMs = 0.0f;
    int mavlinkGaps = 0;
    int mavlinkReceived = 0;
    float roundTripLastMs = 0.0f;
    float lagMs = 0.0f;
    int resyncs = 0;
    /// Frames that arrived with packets missing (core).
    int framesCorrupt = 0;
    /// Frames the decoder rejected, and pictures skipped to show the newest.
    int decodeFailures = 0;
    int framesSkipped = 0;
    /// Time between frames arriving, last 2 s: p95 and max (one period on a smooth link).
    float frameGapP95Ms = 0.0f;
    float frameGapMaxMs = 0.0f;
    bool operator==(const VideoMetrics&) const = default;
};

/// The IR camera, from the payload's data stream.
struct ThermalStatus {
    Q_GADGET
    QML_VALUE_TYPE(thermalStatus)
    Q_PROPERTY(bool hasTemperatures MEMBER hasTemperatures)
    Q_PROPERTY(float minC MEMBER minC)
    Q_PROPERTY(float maxC MEMBER maxC)
    Q_PROPERTY(float meanC MEMBER meanC)
    Q_PROPERTY(int palette MEMBER palette)
    Q_PROPERTY(int cameraType MEMBER cameraType)
    Q_PROPERTY(int ffcMode MEMBER ffcMode)
    Q_PROPERTY(int zoom MEMBER zoom)

  public:
    /// Coldest, hottest and mean temperature in the IR picture, °C (0.1 steps), once reported.
    bool hasTemperatures = false;
    float minC = 0.0f;
    float maxC = 0.0f;
    float meanC = 0.0f;
    /// C_T_PALETTE index; -1 until reported.
    int palette = -1;
    /// Raw sensor type: decides the palette names (see Thermal::paletteName). -1 until reported.
    int cameraType = -1;
    /// Flat-field correction: 1 on its own, 0 only when asked, -1 until reported.
    int ffcMode = -1;
    /// Digital zoom, 1..8; 0 until reported.
    int zoom = 0;
    bool operator==(const ThermalStatus&) const = default;
};

struct PayloadInfo {
    Q_GADGET
    QML_VALUE_TYPE(payloadInfo)
    Q_PROPERTY(QString model MEMBER model)
    Q_PROPERTY(QString softwareVersion MEMBER softwareVersion)
    Q_PROPERTY(QString serialNumber MEMBER serialNumber)

  public:
    QString model;
    QString softwareVersion;
    QString serialNumber;
    bool operator==(const PayloadInfo&) const = default;
};

/// The parts of one imago_state snapshot.
namespace Snapshot {
LinkStatus link(const imago_state& s);
GimbalStatus gimbal(const imago_state& s);
CameraStatus camera(const imago_state& s);
ThermalStatus thermal(const imago_state& s);
TrackingStatus tracking(const imago_state& s);
VideoMetrics metrics(const imago_state& s);
PayloadInfo info(const imago_state& s);
/// Aircraft heading from North, whole degrees (it changes constantly in flight); -1 unknown.
float vehicleHeading(const imago_state& s);
/// Standing video lag in 50 ms steps, so the warning only redraws on real changes.
int videoLagSteps(const imago_state& s);
} // namespace Snapshot
