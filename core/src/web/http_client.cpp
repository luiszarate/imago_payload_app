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

// Reads the status line and the headers we use from raw[0, end).
bool parseHead(const std::string& raw, size_t end, HttpHead& out) {
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
                out.contentLength = std::atoll(value.c_str());
            } else if (name == "content-type") {
                out.contentType = value;
            } else if (name == "last-modified") {
                out.lastModified = value;
            } else if (name == "transfer-encoding") {
                out.chunked = lower(value).find("chunked") != std::string::npos;
            } else if (name == "content-range") {
                // "bytes 0-65535/3456789"
                if (const size_t slash = value.find('/'); slash != std::string::npos &&
                                                          value.compare(slash + 1, 1, "*") != 0) {
                    out.totalLength = std::atoll(value.c_str() + slash + 1);
                }
            }
        }
        lineStart = lineEnd + 2;
    }
    if (out.status == 200 && out.totalLength < 0 && !out.chunked) {
        out.totalLength = out.contentLength;
    }
    return out.status != 0;
}

// Undoes chunked transfer coding as the bytes arrive.
class Dechunker {
  public:
    // Appends the body bytes found in data to out. false once the last chunk went by.
    bool feed(const char* data, size_t size, std::string& out) {
        for (size_t i = 0; i < size;) {
            if (remaining_ > 0) {
                const size_t n = std::min<size_t>(remaining_, size - i);
                out.append(data + i, n);
                remaining_ -= n;
                i += n;
                if (remaining_ == 0) {
                    skipCrlf_ = 2;
                }
                continue;
            }
            if (skipCrlf_ > 0) {
                --skipCrlf_;
                ++i;
                continue;
            }
            const char c = data[i++];
            if (c == '\n') {
                const size_t size_ = std::strtoull(line_.c_str(), nullptr, 16);
                line_.clear();
                if (size_ == 0) {
                    return false;
                }
                remaining_ = size_;
            } else if (c != '\r') {
                line_ += c;
            }
        }
        return true;
    }

  private:
    std::string line_;
    size_t remaining_ = 0;
    int skipCrlf_ = 0;
};

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

HttpHead httpStream(const net::Endpoint& server, const HttpStreamRequest& request,
                    const std::atomic<bool>* cancel, const std::function<bool(const HttpHead&)>& onHead,
                    const std::function<bool(const char*, size_t)>& onBody, std::string* error) {
    HttpHead head;
    std::string localError;
    std::string& err = error != nullptr ? *error : localError;
    err.clear();
    const int64_t deadline = request.totalTimeoutMs > 0
                                 ? monotonicNs() + static_cast<int64_t>(request.totalTimeoutMs) * kNsPerMs
                                 : INT64_MAX;
    auto remainingMs = [&] {
        if (deadline == INT64_MAX) {
            return INT32_MAX;
        }
        return static_cast<int>(std::max<int64_t>(0, (deadline - monotonicNs()) / kNsPerMs));
    };
    net::TcpSocket socket;
    if (!socket.connect(server, std::min(remainingMs(), request.connectTimeoutMs),
                        net::TrafficClass::BestEffort, &err)) {
        return head;
    }
    std::string text = request.method + " " + request.path + " HTTP/1.1\r\nHost: " +
                       server.toString() + "\r\nConnection: close\r\nAccept: */*\r\n" + request.headers;
    if (!request.body.empty() || request.method == "POST") {
        text += "Content-Type: " +
                (request.contentType.empty() ? std::string("application/x-www-form-urlencoded")
                                             : request.contentType) +
                "\r\nContent-Length: " + std::to_string(request.body.size()) + "\r\n";
    }
    text += "\r\n";
    text += request.body;
    if (!socket.sendAll(text.data(), text.size(), std::min(remainingMs(), request.idleTimeoutMs))) {
        err = "could not send the request";
        return head;
    }
    const bool noBody = request.method == "HEAD";
    std::string raw; // the head while it is incomplete
    std::string piece;
    Dechunker dechunker;
    bool haveHead = false;
    bool reading = true;
    int64_t bodyBytes = 0;
    int64_t lastDataNs = monotonicNs();
    std::array<char, 16384> buffer{};

    // Hands body bytes on; false when the body is complete or the caller stopped.
    auto deliver = [&](const char* data, size_t size) {
        if (head.chunked) {
            piece.clear();
            const bool more = dechunker.feed(data, size, piece);
            if (!piece.empty() && !onBody(piece.data(), piece.size())) {
                return false;
            }
            return more;
        }
        if (head.contentLength >= 0) {
            size = static_cast<size_t>(std::min<int64_t>(static_cast<int64_t>(size),
                                                         head.contentLength - bodyBytes));
        }
        bodyBytes += static_cast<int64_t>(size);
        if (size > 0 && !onBody(data, size)) {
            return false;
        }
        return head.contentLength < 0 || bodyBytes < head.contentLength;
    };

    while (reading) {
        const long n = socket.receive(buffer.data(), buffer.size());
        if (n > 0) {
            lastDataNs = monotonicNs();
            if (haveHead) {
                reading = deliver(buffer.data(), static_cast<size_t>(n));
                continue;
            }
            raw.append(buffer.data(), static_cast<size_t>(n));
            const size_t end = raw.find("\r\n\r\n");
            if (end == std::string::npos) {
                continue;
            }
            haveHead = parseHead(raw, end, head);
            if (!haveHead) {
                break;
            }
            if (onHead && !onHead(head)) {
                return head;
            }
            const bool empty = noBody || head.status == 204 || head.status == 304 ||
                               (head.contentLength == 0 && !head.chunked);
            if (empty) {
                return head;
            }
            const std::string rest = raw.substr(end + 4);
            raw.clear();
            reading = rest.empty() || deliver(rest.data(), rest.size());
            continue;
        }
        if (n < 0) {
            // The server closed the connection: complete unless a length said otherwise.
            if (haveHead && ((head.contentLength >= 0 && bodyBytes < head.contentLength) ||
                             head.chunked)) {
                err = "the connection closed early";
            }
            break;
        }
        if (cancel != nullptr && cancel->load()) {
            err = "cancelled";
            break;
        }
        const int64_t idleMs = (monotonicNs() - lastDataNs) / kNsPerMs;
        if (remainingMs() == 0 || idleMs >= request.idleTimeoutMs) {
            err = "timed out";
            break;
        }
        const net::SocketHandle fds[] = {socket.fd()};
        bool readable[] = {false};
        net::waitReadable(fds, std::min(remainingMs(), 100), readable);
    }
    if (!haveHead) {
        head.status = 0;
        if (err.empty()) {
            err = "no HTTP answer";
        }
    }
    return head;
}

HttpResponse httpRequest(const net::Endpoint& server, const std::string& method,
                         const std::string& path, const std::string& body, int timeoutMs,
                         int connectTimeoutMs, const std::atomic<bool>* cancel) {
    HttpStreamRequest request;
    request.method = method;
    request.path = path;
    request.body = body;
    request.connectTimeoutMs = connectTimeoutMs;
    request.idleTimeoutMs = timeoutMs;
    request.totalTimeoutMs = timeoutMs;
    HttpResponse response;
    const HttpHead head = httpStream(
        server, request, cancel, {},
        [&](const char* data, size_t size) {
            response.body.append(data, size);
            return true;
        },
        &response.error);
    response.status = head.status;
    response.location = head.location;
    if (head.status != 0 && response.error != "timed out") {
        response.error.clear();
    }
    return response;
}

} // namespace imago::web
