/*
 * Imago Payload Control core — public C API.
 *
 * One imago_session talks to one Gremsy Orus L: MAVLink v2 over UDP for control and telemetry,
 * RTSP/RTP for video. Video leaves the core still compressed; the platform decodes it.
 *
 * Threading
 * - Every function may be called from any thread.
 * - Command functions encode and send on the caller's thread; they never wait for the network.
 * - Callbacks run on the core's internal threads. They must return quickly and must not call
 *   imago_session_stop() or imago_session_destroy().
 * - imago_session_get_state() copies a consistent snapshot; poll it at display rate.
 */
#ifndef IMAGO_CORE_H
#define IMAGO_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMAGO_CORE_API_VERSION 1

typedef struct imago_session imago_session;

/* Negative return codes. */
typedef enum imago_error {
    IMAGO_ERR_INVALID_ARG = -1,
    IMAGO_ERR_SOCKET = -2,
    IMAGO_ERR_STATE = -3, /* session not started */
} imago_error;

/* ------------------------------------------------------------------------------------------ */
/* Configuration                                                                               */
/* ------------------------------------------------------------------------------------------ */

typedef enum imago_rtsp_transport {
    IMAGO_RTSP_AUTO = 0, /* RTP over UDP; falls back to TCP if no packet arrives */
    IMAGO_RTSP_UDP = 1,
    IMAGO_RTSP_TCP = 2,
} imago_rtsp_transport;

typedef enum imago_nal_format {
    IMAGO_NAL_AVCC = 0,   /* 4-byte big-endian length before each NAL (VideoToolbox) */
    IMAGO_NAL_ANNEXB = 1, /* 00 00 00 01 start code before each NAL (MediaCodec) */
} imago_nal_format;

/*
 * Two ways to reach the payload's MAVLink:
 * - Direct: payload_ip set (e.g. the Orus L at "192.168.55.1"); we send to payload_ip:payload_port.
 * - Listen: payload_ip NULL or "" (e.g. through a Herelink ground unit, which forwards MAVLink to
 *   its Wi-Fi clients). We bind local_port and reply to whoever sends us MAVLink first.
 *
 * Video is independent of MAVLink: rtsp_url can point at the Orus L or at a Herelink stream such
 * as "rtsp://192.168.43.1:8554/fpv_stream".
 */
typedef struct imago_config {
    const char* payload_ip;      /* IPv4 of the MAVLink peer, or NULL/"" for listen mode. */
    uint16_t payload_port;       /* MAVLink UDP port on the peer. 0 = 14566. */
    uint16_t local_port;         /* Local MAVLink UDP port. 0 = payload_port (direct) or
                                    14550 (listen, the port Herelink forwards to). */
    uint8_t system_id;           /* Our MAVLink system ID. 0 = 1. */
    uint8_t component_id;        /* Our MAVLink component ID. 0 = 193 (ONBOARD_COMPUTER3). */
    const char* rtsp_url;        /* Used as given. NULL or "" = use the URL the payload announces. */
    bool rtsp_use_payload_host;  /* For announced URLs only: replace their host with payload_ip. */
    bool auto_start_video;       /* Start video as soon as a URL is known. */
    imago_rtsp_transport rtsp_transport;
    imago_nal_format nal_format;
    uint32_t link_timeout_ms;    /* Link is down after this long without a heartbeat. 0 = 3000. */
    uint32_t gimbal_rate_hz;     /* Resend rate for gimbal rate commands. 0 = 30. */
    uint32_t command_timeout_ms; /* Wait for an ACK before resending. 0 = 300. */
    uint8_t command_retries;     /* Resends after the first attempt. 0 = 3. */
} imago_config;

/* Fills cfg with defaults. payload_ip stays NULL and must be set by the caller. */
void imago_config_init(imago_config* cfg);

/* ------------------------------------------------------------------------------------------ */
/* Video                                                                                       */
/* ------------------------------------------------------------------------------------------ */

typedef enum imago_codec {
    IMAGO_CODEC_UNKNOWN = 0,
    IMAGO_CODEC_H264 = 1,
    IMAGO_CODEC_H265 = 2,
} imago_codec;

