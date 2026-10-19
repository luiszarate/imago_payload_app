#include "core/PayloadStatus.h"

#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

float tenths(float value) { return std::round(value * 10.0f) / 10.0f; }

QString text(const char* chars, size_t capacity) {
    return QString::fromUtf8(chars, static_cast<qsizetype>(strnlen(chars, capacity)));
}

/// Class names of the COCO-80 model the Orus L runs on the EO camera. The IR camera runs a
/// different model, so its class IDs are shown as numbers.
const char* const kCoco80[] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
    "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog",
    "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella",
    "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball", "kite",
    "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle",
    "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich",
    "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote",
    "keyboard", "cell phone", "microwave", "oven", "toaster", "sink", "refrigerator", "book",
    "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush",
};

} // namespace

QString Payload::gimbalModeTitle(int mode) {
    switch (mode) {
    case GimbalOff:
        return QStringLiteral("Off");
    case GimbalLock:
        return QStringLiteral("Lock");
    case GimbalFollow:
        return QStringLiteral("Follow");
    case GimbalMapping:
        return QStringLiteral("Mapping");
    case GimbalReset:
        return QStringLiteral("Reset");
    default:
        return {};
    }
}

QString Payload::viewSourceTitle(int source) {
    switch (source) {
    case IMAGO_VIEW_EO_IR:
        return QStringLiteral("EO + IR");
    case IMAGO_VIEW_EO:
        return QStringLiteral("EO");
    case IMAGO_VIEW_IR:
        return QStringLiteral("IR");
    case IMAGO_VIEW_IR_EO:
        return QStringLiteral("IR + EO");
    case IMAGO_VIEW_SYNC:
        return QStringLiteral("Sync");
    case IMAGO_VIEW_SIDE_BY_SIDE:
        return QStringLiteral("Side by side");
    default:
        return {};
    }
}

QList<int> Payload::viewSources() {
    return {IMAGO_VIEW_EO_IR, IMAGO_VIEW_EO, IMAGO_VIEW_IR, IMAGO_VIEW_IR_EO, IMAGO_VIEW_SYNC,
            IMAGO_VIEW_SIDE_BY_SIDE};
}

QString Detection::label() const {
    constexpr int count = static_cast<int>(sizeof(kCoco80) / sizeof(kCoco80[0]));
    return classId >= 0 && classId < count ? QString::fromLatin1(kCoco80[classId])
                                           : QStringLiteral("class %1").arg(classId);
}

QVariantList TrackingStatus::detectionList() const {
    QVariantList list;
    for (const Detection& d : detections) {
        list.append(QVariantMap{
            {QStringLiteral("x"), d.rect.x()},
            {QStringLiteral("y"), d.rect.y()},
            {QStringLiteral("w"), d.rect.width()},
            {QStringLiteral("h"), d.rect.height()},
            {QStringLiteral("classId"), d.classId},
            {QStringLiteral("label"), d.label()},
            {QStringLiteral("confidence"), d.confidence},
        });
    }
    return list;
}

const Detection* TrackingStatus::hit(double x, double y, double slack) const {
    const Detection* best = nullptr;
    for (const Detection& d : detections) {
        const QRectF r = d.rect.adjusted(-slack, -slack, slack, slack);
        if (x >= r.left() && x <= r.right() && y >= r.top() && y <= r.bottom()) {
            if (best == nullptr || d.rect.width() * d.rect.height() < best->rect.width() * best->rect.height()) {
                best = &d;
            }
        }
    }
    return best;
}

// ----------------------------------------------------------------------------------------------

LinkStatus Snapshot::link(const imago_state& s) {
    LinkStatus l;
    l.up = s.link_up;
    l.peer = text(s.mavlink_peer, sizeof(s.mavlink_peer));
    l.roundTripMs = static_cast<int>(std::lround(s.command_rtt_ms_avg));
    l.cameraComponent = s.camera_component_id;
    l.gimbalComponent = s.gimbal_component_id;
    return l;
}

GimbalStatus Snapshot::gimbal(const imago_state& s) {
    GimbalStatus g;
    g.valid = s.gimbal_valid;
    g.pitch = tenths(s.gimbal_pitch_deg);
    g.hasYaw = s.gimbal_valid && s.gimbal_yaw_valid;
    g.yaw = g.hasYaw ? tenths(s.gimbal_yaw_deg) : 0.0f;
    g.hasYawNorth = s.gimbal_valid && s.gimbal_yaw_north_valid;
    g.yawNorth = g.hasYawNorth ? tenths(s.gimbal_yaw_north_deg) : 0.0f;
    g.mode = Payload::gimbalModeTitle(s.gimbal_mode).isEmpty() ? Payload::GimbalModeUnknown
                                                               : static_cast<int>(s.gimbal_mode);
    g.modeTitle = Payload::gimbalModeTitle(g.mode);
    g.control = s.gimbal_control;
    if (g.control == Payload::ControlOther) {
        g.controlSystem = s.gimbal_control_system;
        g.controlComponent = s.gimbal_control_component;
    }
    switch (g.control) {
    case Payload::ControlThisApp:
        g.controlHolder = QStringLiteral("This app");
        break;
    case Payload::ControlNobody:
        g.controlHolder = QStringLiteral("Nobody");
        break;
    case Payload::ControlOther:
        if (g.controlComponent == 1) {
            g.controlHolder = QStringLiteral("Autopilot");
        } else if (g.controlSystem == 255 || g.controlComponent == 190) {
            g.controlHolder = QStringLiteral("Ground station");
        } else {
            g.controlHolder = QStringLiteral("%1/%2").arg(g.controlSystem).arg(g.controlComponent);
        }
        break;
    default:
        g.control = Payload::ControlUnknown;
        g.controlHolder = QStringLiteral("No gimbal manager");
        break;
    }
    g.hasPan = s.gimbal_valid && s.gimbal_pan_valid;
    g.pan = g.hasPan ? tenths(s.gimbal_pan_deg) : 0.0f;
    g.panNearLimit = s.gimbal_valid && s.gimbal_pan_near_limit;
    return g;
}

