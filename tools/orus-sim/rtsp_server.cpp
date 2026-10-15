#include "rtsp_server.h"

#include "platform/clock.h"
#include "platform/socket.h"
#include "util/base64.h"
#include "util/text.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace imago::sim {

namespace {

std::string header(const std::string& request, const char* name) {
    for (std::string_view line : text::split(request, '\n')) {
        line = text::trim(line);
        const size_t colon = line.find(':');
        if (colon != std::string_view::npos && text::iequals(line.substr(0, colon), name)) {
            return std::string(text::trim(line.substr(colon + 1)));
        }
    }
    return {};
}

uint8_t nalType(const Nal& nal) { return nal.empty() ? 0 : nal[0] & 0x1F; }
bool isVcl(const Nal& nal) { return nalType(nal) >= 1 && nalType(nal) <= 5; }

} // namespace

const Nal& RtspServer::syntheticSps() {
    static const Nal sps = {0x67, 0x42, 0xC0, 0x1F, 0xAA};
    return sps;
}

const Nal& RtspServer::syntheticPps() {
    static const Nal pps = {0x68, 0xCE, 0x3C, 0x80};
    return pps;
}

std::vector<AccessUnit> RtspServer::syntheticFrames(int count, int gop) {
    std::vector<AccessUnit> frames;
    for (int f = 0; f < count; ++f) {
        AccessUnit au;
        const bool idr = f % gop == 0;
        if (idr) {
            au.push_back(syntheticSps());
            au.push_back(syntheticPps());
        }
        Nal slice(idr ? 6000 : 900);
        slice[0] = idr ? 0x65 : 0x41;
        for (size_t i = 1; i < slice.size(); ++i) {
            slice[i] = static_cast<uint8_t>(f + i);
        }
        au.push_back(std::move(slice));
        frames.push_back(std::move(au));
    }
    return frames;
}

std::vector<AccessUnit> RtspServer::parseAnnexB(const std::vector<uint8_t>& s) {
    std::vector<Nal> nals;
    size_t i = 0;
    auto findStart = [&](size_t from) -> std::pair<size_t, size_t> {
        for (size_t k = from; k + 3 <= s.size(); ++k) {
            if (s[k] == 0 && s[k + 1] == 0) {
                if (s[k + 2] == 1) {
                    return {k, 3};
                }
                if (k + 4 <= s.size() && s[k + 2] == 0 && s[k + 3] == 1) {
                    return {k, 4};
                }
            }
        }
        return {s.size(), 0};
    };
    auto [start, len] = findStart(0);
    while (start < s.size()) {
        i = start + len;
        auto [next, nextLen] = findStart(i);
        nals.emplace_back(s.begin() + static_cast<long>(i), s.begin() + static_cast<long>(next));
        start = next;
        len = nextLen;
    }
    std::vector<AccessUnit> frames;
    AccessUnit current;
    bool haveVcl = false;
    for (auto& nal : nals) {
        if (nal.empty()) {
            continue;
        }
        if (haveVcl && (isVcl(nal) || nalType(nal) == 7 || nalType(nal) == 9)) {
            frames.push_back(std::move(current));
            current.clear();
            haveVcl = false;
        }
        haveVcl = haveVcl || isVcl(nal);
        current.push_back(std::move(nal));
    }
    if (haveVcl) {
        frames.push_back(std::move(current));
    }
    return frames;
}

RtspServer::~RtspServer() { stop(); }

std::string RtspServer::url() const {
    return "rtsp://127.0.0.1:" + std::to_string(port_) + options_.path;
}

bool RtspServer::start(const Options& options, std::string* error) {
    options_ = options;
    if (!options.h264File.empty()) {
        std::ifstream in(options.h264File, std::ios::binary);
        if (!in) {
            if (error != nullptr) *error = "cannot open " + options.h264File;
            return false;
        }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
        frames_ = parseAnnexB(data);
    } else {
        frames_ = syntheticFrames(300, 30);
    }
    for (const auto& au : frames_) {
        for (const auto& nal : au) {
            if (nalType(nal) == 7 && sps_.empty()) sps_ = nal;
            if (nalType(nal) == 8 && pps_.empty()) pps_ = nal;
        }
    }
    if (frames_.empty() || sps_.empty() || pps_.empty()) {
        if (error != nullptr) *error = "no H.264 frames with SPS/PPS in the source";
        return false;
    }

    std::string listenError;
    if (!listener_.open(options.port, &listenError)) {
        if (error != nullptr) *error = "rtsp " + listenError;
        return false;
    }
    port_ = listener_.localPort();
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void RtspServer::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    listener_.close();
}

