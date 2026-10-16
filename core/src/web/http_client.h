#pragma once

#include "platform/socket.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace imago::web {

struct HttpResponse {
    int status = 0; // 0: no answer (connection failed or timed out)
    std::string location;
    std::string body;
    std::string error;
};

// One HTTP/1.1 request on its own connection ("Connection: close"), blocking up to timeoutMs
// in all. Only for the payload's web settings page: never call it on the control or video
// path. body non-empty sends it as application/x-www-form-urlencoded. cancel, when set, ends
// the wait within 100 ms (the session stopping); connecting waits at most connectTimeoutMs.
HttpResponse httpRequest(const net::Endpoint& server, const std::string& method,
                         const std::string& path, const std::string& body, int timeoutMs,
                         int connectTimeoutMs, const std::atomic<bool>* cancel);

// application/x-www-form-urlencoded escaping of one name or value.
std::string formEncode(const std::string& text);

} // namespace imago::web
