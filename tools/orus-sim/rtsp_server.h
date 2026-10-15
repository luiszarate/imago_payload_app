#pragma once

// Minimal RTSP server streaming H.264 over RTP (UDP or TCP interleaved).
// Good enough to exercise the core's RTSP client; not a general-purpose server.
// Each client gets its own thread and stream, so an iPad on the Wi-Fi can watch while the
// iOS Simulator (or imago-cli, or VLC) still holds a session.

#include "platform/socket.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace imago::sim {

using Nal = std::vector<uint8_t>;          // without start code
using AccessUnit = std::vector<Nal>;       // one picture

class RtspServer {
  public:
    struct Options {
        uint16_t port = 8554;     // 0 = any free port
        std::string path = "/live";
        std::string h264File;     // Annex B file; empty = synthetic (not decodable) frames
        int fps = 30;
        bool sendOverUdp = true;  // false: accept UDP SETUP but never send (tests TCP fallback)
        size_t mtu = 1400;
        // Datalink trouble: every lagEverySec seconds, stop sending for lagMs. Without catch-up,
        // the frames then keep their pace and stay lagMs late, like a queue that never drains;
        // with catch-up the backlog goes out at once, like a short hiccup.
        int lagMs = 0;
        int lagEverySec = 20;
        bool lagCatchUp = false;
        // Burst delivery, like a dozing Wi-Fi: hold frames and send this many back to back.
        int burstFrames = 1;
        bool verbose = false;
    };

    ~RtspServer();
    bool start(const Options& options, std::string* error);
    void stop();
    uint16_t port() const { return port_; }
    std::string url() const;

    int sessionsStarted() const { return sessionsStarted_.load(); }
    int framesSent() const { return framesSent_.load(); }
    // RTCP packets clients sent to the announced RTCP port (receiver reports).
    int rtcpReceived() const { return rtcpReceived_.load(); }

    // Splits an Annex B byte stream into access units (one slice per picture assumed).
    static std::vector<AccessUnit> parseAnnexB(const std::vector<uint8_t>& stream);
    // Deterministic frames: SPS/PPS + IDR every `gop` frames, P slices otherwise.
    static std::vector<AccessUnit> syntheticFrames(int count, int gop);
    static const Nal& syntheticSps();
    static const Nal& syntheticPps();

  private:
    struct Client {
        std::thread thread;
        std::atomic<bool> done{false};
    };
    // More than this and new connections are closed at once, so they fail fast.
    static constexpr size_t kMaxClients = 8;

    void run();
    void serveClient(const net::TcpSocket& socket, const net::Endpoint& peer);
    void reapClients(bool all);

    Options options_;
    std::vector<AccessUnit> frames_;
    Nal sps_, pps_;
    net::TcpListener listener_;
    uint16_t port_ = 0;
    std::thread thread_;
    std::vector<std::unique_ptr<Client>> clients_; // touched by the accept thread only
    std::atomic<bool> running_{false};
    std::atomic<int> sessionsStarted_{0};
    std::atomic<int> framesSent_{0};
    std::atomic<int> rtcpReceived_{0};
};

} // namespace imago::sim
