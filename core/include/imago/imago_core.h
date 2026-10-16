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
typedef enum imago_gimbal_protocol {
    IMAGO_GIMBAL_PROTOCOL_V2 = 0, /* GIMBAL_DEVICE_SET_ATTITUDE, as the Gremsy SDK sends it */
    IMAGO_GIMBAL_PROTOCOL_V1 = 1, /* MAV_CMD_DO_MOUNT_CONTROL (older MAVLink gimbal protocol) */
    /* Through the autopilot's gimbal manager (MAVLink gimbal protocol v2, e.g. PX4 with
       MNT_MODE_IN/OUT = MAVLink v2): GIMBAL_MANAGER_SET_ATTITUDE, accepted only once this
       session holds primary control (imago_gimbal_take_control). */
    IMAGO_GIMBAL_PROTOCOL_MANAGER = 2,
} imago_gimbal_protocol;

/* Who holds primary control of the gimbal manager (GIMBAL_MANAGER_STATUS). */
typedef enum imago_gimbal_control {
    IMAGO_GIMBAL_CONTROL_UNKNOWN = 0,  /* no manager heard */
    IMAGO_GIMBAL_CONTROL_THIS_APP = 1,
    IMAGO_GIMBAL_CONTROL_OTHER = 2,    /* the autopilot itself, a GCS... see the ids */
    IMAGO_GIMBAL_CONTROL_NOBODY = 3,
} imago_gimbal_control;

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
    uint32_t command_timeout_ms; /* Minimum wait for an ACK before resending. 0 = 500. The wait
                                    grows to 2 x measured round trip + 100 ms on slow links. */
    uint8_t command_retries;     /* Resends after the first attempt. 0 = 3. */
    imago_gimbal_protocol gimbal_protocol; /* How rate and angle commands are sent. */
    /* Video lag guard. Lag is how much later than its own pace the stream arrives, i.e. time
       spent waiting in a queue (sender, datalink, Wi-Fi or socket). When every frame for 2 s
       is at least video_max_lag_ms late, the RTSP session is restarted to drop that queue. */
    bool video_resync_on_lag;
    uint32_t video_max_lag_ms;   /* 0 = 500. */
    /* Send a 1-byte datagram ~30 times a second to the MAVLink peer's discard port (with
       Herelink forwarding, the ground unit on the Wi-Fi hop), whether or not video runs. A
       phone or tablet's Wi-Fi radio dozes while the app sends nothing and then gets video and
       telemetry in bursts (an iPad: ~410 ms holds about twice a second, field test
       2026-10-14); steady outgoing traffic keeps it awake. Default false. Never sent to the
       RTSP server: a first version sent RTCP to the stream's port and the Herelink's server
       stalled the video. */
    bool wifi_keep_awake;
    uint16_t wifi_keep_awake_port; /* 0 = 9 (discard) */
    /* Playout smoothing: up to this many ms of delay to show frames at their own pace when
       the link delivers them in bursts (see imago_video_frame.display_delay_ns). 0 = none,
       every frame at once (lowest latency). */
    uint32_t video_smoothing_ms;
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
    /* Show the picture this long after receive_time_ns (0 = at once). Non-zero only with
       video_smoothing_ms: frames that arrived together in a burst are spread back to the
       stream's own pace, within that delay; a frame later than it is shown at once. */
    int64_t display_delay_ns;
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
    IMAGO_EVENT_VIDEO_RESYNC = 10,    /* code = lag in ms; the session restarts right after */
    IMAGO_EVENT_CAMERA_PARAM = 11,    /* text = param id, code = its value: read, or accepted */
    /* The payload acknowledged a command this session did not send, i.e. someone else
       commands it (the autopilot, a GCS). code = MAV_CMD, result = MAV_RESULT,
       text = "<target sys>/<target comp>" of the acknowledgement: who sent it. */
    IMAGO_EVENT_FOREIGN_COMMAND = 12,
    /* The EO zoom moved while this session had not zoomed for a few seconds.
       text = "<from>x -> <to>x", code = new level x 10. */
    IMAGO_EVENT_ZOOM_CHANGED_EXTERNALLY = 13,
    /* The payload accepted a tracking command (track a point, or stop) but 1.5 s after the
       acknowledgement its tracker had not followed: still tracking after a stop, or not
       tracking near the point asked for (field test 2026-10-15). A later tracking command
       replaces the check. text = "track" or "stop", code = the tracking state reported
       (imago_tracking_state). */
    IMAGO_EVENT_TRACKING_IGNORED = 14,
} imago_event_type;

