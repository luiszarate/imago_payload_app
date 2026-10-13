#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace imago {

struct RtspUrl {
    std::string user;
    std::string password;
    std::string host;
    uint16_t port = 554;
    std::string path = "/"; // includes the query, if any

    static std::optional<RtspUrl> parse(std::string_view url);

    // URL for requests: never contains credentials.
    std::string toString() const;
    RtspUrl withHost(const std::string& newHost) const;
};

// Resolves an SDP a=control value against the base URL (Content-Base or the DESCRIBE URL).
std::string resolveControlUrl(const std::string& base, const std::string& control);

} // namespace imago
