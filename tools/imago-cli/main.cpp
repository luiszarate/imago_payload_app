// imago-cli: drives the core from a terminal, through the public C API only.
//
//   imago-cli [payload-ip|-] [--rtsp URL] [--port N] [--local-port N] [--tcp] [--dump out.h264]
//             [--every SECONDS] [--tlog out.tlog]
//
// "-" as payload IP selects listen mode (MAVLink through Herelink).
// --tlog records every MAVLink message in and out (QGroundControl / MAVExplorer / pymavlink).
// Prints the state every 3 s (--every). Type commands and press Enter, even while it prints:
//   rate <pitch> <yaw>   gimbal rate in deg/s (rate 0 0 stops)
//   angle <pitch> <yaw>  gimbal angle in degrees
//   mode <0-4>           gimbal mode: off, lock, follow, mapping, reset
//   proto <1|2>          gimbal command method: 2 = GIMBAL_DEVICE_SET_ATTITUDE, 1 = DO_MOUNT_CONTROL
//   photo | rec | stop   camera
//   zoom <-1|0|1> | zoomto <x>
//   zoomrange <0-100>    range zoom as the camera takes it, to see which level a percent gives
//   zoomsweep            for each zoom level index of the current mode: from 1x, ask for it and
//                        print where the lens settles (takes a few minutes)
//   view <0-6>           EO/IR, EO, IR, IR/EO, sync, -, side by side
//   track <x> <y>        normalized point, e.g. track 0.5 0.5
//   eagle <x> <y> | untrack
//   detect <0|1>         AI detection on/off
//   lrf <0-3>            1 Hz, 4 Hz, 10 Hz, off
//   param <ID> <value>   any camera setting
//   get <ID>             read a camera setting (arrives as a CAMERA_PARAM event)
//   quit

#include "imago/imago_core.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace {

std::mutex gPrint;
FILE* gDump = nullptr;

const char* eventName(imago_event_type t) {
    switch (t) {
    case IMAGO_EVENT_LINK_UP: return "LINK_UP";
    case IMAGO_EVENT_LINK_DOWN: return "LINK_DOWN";
    case IMAGO_EVENT_VIDEO_STARTED: return "VIDEO_STARTED";
    case IMAGO_EVENT_VIDEO_STOPPED: return "VIDEO_STOPPED";
    case IMAGO_EVENT_VIDEO_ERROR: return "VIDEO_ERROR";
    case IMAGO_EVENT_COMMAND_RESULT: return "COMMAND_RESULT";
    case IMAGO_EVENT_PARAM_RESULT: return "PARAM_RESULT";
    case IMAGO_EVENT_STREAM_URI: return "STREAM_URI";
    case IMAGO_EVENT_STATUS_TEXT: return "STATUS_TEXT";
    case IMAGO_EVENT_VIDEO_RESYNC: return "VIDEO_RESYNC";
    case IMAGO_EVENT_CAMERA_PARAM: return "CAMERA_PARAM";
    case IMAGO_EVENT_FOREIGN_COMMAND: return "FOREIGN_COMMAND";
    case IMAGO_EVENT_ZOOM_CHANGED_EXTERNALLY: return "ZOOM_CHANGED_EXTERNALLY";
    case IMAGO_EVENT_TRACKING_IGNORED: return "TRACKING_IGNORED";
    }
    return "?";
}

void onEvent(const imago_event* e, void*) {
    std::lock_guard lock(gPrint);
    std::printf("[event] %s id=%u code=%u result=%d %s\n", eventName(e->type), e->request_id,
                e->code, e->result, e->text != nullptr ? e->text : "");
}

void onFrame(const imago_video_frame* f, void*) {
    if (gDump == nullptr) {
        return;
    }
    // Annex B with parameter sets before each keyframe: playable with ffplay.
    static const uint8_t start[4] = {0, 0, 0, 1};
    if (f->keyframe || f->parameter_sets_changed) {
        if (f->vps != nullptr) {
            std::fwrite(start, 1, 4, gDump);
            std::fwrite(f->vps, 1, f->vps_size, gDump);
        }
        std::fwrite(start, 1, 4, gDump);
        std::fwrite(f->sps, 1, f->sps_size, gDump);
        std::fwrite(start, 1, 4, gDump);
        std::fwrite(f->pps, 1, f->pps_size, gDump);
    }
    std::fwrite(f->data, 1, f->size, gDump);
}

std::string angle(bool valid, float deg) {
    if (!valid) {
        return "-";
    }
    char text[16];
    std::snprintf(text, sizeof(text), "%.1f", deg);
    return text;
}