void RtspServer::run() {
    while (running_) {
        reapClients(false);
        const net::SocketHandle listenFd = listener_.fd();
        bool readable = false;
        if (net::waitReadable({&listenFd, 1}, 50, {&readable, 1}) <= 0) {
            continue;
        }
        // Non-blocking, so a TCP client that stops reading times out in sendAll() instead of
        // blocking its thread, and stop(), forever.
        auto socket = std::make_shared<net::TcpSocket>();
        net::Endpoint peer;
        if (!listener_.accept(socket.get(), &peer)) {
            continue;
        }
        reapClients(false); // a client that just left frees its slot
        if (clients_.size() >= kMaxClients) {
            if (options_.verbose) {
                std::printf("[rtsp] refused a client: %zu already connected\n", clients_.size());
            }
            continue;
        }
        auto client = std::make_unique<Client>();
        Client* raw = client.get();
        client->thread = std::thread([this, socket, peer, raw] {
            serveClient(*socket, peer);
            socket->close();
            raw->done = true;
        });
        clients_.push_back(std::move(client));
    }
    reapClients(true); // every client loop ends once running_ is false
}

void RtspServer::reapClients(bool all) {
    std::vector<std::unique_ptr<Client>> active;
    for (auto& client : clients_) {
        if (all || client->done) {
            client->thread.join();
        } else {
            active.push_back(std::move(client));
        }
    }
    clients_ = std::move(active);
}

