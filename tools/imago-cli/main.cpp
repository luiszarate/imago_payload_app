// imago-cli: drives the core from a terminal, through the public C API only.
//
//   imago-cli [payload-ip|-] [--rtsp URL] [--port N] [--local-port N] [--tcp] [--dump out.h264]
//             [--every SECONDS] [--tlog out.tlog] [--compact] [--web host[:port]]
//             [--sysid N] [--compid N]
//
// "-" as payload IP selects listen mode (MAVLink through Herelink).
// --sysid / --compid: our MAVLink identity (default 1/193, the Gremsy SDK's), to try whether the
// payload and the gimbal manager take commands from another one.
// --tlog records every MAVLink message in and out (QGroundControl / MAVExplorer / pymavlink);
// with --compact only the payload's side, streams once a second (imago_recording_detail).
// Prints the state every 3 s (--every). Commands are typed at the bottom line: `help` lists them
// (kCommands below), Tab completes them, Up/Down bring back earlier ones. With stdin not a
// terminal (a pipe, a script) lines are read and printed plainly, one command per line.

#include "console.h"
#include "imago/imago_core.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <ctime>
#include <vector>

namespace {

Console gConsole;
FILE* gDump = nullptr;

struct Command {
    const char* name;
    const char* usage;
};

constexpr Command kCommands[] = {
    {"rate", "rate <pitch> <yaw>   gimbal rate in deg/s (rate 0 0 stops)"},
    {"angle", "angle <pitch> <yaw>  gimbal angle in degrees"},
    {"mode", "mode <0-4>           gimbal mode: off, lock, follow, mapping, reset"},
    {"proto", "proto <1|2|3>        gimbal command method: 2 = GIMBAL_DEVICE_SET_ATTITUDE,\n"
              "                     1 = DO_MOUNT_CONTROL, 3 = through the autopilot's gimbal\n"
              "                     manager (needs \"take\")"},
    {"take", "take | release       primary control of the autopilot's gimbal manager"},
    {"release", "take | release       primary control of the autopilot's gimbal manager"},
    {"photo", "photo | rec | stop   camera"},
    {"rec", "photo | rec | stop   camera"},
    {"stop", "photo | rec | stop   camera"},
    {"zoom", "zoom <-1|0|1>        zoom out, stop, in"},
    {"zoomto", "zoomto <x>           zoom to that factor"},
    {"zoomrange", "zoomrange <0-100>    range zoom as the camera takes it, to see which level a\n"
                  "                     percent gives"},
    {"zoomsweep", "zoomsweep            for each zoom level index of the current mode: from 1x,\n"
                  "                     ask for it and print where the lens settles (a few minutes)"},
    {"view", "view <0-6>           EO/IR, EO, IR, IR/EO, sync, -, side by side"},
    {"track", "track <x> <y>        normalized point, e.g. track 0.5 0.5"},
    {"eagle", "eagle <x> <y>        point at a normalized point"},
    {"untrack", "untrack              stop tracking"},
    {"detect", "detect <0|1>         AI detection on/off"},
    {"lrf", "lrf <0-3>            1 Hz, 4 Hz, 10 Hz, off"},
    {"param", "param <ID> <value>   any camera setting"},
    {"get", "get <ID>             read a camera setting (arrives as a CAMERA_PARAM event)"},
    {"ffc", "ffc                  IR flat-field correction now"},
    {"ffcmode", "ffcmode <0|1>        FFC manual (0) or automatic (1)"},
    {"web", "web                  read the payload's web IR page (isotherm), printed when it\n"
            "                     answers"},
    {"iso", "iso <0|1> <C> [0|1]  save the web isotherm: on/off, threshold, range (0: -20..150 C)"},
    {"restart", "restart service|system  restart the payload's VIO service or the whole system\n"
                "                     (web)"},
    {"media", "media                list the payload's media files (web), grouped by flight\n"
              "media show           print the last list again\n"
              "media get <name> [dir]  download a file (to the current folder by default)\n"
              "media rm <name>      delete a file from the payload (cannot be undone)\n"
              "media cancel         cancel the downloads"},
    {"help", "help                 this list"},
    {"quit", "quit | q             close the session (also Ctrl-C or Ctrl-D on an empty line)"},
};

const Command* findCommand(const std::string& name) {
    for (const Command& c : kCommands) {
        if (name == c.name) {
            return &c;
        }
    }
    return nullptr;
}

std::string helpText() {
    std::string text;
    const char* last = "";
    for (const Command& c : kCommands) {
        if (std::strcmp(c.usage, last) != 0) { // take/release and photo/rec/stop share a line
            std::istringstream lines(c.usage);
            for (std::string line; std::getline(lines, line);) {
                text += "  " + line + "\n";
            }
        }
        last = c.usage;
    }
    return text;
}

// Param IDs the payload answered for this session, for Tab after param and get.
std::mutex gParamMutex;
std::set<std::string> gParamIds;

void rememberParam(const std::string& id) {
    std::lock_guard lock(gParamMutex);
    gParamIds.insert(id);
}

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
    case IMAGO_EVENT_WEB_RESULT: return "WEB_RESULT";
    case IMAGO_EVENT_MEDIA_RESULT: return "MEDIA_RESULT";
    }
    return "?";
}