/* One access unit (one picture). Pointers are valid only during the callback. */
typedef struct imago_video_frame {
    imago_codec codec;
    const uint8_t* data; /* NAL units in the configured imago_nal_format, parameter sets removed */
    size_t size;
    /* Latest parameter sets, raw NAL units without start code or length prefix.
       vps is only set for H.265. */
    const uint8_t* vps;
    size_t vps_size;
    const uint8_t* sps;
    size_t sps_size;
    const uint8_t* pps;
    size_t pps_size;
    bool parameter_sets_changed; /* The decoder must be (re)configured before this frame. */
    bool keyframe;               /* IDR (H.264) or IRAP (H.265) picture. */
    bool corrupt;                /* Packets were lost; the picture may show artifacts. */
    uint32_t rtp_timestamp;      /* 90 kHz clock */
    uint32_t frame_number;       /* Increments for every delivered frame. */
    int64_t receive_time_ns;     /* Monotonic time when the frame's last packet arrived. */
} imago_video_frame;

typedef void (*imago_video_frame_fn)(const imago_video_frame* frame, void* user);

/* Platform layers report their own stages so the HUD shows the full app-side latency.
   Times are monotonic nanoseconds on the same clock as imago_now_ns(). */
void imago_report_frame_presented(imago_session* session, int64_t receive_time_ns,
                                  int64_t decoded_time_ns, int64_t presented_time_ns);

/* Monotonic clock used for every *_ns value in this API. */
int64_t imago_now_ns(void);

/* ------------------------------------------------------------------------------------------ */
/* Events                                                                                      */
/* ------------------------------------------------------------------------------------------ */

typedef enum imago_event_type {
    IMAGO_EVENT_LINK_UP = 1,
    IMAGO_EVENT_LINK_DOWN = 2,
    IMAGO_EVENT_VIDEO_STARTED = 3,    /* text = URL, code = transport (imago_rtsp_transport) */
    IMAGO_EVENT_VIDEO_STOPPED = 4,
    IMAGO_EVENT_VIDEO_ERROR = 5,      /* text = reason; the core retries on its own */
    IMAGO_EVENT_COMMAND_RESULT = 6,   /* request_id, code = MAV_CMD, result */
    IMAGO_EVENT_PARAM_RESULT = 7,     /* request_id, text = param id, result */
    IMAGO_EVENT_STREAM_URI = 8,       /* text = URL announced by the payload, code = stream id */
    IMAGO_EVENT_STATUS_TEXT = 9,      /* text = message from the payload, code = severity */
} imago_event_type;

/* result values for COMMAND_RESULT and PARAM_RESULT */
#define IMAGO_RESULT_ACCEPTED 0  /* MAV_RESULT_ACCEPTED / PARAM_ACK_ACCEPTED */
#define IMAGO_RESULT_TIMEOUT (-1) /* No acknowledgement after every retry */
/* Any other value is the MAV_RESULT or PARAM_ACK code returned by the payload. */

typedef struct imago_event {
    imago_event_type type;
    uint32_t request_id;
    uint32_t code;
    int32_t result;
    const char* text; /* Valid only during the callback. May be NULL. */
} imago_event;

typedef void (*imago_event_fn)(const imago_event* event, void* user);

/* ------------------------------------------------------------------------------------------ */
/* State snapshot                                                                              */
/* ------------------------------------------------------------------------------------------ */

#define IMAGO_MAX_DETECTIONS 20

typedef enum imago_gimbal_mode {
    IMAGO_GIMBAL_MODE_UNKNOWN = -1,
    IMAGO_GIMBAL_MODE_OFF = 0,
    IMAGO_GIMBAL_MODE_LOCK = 1,
    IMAGO_GIMBAL_MODE_FOLLOW = 2,
    IMAGO_GIMBAL_MODE_MAPPING = 3,
    IMAGO_GIMBAL_MODE_RESET = 4,
} imago_gimbal_mode;

typedef enum imago_view_source {
    IMAGO_VIEW_EO_IR = 0, /* EO with IR picture-in-picture */
    IMAGO_VIEW_EO = 1,
    IMAGO_VIEW_IR = 2,
    IMAGO_VIEW_IR_EO = 3, /* IR with EO picture-in-picture */
    IMAGO_VIEW_SYNC = 4,
    IMAGO_VIEW_SIDE_BY_SIDE = 6,
} imago_view_source;

typedef enum imago_tracker_function {
    IMAGO_TRACKER_OBJECT_TRACKING = 0,
    IMAGO_TRACKER_OBJECT_DETECTION = 1,
} imago_tracker_function;