void printState(imago_session* s) {
    imago_state st;
    imago_session_get_state(s, &st);
    std::lock_guard lock(gPrint);
    std::printf("\nlink=%s peer=%s ids cam=%u gimbal=%u rx=%u tx=%u gaps=%u rtt=%.0fms "
                "(last %.0fms)\n  gimbal p=%.1f y=%s r=%.1f north=%s hdg=%s mode=%d | "
                "zoom=%.1fx (mode %d, max %.0fx) view=%d "
                "rec=%d | track mode=%d state=%d box=%s(%.2f,%.2f %.2fx%.2f) det=%u\n"
                "  video %s %.1ffps %.0fkbps lost=%u late=%u jitter=%.1fms xfer=%.1fms "
                "asm=%.3fms lag=%.0fms resyncs=%u\n",
                st.link_up ? "UP" : "down", st.mavlink_peer, st.camera_component_id,
                st.gimbal_component_id, st.mavlink_rx_messages, st.mavlink_tx_messages,
                st.mavlink_rx_dropped, st.command_rtt_ms_avg, st.command_rtt_ms_last,
                st.gimbal_pitch_deg, angle(st.gimbal_yaw_valid, st.gimbal_yaw_deg).c_str(),
                st.gimbal_roll_deg, angle(st.gimbal_yaw_north_valid, st.gimbal_yaw_north_deg).c_str(),
                angle(st.vehicle_heading_valid, st.vehicle_heading_deg).c_str(), st.gimbal_mode,
                st.eo_zoom_level, st.eo_zoom_mode, st.eo_zoom_max, st.view_source, st.recording, st.tracking_mode,
                st.tracking_state, st.tracking_box_valid ? "" : "none", st.tracking_box.x,
                st.tracking_box.y, st.tracking_box.w, st.tracking_box.h,
                st.detection_count, st.video_active ? "on" : "off", st.video_fps,
                st.video_bitrate_kbps, st.video_packets_lost, st.video_packets_late,
                st.video_jitter_ms, st.video_frame_transfer_ms, st.latency_assembly_ms,
                st.video_lag_ms, st.video_resyncs);
}

float zoomLevel(imago_session* s) {
    imago_state st;
    imago_session_get_state(s, &st);
    return st.eo_zoom_level;
}

// Waits until the reported zoom stops moving (under 1 % for 1.5 s) or 20 s pass.
float waitZoomSettled(imago_session* s) {
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + seconds(20);
    float last = zoomLevel(s);
    auto stableSince = steady_clock::now();
    while (steady_clock::now() < deadline) {
        std::this_thread::sleep_for(milliseconds(100));
        const float now = zoomLevel(s);
        if (last <= 0 || std::abs(now - last) > 0.01f * last) {
            last = now;
            stableSince = steady_clock::now();
        } else if (steady_clock::now() - stableSince >= milliseconds(1500)) {
            break;
        }
    }
    return zoomLevel(s);
}

// Ground truth for the level tables: what each index of C_V_ZM_CB_LV / C_V_ZM_SR_LV really gives.
void zoomSweep(imago_session* s) {
    imago_state st;
    imago_session_get_state(s, &st);
    const int32_t mode = st.eo_zoom_mode == IMAGO_ZOOM_MODE_SUPER_RESOLUTION
                             ? IMAGO_ZOOM_MODE_SUPER_RESOLUTION
                             : IMAGO_ZOOM_MODE_COMBINED;
    float levels[32];
    const size_t count = std::min<size_t>(imago_zoom_levels(mode, levels, 32), 32);
    std::printf("zoomsweep: %s mode, %zu levels\n",
                mode == IMAGO_ZOOM_MODE_SUPER_RESOLUTION ? "super resolution" : "combined", count);
    for (size_t i = 0; i < count; ++i) {
        imago_camera_zoom_to(s, 1.0f);
        const float start = waitZoomSettled(s);
        imago_camera_zoom_to(s, levels[i]);
        const float settled = waitZoomSettled(s);
        std::printf("zoomsweep: index %2zu  table %6.1fx  from %6.2fx  settled %7.2fx\n", i,
                    static_cast<double>(levels[i]), static_cast<double>(start),
                    static_cast<double>(settled));
        std::fflush(stdout);
    }
    imago_camera_zoom_to(s, 1.0f);
    std::printf("zoomsweep: done\n");
}

} // namespace