imago_session* gSession = nullptr;

std::string utc(int64_t seconds) {
    if (seconds < 0) {
        return "time unknown";
    }
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &tm);
    return text;
}

std::string mediaText(imago_session* s) {
    std::vector<imago_media_file> files(imago_media_get_files(s, nullptr, 0));
    files.resize(imago_media_get_files(s, files.data(), static_cast<uint32_t>(files.size())));
    std::vector<imago_media_flight> flights(imago_media_get_flights(s, nullptr, 0));
    flights.resize(imago_media_get_flights(s, flights.data(), static_cast<uint32_t>(flights.size())));
    std::string text;
    for (size_t g = 0; g < flights.size(); ++g) {
        const imago_media_flight& f = flights[g];
        text += strprintf("  flight %zu: %s .. %s UTC, %u photos, %u videos, %.1f MB\n", g + 1,
                          utc(f.start_time).c_str(), utc(f.end_time).c_str(), f.photos, f.videos,
                          static_cast<double>(f.size_bytes) / 1e6);
        for (uint32_t i = f.first_file; i < f.first_file + f.file_count && i < files.size(); ++i) {
            const imago_media_file& m = files[i];
            text += strprintf("    %-40s %10lld B  %s%s\n", m.name,
                              static_cast<long long>(m.size_bytes), utc(m.capture_time).c_str(),
                              m.thumbnail ? "  [thumbnail]" : "");
        }
    }
    return text;
}

std::vector<std::string> mediaNames(imago_session* s) {
    std::vector<imago_media_file> files(imago_media_get_files(s, nullptr, 0));
    files.resize(imago_media_get_files(s, files.data(), static_cast<uint32_t>(files.size())));
    std::vector<std::string> names;
    for (const imago_media_file& f : files) {
        names.emplace_back(f.name);
    }
    return names;
}

