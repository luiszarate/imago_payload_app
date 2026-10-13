#include "rtsp/rtsp_client.h"

#include "platform/clock.h"

#include <cstdio>
#include <random>

namespace imago {

namespace {
constexpr char kUserAgent[] = "ImagoPayloadControl/0.1";
constexpr int kSendTimeoutMs = 1000;
} // namespace

bool RtspClient::connect(const RtspUrl& url, int timeoutMs, std::string* error) {
    close();
    url_ = url;
    const auto endpoint = net::Endpoint::resolve(url.host, url.port);
    if (!endpoint) {
        if (error != nullptr) {
            *error = "cannot resolve " + url.host;
        }
        return false;
    }
    return socket_.connect(*endpoint, timeoutMs, net::TrafficClass::Video, error);
}

void RtspClient::close() {
    socket_.close();
    parser_.clear();
    session_.clear();
    auth_.reset();
    nonceCount_ = 0;
    cseq_ = 0;
}

int RtspClient::sendRequest(std::string_view method, const std::string& uri,
                            const Headers& headers) {
    const int cseq = ++cseq_;
    std::string request;
    request.reserve(512);
    request.append(method).append(" ").append(uri).append(" RTSP/1.0\r\n");
    request.append("CSeq: ").append(std::to_string(cseq)).append("\r\n");
    request.append("User-Agent: ").append(kUserAgent).append("\r\n");
    if (!session_.empty()) {
        request.append("Session: ").append(session_).append("\r\n");
    }
    if (auth_) {
        static thread_local std::mt19937 rng{std::random_device{}()};
        char cnonce[17];
        std::snprintf(cnonce, sizeof(cnonce), "%08x%08x", static_cast<unsigned>(rng()),
                      static_cast<unsigned>(rng()));
        request.append("Authorization: ")
            .append(buildAuthorization(*auth_, method, uri, url_.user, url_.password, ++nonceCount_,
                                       cnonce))
            .append("\r\n");
    }
    for (const auto& [name, value] : headers) {
        request.append(name).append(": ").append(value).append("\r\n");
    }
    request.append("\r\n");
    return socket_.sendAll(request.data(), request.size(), kSendTimeoutMs) ? cseq : -1;
}

bool RtspClient::sendAsync(std::string_view method, const std::string& uri,
                           const Headers& headers) {
    return sendRequest(method, uri, headers) > 0;
}

bool RtspClient::readAvailable() {
    uint8_t buffer[64 * 1024];
    while (true) {
        const long n = socket_.receive(buffer, sizeof(buffer));
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            return true;
        }
        parser_.append(buffer, static_cast<size_t>(n));
        if (static_cast<size_t>(n) < sizeof(buffer)) {
            return true;
        }
    }
}

bool RtspClient::pump(const InterleavedHandler& onInterleaved) {
    if (!readAvailable()) {
        return false;
    }
    RtspResponse response;
    uint8_t channel = 0;
    const uint8_t* data = nullptr;
    size_t size = 0;
    while (true) {
        const auto item = parser_.next(response, channel, data, size);
        if (item == RtspStreamParser::Item::None) {
            return true;
        }
        if (item == RtspStreamParser::Item::Error) {
            return false;
        }
        if (item == RtspStreamParser::Item::Interleaved && onInterleaved) {
            onInterleaved(channel, data, size);
        }
        // Responses to keepalives are ignored.
    }
}

std::optional<RtspResponse> RtspClient::request(std::string_view method, const std::string& uri,
                                                const Headers& headers, int timeoutMs,
                                                std::string* error,
                                                const InterleavedHandler& onInterleaved) {
    bool retriedWithAuth = false;
    int cseq = sendRequest(method, uri, headers);
    if (cseq < 0) {
        if (error != nullptr) {
            *error = std::string(method) + ": send failed";
        }
        return std::nullopt;
    }
    const int64_t deadline = monotonicNs() + static_cast<int64_t>(timeoutMs) * kNsPerMs;
    while (true) {
        RtspResponse response;
        uint8_t channel = 0;
        const uint8_t* data = nullptr;
        size_t size = 0;
        const auto item = parser_.next(response, channel, data, size);
        if (item == RtspStreamParser::Item::Error) {
            if (error != nullptr) {
                *error = std::string(method) + ": malformed response";
            }
            return std::nullopt;
        }
        if (item == RtspStreamParser::Item::Interleaved) {
            if (onInterleaved) {
                onInterleaved(channel, data, size);
            }
            continue;
        }
        if (item == RtspStreamParser::Item::Response) {
            if (response.cseq() != cseq) {
                continue; // late answer to an earlier request
            }
            if (response.status == 401 && !retriedWithAuth && !url_.user.empty()) {
                if (auto header = response.header("WWW-Authenticate")) {
                    auth_ = parseAuthChallenge(*header);
                }
                if (auth_) {
                    retriedWithAuth = true;
                    nonceCount_ = 0;
                    cseq = sendRequest(method, uri, headers);
                    if (cseq < 0) {
                        return std::nullopt;
                    }
                    continue;
                }
            }
            return response;
        }
        // Need more bytes.
        const int64_t remainingNs = deadline - monotonicNs();
        if (remainingNs <= 0) {
            if (error != nullptr) {
                *error = std::string(method) + ": timed out";
            }
            return std::nullopt;
        }
        const int fd = socket_.fd();
        bool readable = false;
        net::waitReadable({&fd, 1}, static_cast<int>(remainingNs / kNsPerMs) + 1, {&readable, 1});
        if (readable && !readAvailable()) {
            if (error != nullptr) {
                *error = std::string(method) + ": connection closed";
            }
            return std::nullopt;
        }
    }
}

} // namespace imago
