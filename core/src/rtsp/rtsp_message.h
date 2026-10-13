#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace imago {

struct RtspResponse {
    int status = 0;
    std::string reason;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    // Case-insensitive lookup of the first header with this name.
    std::optional<std::string_view> header(std::string_view name) const;
    int cseq() const;
};

// Splits a TCP byte stream into RTSP responses and interleaved binary frames ("$" channel len).
class RtspStreamParser {
  public:
    enum class Item { None, Response, Interleaved, Error };

    void append(const uint8_t* data, size_t size);

    // Extracts the next complete item. For Interleaved, channel/data/size point into the
    // internal buffer and stay valid until the next call to next() or append().
    Item next(RtspResponse& response, uint8_t& channel, const uint8_t*& data, size_t& size);

    void clear();

  private:
    std::vector<uint8_t> buffer_;
    size_t consumed_ = 0;
};

// Session header value without parameters, and its timeout in seconds (default 60).
struct RtspSessionHeader {
    std::string id;
    int timeoutSec = 60;
};
RtspSessionHeader parseSessionHeader(std::string_view value);

// The parts of a SETUP Transport response we use.
struct RtspTransportHeader {
    bool tcp = false;
    uint8_t interleavedRtp = 0;
    uint8_t interleavedRtcp = 1;
    uint16_t serverRtpPort = 0;
    uint16_t serverRtcpPort = 0;
    std::optional<uint32_t> ssrc;
};
RtspTransportHeader parseTransportHeader(std::string_view value);

// WWW-Authenticate challenge and the matching Authorization header.
struct RtspAuthChallenge {
    bool digest = false;
    std::string realm;
    std::string nonce;
    std::string opaque;
    std::string qop;
};
std::optional<RtspAuthChallenge> parseAuthChallenge(std::string_view value);
std::string buildAuthorization(const RtspAuthChallenge& challenge, std::string_view method,
                               std::string_view uri, std::string_view user,
                               std::string_view password, uint32_t nonceCount,
                               std::string_view cnonce);

} // namespace imago