/* Gimbal pan travel (Orus L: +-320) and where imago_state.gimbal_pan_near_limit starts. */
#define IMAGO_GIMBAL_PAN_LIMIT_DEG 320.0f
#define IMAGO_GIMBAL_PAN_WARN_DEG 290.0f

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

/* EO zoom mode (camera setting C_V_ZM_MODE). */
typedef enum imago_zoom_mode {
    IMAGO_ZOOM_MODE_UNKNOWN = -1,
    IMAGO_ZOOM_MODE_COMBINED = 0,          /* optical, then digital up to 300x */
    IMAGO_ZOOM_MODE_SUPER_RESOLUTION = 2,  /* up to 25x */
} imago_zoom_mode;

/* Above this EO zoom level the camera magnifies digitally. */
#define IMAGO_EO_ZOOM_OPTICAL_MAX 25.0f

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

    /* Gimbal. Roll and pitch are relative to the horizon. */
    bool gimbal_valid;
    float gimbal_roll_deg;
    float gimbal_pitch_deg;
    imago_gimbal_mode gimbal_mode;
    /* Gimbal manager primary control (imago_gimbal_control) and who holds it. */
    int32_t gimbal_control;
    uint8_t gimbal_control_system;
    uint8_t gimbal_control_component;
    /* Yaw relative to the vehicle's nose, -180..180, positive to the right. Not valid when
       the gimbal reports yaw relative to North and the vehicle heading is unknown. */
    bool gimbal_yaw_valid;
    float gimbal_yaw_deg;
    /* Yaw relative to North, 0..360. Needs the vehicle heading or a North-referenced gimbal. */
    bool gimbal_yaw_north_valid;
    float gimbal_yaw_north_deg;
    /* Pan: the yaw relative to the nose followed across +-180, so it shows how far along its
       mechanical travel the gimbal is (Orus L: +-IMAGO_GIMBAL_PAN_LIMIT_DEG). The gimbal
       reports wrapped angles, so this assumes it sat within +-180 when first seen; any pass
       through the front +-30 (e.g. a recentre) or a reading past the travel corrects it.
       Not valid until the gimbal is seen, or when its attitude stops. near_limit from
       IMAGO_GIMBAL_PAN_WARN_DEG, with hysteresis. */
    bool gimbal_pan_valid;
    float gimbal_pan_deg;
    bool gimbal_pan_near_limit;

    /* Vehicle heading, 0..360 from North: from the gimbal (delta_yaw) or from the autopilot's
       ATTITUDE when its telemetry shares the link (Herelink forwarding). */
    bool vehicle_heading_valid;
    float vehicle_heading_deg;

    /* Camera */
    int32_t view_source;    /* imago_view_source, -1 unknown */
    int32_t camera_mode;    /* imago_camera_mode, -1 unknown */
    float eo_zoom_level;    /* x, 0 unknown */
    int32_t eo_zoom_mode;   /* imago_zoom_mode */
    float eo_zoom_max;      /* top of the EO zoom range in the current mode, 0 unknown */
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

    /* IR temperature statistics of the whole IR picture, degrees Celsius */
    bool ir_temp_valid;
    float ir_temp_max_c;
    float ir_temp_min_c;
    float ir_temp_mean_c;
    /* IR camera, from the payload's data stream; -1 until reported. ir_camera_type is the raw
       sensor type (which value means which thermal core is unverified, see
       imago_ir_palette_name); ir_palette the C_T_PALETTE index; ir_ffc_mode 0 manual, 1 auto. */
    int32_t ir_camera_type;
    int32_t ir_palette;
    int32_t ir_ffc_mode;

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
    /* Time between consecutive frames arriving, last 2 s: p95 and max. About one frame period
       on a smooth link; a burst shows as gaps of several periods. */
    float video_frame_gap_p95_ms;
    float video_frame_gap_max_ms;
    /* App-side latency, p95 over the last 2 s (0 = no data yet) */
    float latency_assembly_ms; /* last packet of a frame -> frame delivered to the platform */
    float latency_decode_ms;   /* delivered -> decoded (reported by the platform) */
    float latency_present_ms;  /* last packet -> on screen (reported by the platform) */
    /* Lag: how much later than the stream's own pace frames arrive (lowest value over the last
       second, so short bursts do not count). Grows when a queue builds up anywhere upstream. */
    float video_lag_ms;
    uint32_t video_resyncs;    /* Sessions restarted by the lag guard. */
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

