#include "web_server.h"

#include "platform/clock.h"
#include "web/orus_media.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>

namespace imago::sim {

namespace {

std::map<std::string, std::string> parseForm(const std::string& body) {
    std::map<std::string, std::string> fields;
    size_t at = 0;
    while (at < body.size()) {
        size_t end = body.find('&', at);
        if (end == std::string::npos) {
            end = body.size();
        }
        const std::string pair = body.substr(at, end - at);
        const size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            fields[pair.substr(0, eq)] = pair.substr(eq + 1); // our values need no decoding
        }
        at = end + 1;
    }
    return fields;
}

std::string option(int value, int current, const char* label) {
    return "<option value=\"" + std::to_string(value) + "\"" + (value == current ? " selected" : "") +
           ">" + label + "</option>";
}

std::string httpDate(int64_t t) {
    const std::time_t time = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    char text[64];
    std::strftime(text, sizeof(text), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return text;
}

std::string contentType(const std::string& name) {
    switch (web::mediaKindOf(name)) {
    case web::MediaKind::Photo:
        return "image/jpeg";
    case web::MediaKind::Video:
        return "video/mp4";
    case web::MediaKind::Radiometric:
        return "image/tiff";
    case web::MediaKind::Other:
        break;
    }
    return "application/octet-stream";
}

std::string htmlEscape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

} // namespace

WebServer::~WebServer() { stop(); }

bool WebServer::start(uint16_t port, std::string* error) {
    if (!listener_.open(port, error)) {
        return false;
    }
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void WebServer::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join(); // joins the connections too
    }
    listener_.close();
}

WebServer::Isotherm WebServer::isotherm() const {
    std::lock_guard lock(mutex_);
    return isotherm_;
}

std::string WebServer::page() const {
    const Isotherm iso = isotherm();
    return "<!DOCTYPE html><html><body><h1>IR Camera (orus-sim)</h1>"
           "<form method=\"post\"><h2>Isotherms Regions</h2>"
           "<select name=\"IsoTherms_Status\">" +
           option(0, iso.enabled ? 1 : 0, "Disable") + option(1, iso.enabled ? 1 : 0, "Enable") +
           "</select><input type=\"number\" name=\"Threshold\" min=\"0\" max=\"550\" value=\"" +
           std::to_string(iso.thresholdC) +
           "\"><select name=\"Measurement_Range\">" +
           option(0, iso.measurementRange, "-20~150") + option(1, iso.measurementRange, "-20~550") +
           "</select><input type=\"submit\" name=\"submit\" value=\"Apply\"></form>"
           "<form method=\"post\"><h2>IR Camera</h2><select name=\"FFC_Mode\">" +
           option(0, 1, "Manual") + option(1, 1, "Auto") +
           "</select><input type=\"number\" name=\"FFC_Period\" value=\"2\"></form></body></html>";
}

std::string WebServer::mediaPage() const {
    // The links Gremsy's SDK reads: one "/delete/<name>" with class "delete-link" per file.
    std::string html = "<!DOCTYPE html><html><body><h1>Media Files (orus-sim)</h1><ul>";
    for (const MediaLibrary::Item& item : media_.list()) {
        const std::string link = web::pathEncode(item.name);
        html += "<li><a href=\"/download/" + link + "\">" + htmlEscape(item.name) +
                "</a> <a href=\"/delete/" + link +
                "\" class=\"delete-link\" onclick=\"return confirm('Are you sure you want to delete "
                "this image?')\">[Delete]</a></li>";
    }
    return html + "</ul><form method=\"post\" action=\"/delete-all-images\"></form>"
                  "<form method=\"post\" action=\"/delete-all-videos\"></form></body></html>";
}

