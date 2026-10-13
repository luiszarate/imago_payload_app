#pragma once

#include "platform/socket.h"
#include "rtsp/rtsp_message.h"
#include "rtsp/rtsp_url.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace imago {

// RTSP 1.0 client connection: sends requests, matches responses by CSeq, handles Basic and
// Digest authentication, and passes interleaved RTP/RTCP frames to a callback.
class RtspClient {
  public:
    using Headers = std::vector<std::pair<std::string, std::string>>;
    using InterleavedHandler = std::function<void(uint8_t channel, const uint8_t*, size_t)>;

    bool connect(const RtspUrl& url, int timeoutMs, std::string* error);
    void close();
    int fd() const { return socket_.fd(); }

    // Sends a request and waits for the response. Interleaved frames that arrive first are
    // passed to onInterleaved. A 401 is retried once with credentials from the URL.
    std::optional<RtspResponse> request(std::string_view method, const std::string& uri,
                                        const Headers& headers, int timeoutMs,
                                        std::string* error,
                                        const InterleavedHandler& onInterleaved = {});

    // Sends without waiting; the response is consumed and discarded by pump().
    bool sendAsync(std::string_view method, const std::string& uri, const Headers& headers = {});

    // Reads whatever is available and dispatches interleaved frames.
    // Returns false if the connection failed or the server closed it.
    bool pump(const InterleavedHandler& onInterleaved);

    void setSession(const std::string& id) { session_ = id; }
    const std::string& session() const { return session_; }

  private:
    int sendRequest(std::string_view method, const std::string& uri, const Headers& headers);
    bool readAvailable();

    net::TcpSocket socket_;
    RtspStreamParser parser_;
    RtspUrl url_;
    std::string session_;
    std::optional<RtspAuthChallenge> auth_;
    uint32_t nonceCount_ = 0;
    int cseq_ = 0;
};

} // namespace imago