int main(int argc, char** argv) {
    imago_config cfg;
    imago_config_init(&cfg);
    std::string ip = "192.168.55.1";
    std::string tlogPath;
    std::string rtsp;
    int everySeconds = 3;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--rtsp") {
            rtsp = next();
        } else if (arg == "--port") {
            cfg.payload_port = static_cast<uint16_t>(std::stoi(next()));
        } else if (arg == "--local-port") {
            cfg.local_port = static_cast<uint16_t>(std::stoi(next()));
        } else if (arg == "--tcp") {
            cfg.rtsp_transport = IMAGO_RTSP_TCP;
        } else if (arg == "--every") {
            everySeconds = std::max(1, std::stoi(next()));
        } else if (arg == "--dump") {
            gDump = std::fopen(next().c_str(), "wb");
        } else if (arg == "--tlog") {
            tlogPath = next();
        } else {
            ip = arg;
        }
    }
    cfg.payload_ip = ip == "-" ? nullptr : ip.c_str();
    cfg.rtsp_url = rtsp.empty() ? nullptr : rtsp.c_str();
    cfg.nal_format = IMAGO_NAL_ANNEXB;

    imago_session* s = imago_session_create(&cfg, onEvent, onFrame, nullptr);
    if (s == nullptr) {
        std::fprintf(stderr, "invalid configuration\n");
        return 2;
    }
    if (imago_session_start(s) != 0) {
        std::fprintf(stderr, "cannot start session (port in use?)\n");
        return 1;
    }
    if (!tlogPath.empty() && imago_session_start_recording(s, tlogPath.c_str()) != 0) {
        std::fprintf(stderr, "cannot write %s\n", tlogPath.c_str());
        return 1;
    }

    std::atomic<bool> quit{false};
    std::thread printer([&] {
        while (!quit) {
            for (int i = 0; i < everySeconds * 10 && !quit; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!quit) printState(s);
        }
    });

    std::string line;
    while (!quit && std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        float a = 0, b = 0;
        uint32_t id = 0;
        if (cmd == "quit" || cmd == "q") {
            break;
        } else if (cmd == "rate" && in >> a >> b) {
            imago_gimbal_set_rate(s, a, b);
        } else if (cmd == "angle" && in >> a >> b) {
            id = imago_gimbal_set_angle(s, a, b);
        } else if (cmd == "mode" && in >> a) {
            id = imago_gimbal_set_mode(s, static_cast<imago_gimbal_mode>(static_cast<int>(a)));
        } else if (cmd == "proto" && in >> a) {
            imago_gimbal_set_protocol(s, static_cast<int>(a) == 1 ? IMAGO_GIMBAL_PROTOCOL_V1
                                                                  : IMAGO_GIMBAL_PROTOCOL_V2);
            std::printf("gimbal protocol %s\n", static_cast<int>(a) == 1 ? "V1 DO_MOUNT_CONTROL"
                                                                          : "V2 DEVICE_SET_ATTITUDE");
        } else if (cmd == "photo") {
            id = imago_camera_take_photo(s);
        } else if (cmd == "rec") {
            id = imago_camera_set_recording(s, true);
        } else if (cmd == "stop") {
            id = imago_camera_set_recording(s, false);
        } else if (cmd == "zoom" && in >> a) {
            id = imago_camera_zoom(s, static_cast<int>(a));
        } else if (cmd == "zoomto" && in >> a) {
            id = imago_camera_zoom_to(s, a);
        } else if (cmd == "zoomrange" && in >> a) {
            id = imago_camera_zoom_range(s, a);
        } else if (cmd == "zoomsweep") {
            zoomSweep(s);
        } else if (cmd == "view" && in >> a) {
            id = imago_camera_set_view(s, static_cast<imago_view_source>(static_cast<int>(a)));
        } else if (cmd == "track" && in >> a >> b) {
            id = imago_tracking_start_at(s, a, b, 0, 0);
        } else if (cmd == "eagle" && in >> a >> b) {
            id = imago_tracking_point_at(s, a, b);
        } else if (cmd == "untrack") {
            id = imago_tracking_stop(s);
        } else if (cmd == "detect" && in >> a) {
            id = imago_tracker_set_function(s, a != 0 ? IMAGO_TRACKER_OBJECT_DETECTION
                                                      : IMAGO_TRACKER_OBJECT_TRACKING);
        } else if (cmd == "lrf" && in >> a) {
            id = imago_lrf_set_mode(s, static_cast<uint32_t>(a));
        } else if (cmd == "get") {
            std::string pid;
            if (in >> pid && imago_camera_read_param(s, pid.c_str()) != 0) {
                std::printf("cannot read %s\n", pid.c_str());
            }
        } else if (cmd == "param") {
            std::string pid;
            uint32_t value = 0;
            if (in >> pid >> value) {
                id = imago_camera_set_param(s, pid.c_str(), value);
            }
        } else if (!cmd.empty()) {
            std::printf("unknown command\n");
            continue;
        }
        if (id != 0) {
            std::lock_guard lock(gPrint);
            std::printf("sent, request id %u\n", id);
        }
    }
    quit = true;
    printer.join();
    imago_session_destroy(s);
    if (gDump != nullptr) {
        std::fclose(gDump);
    }
    return 0;
}
