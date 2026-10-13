#include "rtsp/rtsp_url.h"

#include "util/text.h"

namespace imago {

std::optional<RtspUrl> RtspUrl::parse(std::string_view url) {
    url = text::trim(url);
    constexpr std::string_view kScheme = "rtsp://";
    if (!text::istartsWith(url, kScheme)) {
        return std::nullopt;
    }
    url.remove_prefix(kScheme.size());

    RtspUrl out;
    const size_t slash = url.find('/');
    std::string_view authority = url.substr(0, slash);
    out.path = slash == std::string_view::npos ? "/" : std::string(url.substr(slash));

    const size_t at = authority.rfind('@');
    if (at != std::string_view::npos) {
        const std::string_view credentials = authority.substr(0, at);
        authority.remove_prefix(at + 1);
        const size_t colon = credentials.find(':');
        out.user = std::string(credentials.substr(0, colon));
        if (colon != std::string_view::npos) {
            out.password = std::string(credentials.substr(colon + 1));
        }
    }
    const size_t colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        const auto port = text::parseInt(authority.substr(colon + 1));
        if (!port || *port <= 0 || *port > 65535) {
            return std::nullopt;
        }
        out.port = static_cast<uint16_t>(*port);
        authority = authority.substr(0, colon);
    }
    if (authority.empty()) {
        return std::nullopt;
    }
    out.host = std::string(authority);
    return out;
}

std::string RtspUrl::toString() const {
    std::string s = "rtsp://" + host;
    if (port != 554) {
        s += ":" + std::to_string(port);
    }
    return s + path;
}

RtspUrl RtspUrl::withHost(const std::string& newHost) const {
    RtspUrl copy = *this;
    copy.host = newHost;
    return copy;
}

std::string resolveControlUrl(const std::string& base, const std::string& control) {
    if (control.empty() || control == "*") {
        return base;
    }
    if (text::istartsWith(control, "rtsp://")) {
        return control;
    }
    if (!base.empty() && base.back() == '/') {
        return base + control;
    }
    return base + "/" + control;
}

} // namespace imago