void WebServer::sendMedia(const net::TcpSocket& socket, const MediaLibrary::Item& item, bool head,
                          const std::string& range) {
    size_t first = 0;
    size_t last = item.size - 1;
    bool partial = false;
    // "bytes=a-b" or "bytes=a-"
    if (range.rfind("bytes=", 0) == 0 && item.size > 0) {
        const size_t dash = range.find('-');
        first = std::strtoull(range.c_str() + 6, nullptr, 10);
        if (dash != std::string::npos && dash + 1 < range.size()) {
            last = std::min<size_t>(std::strtoull(range.c_str() + dash + 1, nullptr, 10), item.size - 1);
        }
        partial = first <= last && first < item.size;
        if (!partial) {
            first = 0;
            last = item.size - 1;
        }
    }
    const size_t length = item.size == 0 ? 0 : last - first + 1;
    std::string response = std::string(partial ? "HTTP/1.1 206 PARTIAL CONTENT" : "HTTP/1.1 200 OK") +
                           "\r\nContent-Type: " + contentType(item.name) +
                           "\r\nContent-Length: " + std::to_string(length) +
                           "\r\nLast-Modified: " + httpDate(item.modified) + "\r\nAccept-Ranges: bytes\r\n";
    if (partial) {
        response += "Content-Range: bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" +
                    std::to_string(item.size) + "\r\n";
    }
    response += "Connection: close\r\n\r\n";
    if (!socket.sendAll(response.data(), response.size(), 2000) || head) {
        return;
    }
    std::string chunk(64 * 1024, '\0');
    const int64_t startNs = monotonicNs();
    size_t sent = 0;
    while (sent < length && running_) {
        const size_t n = std::min(chunk.size(), length - sent);
        MediaLibrary::read(item, first + sent, chunk.data(), n);
        if (!socket.sendAll(chunk.data(), n, 5000)) {
            return; // the client went away (a thumbnail read stops early)
        }
        sent += n;
        mediaBytes_ += static_cast<int64_t>(n);
        if (const int rate = mediaRateKBps_.load(); rate > 0) {
            const int64_t dueNs = startNs + static_cast<int64_t>(sent) * kNsPerSec / (static_cast<int64_t>(rate) * 1000);
            while (monotonicNs() < dueNs && running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }
}

void WebServer::run() {
    while (running_) {
        for (auto it = connections_.begin(); it != connections_.end();) {
            if ((*it)->done.load()) {
                (*it)->thread.join();
                it = connections_.erase(it);
            } else {
                ++it;
            }
        }
        const net::SocketHandle fd = listener_.fd();
        bool readable = false;
        if (net::waitReadable({&fd, 1}, 50, {&readable, 1}) <= 0) {
            continue;
        }
        auto connection = std::make_unique<Connection>();
        net::Endpoint peer;
        if (listener_.accept(&connection->socket, &peer)) {
            Connection* c = connection.get();
            c->thread = std::thread([this, c] {
                serve(c->socket);
                c->socket.close();
                c->done = true;
            });
            connections_.push_back(std::move(connection));
        }
    }
    for (auto& c : connections_) {
        c->thread.join();
    }
    connections_.clear();
}

void WebServer::serve(const net::TcpSocket& socket) {
    std::string raw;
    char buffer[2048];
    const int64_t deadline = monotonicNs() + 2 * kNsPerSec;
    size_t headEnd = std::string::npos;
    size_t length = 0;
    while (monotonicNs() < deadline) {
        const long n = socket.receive(buffer, sizeof(buffer));
        if (n > 0) {
            raw.append(buffer, static_cast<size_t>(n));
        } else if (n < 0) {
            break;
        } else {
            const net::SocketHandle fd = socket.fd();
            bool readable = false;
            net::waitReadable({&fd, 1}, 20, {&readable, 1});
        }
        if (headEnd == std::string::npos && (headEnd = raw.find("\r\n\r\n")) != std::string::npos) {
            const size_t cl = raw.find("Content-Length:");
            length = cl != std::string::npos && cl < headEnd ? std::strtoul(raw.c_str() + cl + 15, nullptr, 10) : 0;
        }
        if (headEnd != std::string::npos && raw.size() >= headEnd + 4 + length) {
            break;
        }
    }
    if (headEnd == std::string::npos) {
        return;
    }
    const std::string requestLine = raw.substr(0, raw.find("\r\n"));
    const std::string body = raw.substr(headEnd + 4, length);
    std::string response;
    auto redirect = [](const char* to) {
        return std::string("HTTP/1.1 302 FOUND\r\nLocation: ") + to +
               "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    };
    std::string range;
    if (const size_t r = raw.find("\r\nRange:"); r != std::string::npos && r < headEnd) {
        const size_t end = raw.find("\r\n", r + 2);
        range = raw.substr(r + 9, end - r - 9);
        range.erase(0, range.find_first_not_of(' '));
    }
    const size_t space = requestLine.find(' ');
    const std::string method = requestLine.substr(0, space);
    const std::string target = requestLine.substr(space + 1, requestLine.rfind(' ') - space - 1);
    auto htmlPage = [](const std::string& text) {
        return "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
               std::to_string(text.size()) + "\r\nConnection: close\r\n\r\n" + text;
    };
    if (method == "GET" && target == web::kMediaListPath) {
        response = htmlPage(mediaPage());
    } else if ((method == "GET" || method == "HEAD") && target.rfind(web::kMediaDownloadPrefix, 0) == 0) {
        const auto item = media_.find(web::percentDecode(target.substr(std::strlen(web::kMediaDownloadPrefix))));
        if (item) {
            sendMedia(socket, *item, method == "HEAD", range);
            return;
        }
        response = "HTTP/1.1 404 NOT FOUND\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    } else if (method == "GET" && target.rfind(web::kMediaDeletePrefix, 0) == 0) {
        media_.remove(web::percentDecode(target.substr(std::strlen(web::kMediaDeletePrefix))));
        response = redirect(web::kMediaListPath);
    } else if (method == "POST" && (target == "/delete-all-images" || target == "/delete-all-videos")) {
        media_.removeAll(target == "/delete-all-videos");
        response = redirect(web::kMediaListPath);
    } else if (requestLine.rfind("GET /setup-ir-camera ", 0) == 0) {
        const std::string html = page();
        response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                   std::to_string(html.size()) + "\r\nConnection: close\r\n\r\n" + html;
    } else if (requestLine.rfind("POST /setup-ir-camera ", 0) == 0) {
        ++posts_;
        const auto fields = parseForm(body);
        if (fields.count("IsoTherms_Status") != 0) {
            std::lock_guard lock(mutex_);
            isotherm_.enabled = fields.at("IsoTherms_Status") == "1";
            if (fields.count("Threshold") != 0) {
                isotherm_.thresholdC = std::atoi(fields.at("Threshold").c_str());
            }
            if (fields.count("Measurement_Range") != 0) {
                isotherm_.measurementRange = std::atoi(fields.at("Measurement_Range").c_str());
            }
        }
        response = redirect("/setup-ir-camera");
    } else if (requestLine.rfind("POST /setup-systems ", 0) == 0) {
        const auto fields = parseForm(body);
        if (fields.count("Reboot_Service") != 0) {
            ++reboots_;
        } else if (fields.count("Restart_Service") != 0) {
            ++restarts_;
        }
        response = redirect("/setup-systems");
    } else {
        response = "HTTP/1.1 404 NOT FOUND\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    }
    socket.sendAll(response.data(), response.size(), 1000);
}

} // namespace imago::sim
