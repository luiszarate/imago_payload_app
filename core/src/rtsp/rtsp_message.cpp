#include "rtsp/rtsp_message.h"

#include "util/base64.h"
#include "util/md5.h"
#include "util/text.h"

#include <cstdlib>
#include <cstring>

namespace imago {

namespace {

constexpr size_t kMaxHeaderBytes = 64 * 1024;

std::string_view unquote(std::string_view s) {
    s = text::trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

// Splits "a=1, b=\"x, y\"" into key/value pairs, honouring quotes.
std::vector<std::pair<std::string, std::string>> parseAuthParams(std::string_view s) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == ',')) {
            ++i;
        }
        const size_t keyStart = i;
        while (i < s.size() && s[i] != '=' && s[i] != ',') {
            ++i;
        }
        const std::string key(text::trim(s.substr(keyStart, i - keyStart)));
        std::string value;
        if (i < s.size() && s[i] == '=') {
            ++i;
            if (i < s.size() && s[i] == '"') {
                const size_t end = s.find('"', i + 1);
                value = std::string(s.substr(i + 1, (end == std::string_view::npos ? s.size() : end) - i - 1));
                i = end == std::string_view::npos ? s.size() : end + 1;
            } else {
                const size_t valueStart = i;
                while (i < s.size() && s[i] != ',') {
                    ++i;
                }
                value = std::string(text::trim(s.substr(valueStart, i - valueStart)));
            }
        }
        if (!key.empty()) {
            out.emplace_back(key, value);
        }
    }
    return out;
}

} // namespace

std::optional<std::string_view> RtspResponse::header(std::string_view name) const {
    for (const auto& [key, value] : headers) {
        if (text::iequals(key, name)) {
            return std::string_view(value);
        }
    }
    return std::nullopt;
}

int RtspResponse::cseq() const {
    if (auto value = header("CSeq")) {
        if (auto n = text::parseInt(*value)) {
            return static_cast<int>(*n);
        }
    }
    return -1;
}

void RtspStreamParser::append(const uint8_t* data, size_t size) {
    if (consumed_ > 0 && consumed_ == buffer_.size()) {
        buffer_.clear();
        consumed_ = 0;
    } else if (consumed_ > 64 * 1024) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(consumed_));
        consumed_ = 0;
    }
    buffer_.insert(buffer_.end(), data, data + size);
}

void RtspStreamParser::clear() {
    buffer_.clear();
    consumed_ = 0;
}

RtspStreamParser::Item RtspStreamParser::next(RtspResponse& response, uint8_t& channel,
                                              const uint8_t*& data, size_t& size) {
    const size_t available = buffer_.size() - consumed_;
    if (available == 0) {
        return Item::None;
    }
    const uint8_t* p = buffer_.data() + consumed_;

    if (p[0] == '$') {
        if (available < 4) {
            return Item::None;
        }
        const size_t length = static_cast<size_t>((p[2] << 8) | p[3]);
        if (available < 4 + length) {
            return Item::None;
        }
        channel = p[1];
        data = p + 4;
        size = length;
        consumed_ += 4 + length;
        return Item::Interleaved;
    }

    const std::string_view view(reinterpret_cast<const char*>(p), available);
    const size_t headerEnd = view.find("\r\n\r\n");
    if (headerEnd == std::string_view::npos) {
        return available > kMaxHeaderBytes ? Item::Error : Item::None;
    }
    if (!text::istartsWith(view, "RTSP/")) {
        return Item::Error;
    }

    response = RtspResponse{};
    const auto lines = text::split(view.substr(0, headerEnd), '\n');
    // Status line: RTSP/1.0 200 OK
    const auto statusParts = text::split(text::trim(lines[0]), ' ');
    if (statusParts.size() < 2) {
        return Item::Error;
    }
    response.status = static_cast<int>(text::parseInt(statusParts[1]).value_or(0));
    const size_t reasonPos = text::trim(lines[0]).find(' ', statusParts[0].size() + 1);
    if (reasonPos != std::string_view::npos) {
        response.reason = std::string(text::trim(text::trim(lines[0]).substr(reasonPos)));
    }
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::string_view line = text::trim(lines[i]);
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        response.headers.emplace_back(std::string(text::trim(line.substr(0, colon))),
                                      std::string(text::trim(line.substr(colon + 1))));
    }
    size_t contentLength = 0;
    if (auto value = response.header("Content-Length")) {
        contentLength = static_cast<size_t>(text::parseInt(*value).value_or(0));
    }
    const size_t total = headerEnd + 4 + contentLength;
    if (available < total) {
        return Item::None;
    }
    response.body.assign(view.substr(headerEnd + 4, contentLength));
    consumed_ += total;
    return Item::Response;
}