void onEvent(const imago_event* e, void*) {
    std::string text = strprintf("[event] %s id=%u code=%u result=%d %s\n", eventName(e->type),
                                 e->request_id, e->code, e->result, e->text != nullptr ? e->text : "");
    if ((e->type == IMAGO_EVENT_CAMERA_PARAM || e->type == IMAGO_EVENT_PARAM_RESULT) &&
        e->text != nullptr && e->text[0] != '\0') {
        rememberParam(e->text);
    }
    if (e->type == IMAGO_EVENT_WEB_RESULT && e->result == IMAGO_RESULT_ACCEPTED && gSession != nullptr) {
        imago_web_ir_settings w;
        imago_web_get_ir_settings(gSession, &w);
        if (w.valid) {
            text += strprintf("  web isotherm %s threshold=%d C range=%s\n",
                              w.isotherm_enabled ? "on" : "off", w.isotherm_threshold_c,
                              w.measurement_range == 1 ? "-20..550 C" : "-20..150 C");
        }
    }
    if (e->type == IMAGO_EVENT_MEDIA_RESULT && e->code == IMAGO_MEDIA_OP_LIST && gSession != nullptr) {
        imago_media_state m;
        imago_media_get_state(gSession, &m);
        text += strprintf("  media: %u files in %u flights%s%s\n", m.file_count, m.flight_count,
                          m.error[0] != '\0' ? ", " : "", m.error);
        text += mediaText(gSession);
    }
    gConsole.print(text);
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
    std::string text = strprintf(
        "\nlink=%s peer=%s ids cam=%u gimbal=%u rx=%u tx=%u gaps=%u rtt=%.0fms "
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
    // The thermal readings and the pan, to check the IR bar and the pan warning from here.
    char temps[64] = "-";
    if (st.ir_temp_valid) {
        std::snprintf(temps, sizeof(temps), "%.1f/%.1f/%.1f", st.ir_temp_min_c, st.ir_temp_mean_c,
                      st.ir_temp_max_c);
    }
    char target[64] = "-";
    if (st.target_valid) {
        std::snprintf(target, sizeof(target), "%.5f,%.5f alt %.0f m", st.target_lat_deg,
                      st.target_lon_deg, st.target_alt_m);
    }
    // Who the autopilot's gimbal manager obeys, to tell an ignored setpoint from a refused one.
    static const char* const kControl[] = {"unknown", "this app", "other", "nobody"};
    const int control = st.gimbal_control >= 0 && st.gimbal_control <= 3 ? st.gimbal_control : 0;
    text += strprintf("  gimbal manager control=%s (%u/%u)\n", kControl[control],
                      st.gimbal_control_system, st.gimbal_control_component);
    text += strprintf(
        "  ir type=%d palette=%d (%s) zoom=%.0fx ffc=%s temp min/mean/max=%s | "
        "pan=%s%s | target %s\n",
        st.ir_camera_type, st.ir_palette,
        st.ir_palette >= 0
            ? imago_ir_palette_name(st.ir_camera_type, static_cast<uint32_t>(st.ir_palette))
            : "-",
        st.ir_zoom_level, st.ir_ffc_mode < 0 ? "-" : (st.ir_ffc_mode == 1 ? "auto" : "manual"),
        temps, angle(st.gimbal_pan_valid, st.gimbal_pan_deg).c_str(),
        st.gimbal_pan_near_limit ? " NEAR LIMIT" : "", target);
    gConsole.print(text);
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
    gConsole.printf("zoomsweep: %s mode, %zu levels\n",
                mode == IMAGO_ZOOM_MODE_SUPER_RESOLUTION ? "super resolution" : "combined", count);
    for (size_t i = 0; i < count; ++i) {
        imago_camera_zoom_to(s, 1.0f);
        const float start = waitZoomSettled(s);
        imago_camera_zoom_to(s, levels[i]);
        const float settled = waitZoomSettled(s);
        gConsole.printf("zoomsweep: index %2zu  table %6.1fx  from %6.2fx  settled %7.2fx\n", i,
                    static_cast<double>(levels[i]), static_cast<double>(start),
                    static_cast<double>(settled));
    }
    imago_camera_zoom_to(s, 1.0f);
    gConsole.printf("zoomsweep: done\n");
}

// Tab: command names, then the arguments that have a short list (media subcommands and files,
// param IDs seen, restart targets); otherwise the command's usage line.
Completion completeWord(const std::vector<std::string>& before) {
    Completion out;
    if (before.empty()) {
        for (const Command& c : kCommands) {
            out.words.emplace_back(c.name);
        }
        return out;
    }
    const Command* command = findCommand(before[0]);
    if (command == nullptr) {
        return out;
    }
    const std::string& name = before[0];
    if (name == "media" && before.size() == 1) {
        out.words = {"get", "rm", "cancel", "show"};
    } else if (name == "media" && before.size() == 2 && (before[1] == "get" || before[1] == "rm") &&
               gSession != nullptr) {
        out.words = mediaNames(gSession);
    } else if ((name == "param" || name == "get") && before.size() == 1) {
        std::lock_guard lock(gParamMutex);
        out.words.assign(gParamIds.begin(), gParamIds.end());
    } else if (name == "restart" && before.size() == 1) {
        out.words = {"service", "system"};
    }
    if (out.words.empty()) {
        out.hint = command->usage;
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    imago_config cfg;
    imago_config_init(&cfg);
    std::string ip = "192.168.144.240"; // our Orus L behind Herelink
    std::string tlogPath;
    std::string rtsp;
    int everySeconds = 3;
    bool compactLog = false;
    std::string webHost;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--rtsp") {
            rtsp = next();
        } else if (arg == "--port") {
            cfg.payload_port = static_cast<uint16_t>(std::stoi(next()));
        } else if (arg == "--local-port") {
            cfg.local_port = static_cast<uint16_t>(std::stoi(next()));
        } else if (arg == "--sysid") {
            cfg.system_id = static_cast<uint8_t>(std::stoi(next()));
        } else if (arg == "--compid") {
            cfg.component_id = static_cast<uint8_t>(std::stoi(next()));
        } else if (arg == "--tcp") {
            cfg.rtsp_transport = IMAGO_RTSP_TCP;
        } else if (arg == "--every") {
            everySeconds = std::max(1, std::stoi(next()));
        } else if (arg == "--dump") {
            gDump = std::fopen(next().c_str(), "wb");
        } else if (arg == "--tlog") {
            tlogPath = next();
        } else if (arg == "--compact") {
            compactLog = true;
        } else if (arg == "--web") {
            webHost = next(); // host[:port] of the payload's web server (default: payload IP, 8000)
        } else {
            ip = arg;
        }
    }
    cfg.payload_ip = ip == "-" ? nullptr : ip.c_str();
    cfg.rtsp_url = rtsp.empty() ? nullptr : rtsp.c_str();
    cfg.nal_format = IMAGO_NAL_ANNEXB;
    if (const size_t colon = webHost.rfind(':'); colon != std::string::npos) {
        cfg.web_port = static_cast<uint16_t>(std::stoi(webHost.substr(colon + 1)));
        webHost.resize(colon);
    }
    cfg.web_host = webHost.empty() ? nullptr : webHost.c_str();

    imago_session* s = imago_session_create(&cfg, onEvent, onFrame, nullptr);
    gSession = s;
    if (s == nullptr) {
        std::fprintf(stderr, "invalid configuration\n");
        return 2;
    }
    if (imago_session_start(s) != 0) {
        std::fprintf(stderr, "cannot start session (port in use?)\n");
        return 1;
    }
    if (compactLog) {
        imago_session_set_recording_detail(s, IMAGO_RECORDING_COMPACT);
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

    gConsole.start("imago> ", completeWord);
    std::string line;
    while (!quit && gConsole.readLine(line)) {
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
            const int p = static_cast<int>(a);
            imago_gimbal_set_protocol(s, p == 1   ? IMAGO_GIMBAL_PROTOCOL_V1
                                         : p == 3 ? IMAGO_GIMBAL_PROTOCOL_MANAGER
                                                  : IMAGO_GIMBAL_PROTOCOL_V2);
            gConsole.printf("gimbal protocol %s\n", p == 1   ? "V1 DO_MOUNT_CONTROL"
                                                    : p == 3 ? "MANAGER GIMBAL_MANAGER_SET_ATTITUDE"
                                                             : "V2 DEVICE_SET_ATTITUDE");
        } else if (cmd == "take" || cmd == "release") {
            id = imago_gimbal_take_control(s, cmd == "take");
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
        } else if (cmd == "web") {
            id = imago_web_read_ir_settings(s);
        } else if (cmd == "iso" && in >> a >> b) {
            float range = 0;
            in >> range;
            id = imago_web_set_isotherm(s, a != 0, static_cast<int32_t>(b), static_cast<int32_t>(range));
        } else if (cmd == "restart") {
            std::string what;
            in >> what;
            if (what == "service" || what == "system") {
                id = imago_web_restart(s, what == "system");
            } else {
                gConsole.printf("restart service | restart system\n");
            }
        } else if (cmd == "media") {
            std::string what, name, dir;
            in >> what >> name >> dir;
            if (what.empty()) {
                id = imago_media_refresh(s);
            } else if (what == "get" && !name.empty()) {
                id = imago_media_download(s, name.c_str(), (dir.empty() ? name : dir + "/" + name).c_str());
            } else if (what == "rm" && !name.empty()) {
                id = imago_media_delete(s, name.c_str());
            } else if (what == "cancel") {
                imago_media_cancel_downloads(s);
            } else if (what == "show") {
                gConsole.print(mediaText(s));
            } else {
                gConsole.printf("media | media get <name> [dir] | media rm <name> | media cancel | "
                                "media show\n");
            }
        } else if (cmd == "ffc") {
            id = imago_ir_ffc(s);
        } else if (cmd == "ffcmode" && in >> a) {
            id = imago_ir_set_ffc_mode(s, a != 0);
        } else if (cmd == "get") {
            std::string pid;
            if (in >> pid && imago_camera_read_param(s, pid.c_str()) != 0) {
                gConsole.printf("cannot read %s\n", pid.c_str());
            }
        } else if (cmd == "param") {
            std::string pid;
            uint32_t value = 0;
            if (in >> pid >> value) {
                id = imago_camera_set_param(s, pid.c_str(), value);
            }
        } else if (cmd == "help") {
            gConsole.print(helpText());
        } else if (!cmd.empty()) {
            gConsole.printf("unknown command (help lists them)\n");
            continue;
        }
        if (id != 0) {
            gConsole.printf("sent, request id %u\n", id);
        }
    }
    quit = true;
    printer.join();
    gConsole.stop();
    imago_session_destroy(s);
    if (gDump != nullptr) {
        std::fclose(gDump);
    }
    return 0;
}