/* Records every MAVLink message the session sends and receives to a telemetry log: each
   record is 8 bytes of big-endian microseconds since 1970 followed by the raw packet, the
   .tlog format QGroundControl, MAVExplorer and pymavlink read. Disk writes happen on their
   own thread, never on the control path. Replaces a recording in progress.
   0, or IMAGO_ERR_INVALID_ARG if the file cannot be created. */
int imago_session_start_recording(imago_session* session, const char* path);
/* Flushes and closes the recording, if any. 0, or IMAGO_ERR_SOCKET if part of it could not be
   written (full disk) or was dropped because the disk fell behind. */
int imago_session_stop_recording(imago_session* session);

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

/* Gimbal mode through the payload's GB_MODE setting (PARAM_RESULT follows), except RESET
   with the autopilot's gimbal manager: then it is an angle setpoint of 0/0 against the nose
   through the manager (not acknowledged; returns 1 if sent), so that only one controller
   commands the gimbal. */
uint32_t imago_gimbal_set_mode(imago_session* session, imago_gimbal_mode mode);

/* Switches how gimbal rate and angle commands are sent, while running. */
void imago_gimbal_set_protocol(imago_session* session, imago_gimbal_protocol protocol);
/* Takes (true) or gives back (false) primary control of the autopilot's gimbal manager with
   MAV_CMD_DO_GIMBAL_MANAGER_CONFIGURE, as one tracked command. Needed with
   IMAGO_GIMBAL_PROTOCOL_MANAGER; the session also gives control back when it stops. */
uint32_t imago_gimbal_take_control(imago_session* session, bool take);

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
/* Exact zoom level in x (1..25 optical, up to 300 combined), as one tracked command.
   Sent as a range zoom: the level becomes a percent of the current zoom mode's range. */
uint32_t imago_camera_zoom_to(imago_session* session, float level);
/* Range zoom as the camera takes it: percent 0..100 of the current mode's range. For checking
   how a camera maps the percent to a level. */
uint32_t imago_camera_zoom_range(imago_session* session, float percent);
/* The levels the camera steps through in a zoom mode (imago_zoom_mode; unknown = combined).
   Copies up to `capacity` into `out` and returns how many there are (24 combined, 13 super
   resolution). */
size_t imago_zoom_levels(int32_t zoom_mode, float* out, size_t capacity);
/* Exact zoom level from a control being dragged, snapped to the camera's nearest level and
   sent as its level setting (C_V_ZM_CB_LV / C_V_ZM_SR_LV). Not tracked: the core sends the latest level
   at most every 150 ms and repeats the final one once, so a lost or late command is replaced
   by a newer one instead of being retried after it. A continuous zoom cancels it. */
void imago_camera_zoom_target(imago_session* session, float level);