RtspSessionHeader parseSessionHeader(std::string_view value) {
    RtspSessionHeader out;
    const auto parts = text::split(value, ';');
    out.id = std::string(text::trim(parts[0]));
    for (size_t i = 1; i < parts.size(); ++i) {
        const std::string_view param = text::trim(parts[i]);
        if (text::istartsWith(param, "timeout=")) {
            if (auto t = text::parseInt(param.substr(8)); t && *t > 0) {
                out.timeoutSec = static_cast<int>(*t);
            }
        }
    }
    return out;
}

RtspTransportHeader parseTransportHeader(std::string_view value) {
    RtspTransportHeader out;
    // Servers may list several transports separated by commas; the first is the chosen one.
    const std::string_view first = text::split(value, ',')[0];
    for (std::string_view part : text::split(first, ';')) {
        part = text::trim(part);
        if (text::istartsWith(part, "RTP/AVP/TCP")) {
            out.tcp = true;
        } else if (text::istartsWith(part, "interleaved=")) {
            const auto ports = text::split(part.substr(12), '-');
            out.tcp = true;
            out.interleavedRtp = static_cast<uint8_t>(text::parseInt(ports[0]).value_or(0));
            out.interleavedRtcp = ports.size() > 1
                                      ? static_cast<uint8_t>(text::parseInt(ports[1]).value_or(1))
                                      : static_cast<uint8_t>(out.interleavedRtp + 1);
        } else if (text::istartsWith(part, "server_port=")) {
            const auto ports = text::split(part.substr(12), '-');
            out.serverRtpPort = static_cast<uint16_t>(text::parseInt(ports[0]).value_or(0));
            out.serverRtcpPort =
                ports.size() > 1 ? static_cast<uint16_t>(text::parseInt(ports[1]).value_or(0))
                                 : static_cast<uint16_t>(out.serverRtpPort + 1);
        } else if (text::istartsWith(part, "ssrc=")) {
            const std::string hex(part.substr(5));
            char* end = nullptr;
            const unsigned long v = std::strtoul(hex.c_str(), &end, 16);
            if (end != hex.c_str()) {
                out.ssrc = static_cast<uint32_t>(v);
            }
        }
    }
    return out;
}

std::optional<RtspAuthChallenge> parseAuthChallenge(std::string_view value) {
    value = text::trim(value);
    RtspAuthChallenge out;
    if (text::istartsWith(value, "Digest ")) {
        out.digest = true;
        value.remove_prefix(7);
    } else if (text::istartsWith(value, "Basic ")) {
        value.remove_prefix(6);
    } else {
        return std::nullopt;
    }
    for (const auto& [key, v] : parseAuthParams(value)) {
        if (text::iequals(key, "realm")) {
            out.realm = std::string(unquote(v));
        } else if (text::iequals(key, "nonce")) {
            out.nonce = std::string(unquote(v));
        } else if (text::iequals(key, "opaque")) {
            out.opaque = std::string(unquote(v));
        } else if (text::iequals(key, "qop")) {
            // Prefer "auth" when the server offers a list.
            const std::string qop(unquote(v));
            out.qop = qop.find("auth") != std::string::npos ? "auth" : "";
        }
    }
    return out;
}

std::string buildAuthorization(const RtspAuthChallenge& challenge, std::string_view method,
                               std::string_view uri, std::string_view user,
                               std::string_view password, uint32_t nonceCount,
                               std::string_view cnonce) {
    if (!challenge.digest) {
        std::string credentials(user);
        credentials += ':';
        credentials += password;
        return "Basic " + base64Encode(credentials);
    }
    const std::string ha1 =
        Md5::hex(std::string(user) + ":" + challenge.realm + ":" + std::string(password));
    const std::string ha2 = Md5::hex(std::string(method) + ":" + std::string(uri));
    std::string header = "Digest username=\"" + std::string(user) + "\", realm=\"" +
                         challenge.realm + "\", nonce=\"" + challenge.nonce + "\", uri=\"" +
                         std::string(uri) + "\"";
    if (challenge.qop.empty()) {
        header += ", response=\"" + Md5::hex(ha1 + ":" + challenge.nonce + ":" + ha2) + "\"";
    } else {
        char nc[9];
        std::snprintf(nc, sizeof(nc), "%08x", nonceCount);
        const std::string response = Md5::hex(ha1 + ":" + challenge.nonce + ":" + nc + ":" +
                                              std::string(cnonce) + ":" + challenge.qop + ":" + ha2);
        header += ", qop=" + challenge.qop + ", nc=" + nc + ", cnonce=\"" + std::string(cnonce) +
                  "\", response=\"" + response + "\"";
    }
    if (!challenge.opaque.empty()) {
        header += ", opaque=\"" + challenge.opaque + "\"";
    }
    return header;
}

} // namespace imago
