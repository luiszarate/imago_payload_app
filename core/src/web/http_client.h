#pragma once

#include "platform/socket.h"

#include <atomic>
#include <cstdint>
#include <functional>
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

// What the head of an answer said.
struct HttpHead {
    int status = 0;
    std::string location;
    std::string contentType;
    std::string lastModified;
    int64_t contentLength = -1; // -1: not given (the body ends when the server closes)
    // Size of the whole resource: the total of a Content-Range, else the Content-Length of a
    // 200 answer; -1 unknown.
    int64_t totalLength = -1;
    bool chunked = false;
};

struct HttpStreamRequest {
    std::string method = "GET";
    std::string path;
    std::string headers;     // extra header lines, each ending in \r\n (e.g. a Range)
    std::string body;
    std::string contentType; // of the body; "" = application/x-www-form-urlencoded
    int connectTimeoutMs = 1500;
    int idleTimeoutMs = 5000;  // gives up when nothing arrives for this long
    int totalTimeoutMs = 0;    // 0 = no limit (downloads)
};

// One request on its own connection, streaming the answer: onHead runs once the head is in,
// onBody for each piece of the (de-chunked) body; either returns false to stop reading, which
// closes the connection. Only for the payload's web server: never on the control or video
// path. cancel, when set, ends the wait within 100 ms. Returns the head (status 0: no answer,
// error says why); error is also set when the body broke off or timed out.
HttpHead httpStream(const net::Endpoint& server, const HttpStreamRequest& request,
                    const std::atomic<bool>* cancel, const std::function<bool(const HttpHead&)>& onHead,
                    const std::function<bool(const char*, size_t)>& onBody, std::string* error);

// application/x-www-form-urlencoded escaping of one name or value.
std::string formEncode(const std::string& text);

} // namespace imago::web