/* direction: -1 focus out, 0 stop, 1 focus in */
uint32_t imago_camera_focus(imago_session* session, int direction);
uint32_t imago_camera_autofocus(imago_session* session);

uint32_t imago_camera_set_view(imago_session* session, imago_view_source source);

/* Any camera setting by its param_id (see docs/protocol/orus-l.md), e.g. "C_T_PALETTE". */
uint32_t imago_camera_set_param(imago_session* session, const char* param_id, uint32_t value);
/* Asks the camera for a setting's current value; it arrives as IMAGO_EVENT_CAMERA_PARAM.
   Reads are queued and sent a few tens of ms apart, so a whole settings page can ask at once.
   A setting the camera does not know gets no answer. 0, or IMAGO_ERR_INVALID_ARG for an
   empty or over-long (> 16 chars) id, or IMAGO_ERR_STATE. */
int imago_camera_read_param(imago_session* session, const char* param_id);

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

/* IR camera ------------------------------------------------------------------------------ */

/* Palettes the IR camera offers (C_T_PALETTE 0..IMAGO_IR_PALETTE_COUNT-1). Their names and
   look depend on the thermal core inside the payload: Gremsy lists two (F1 and G1). Our Orus L
   has the G1 (field check 2026-10-16), so every type uses the G1 list until a payload with the
   F1 reports its ir_camera_type value. */
#define IMAGO_IR_PALETTE_COUNT 10
/* English name for the palette, e.g. "Iron red"; "" for an index out of range. */
const char* imago_ir_palette_name(int32_t ir_camera_type, uint32_t index);
/* Approximate colours of the palette from cold to hot, 0xRRGGBB, for swatches and the
   temperature scale (not exact: the camera does not publish them). Copies up to `capacity`
   and returns how many there are (2..8), 0 for an index out of range. */
size_t imago_ir_palette_colors(int32_t ir_camera_type, uint32_t index, uint32_t* out_rgb,
                               size_t capacity);

/* Flat-field correction: recalibrates the IR sensor now (the picture freezes for a moment). */
uint32_t imago_ir_ffc(imago_session* session);
/* FFC automatic (the camera recalibrates by itself when it drifts) or manual (only on
   imago_ir_ffc). */
uint32_t imago_ir_set_ffc_mode(imago_session* session, bool automatic);

/* IR gain (camera setting C_T_G): high gain measures -50..150 C, low gain -50..550 C. */
#define IMAGO_IR_GAIN_HIGH 0
#define IMAGO_IR_GAIN_LOW 1
#define IMAGO_IR_GAIN_HIGH_MAX_C 150.0f
#define IMAGO_IR_GAIN_LOW_MAX_C 550.0f

/* Isotherm: colours everything between low_c and high_c (degrees Celsius, 0..550) from black
   to red over the palette, so objects in that band stand out (people 35..40, fire above 100).
   Gremsy documents it for its F1 thermal core (camera settings C_T_ISO_M, C_T_ISO_U and
   C_T_RGn_*); a camera without it answers no read of C_T_ISO_M, which is how an app knows
   whether to offer it. Enabling sends the whole band as settings: Celsius, region 1 from
   low_c to high_c in linear colour, the other regions off, then the isotherm on; a band above
   IMAGO_IR_GAIN_HIGH_MAX_C also switches the camera to low gain. Disabling only turns the
   isotherm off. Returns the request ID of the C_T_ISO_M setting, sent last; the others
   report their own PARAM_RESULT. 0 if the band is invalid (low >= high). */
uint32_t imago_ir_set_isotherm(imago_session* session, bool enabled, float low_c, float high_c);

/* Encoder settings for camera cam_id (0 EO, 1 IR as defined by the payload). */
uint32_t imago_stream_set_bitrate(imago_session* session, uint32_t cam_id, uint32_t bits_per_s);
uint32_t imago_stream_set_resolution(imago_session* session, uint32_t cam_id, uint32_t level);

#ifdef __cplusplus
}
#endif

#endif /* IMAGO_CORE_H */
