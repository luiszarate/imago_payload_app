// orus-sim: stand-in for a Gremsy Orus L during development.
//
//   orus-sim [--port 14566] [--rtsp-port 8554] [--video clip.h264] [--fps 30]
//            [--push 127.0.0.1:14550] [--no-ack] [--heading 30] [--autopilot]
//            [--lag 800] [--lag-every 20] [--lag-catch-up] [--verbose]
//
// MAVLink is answered on --port. Video is served at rtsp://<this host>:<rtsp-port>/live.
// Without --video the frames are synthetic and not decodable (enough for protocol tests).
// Make a decodable clip with:
//   ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=30 -t 10 -c:v libx264
//          -profile:v baseline -bf 0 -g 30 -x264-params slices=1 -pix_fmt yuv420p clip.h264

#include "orus_simulator.h"
#include "rtsp_server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {
std::atomic<bool> gStop{false};
void onSignal(int) { gStop = true; }
} // namespace

int main(int argc, char** argv) {
    imago::sim::OrusSimulator::Options sim;
    imago::sim::RtspServer::Options rtsp;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--port") {
            sim.port = static_cast<uint16_t>(std::stoi(next()));
        } else if (arg == "--rtsp-port") {
            rtsp.port = static_cast<uint16_t>(std::stoi(next()));
        } else if (arg == "--video") {
            rtsp.h264File = next();
        } else if (arg == "--fps") {
            rtsp.fps = std::stoi(next());
        } else if (arg == "--push") {
            const std::string target = next();
            const size_t colon = target.rfind(':');
            sim.pushTo = imago::net::Endpoint::parse(
                target.substr(0, colon), static_cast<uint16_t>(std::stoi(target.substr(colon + 1))));
        } else if (arg == "--heading") {
            sim.headingDeg = std::stof(next());
        } else if (arg == "--autopilot") {
            sim.autopilot = true; // PX4-like gimbal manager in front of the gimbal
        } else if (arg == "--lag") {
            rtsp.lagMs = std::stoi(next());
        } else if (arg == "--lag-every") {
            rtsp.lagEverySec = std::stoi(next());
        } else if (arg == "--lag-catch-up") {
            rtsp.lagCatchUp = true;
        } else if (arg == "--no-ack") {
            sim.acknowledgeCommands = false;
        } else if (arg == "--verbose") {
            sim.verbose = rtsp.verbose = true;
        } else {
            std::fprintf(stderr, "unknown option %s\n", arg.c_str());
            return 2;
        }
    }

    imago::sim::RtspServer server;
    std::string error;
    if (!server.start(rtsp, &error)) {
        std::fprintf(stderr, "RTSP: %s\n", error.c_str());
        return 1;
    }
    sim.streamUri = server.url();
    imago::sim::OrusSimulator simulator;
    if (!simulator.start(sim, &error)) {
        std::fprintf(stderr, "MAVLink: %s\n", error.c_str());
        return 1;
    }
    std::printf("Orus L simulator\n  MAVLink: UDP %u\n  Video:   %s (%s)\n", simulator.port(),
                server.url().c_str(), rtsp.h264File.empty() ? "synthetic" : rtsp.h264File.c_str());
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    while (!gStop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    simulator.stop();
    server.stop();
    return 0;
}