typedef enum imago_tracking_mode {
    IMAGO_TRACKING_STOP = 0,
    IMAGO_TRACKING_ACTIVE = 1,
    IMAGO_TRACKING_EAGLE_EYE = 2,
} imago_tracking_mode;

typedef enum imago_tracking_state {
    IMAGO_TRACKING_IDLE = 0,
    IMAGO_TRACKING_TRACKED = 1,
    IMAGO_TRACKING_LOST = 2,
} imago_tracking_state;

/* Normalized image rectangle: (0, 0) top-left, (1, 1) bottom-right. */
typedef struct imago_rect {
    float x, y, w, h;
} imago_rect;

typedef struct imago_detection {
    imago_rect box;
    uint16_t track_id; /* 0 = not tracked yet */
    uint8_t class_id;  /* COCO-80 index on the EO camera */
    float confidence;  /* 0..1 */
} imago_detection;

typedef struct imago_state {
    /* Link */
    bool link_up;
    char mavlink_peer[24]; /* "ip:port" we exchange MAVLink with, "" until known */
    int64_t last_heartbeat_ns;
    uint8_t payload_system_id;
    uint8_t camera_component_id;
    uint8_t gimbal_component_id;
    uint32_t mavlink_rx_messages;
    uint32_t mavlink_tx_messages;
    /* Bad CRC plus sequence gaps. Through a router (Herelink, an autopilot) the gaps also count
       messages the router chose not to forward, so treat it as a trend, not as radio loss. */
    uint32_t mavlink_rx_dropped;
    float command_rtt_ms_last;
    float command_rtt_ms_avg;
    uint32_t commands_pending;

    /* Payload identity (from COMPONENT_INFORMATION_BASIC) */
    char model_name[32];
    char software_version[24];
    char serial_number[32];

    /* Gimbal */
    bool gimbal_valid;
    float gimbal_roll_deg;
    float gimbal_pitch_deg;
    float gimbal_yaw_deg; /* relative to the vehicle */
    imago_gimbal_mode gimbal_mode;

    /* Camera */
    int32_t view_source;    /* imago_view_source, -1 unknown */
    int32_t camera_mode;    /* imago_camera_mode, -1 unknown */
    float eo_zoom_level;    /* x, 0 unknown */
    float ir_zoom_level;    /* x, 0 unknown */
    float eo_hfov_deg;      /* 0 unknown */
    bool recording;
    uint32_t recording_time_ms;
    int32_t image_count;
    bool storage_valid;
    float storage_total_mib;
    float storage_available_mib;

    /* Laser rangefinder */
    bool lrf_valid;
    float lrf_range_m;

    /* Tracking */
    int32_t tracking_mode;  /* imago_tracking_mode as reported by the payload */
    int32_t tracking_state; /* imago_tracking_state */
    bool tracking_box_valid;
    imago_rect tracking_box;

    /* AI detection: cleared 1.5 s after the last packet */
    uint32_t detection_frame;
    uint8_t detection_count;
    imago_detection detections[IMAGO_MAX_DETECTIONS];

    /* IR temperature statistics, degrees Celsius */
    bool ir_temp_valid;
    float ir_temp_max_c;
    float ir_temp_min_c;
    float ir_temp_mean_c;

    /* Video */
    bool video_active;
    imago_codec video_codec;
    int32_t video_transport; /* imago_rtsp_transport actually in use */
    float video_fps;
    float video_bitrate_kbps;
    uint32_t video_packets_received;
    uint32_t video_packets_lost;
    uint32_t video_packets_late; /* Arrived after their frame was delivered: dropped */
    uint32_t video_frames_delivered;
    uint32_t video_frames_corrupt;
    float video_jitter_ms;
    float video_frame_transfer_ms; /* p95 first -> last packet of a frame: sender and network */
    /* App-side latency, p95 over the last 2 s (0 = no data yet) */
    float latency_assembly_ms; /* last packet of a frame -> frame delivered to the platform */
    float latency_decode_ms;   /* delivered -> decoded (reported by the platform) */
    float latency_present_ms;  /* last packet -> on screen (reported by the platform) */
} imago_state;

/* ------------------------------------------------------------------------------------------ */
/* Session                                                                                     */
/* ------------------------------------------------------------------------------------------ */