void RtspServer::serveClient(const net::TcpSocket& socket, const net::Endpoint& peer) {
    const net::Endpoint clientIp{peer.address, 0};
    const net::SocketHandle fd = socket.fd();
    auto sendAll = [&socket](const void* data, size_t size) { return socket.sendAll(data, size, 1000); };

    net::UdpSocket rtpOut;
    rtpOut.open(0, 0, net::TrafficClass::Video, nullptr);
    // Receiver reports from the client land here; only counted.
    net::UdpSocket rtcpIn;
    rtcpIn.open(0, 64 * 1024, net::TrafficClass::Video, nullptr);
    net::Endpoint clientRtp;
    bool interleaved = false;
    uint8_t channel = 0;
    bool playing = false;
    const std::string session = "5EC0FFEE";

    std::string buffer;
    uint16_t seq = 1000;
    uint32_t timestamp = 90000;
    size_t frameIndex = 0;
    int framesDue = 0;
    int64_t nextFrameNs = 0;
    int64_t nextLagNs = 0;   // when the next simulated hiccup starts (0 = never)
    int64_t pausedUntilNs = 0;
    const int64_t lagNs = static_cast<int64_t>(options_.lagMs) * kNsPerMs;
    const int64_t lagEveryNs = static_cast<int64_t>(std::max(options_.lagEverySec, 1)) * kNsPerSec;
    const int64_t framePeriodNs = kNsPerSec / std::max(options_.fps, 1);

    auto sendPacket = [&](const std::vector<uint8_t>& packet) {
        if (interleaved) {
            const uint8_t prefix[4] = {'$', channel, static_cast<uint8_t>(packet.size() >> 8),
                                       static_cast<uint8_t>(packet.size())};
            return sendAll(prefix, 4) && sendAll(packet.data(), packet.size());
        }
        if (options_.sendOverUdp) {
            rtpOut.send(clientRtp, packet.data(), packet.size());
        }
        return true;
    };
    auto rtpHeader = [&](bool marker) {
        std::vector<uint8_t> h = {0x80, static_cast<uint8_t>((marker ? 0x80 : 0) | 96),
                                  static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq),
                                  static_cast<uint8_t>(timestamp >> 24),
                                  static_cast<uint8_t>(timestamp >> 16),
                                  static_cast<uint8_t>(timestamp >> 8),
                                  static_cast<uint8_t>(timestamp), 0xC0, 0xFF, 0xEE, 0x01};
        ++seq;
        return h;
    };
    auto sendFrame = [&](const AccessUnit& au) {
        for (size_t n = 0; n < au.size(); ++n) {
            const Nal& nal = au[n];
            const bool lastNal = n + 1 == au.size();
            if (nal.size() <= options_.mtu) {
                auto packet = rtpHeader(lastNal);
                packet.insert(packet.end(), nal.begin(), nal.end());
                if (!sendPacket(packet)) return false;
                continue;
            }
            const uint8_t indicator = static_cast<uint8_t>((nal[0] & 0xE0) | 28);
            for (size_t offset = 1; offset < nal.size(); offset += options_.mtu) {
                const size_t chunk = std::min(options_.mtu, nal.size() - offset);
                const bool first = offset == 1;
                const bool last = offset + chunk == nal.size();
                auto packet = rtpHeader(last && lastNal);
                packet.push_back(indicator);
                packet.push_back(static_cast<uint8_t>((first ? 0x80 : 0) | (last ? 0x40 : 0) |
                                                      (nal[0] & 0x1F)));
                packet.insert(packet.end(), nal.begin() + static_cast<long>(offset),
                              nal.begin() + static_cast<long>(offset + chunk));
                if (!sendPacket(packet)) return false;
            }
        }
        timestamp += static_cast<uint32_t>(90000 / std::max(options_.fps, 1));
        ++framesSent_;
        return true;
    };

    while (running_) {
        int waitMs = 50;
        if (playing) {
            const int64_t sendAtNs = std::max(nextFrameNs, pausedUntilNs);
            // Capped so stop() is noticed even during a long simulated hiccup.
            waitMs = static_cast<int>(
                std::clamp<int64_t>((sendAtNs - monotonicNs()) / kNsPerMs, 0, 50));
        }
        {
            uint8_t report[1500];
            while (rtcpIn.isOpen() && rtcpIn.receive(report, sizeof(report), nullptr) > 0) {
                ++rtcpReceived_;
            }
        }
        bool readable = false;
        if (net::waitReadable({&fd, 1}, waitMs, {&readable, 1}) > 0) {
            char chunk[4096];
            const long n = socket.receive(chunk, sizeof(chunk));
            if (n < 0) {
                return;
            }
            buffer.append(chunk, static_cast<size_t>(n));
        }
        // Drop interleaved RTCP from the client, then handle complete requests.
        while (!buffer.empty() && buffer[0] == '$' && buffer.size() >= 4) {
            const size_t len = (static_cast<uint8_t>(buffer[2]) << 8) | static_cast<uint8_t>(buffer[3]);
            if (buffer.size() < 4 + len) break;
            buffer.erase(0, 4 + len);
        }
        const size_t end = buffer.find("\r\n\r\n");
        if (end != std::string::npos) {
            const std::string request = buffer.substr(0, end + 4);
            buffer.erase(0, end + 4);
            const std::string method = request.substr(0, request.find(' '));
            const std::string cseq = header(request, "CSeq");
            std::string extra;
            std::string body;
            bool closeAfter = false;
            if (options_.verbose) {
                std::printf("[rtsp] %s\n", method.c_str());
            }
            if (method == "OPTIONS") {
                extra = "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER\r\n";
            } else if (method == "DESCRIBE") {
                const std::string spropSps(sps_.begin(), sps_.end());
                const std::string spropPps(pps_.begin(), pps_.end());
                body = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=Orus L simulator\r\nt=0 0\r\n"
                       "a=control:*\r\nm=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\n"
                       "a=fmtp:96 packetization-mode=1;sprop-parameter-sets=" +
                       base64Encode(spropSps) + "," + base64Encode(spropPps) +
                       "\r\na=control:trackID=0\r\n";
                extra = "Content-Type: application/sdp\r\nContent-Base: " + url() + "/\r\n";
            } else if (method == "SETUP") {
                const std::string transport = header(request, "Transport");
                if (transport.find("TCP") != std::string::npos) {
                    interleaved = true;
                    extra = "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n";
                } else {
                    const size_t p = transport.find("client_port=");
                    const auto port = text::parseInt(text::split(transport.substr(p + 12), '-')[0]);
                    clientRtp = {clientIp.address, static_cast<uint16_t>(port.value_or(0))};
                    extra = "Transport: RTP/AVP;unicast;client_port=" +
                            transport.substr(p + 12, transport.find(';', p) - p - 12) +
                            ";server_port=" + std::to_string(rtpOut.localPort()) + "-" +
                            std::to_string(rtcpIn.localPort()) + "\r\n";
                }
                extra += "Session: " + session + ";timeout=60\r\n";
            } else if (method == "PLAY") {
                playing = true;
                nextFrameNs = monotonicNs();
                nextLagNs = lagNs > 0 ? nextFrameNs + lagEveryNs : 0;
                pausedUntilNs = 0;
                ++sessionsStarted_;
                extra = "Session: " + session + "\r\nRange: npt=0.000-\r\n";
            } else if (method == "GET_PARAMETER") {
                extra = "Session: " + session + "\r\n";
            } else if (method == "TEARDOWN") {
                closeAfter = true;
            }
            std::string response = "RTSP/1.0 200 OK\r\nCSeq: " + cseq + "\r\n" + extra;
            if (!body.empty()) {
                response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
            }
            response += "\r\n" + body;
            if (!sendAll(response.data(), response.size()) || closeAfter) {
                return;
            }
        }
        if (playing && nextLagNs != 0 && monotonicNs() >= nextLagNs) {
            if (options_.lagCatchUp) {
                pausedUntilNs = nextLagNs + lagNs;
            } else {
                nextFrameNs += lagNs;
            }
            nextLagNs += lagEveryNs;
            if (options_.verbose) {
                std::printf("[rtsp] simulated %d ms hiccup%s\n", options_.lagMs,
                            options_.lagCatchUp ? " (catch-up)" : "");
            }
        }
        if (playing && monotonicNs() >= nextFrameNs && monotonicNs() >= pausedUntilNs) {
            // With bursts, frames wait until a whole burst is due, then leave back to back.
            ++framesDue;
            if (framesDue >= std::max(options_.burstFrames, 1)) {
                for (; framesDue > 0; --framesDue) {
                    if (!sendFrame(frames_[frameIndex])) {
                        return;
                    }
                    frameIndex = (frameIndex + 1) % frames_.size();
                }
            }
            nextFrameNs += framePeriodNs;
        }
    }
}

} // namespace imago::sim