CameraStatus Snapshot::camera(const imago_state& s) {
    CameraStatus c;
    c.viewTitle = Payload::viewSourceTitle(s.view_source);
    c.view = c.viewTitle.isEmpty() ? -1 : s.view_source;
    c.mode = s.camera_mode == IMAGO_CAMERA_MODE_PHOTO || s.camera_mode == IMAGO_CAMERA_MODE_VIDEO
                 ? s.camera_mode
                 : Payload::CameraModeUnknown;
    c.zoom = tenths(s.eo_zoom_level);
    c.zoomMax = s.eo_zoom_max > 0 ? s.eo_zoom_max : 0.0f;
    c.recording = s.recording;
    c.recordingSeconds = static_cast<int>(s.recording_time_ms / 1000);
    c.hasStorage = s.storage_valid;
    c.storageAvailableGB = s.storage_valid ? s.storage_available_mib / 1024.0f : 0.0f;
    c.hasLaserRange = s.lrf_valid;
    c.laserRangeMeters = s.lrf_valid ? s.lrf_range_m : 0.0f;
    c.horizontalFov = s.eo_hfov_deg > 0 ? std::round(s.eo_hfov_deg) : 0.0f;
    return c;
}

ThermalStatus Snapshot::thermal(const imago_state& s) {
    ThermalStatus t;
    t.hasTemperatures = s.ir_temp_valid;
    if (t.hasTemperatures) {
        t.minC = tenths(s.ir_temp_min_c);
        t.maxC = tenths(s.ir_temp_max_c);
        t.meanC = tenths(s.ir_temp_mean_c);
    }
    t.palette = s.ir_palette >= 0 ? s.ir_palette : -1;
    t.cameraType = s.ir_camera_type;
    t.zoom = s.ir_zoom_level >= 1.0f ? static_cast<int>(std::lround(s.ir_zoom_level)) : 0;
    return t;
}

std::optional<TargetPosition> Snapshot::target(const imago_state& s) {
    if (!s.target_valid) {
        return std::nullopt;
    }
    return MarkedPoints::rounded(s.target_lat_deg, s.target_lon_deg, s.target_alt_m);
}

TrackingStatus Snapshot::tracking(const imago_state& s) {
    TrackingStatus t;
    t.state = s.tracking_state == IMAGO_TRACKING_TRACKED || s.tracking_state == IMAGO_TRACKING_LOST
                  ? s.tracking_state
                  : Payload::TrackingIdle;
    t.hasBox = s.tracking_box_valid;
    if (t.hasBox) {
        t.box = QRectF(s.tracking_box.x, s.tracking_box.y, s.tracking_box.w, s.tracking_box.h);
    }
    const int count = std::min<int>(s.detection_count, IMAGO_MAX_DETECTIONS);
    for (int i = 0; i < count; ++i) {
        const imago_detection& d = s.detections[i];
        t.detections.append(Detection{QRectF(d.box.x, d.box.y, d.box.w, d.box.h), d.class_id,
                                      d.track_id, d.confidence});
    }
    return t;
}

VideoMetrics Snapshot::metrics(const imago_state& s) {
    VideoMetrics m;
    m.active = s.video_active;
    m.codec = s.video_codec == IMAGO_CODEC_H265   ? QStringLiteral("H.265")
              : s.video_codec == IMAGO_CODEC_H264 ? QStringLiteral("H.264")
                                                  : QStringLiteral("—");
    m.transport = s.video_transport == IMAGO_RTSP_TCP ? QStringLiteral("TCP")
                  : s.video_active                    ? QStringLiteral("UDP")
                                                      : QStringLiteral("—");
    m.fps = s.video_fps;
    m.kbps = s.video_bitrate_kbps;
    m.packetsLost = static_cast<int>(s.video_packets_lost);
    m.packetsLate = static_cast<int>(s.video_packets_late);
    m.jitterMs = s.video_jitter_ms;
    m.transferMs = s.video_frame_transfer_ms;
    m.assemblyMs = s.latency_assembly_ms;
    m.decodeMs = s.latency_decode_ms;
    m.presentMs = s.latency_present_ms;
    m.mavlinkGaps = static_cast<int>(s.mavlink_rx_dropped);
    m.mavlinkReceived = static_cast<int>(s.mavlink_rx_messages);
    m.roundTripLastMs = s.command_rtt_ms_last;
    m.lagMs = s.video_lag_ms;
    m.resyncs = static_cast<int>(s.video_resyncs);
    m.framesCorrupt = static_cast<int>(s.video_frames_corrupt);
    m.frameGapP95Ms = s.video_frame_gap_p95_ms;
    m.frameGapMaxMs = s.video_frame_gap_max_ms;
    return m;
}

PayloadInfo Snapshot::info(const imago_state& s) {
    return PayloadInfo{text(s.model_name, sizeof(s.model_name)),
                       text(s.software_version, sizeof(s.software_version)),
                       text(s.serial_number, sizeof(s.serial_number))};
}

float Snapshot::vehicleHeading(const imago_state& s) {
    return s.vehicle_heading_valid ? std::round(s.vehicle_heading_deg) : -1.0f;
}

int Snapshot::videoLagSteps(const imago_state& s) {
    return s.video_active ? static_cast<int>(std::lround(s.video_lag_ms / 50.0f)) * 50 : 0;
}