/* Returns NULL if cfg is invalid. Callbacks may be NULL. Nothing runs until start. */
imago_session* imago_session_create(const imago_config* cfg, imago_event_fn on_event,
                                    imago_video_frame_fn on_video_frame, void* user);

/* Opens the MAVLink socket and starts the core threads. 0 on success, negative on error. */
int imago_session_start(imago_session* session);

/* Stops all threads and closes sockets. The gimbal receives a zero-rate command first. */
void imago_session_stop(imago_session* session);

/* Stops the session if needed and frees it. */
void imago_session_destroy(imago_session* session);

void imago_session_get_state(imago_session* session, imago_state* out);

/* Starts video now. url = NULL uses the configured or announced URL. 0 on success. */
int imago_video_start(imago_session* session, const char* url);
void imago_video_stop(imago_session* session);

/* ------------------------------------------------------------------------------------------ */
/* Commands. Each returns a request ID (> 0) reported later in a *_RESULT event, or 0 if it    */
/* could not be sent (session stopped, invalid argument).                                      */
/* ------------------------------------------------------------------------------------------ */

/* Gimbal rate in degrees per second. Sent immediately, then repeated at gimbal_rate_hz while
   non-zero. Setting both to zero sends a stop three times. Returns nothing: rate commands are
   not acknowledged by the payload. */
void imago_gimbal_set_rate(imago_session* session, float pitch_dps, float yaw_dps);

/* Absolute attitude: pitch -90..90, yaw -180..180 degrees. Not acknowledged; returns 1 if sent. */
uint32_t imago_gimbal_set_angle(imago_session* session, float pitch_deg, float yaw_deg);

uint32_t imago_gimbal_set_mode(imago_session* session, imago_gimbal_mode mode);

typedef enum imago_camera_mode {
    IMAGO_CAMERA_MODE_PHOTO = 0,
    IMAGO_CAMERA_MODE_VIDEO = 1,
} imago_camera_mode;

uint32_t imago_camera_set_mode(imago_session* session, imago_camera_mode mode);

/* Both switch the camera mode first when it is not already the right one. */
uint32_t imago_camera_take_photo(imago_session* session);
uint32_t imago_camera_set_recording(imago_session* session, bool recording);

/* direction: -1 zoom out (wide), 0 stop, 1 zoom in (tele) */
uint32_t imago_camera_zoom(imago_session* session, int direction);
/* Exact zoom level in x (1..25 optical, up to 300 combined). */
uint32_t imago_camera_zoom_to(imago_session* session, float level);

/* direction: -1 focus out, 0 stop, 1 focus in */
uint32_t imago_camera_focus(imago_session* session, int direction);
uint32_t imago_camera_autofocus(imago_session* session);

uint32_t imago_camera_set_view(imago_session* session, imago_view_source source);

/* Any camera setting by its param_id (see docs/protocol/orus-l.md), e.g. "C_T_PALETTE". */
uint32_t imago_camera_set_param(imago_session* session, const char* param_id, uint32_t value);

/* Selects what the payload's tracker does: object tracking or AI detection. */
uint32_t imago_tracker_set_function(imago_session* session, imago_tracker_function function);

uint32_t imago_tracking_set_mode(imago_session* session, imago_tracking_mode mode);

/* Activates the tracker and locks onto the object at the normalized point (x, y) with a box of
   normalized size (w, h). w or h <= 0 uses the payload's default box. */
uint32_t imago_tracking_start_at(imago_session* session, float x, float y, float w, float h);

/* Re-points the gimbal at the normalized point without tracking (Eagle Eye). */
uint32_t imago_tracking_point_at(imago_session* session, float x, float y);

uint32_t imago_tracking_stop(imago_session* session);

/* Laser rangefinder: 0 = 1 Hz, 1 = 4 Hz, 2 = 10 Hz, 3 = off */
uint32_t imago_lrf_set_mode(imago_session* session, uint32_t mode);

/* Encoder settings for camera cam_id (0 EO, 1 IR as defined by the payload). */
uint32_t imago_stream_set_bitrate(imago_session* session, uint32_t cam_id, uint32_t bits_per_s);
uint32_t imago_stream_set_resolution(imago_session* session, uint32_t cam_id, uint32_t level);

#ifdef __cplusplus
}
#endif

#endif /* IMAGO_CORE_H */
