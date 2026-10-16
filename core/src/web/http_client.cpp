#include "web/http_client.h"

#include "platform/clock.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace imago::web {

namespace {

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

// Splits the head off and reads the status line, Location and Content-Length.
bool parseHead(const std::string& raw, HttpResponse& out, size_t& bodyStart, long& length) {
    const size_t end = raw.find("\r\n\r\n");
    if (end == std::string::npos) {
        return false;
    }
    bodyStart = end + 4;
    length = -1;
    size_t lineStart = 0;
    bool first = true;
    while (lineStart < end) {
        size_t lineEnd = raw.find("\r\n", lineStart);
        if (lineEnd == std::string::npos || lineEnd > end) {
            lineEnd = end;
        }
        const std::string line = raw.substr(lineStart, lineEnd - lineStart);
        if (first) {
            int major = 0, minor = 0, status = 0;
            if (std::sscanf(line.c_str(), "HTTP/%d.%d %d", &major, &minor, &status) == 3) {
                out.status = status;
            }
            first = false;
        } else if (const size_t colon = line.find(':'); colon != std::string::npos) {
            const std::string name = lower(line.substr(0, colon));
            size_t v = colon + 1;
            while (v < line.size() && line[v] == ' ') {
                ++v;
            }
            const std::string value = line.substr(v);
            if (name == "location") {
                out.location = value;
            } else if (name == "content-length") {
                length = std::atol(value.c_str());
            }
        }
        lineStart = lineEnd + 2;
    }
    return out.status != 0;
}

} // namespace

std::string formEncode(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else if (c == ' ') {
            out += '+';
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

HttpResponse httpRequest(const net::Endpoint& server, const std::string& method,
                         const std::string& path, const std::string& body, int timeoutMs,
                         int connectTimeoutMs, const std::atomic<bool>* cancel) {
    HttpResponse response;
    const int64_t deadline = monotonicNs() + static_cast<int64_t>(timeoutMs) * kNsPerMs;
    auto remainingMs = [&] {
        return static_cast<int>(std::max<int64_t>(0, (deadline - monotonicNs()) / kNsPerMs));
    };
    net::TcpSocket socket;
    if (!socket.connect(server, std::min(timeoutMs, connectTimeoutMs), net::TrafficClass::BestEffort,
                        &response.error)) {
        return response;
    }
    std::string request = method + " " + path + " HTTP/1.1\r\nHost: " + server.toString() +
                          "\r\nConnection: close\r\nAccept: text/html\r\n";
    if (!body.empty() || method == "POST") {
        request += "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: " +
                   std::to_string(body.size()) + "\r\n";
    }
    request += "\r\n";
    request += body;
    if (!socket.sendAll(request.data(), request.size(), remainingMs())) {
        response.error = "could not send the request";
        return response;
    }
    std::string raw;
    std::array<char, 4096> buffer{};
    size_t bodyStart = 0;
    long length = -1;
    bool head = false;
    while (true) {
        const long n = socket.receive(buffer.data(), buffer.size());
        if (n > 0) {
            raw.append(buffer.data(), static_cast<size_t>(n));
            if (!head) {
                head = parseHead(raw, response, bodyStart, length);
            }
            if (head && length >= 0 && raw.size() >= bodyStart + static_cast<size_t>(length)) {
                break;
            }
            continue;
        }
        if (n < 0) {
            break; // the server closed the connection: the whole answer is in
        }
        if (remainingMs() == 0 || (cancel != nullptr && cancel->load())) {
            response.error = "timed out";
            break;
        }
        const net::SocketHandle fds[] = {socket.fd()};
        bool readable[] = {false};
        net::waitReadable(fds, std::min(remainingMs(), 100), readable);
    }
    if (!head) {
        response.status = 0;
        if (response.error.empty()) {
            response.error = "no HTTP answer";
        }
        return response;
    }
    response.body = raw.substr(bodyStart, length >= 0 ? static_cast<size_t>(length) : std::string::npos);
    return response;
}

} // namespace imago::web
